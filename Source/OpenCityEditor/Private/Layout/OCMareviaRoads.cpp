// Road network of the Marevia plan: district grids, arterials, bridges, the coast and hills roads, the
// Monte Ferrale roads routed over the terrain (with hairpin loops), and the clean-up that turns the
// result into a proper graph (junctions where roads cross, no coincident roads, no stubs, no shallow
// forks). Order of operations and iteration orders follow the original generator exactly.
#pragma clang fp contract(off)

#include "Layout/OCMareviaLayout.h"

#include "Async/ParallelFor.h"
#include "OCLog.h"
#include <cmath>
#include <queue>
#include <vector>

namespace OCLayout
{
	const TArray<FKind> RoadKinds = {   // lanes per direction, width m, speed km/h, importance rank
		{ TEXT("avenue"), 2, 14.0, 60, 3 },
		{ TEXT("street"), 1, 9.0, 50, 2 },
		{ TEXT("alley"), 1, 5.5, 25, 1 },
		{ TEXT("coast"), 1, 8.0, 70, 3 },
		{ TEXT("pass"), 1, 7.0, 50, 3 },
		{ TEXT("pier"), 1, 10.0, 30, 0 },
	};
	const TSet<FString> SidewalkKinds = { TEXT("avenue"), TEXT("street") };   // 2.5 m sidewalks both sides in the city

	const FKind& RoadKind(const FString& Name)
	{
		for (const FKind& K : RoadKinds)
		{
			if (Name == K.Name)
			{
				return K;
			}
		}
		checkf(false, TEXT("unknown road kind %s"), *Name);
		return RoadKinds[0];
	}

	// --- Graph ----------------------------------------------------------------------------------

	double FRoadGraph::Height(double XV, double Y) const
	{
		const int32 Xi = int32(FMath::Clamp<int64>(PyRoundInt(XV), 0, W));
		const int32 Yi = int32(FMath::Clamp<int64>(PyRoundInt(Y - YMin), 0, H));
		return (*Terrain)(Yi, Xi);
	}

	int32 FRoadGraph::Node(double XV, double Y, double Merge)
	{
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			const double Dx = Nodes[i].X - XV, Dy = Nodes[i].Y - Y;
			if (Dx * Dx + Dy * Dy < Merge * Merge)
			{
				return i;
			}
		}
		Nodes.Add({ PyRound(XV, 1), PyRound(Y, 1), PyRound(FMath::Max(Height(XV, Y), 1.0), 2) });
		return Nodes.Num() - 1;
	}

	void FRoadGraph::Connect(int32 A, int32 B, const FString& Kind, const FString& Bridge, const FString& Gate)
	{
		if (A == B)
		{
			return;
		}
		for (const FSeg& S : Segments)
		{
			if ((S.A == A && S.B == B) || (S.A == B && S.B == A))
			{
				return;
			}
		}
		const FKind& K = RoadKind(Kind);
		FSeg S;
		S.A = A;
		S.B = B;
		S.Kind = Kind;
		S.Lanes = K.Lanes;
		S.Width = K.Width;
		S.Speed = K.Speed;
		S.Bridge = Bridge;
		S.Gate = Gate;
		S.Uid = NextUid++;
		Segments.Add(MoveTemp(S));
	}

	TArray<int32> FRoadGraph::Path(const TArray<FPt>& Points, const FString& Kind, double Merge, const FString& Bridge, const FString& Gate)
	{
		TArray<int32> Ids;
		for (const FPt& P : Points)
		{
			Ids.Add(Node(P.X, P.Y, Merge));
		}
		for (int32 i = 0; i + 1 < Ids.Num(); ++i)
		{
			Connect(Ids[i], Ids[i + 1], Kind, Bridge, Gate);
		}
		return Ids;
	}

	// --- Mountain roads: routed over the terrain ----------------------------------------------------

	namespace
	{
		constexpr double RouteCell = 6.0;         // m: routing grid
		constexpr double RouteGradeOk = 0.07;     // grades up to this cost nothing extra
		constexpr double RouteGradeMax = 0.18;    // never steeper
		constexpr double RouteEdge = 45.0;        // m: routed roads keep this far from the map edge (a loop needs ~30 m beyond its apex)
		constexpr double RouteStep = 25.0;        // m between nodes of a routed road
		constexpr double HairpinR = 13.0;         // m: hairpin loop radius, 1 m over the 12 m game minimum
		constexpr double KeepOffCost = 150.0;     // m a routed road would rather build than cross one cell of another road's corridor
		constexpr double SwitchbackMin = 12.0;    // m between the legs of a switchback where it turns
		constexpr double SwitchbackCost = 300.0;  // m-equivalent for doubling back closer than SwitchbackMin
		constexpr double BranchMinDeg = 30.0;     // a branch leaves its parent road at least this far off its line
		constexpr double BranchFallbackDeg = 20.0; // ... or this, where the hillside is too steep to leave it at BranchMinDeg
		constexpr double NeckSlope = 1.5;         // m per m: the land between a hairpin loop's two legs at its neck (validator MaxTerrainSlope 1.6)
		constexpr double BranchLoopFree = 20.0;   // m: the quarry spur keeps this far from the pass's hairpin loops (centre to centre)
		using FTest = TFunction<bool(double, double)>;

		double Heading(const FPt& A, const FPt& B) { return std::atan2(B.Y - A.Y, B.X - A.X); }
		double Wrap(double T) { return PyMod(T + Pi, 2 * Pi) - Pi; }

		/** Hairpins tighter than R become "light bulbs": the road swings out, loops round a circle of radius
		 *  R beyond the apex and swings back, joined by reverse curves of the same radius. Returns the new
		 *  dense path and the loops (for an even grade round each one). */
		TArray<FPt> BulbHairpins(const TArray<FPt>& Pts, const FTest& Avoid, TArray<TArray<FPt>>& OutBulbs)
		{
			const double R = HairpinR;
			const int32 Num = Pts.Num();
			TArray<double> Acc = { 0.0 };
			for (int32 k = 0; k + 1 < Num; ++k)
			{
				Acc.Add(Acc.Last() + Dist(Pts[k], Pts[k + 1]));
			}
			TArray<FPt> Out;
			int32 i = 0;
			while (i < Num - 2)
			{
				// Total signed turn over the next 50 m, up to where the road turns back the other way.
				int32 j = i;
				double Turn = 0.0;
				while (j < Num - 2 && Acc[j + 1] - Acc[i] < 50.0)
				{
					const double D = Wrap(Heading(Pts[j + 1], Pts[j + 2]) - Heading(Pts[j], Pts[j + 1]));
					if (std::fabs(Turn) > Radians(30) && D * Turn < 0)
					{
						break;
					}
					Turn += D;
					++j;
				}
				auto Keep = [&]() { Out.Add(Pts[i]); ++i; };
				if (std::fabs(Turn) < Radians(120))
				{
					Keep();
					continue;
				}
				// Apex: the sharpest point in the window. The loop starts where the legs are at least 9 m apart.
				int32 Apex = i + 1;
				double BestTurn = -1.0;
				for (int32 k = i + 1; k <= j; ++k)
				{
					const double T = std::fabs(Wrap(Heading(Pts[k], Pts[k + 1]) - Heading(Pts[k - 1], Pts[k])));
					if (T > BestTurn) { BestTurn = T; Apex = k; }
				}
				TArray<int32> OutLeg;
				for (int32 m = Apex; m < Num; ++m)
				{
					if (Acc[m] - Acc[Apex] < 90.0) { OutLeg.Add(m); }
				}
				int32 KIn = INDEX_NONE;
				for (int32 k = Apex - 1; k > 0; --k)
				{
					if (Acc[Apex] - Acc[k] > 90.0)
					{
						break;
					}
					double MinD = TNumericLimits<double>::Max();
					for (const int32 m : OutLeg) { MinD = FMath::Min(MinD, Dist(Pts[k], Pts[m])); }
					if (MinD >= 9.0)
					{
						KIn = k;
						break;
					}
				}
				if (KIn == INDEX_NONE)
				{
					Keep();
					continue;
				}
				int32 KOut = OutLeg[0];
				for (const int32 m : OutLeg)
				{
					if (Dist(Pts[m], Pts[KIn]) < Dist(Pts[KOut], Pts[KIn])) { KOut = m; }
				}
				const double DIn = Heading(Pts[KIn - 1], Pts[KIn + 1]);
				const double DOut = Heading(Pts[KOut - 1], Pts[FMath::Min(KOut + 1, Num - 1)]);
				double Ux = std::cos(DIn) - std::cos(DOut), Uy = std::sin(DIn) - std::sin(DOut);
				const double Un = std::hypot(Ux, Uy);
				if (Un < 1e-6 || KIn <= i)
				{
					Keep();
					continue;
				}
				Ux = Ux / Un;
				Uy = Uy / Un;
				const double Wx = Pts[KOut].X - Pts[KIn].X, Wy = Pts[KOut].Y - Pts[KIn].Y;
				const double Along = Wx * Ux + Wy * Uy;
				const double Vvx = Wx - Along * Ux, Vvy = Wy - Along * Uy;
				const double Sep = std::hypot(Vvx, Vvy);
				if (Sep >= 2 * R || Sep < 1.0)
				{
					Keep();
					continue;
				}
				const double Vx = Vvx / Sep, Vy = Vvy / Sep;
				const FPt O = Pts[KIn];
				auto World = [&](double LX, double LY) { return FPt(O.X + LX * Ux + LY * Vx, O.Y + LX * Uy + LY * Vy); };
				const double X1 = 0.0;
				const double Xc = X1 + std::sqrt(std::pow(2 * R, 2.0) - std::pow(Sep / 2.0 + R, 2.0)), Yc = Sep / 2.0;
				const double Alpha = std::atan2(Xc - X1, Sep / 2.0 + R);
				TArray<FPt> Loop;
				const double Step = 7.0 / R;   // ~7 m between loop nodes
				int32 N = FMath::Max(1, int32(Alpha / Step));
				for (int32 q = 1; q <= N; ++q)   // entry curve, turning away from the other leg
				{
					const double Ph = Alpha * q / N;
					Loop.Add(World(X1 + R * std::sin(Ph), -R + R * std::cos(Ph)));
				}
				const double T1x = X1 + R * std::sin(Alpha), T1y = -R + R * std::cos(Alpha);
				const double T2x = X1 + R * std::sin(Alpha), T2y = Sep + R - R * std::cos(Alpha);
				const double B1 = std::atan2(T1y - Yc, T1x - Xc);
				double B2 = std::atan2(T2y - Yc, T2x - Xc);
				while (B2 <= B1)
				{
					B2 += 2 * Pi;
				}
				N = FMath::Max(2, int32((B2 - B1) / Step));
				for (int32 q = 1; q < N; ++q)   // round the loop
				{
					const double B = B1 + (B2 - B1) * q / N;
					Loop.Add(World(Xc + R * std::cos(B), Yc + R * std::sin(B)));
				}
				N = FMath::Max(1, int32(Alpha / Step));
				for (int32 q = N; q > 0; --q)   // exit curve back onto the other leg (pts[k_out] closes it)
				{
					const double Ph = Alpha * q / N;
					Loop.Add(World(X1 + R * std::sin(Ph), Sep + R - R * std::cos(Ph)));
				}
				// A loop must stay on the map and on dry land, and not touch the road elsewhere (a stacked
				// leg 30 m+ along the road from its ends): graph nodes there would merge into a junction.
				TArray<FPt> Far;
				for (int32 m = 0; m < Num; ++m)
				{
					if (Acc[m] < Acc[KIn] - 30.0 || Acc[m] > Acc[KOut] + 30.0) { Far.Add(Pts[m]); }
				}
				bool bBad = false;
				for (const FPt& P : Loop)
				{
					bool bPBad = !(15.0 < P.X && P.X < W - 15.0 && YMin + 15.0 < P.Y && P.Y < YMin + H - 15.0) || Avoid(P.X, P.Y);
					for (int32 f = 0; !bPBad && f < Far.Num(); f += 2)
					{
						bPBad = Dist(P, Far[f]) < 14.0;
					}
					if (bPBad) { bBad = true; break; }
				}
				if (bBad)
				{
					Keep();
					continue;
				}
				for (int32 k = i; k <= KIn; ++k) { Out.Add(Pts[k]); }
				Out.Append(Loop);
				TArray<FPt> Bulb = { Pts[KIn] };
				Bulb.Append(Loop);
				Bulb.Add(Pts[KOut]);
				OutBulbs.Add(MoveTemp(Bulb));
				i = KOut;
			}
			for (int32 k = i; k < Num; ++k) { Out.Add(Pts[k]); }
			return Out;
		}

		struct FRouteArgs
		{
			FTest Avoid, LoopAvoid, KeepOff;
			bool bStartDir = false;
			double MinDeg = BranchMinDeg;   // with bStartDir: the first move keeps this far off the start direction
			double SDx = 0.0, SDy = 0.0;
			double TurnCost = 40.0;
			bool bStrict = true, bFallback = true;
		};
		struct FRouteResult
		{
			bool bOk = false;
			TArray<FPt> Points;
			TArray<TArray<FPt>> Bulbs;
		};

		/** The land smoothed over 10 m (two 2k-wide passes): routes follow its shape, not its noise. */
		FGridD RouteSoft(const FGridD& Terrain)
		{
			FGridD Soft = Terrain, Tmp(NX, NY, 0.0);
			const int32 K = 5;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				ParallelFor(NY, [&](int32 r)
				{
					TArray<double> C;
					C.SetNum(NX + 2 * K);
					double Acc = 0.0;
					for (int32 i = 0; i < NX + 2 * K; ++i) { Acc += Soft(r, FMath::Clamp(i - K, 0, NX - 1)); C[i] = Acc; }
					for (int32 j = 0; j < NX; ++j) { Tmp(r, j) = (C[j + 2 * K] - C[j]) / (2 * K); }
				});
				ParallelFor(NX, [&](int32 c)
				{
					TArray<double> C;
					C.SetNum(NY + 2 * K);
					double Acc = 0.0;
					for (int32 i = 0; i < NY + 2 * K; ++i) { Acc += Tmp(FMath::Clamp(i - K, 0, NY - 1), c); C[i] = Acc; }
					for (int32 j = 0; j < NY; ++j) { Soft(j, c) = (C[j + 2 * K] - C[j]) / (2 * K); }
				});
			}
			return Soft;
		}

		/** Terrain-following road from Start to Goal: A* on a RouteCell grid over the smoothed land, 48 move
		 *  directions, cost = length x (1 + grade penalty) + a turn penalty, never steeper than RouteGradeMax.
		 *  Switchbacks appear by themselves where the slope demands them. Returns a smoothed polyline. */
		FRouteResult RouteRoad(const FGridD& Soft, const FPt& Start, const FPt& Goal, const FRouteArgs& Args)
		{
			const int32 Nx = int32(W / RouteCell), Ny = int32(H / RouteCell);
			const int32 Dim = 512;   // cells 0..Nx+1 fit (start and goal round up to Nx)
			auto Hgt = [&](int32 I, int32 J) { return Soft(FMath::Min(int32(J * RouteCell), H), FMath::Min(int32(I * RouteCell), W)); };
			auto Cell = [](const FPt& P) { return FIntPoint(int32(PyRoundInt(P.X / RouteCell)), int32(PyRoundInt((P.Y - YMin) / RouteCell))); };
			TArray<FIntPoint> Moves;
			for (int32 Dx = -4; Dx <= 4; ++Dx)
			{
				for (int32 Dy = -4; Dy <= 4; ++Dy)
				{
					if ((Dx || Dy) && FMath::GreatestCommonDivisor(FMath::Abs(Dx), FMath::Abs(Dy)) == 1)
					{
						Moves.Add({ Dx, Dy });
					}
				}
			}
			const FIntPoint S0 = Cell(Start), S1 = Cell(Goal);
			const int32 Pad = int32(600 / RouteCell);   // search a box around the two ends (speed)
			const int32 Bx0 = FMath::Max(1, FMath::Min(S0.X, S1.X) - Pad), Bx1 = FMath::Min(Nx - 1, FMath::Max(S0.X, S1.X) + Pad);
			const int32 By0 = FMath::Max(1, FMath::Min(S0.Y, S1.Y) - Pad), By1 = FMath::Min(Ny - 1, FMath::Max(S0.Y, S1.Y) + Pad);
			TArray<double> Best;
			Best.Init(1e18, Dim * Dim);
			TArray<int32> Came;     // parent cell index, -1 = None
			Came.Init(-1, Dim * Dim);
			TArray<uint8> Reached;  // has a "came" entry
			Reached.Init(0, Dim * Dim);
			auto Idx = [Dim](const FIntPoint& C) { return C.Y * Dim + C.X; };
			struct FOpen
			{
				double F; int32 X, Y;
				bool operator>(const FOpen& O) const { return F != O.F ? F > O.F : (X != O.X ? X > O.X : Y > O.Y); }
			};
			std::priority_queue<FOpen, std::vector<FOpen>, std::greater<FOpen>> Todo;
			Best[Idx(S0)] = 0.0;
			Reached[Idx(S0)] = 1;
			Todo.push({ 0.0, S0.X, S0.Y });
			const double BranchCos = std::cos(Radians(Args.MinDeg));
			while (!Todo.empty())
			{
				const FOpen Top = Todo.top();
				Todo.pop();
				const FIntPoint C(Top.X, Top.Y);
				if (C == S1)
				{
					break;
				}
				const double G0 = Best[Idx(C)];
				const int32 ParIdx = Came[Idx(C)];
				const bool bPar = ParIdx >= 0;
				const FIntPoint Par = bPar ? FIntPoint(ParIdx % Dim, ParIdx / Dim) : FIntPoint();
				const double H0 = Hgt(C.X, C.Y);
				for (const FIntPoint& Mv : Moves)
				{
					const FIntPoint N(C.X + Mv.X, C.Y + Mv.Y);
					if (!(Bx0 <= N.X && N.X <= Bx1 && By0 <= N.Y && N.Y <= By1))
					{
						continue;
					}
					const double XV = N.X * RouteCell, YV = N.Y * RouteCell + YMin;
					if (N != S1 && !(RouteEdge <= XV && XV <= W - RouteEdge && YMin + RouteEdge <= YV && YV <= YMin + H - RouteEdge)
						&& Dist(XV, YV, Goal.X, Goal.Y) > 60.0)   // the radio mast stands 44 m from the edge
					{
						continue;
					}
					if (Args.Avoid && Args.Avoid(XV, YV) && N != S1)
					{
						continue;
					}
					const bool bOffRoad = Args.KeepOff && N != S1 && Args.KeepOff(XV, YV);
					const double Length = std::hypot(double(Mv.X), double(Mv.Y)) * RouteCell;
					const double Grade = std::fabs(Hgt(N.X, N.Y) - H0) / Length;
					if (Grade > RouteGradeMax)
					{
						continue;
					}
					double Cost = Length * (1.0 + 60.0 * FMath::Max(0.0, Grade - RouteGradeOk)) + (bOffRoad ? KeepOffCost : 0.0);
					if (!bPar && Args.bStartDir && Args.bStrict)
					{
						// A branch leaves its road square-ish (at 17 degrees the quarry spur ran beside the pass).
						if (std::fabs(Mv.X * Args.SDx + Mv.Y * Args.SDy) / std::hypot(double(Mv.X), double(Mv.Y)) > BranchCos)
						{
							continue;
						}
					}
					if (bPar)
					{
						const int32 Ax = C.X - Par.X, Ay = C.Y - Par.Y;
						const double CosAng = double(Ax * Mv.X + Ay * Mv.Y) / (std::hypot(double(Ax), double(Ay)) * std::hypot(double(Mv.X), double(Mv.Y)));
						Cost += Args.TurnCost * (1.0 - CosAng);
						if (Args.bStrict)
						{
							// No switchback onto a leg closer than SwitchbackMin, checked against the last move and
							// the one before it (a cost, not a ban: the search keeps one parent per cell).
							TArray<TPair<FIntPoint, FIntPoint>, TInlineAllocator<2>> Back = { { Par, C } };
							const int32 GrandIdx = Came[Idx(Par)];
							if (Reached[Idx(Par)] && GrandIdx >= 0)
							{
								Back.Add({ FIntPoint(GrandIdx % Dim, GrandIdx / Dim), Par });
							}
							bool bTooClose = false;
							for (const TPair<FIntPoint, FIntPoint>& B : Back)
							{
								const int32 Bx = B.Value.X - B.Key.X, By = B.Value.Y - B.Key.Y;
								const double Bl = std::hypot(double(Bx), double(By));
								if (double(Bx * Mv.X + By * Mv.Y) / (Bl * std::hypot(double(Mv.X), double(Mv.Y))) < -0.85)   // heading back
								{
									const double Off = std::fabs(double(Bx * (N.Y - B.Key.Y) - By * (N.X - B.Key.X))) / Bl * RouteCell;
									if (Off < SwitchbackMin)
									{
										bTooClose = true;
										break;
									}
								}
							}
							if (bTooClose)
							{
								Cost += SwitchbackCost;
							}
						}
					}
					const double G1 = G0 + Cost;
					if (G1 < Best[Idx(N)])
					{
						Best[Idx(N)] = G1;
						Came[Idx(N)] = Idx(C);
						Reached[Idx(N)] = 1;
						Todo.push({ G1 + std::hypot(double(N.X - S1.X), double(N.Y - S1.Y)) * RouteCell, N.X, N.Y });
					}
				}
			}
			if (!Reached[Idx(S1)])
			{
				if (Args.bStrict && Args.bFallback)   // the rules boxed it in: route without them
				{
					UE_LOG(LogOpenCity, Warning, TEXT("  no route from (%s, %s) to (%s, %s) leaving at %.0f degrees; routing without the rule"),
						*PyRepr(Start.X), *PyRepr(Start.Y), *PyRepr(Goal.X), *PyRepr(Goal.Y), Args.MinDeg);
					FRouteArgs Loose = Args;
					Loose.bStrict = false;
					return RouteRoad(Soft, Start, Goal, Loose);
				}
				return FRouteResult();
			}
			TArray<FIntPoint> Cells = { S1 };
			while (Came[Idx(Cells.Last())] >= 0)
			{
				const int32 P = Came[Idx(Cells.Last())];
				Cells.Add(FIntPoint(P % Dim, P / Dim));
			}
			TArray<FPt> Pts;
			for (int32 k = Cells.Num() - 1; k >= 0; --k)
			{
				Pts.Add({ Cells[k].X * RouteCell, Cells[k].Y * RouteCell + YMin });
			}
			Pts[0] = Start;
			Pts.Last() = Goal;
			// Smooth the grid staircase into curves (Chaikin, 3 rounds), then resample every RouteStep m.
			for (int32 Round = 0; Round < 3; ++Round)
			{
				TArray<FPt> Sm = { Pts[0] };
				for (int32 k = 0; k + 1 < Pts.Num(); ++k)
				{
					const FPt& A = Pts[k];
					const FPt& B = Pts[k + 1];
					Sm.Add({ 0.75 * A.X + 0.25 * B.X, 0.75 * A.Y + 0.25 * B.Y });
					Sm.Add({ 0.25 * A.X + 0.75 * B.X, 0.25 * A.Y + 0.75 * B.Y });
				}
				Sm.Add(Pts.Last());
				Pts = MoveTemp(Sm);
			}
			FRouteResult Res;
			const FTest BulbAvoid = [&Args](double XV, double YV)
			{
				return (Args.Avoid && Args.Avoid(XV, YV)) || (Args.LoopAvoid && Args.LoopAvoid(XV, YV));
			};
			Pts = BulbHairpins(Pts, BulbAvoid, Res.Bulbs);
			for (const TArray<FPt>& Loop : Res.Bulbs)
			{
				UE_LOG(LogOpenCity, Display, TEXT("  hairpin loop at (%.0f, %.0f), %d points"), Loop[Loop.Num() / 2].X, Loop[Loop.Num() / 2].Y, Loop.Num());
			}
			auto InKeep = [&Res](const FPt& P)
			{
				for (const TArray<FPt>& Loop : Res.Bulbs) { for (const FPt& Q : Loop) { if (Q == P) { return true; } } }
				return false;
			};
			TArray<FPt> Out = { Pts[0] };
			double Acc = 0.0;
			for (int32 k = 0; k + 1 < Pts.Num(); ++k)
			{
				Acc += Dist(Pts[k], Pts[k + 1]);
				if (Acc >= RouteStep || InKeep(Pts[k + 1]))
				{
					Out.Add(Pts[k + 1]);
					Acc = 0.0;
				}
			}
			if (Dist(Out.Last(), Goal) > 1.0)
			{
				if (Dist(Out.Last(), Goal) < RouteStep * 0.4) { Out.Last() = Goal; } else { Out.Add(Goal); }
			}
			for (const FPt& P : Out)
			{
				Res.Points.Add({ PyRound(P.X, 1), PyRound(P.Y, 1) });
			}
			Res.bOk = true;
			return Res;
		}

		/** keep_off(x, y) for RouteRoad: true within Margin m of any road already in the graph, except
		 *  within Free m of the route's ends, where it joins the network. */
		FTest RoadMask(const FRoadGraph& G, const TArray<FPt>& Ends, double Margin = 12.0, double Free = 40.0)
		{
			const int32 Nx = int32(W / RouteCell) + 2, Ny = int32(H / RouteCell) + 2;
			TSharedRef<TArray<uint8>> Mask = MakeShared<TArray<uint8>>();
			Mask->Init(0, Nx * Ny);
			const int32 R = int32(std::ceil(Margin / RouteCell));
			TArray<FIntPoint> Offs;
			for (int32 Dx = -R; Dx <= R; ++Dx)
			{
				for (int32 Dy = -R; Dy <= R; ++Dy)
				{
					if (std::hypot(double(Dx), double(Dy)) * RouteCell <= Margin) { Offs.Add({ Dx, Dy }); }
				}
			}
			for (const FSeg& S : G.Segments)
			{
				const FNode& A = G.Nodes[S.A];
				const FNode& B = G.Nodes[S.B];
				const int32 N = FMath::Max(1, int32(Dist(A.X, A.Y, B.X, B.Y) / (RouteCell / 2)));
				for (int32 k = 0; k <= N; ++k)
				{
					const double T = double(k) / N;
					const int32 Cx = int32(PyRoundInt((A.X + (B.X - A.X) * T) / RouteCell));
					const int32 Cy = int32(PyRoundInt((A.Y + (B.Y - A.Y) * T - YMin) / RouteCell));
					for (const FIntPoint& O : Offs)
					{
						const int32 Xx = Cx + O.X, Yy = Cy + O.Y;
						if (0 <= Xx && Xx < Nx && 0 <= Yy && Yy < Ny) { (*Mask)[Yy * Nx + Xx] = 1; }
					}
				}
			}
			return [Mask, Ends, Free, Nx](double XV, double YV)
			{
				for (const FPt& E : Ends)
				{
					if (Dist(XV, YV, E.X, E.Y) < Free) { return false; }
				}
				return (*Mask)[int32(PyRoundInt((YV - YMin) / RouteCell)) * Nx + int32(PyRoundInt(XV / RouteCell))] != 0;
			};
		}

		void GradeChain(FRoadGraph& G, const TArray<int32>& Ids, double Z0, double Z1)
		{
			// Heights along a chain of nodes at a steady grade from Z0 to Z1 (by distance).
			TArray<double> D = { 0.0 };
			for (int32 k = 0; k + 1 < Ids.Num(); ++k)
			{
				D.Add(D.Last() + std::hypot(G.Nodes[Ids[k + 1]].X - G.Nodes[Ids[k]].X, G.Nodes[Ids[k + 1]].Y - G.Nodes[Ids[k]].Y));
			}
			for (int32 k = 0; k < Ids.Num(); ++k)
			{
				G.Nodes[Ids[k]].Z = PyRound(Z0 + (Z1 - Z0) * D[k] / FMath::Max(D.Last(), 1e-6), 2);
			}
		}

		void GradeBulbs(FRoadGraph& G, const TArray<TArray<FPt>>& Bulbs)
		{
			// An even grade round each hairpin loop, from where it leaves one leg to where it joins the other,
			// and the loop widened per D.M. 5/11/2001 5.2.7: E = 45 / R per lane (capped at 2 m).
			const double Extra = 2.0 * FMath::Min(45.0 / HairpinR, 2.0);
			for (const TArray<FPt>& Loop : Bulbs)
			{
				TArray<int32> Raw, Ids;
				for (const FPt& P : Loop) { Raw.Add(G.Node(P.X, P.Y, 2.0)); }
				for (int32 n = 0; n < Raw.Num(); ++n)
				{
					if (n == 0 || Raw[n] != Raw[n - 1]) { Ids.Add(Raw[n]); }
				}
				G.LoopNodes.Append(Ids);
				if (Ids.Num() > 2)
				{
					// The legs pass about 9 m apart at the neck: hold their height difference to what NeckSlope allows
					// across the land between their edges (a deeper step was a sheer wall between them). The grade
					// limiter keeps the pair to it and takes the rest of the climb on the legs.
					FNode& N0 = G.Nodes[Ids[0]];
					FNode& N1 = G.Nodes[Ids.Last()];
					const double Gap = std::hypot(N1.X - N0.X, N1.Y - N0.Y) - RoadKind(TEXT("pass")).Width;
					const double Rise = FMath::Max(Gap, 0.5) * NeckSlope;
					if (std::fabs(N1.Z - N0.Z) > Rise)
					{
						const double Mid = 0.5 * (N0.Z + N1.Z), Half = (N1.Z > N0.Z ? 0.5 : -0.5) * Rise;
						N0.Z = PyRound(Mid - Half, 2);
						N1.Z = PyRound(Mid + Half, 2);
					}
					G.Necks.Add({ FPt(N0.X, N0.Y), FPt(N1.X, N1.Y), Rise });
					GradeChain(G, Ids, N0.Z, N1.Z);
				}
				// Widen only round the loop itself: at the neck the legs are 9 m apart and widened surfaces merged them.
				TSet<TPair<int32, int32>> Pairs;
				for (int32 k = 2; k + 1 < Ids.Num() - 2; ++k)
				{
					Pairs.Add({ FMath::Min(Ids[k], Ids[k + 1]), FMath::Max(Ids[k], Ids[k + 1]) });
				}
				for (FSeg& S : G.Segments)
				{
					if (Pairs.Contains({ FMath::Min(S.A, S.B), FMath::Max(S.A, S.B) }))
					{
						S.Width = RoadKind(S.Kind).Width + Extra;
						S.bWidening = true;
						S.Widening = PyRound(Extra, 2);
					}
				}
			}
		}

		double MaxGrade(const FSeg& S, const FRoadGraph& G)
		{
			// The steepest a segment may be (INFRA_DESIGN.md section 4, by the profile it will get).
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const int32 D = DistrictAt((A.X + B.X) / 2.0, (A.Y + B.Y) / 2.0);
			const FString Dist = D == INDEX_NONE ? FString() : Districts[D].Name;
			if (S.Kind == TEXT("pier")) { return 0.04; }
			if (S.Kind == TEXT("pass")) { return 0.12; }
			if (S.Kind == TEXT("coast") || Dist == TEXT("LeColline") || Dist == TEXT("MonteFerrale")) { return 0.10; }
			if (Dist == TEXT("CittaAlta")) { return (S.Kind == TEXT("street") || S.Kind == TEXT("alley")) ? 0.15 : 0.08; }
			return S.Kind == TEXT("alley") ? 0.10 : 0.08;
		}

		/** Jittered, rotated street grid clipped to land inside the rect. */
		void Grid(FRoadGraph& G, FPyRandom& Rng, const FRect& Rect, int32 Block, double AngleDeg, double Jitter, const FString& Kind,
			const TCHAR* District, double Keep = 1.0)
		{
			const int32 Want = DistrictIndex(District);
			const double Cx = (Rect.X0 + Rect.X1) / 2, Cy = (Rect.Y0 + Rect.Y1) / 2;
			const double Ang = Radians(AngleDeg);
			const int32 Span = int32(FMath::Max(Rect.X1 - Rect.X0, Rect.Y1 - Rect.Y0) / Block) + 2;
			TArray<FIntPoint> Order;
			TMap<FIntPoint, FPt> Pts;
			for (int32 i = -Span; i <= Span; ++i)
			{
				for (int32 j = -Span; j <= Span; ++j)
				{
					const double U = i * Block + Rng.Uniform(-Jitter, Jitter);
					const double V = j * Block + Rng.Uniform(-Jitter, Jitter);
					const double XV = Cx + U * std::cos(Ang) - V * std::sin(Ang);
					const double YV = Cy + U * std::sin(Ang) + V * std::cos(Ang);
					if (Rect.X0 + 8 <= XV && XV <= Rect.X1 - 8 && Rect.Y0 + 8 <= YV && YV <= Rect.Y1 - 8 && IsLand(XV, YV) && DistrictAt(XV, YV) == Want)
					{
						Order.Add({ i, j });
						Pts.Add({ i, j }, { XV, YV });
					}
				}
			}
			for (const FIntPoint& Ij : Order)
			{
				const FPt P = Pts[Ij];
				for (const FIntPoint& Dij : { FIntPoint(1, 0), FIntPoint(0, 1) })
				{
					const FPt* Q = Pts.Find(Ij + Dij);
					if (Q && Rng.Random() < Keep && SegmentOnLand(P.X, P.Y, Q->X, Q->Y))
					{
						const int32 A = G.Node(P.X, P.Y);
						const int32 B = G.Node(Q->X, Q->Y);
						G.Connect(A, B, Kind);
					}
				}
			}
		}

		TArray<FPt> PlanPts(std::initializer_list<FPt> L)
		{
			TArray<FPt> Out;
			for (const FPt& P : L) { Out.Add({ X(P.X), P.Y, true }); }
			return Out;
		}

		// --- Network clean-up ---------------------------------------------------------------------

		void MergeShortSegments(FRoadGraph& G, double MinLen = 4.0)
		{
			// Merge the two ends of any segment shorter than MinLen m: the stubs made one-node-wide zigzags in
			// the road meshes. The end with more roads stays.
			int32 Merged = 0;
			while (true)
			{
				TMap<int32, int32> Degree;
				for (const FSeg& S : G.Segments) { Degree.FindOrAdd(S.A) += 1; Degree.FindOrAdd(S.B) += 1; }
				int32 Victim = INDEX_NONE;
				for (int32 i = 0; i < G.Segments.Num(); ++i)
				{
					const FSeg& S = G.Segments[i];
					if (!S.Bridge.IsEmpty() || S.Kind == TEXT("pier"))
					{
						continue;
					}
					if (Dist(G.Nodes[S.A].X, G.Nodes[S.A].Y, G.Nodes[S.B].X, G.Nodes[S.B].Y) < MinLen)
					{
						Victim = i;
						break;
					}
				}
				if (Victim == INDEX_NONE)
				{
					break;
				}
				const FSeg& V = G.Segments[Victim];
				const int32 KeepN = Degree[V.A] >= Degree[V.B] ? V.A : V.B;
				const int32 Drop = Degree[V.A] >= Degree[V.B] ? V.B : V.A;
				TArray<FSeg> Out;
				TSet<TPair<int32, int32>> Have;
				for (int32 i = 0; i < G.Segments.Num(); ++i)
				{
					if (i == Victim)
					{
						continue;
					}
					FSeg S = G.Segments[i];
					if (S.A == Drop) { S.A = KeepN; }
					if (S.B == Drop) { S.B = KeepN; }
					const TPair<int32, int32> Key(FMath::Min(S.A, S.B), FMath::Max(S.A, S.B));
					if (S.A == S.B || Have.Contains(Key))
					{
						continue;
					}
					Have.Add(Key);
					Out.Add(MoveTemp(S));
				}
				G.Segments = MoveTemp(Out);
				++Merged;
			}
			UE_LOG(LogOpenCity, Display, TEXT("short segments: %d under %.0f m merged away"), Merged, MinLen);
		}

		void FixShallowForks(FRoadGraph& G, double MaxAngle = 30.0)
		{
			// At a junction where two roads leave within MaxAngle degrees of each other, drop the lesser one
			// (lower rank, then shorter) when the network stays connected without it. Bends are not junctions.
			auto ConnectedWithout = [&G](int32 DropUid)
			{
				const FSeg* Drop = G.Segments.FindByPredicate([DropUid](const FSeg& S) { return S.Uid == DropUid; });
				TMap<int32, TArray<int32>> Adj;
				for (const FSeg& S : G.Segments)
				{
					if (S.Uid == DropUid) { continue; }
					Adj.FindOrAdd(S.A).Add(S.B);
					Adj.FindOrAdd(S.B).Add(S.A);
				}
				if (!Adj.Contains(Drop->A) || !Adj.Contains(Drop->B))
				{
					return false;   // one end would be stranded
				}
				TSet<int32> Seen = { Drop->A };
				TArray<int32> Stack = { Drop->A };
				while (!Stack.IsEmpty())
				{
					for (const int32 N : Adj[Stack.Pop()])
					{
						if (!Seen.Contains(N)) { Seen.Add(N); Stack.Add(N); }
					}
				}
				return Seen.Contains(Drop->B);
			};
			int32 Dropped = 0;
			TSet<int32> Tried;
			const double CosMax = std::cos(Radians(MaxAngle));
			while (true)
			{
				TArray<int32> Order;
				TMap<int32, TArray<int32>> Arms;   // node -> segment indices, in first-appearance order
				for (int32 i = 0; i < G.Segments.Num(); ++i)
				{
					for (const int32 N : { G.Segments[i].A, G.Segments[i].B })
					{
						if (!Arms.Contains(N)) { Order.Add(N); }
						Arms.FindOrAdd(N).Add(i);
					}
				}
				int32 VictimUid = 0;
				for (const int32 N : Order)
				{
					const TArray<int32>& Segs = Arms[N];
					if (Segs.Num() < 3)
					{
						continue;
					}
					struct FDir { double X, Y, L; int32 Seg; };
					TArray<FDir> Dirs;
					for (const int32 Si : Segs)
					{
						const FSeg& S = G.Segments[Si];
						const FNode& O = G.Nodes[S.A == N ? S.B : S.A];
						const double Dx = O.X - G.Nodes[N].X, Dy = O.Y - G.Nodes[N].Y;
						double L = std::hypot(Dx, Dy);
						L = L != 0.0 ? L : 1.0;
						Dirs.Add({ Dx / L, Dy / L, L, Si });
					}
					for (int32 i = 0; i < Dirs.Num() && !VictimUid; ++i)
					{
						for (int32 k = i + 1; k < Dirs.Num() && !VictimUid; ++k)
						{
							if (Dirs[i].X * Dirs[k].X + Dirs[i].Y * Dirs[k].Y < CosMax)
							{
								continue;
							}
							FDir Pair[2] = { Dirs[i], Dirs[k] };
							const int32 R0 = RoadKind(G.Segments[Pair[0].Seg].Kind).Rank, R1 = RoadKind(G.Segments[Pair[1].Seg].Kind).Rank;
							if (R1 < R0 || (R1 == R0 && Pair[1].L < Pair[0].L))   // stable sort of two by (rank, length)
							{
								Swap(Pair[0], Pair[1]);
							}
							for (const FDir& D : Pair)
							{
								const FSeg& S = G.Segments[D.Seg];
								if (!S.Bridge.IsEmpty() || S.Kind == TEXT("pier") || Tried.Contains(S.Uid))
								{
									continue;
								}
								Tried.Add(S.Uid);
								if (ConnectedWithout(S.Uid))
								{
									VictimUid = S.Uid;
									break;
								}
							}
						}
					}
					if (VictimUid)
					{
						break;
					}
				}
				if (!VictimUid)
				{
					break;
				}
				G.Segments.RemoveAll([VictimUid](const FSeg& S) { return S.Uid == VictimUid; });
				++Dropped;
			}
			UE_LOG(LogOpenCity, Display, TEXT("shallow forks: %d road(s) dropped that left a junction within %.0f degrees of another"), Dropped, MaxAngle);
		}

		void DropCoincident(FRoadGraph& G, double MaxAngle = 30.0)
		{
			// Drop a street that runs inside another road's corridor (the district grids' edge columns lay
			// along the viale and the coast road at a few degrees). The road that matters stays.
			const double Cell = 50.0;
			TMap<FIntPoint, TSet<int32>> Grid;
			for (int32 j = 0; j < G.Segments.Num(); ++j)
			{
				const FNode& A = G.Nodes[G.Segments[j].A];
				const FNode& B = G.Nodes[G.Segments[j].B];
				for (int32 Gx = int32(PyFloorDiv(FMath::Min(A.X, B.X), Cell)) - 1; Gx < int32(PyFloorDiv(FMath::Max(A.X, B.X), Cell)) + 2; ++Gx)
				{
					for (int32 Gy = int32(PyFloorDiv(FMath::Min(A.Y, B.Y), Cell)) - 1; Gy < int32(PyFloorDiv(FMath::Max(A.Y, B.Y), Cell)) + 2; ++Gy)
					{
						Grid.FindOrAdd({ Gx, Gy }).Add(j);
					}
				}
			}
			TSet<int32> Dropped;
			const double CosMax = std::cos(Radians(MaxAngle));
			for (int32 p = 0; p < G.Segments.Num(); ++p)
			{
				const FSeg& Sp = G.Segments[p];
				if (!Sp.Bridge.IsEmpty() || Sp.Kind == TEXT("pier") || Sp.Kind == TEXT("pass"))
				{
					continue;
				}
				const FNode& A = G.Nodes[Sp.A];
				const FNode& B = G.Nodes[Sp.B];
				const double Lp = Dist(A.X, A.Y, B.X, B.Y);
				if (Lp < 1.0)
				{
					continue;
				}
				TSet<int32> Near;
				for (int32 Gx = int32(PyFloorDiv(FMath::Min(A.X, B.X), Cell)); Gx < int32(PyFloorDiv(FMath::Max(A.X, B.X), Cell)) + 1; ++Gx)
				{
					for (int32 Gy = int32(PyFloorDiv(FMath::Min(A.Y, B.Y), Cell)); Gy < int32(PyFloorDiv(FMath::Max(A.Y, B.Y), Cell)) + 1; ++Gy)
					{
						if (const TSet<int32>* S = Grid.Find({ Gx, Gy })) { Near.Append(*S); }
					}
				}
				for (const int32 q : Near)
				{
					const FSeg& Sq = G.Segments[q];
					if (q == p || Dropped.Contains(q) || !Sq.Bridge.IsEmpty())
					{
						continue;
					}
					const FNode& C = G.Nodes[Sq.A];
					const FNode& D = G.Nodes[Sq.B];
					const double Lq = Dist(C.X, C.Y, D.X, D.Y);
					const int32 Rq = RoadKind(Sq.Kind).Rank, Rp = RoadKind(Sp.Kind).Rank;
					if (Lq < 1.0 || (Rq < Rp || (Rq == Rp && Lq <= Lp)))
					{
						continue;
					}
					const double CosAng = std::fabs(((B.X - A.X) * (D.X - C.X) + (B.Y - A.Y) * (D.Y - C.Y)) / (Lp * Lq));
					if (CosAng < CosMax)
					{
						continue;
					}
					const double Reach = (Sp.Width + Sq.Width) / 2.0;   // edges overlap
					const double Vx = D.X - C.X, Vy = D.Y - C.Y;
					bool bInside = true;
					for (int32 k = 0; k < 5; ++k)
					{
						const double XV = A.X + (B.X - A.X) * k / 4.0, YV = A.Y + (B.Y - A.Y) * k / 4.0;
						const double T = ((XV - C.X) * Vx + (YV - C.Y) * Vy) / (Lq * Lq);
						if (!(-0.05 <= T && T <= 1.05) || std::hypot(XV - C.X - Vx * T, YV - C.Y - Vy * T) > Reach)
						{
							bInside = false;
							break;
						}
					}
					if (bInside)
					{
						Dropped.Add(p);
						break;
					}
				}
			}
			TArray<FSeg> Out;
			for (int32 j = 0; j < G.Segments.Num(); ++j)
			{
				if (!Dropped.Contains(j)) { Out.Add(G.Segments[j]); }
			}
			G.Segments = MoveTemp(Out);
			UE_LOG(LogOpenCity, Display, TEXT("coincident roads: %d street segment(s) dropped from inside another road's corridor"), Dropped.Num());
		}

		void SplitCrossings(FRoadGraph& G, double SameLevel = 2.5, double Snap = 4.0, double BridgeSnap = 15.0)
		{
			// Roads that cross in plan without sharing a node get a junction node at the crossing, and a road
			// end that all but touches another road is joined to it. Graded mountain roads are only joined when
			// the two heights are within SameLevel m; other crossings are reported. A bridge deck that overruns
			// the riverside street by up to BridgeSnap m is shortened to end at the street.
			auto Cross = [](const FNode& O, const FNode& A, const FNode& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
			auto Project = [](const FNode& P, const FNode& A, const FNode& B, double& OutT, double& OutD)
			{
				const double Dx = B.X - A.X, Dy = B.Y - A.Y;
				const double L2 = Dx * Dx + Dy * Dy;
				OutT = L2 != 0.0 ? FMath::Max(0.0, FMath::Min(1.0, ((P.X - A.X) * Dx + (P.Y - A.Y) * Dy) / L2)) : 0.0;
				OutD = std::hypot(P.X - A.X - Dx * OutT, P.Y - A.Y - Dy * OutT);
			};
			const double Cell = 50.0;
			TMap<FIntPoint, int32> CellIdx;
			TArray<TArray<int32>> CellLists;   // in first-insertion order of the cells
			for (int32 j = 0; j < G.Segments.Num(); ++j)
			{
				const FNode& A = G.Nodes[G.Segments[j].A];
				const FNode& B = G.Nodes[G.Segments[j].B];
				for (int32 Gx = int32(PyFloorDiv(FMath::Min(A.X, B.X), Cell)); Gx < int32(PyFloorDiv(FMath::Max(A.X, B.X), Cell)) + 1; ++Gx)
				{
					for (int32 Gy = int32(PyFloorDiv(FMath::Min(A.Y, B.Y), Cell)); Gy < int32(PyFloorDiv(FMath::Max(A.Y, B.Y), Cell)) + 1; ++Gy)
					{
						const int32* Found = CellIdx.Find({ Gx, Gy });
						const int32 Ci = Found ? *Found : CellLists.AddDefaulted();
						if (!Found) { CellIdx.Add({ Gx, Gy }, Ci); }
						CellLists[Ci].Add(j);
					}
				}
			}
			TMap<int32, TArray<TPair<double, int32>>> Splits;   // segment -> (t, node)
			TMap<int32, TArray<int32>> Joined;                   // node -> segments it was joined into
			struct FUnbridged { int32 P, Q; double X, Y, Za, Zb; };
			TArray<FUnbridged> Unbridged;
			int32 Crossings = 0, Touches = 0;
			TSet<uint64> Done;
			for (const TArray<int32>& Js : CellLists)
			{
				for (int32 i = 0; i < Js.Num(); ++i)
				{
					for (int32 k = i + 1; k < Js.Num(); ++k)
					{
						const int32 p = FMath::Min(Js[i], Js[k]), q = FMath::Max(Js[i], Js[k]);
						bool bDone = false;
						Done.Add((uint64(p) << 32) | uint64(q), &bDone);
						if (bDone)
						{
							continue;
						}
						const FSeg Sp = G.Segments[p];
						const FSeg Sq = G.Segments[q];
						if (Sp.A == Sq.A || Sp.A == Sq.B || Sp.B == Sq.A || Sp.B == Sq.B)
						{
							continue;
						}
						// A road end that all but touches the other road: join it there.
						bool bTouched = false;
						const int32 Tn[4] = { Sp.A, Sp.B, Sq.A, Sq.B };
						const int32 Tseg[4] = { q, q, p, p };
						const int32 Te[4][2] = { { Sq.A, Sq.B }, { Sq.A, Sq.B }, { Sp.A, Sp.B }, { Sp.A, Sp.B } };
						for (int32 m = 0; m < 4; ++m)
						{
							double T, D;
							Project(G.Nodes[Tn[m]], G.Nodes[Te[m][0]], G.Nodes[Te[m][1]], T, D);
							const TArray<int32>* J = Joined.Find(Tn[m]);
							if (D < Snap && 0.02 < T && T < 0.98 && !(J && J->Contains(Tseg[m])))
							{
								Splits.FindOrAdd(Tseg[m]).Add({ T, Tn[m] });
								Joined.FindOrAdd(Tn[m]).Add(Tseg[m]);
								++Touches;
								bTouched = true;
							}
						}
						if (bTouched)
						{
							continue;
						}
						const FNode A = G.Nodes[Sp.A], B = G.Nodes[Sp.B], C = G.Nodes[Sq.A], D = G.Nodes[Sq.B];
						const double D1 = Cross(C, D, A), D2 = Cross(C, D, B), D3 = Cross(A, B, C), D4 = Cross(A, B, D);
						if (!(((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0))) || D1 == D2 || D3 == D4)
						{
							continue;
						}
						const double T = D1 / (D1 - D2), U = D3 / (D3 - D4);
						const double XV = A.X + (B.X - A.X) * T, YV = A.Y + (B.Y - A.Y) * T;
						const double Za = A.Z + (B.Z - A.Z) * T, Zb = C.Z + (D.Z - C.Z) * U;
						if ((Sp.Kind == TEXT("pass") || Sq.Kind == TEXT("pass")) && std::fabs(Za - Zb) > SameLevel)
						{
							Unbridged.Add({ p, q, XV, YV, Za, Zb });
							continue;
						}
						// A bridge deck overrunning the street at its end: pull the deck's end node to the street.
						int32 MovedSeg = INDEX_NONE, MovedEnd = INDEX_NONE;
						const int32 Bs[2] = { p, q };
						const double Bt[2] = { T, U };
						for (int32 m = 0; m < 2; ++m)
						{
							const FSeg& S = G.Segments[Bs[m]];
							if (!S.Bridge.IsEmpty())
							{
								const int32 End = Bt[m] < 0.5 ? S.A : S.B;
								if (Dist(G.Nodes[End].X, G.Nodes[End].Y, XV, YV) < BridgeSnap)
								{
									G.Nodes[End].X = PyRound(XV, 1);
									G.Nodes[End].Y = PyRound(YV, 1);
									MovedSeg = Bs[m];
									MovedEnd = End;
								}
							}
						}
						if (MovedSeg != INDEX_NONE)
						{
							const int32 Other = MovedSeg == p ? q : p;
							Splits.FindOrAdd(Other).Add({ Other == q ? U : T, MovedEnd });
							++Crossings;
							continue;
						}
						const int32 N = G.Node(XV, YV, 1.0);
						G.Nodes[N].Z = PyRound((Za + Zb) / 2.0, 2);
						Splits.FindOrAdd(p).Add({ T, N });
						Splits.FindOrAdd(q).Add({ U, N });
						++Crossings;
					}
				}
			}
			TSet<TPair<int32, int32>> Have;
			for (const FSeg& S : G.Segments) { Have.Add({ FMath::Min(S.A, S.B), FMath::Max(S.A, S.B) }); }
			TArray<FSeg> Out;
			for (int32 j = 0; j < G.Segments.Num(); ++j)
			{
				const FSeg& S = G.Segments[j];
				TArray<TPair<double, int32>>* Sp = Splits.Find(j);
				if (!Sp)
				{
					Out.Add(S);
					continue;
				}
				Sp->Sort([](const TPair<double, int32>& L, const TPair<double, int32>& R) { return L.Key != R.Key ? L.Key < R.Key : L.Value < R.Value; });
				TArray<int32> Chain = { S.A };
				for (const TPair<double, int32>& P : *Sp) { Chain.Add(P.Value); }
				Chain.Add(S.B);
				const TPair<int32, int32> Own(FMath::Min(S.A, S.B), FMath::Max(S.A, S.B));
				for (int32 k = 0; k + 1 < Chain.Num(); ++k)
				{
					const int32 A = Chain[k], B = Chain[k + 1];
					const TPair<int32, int32> Key(FMath::Min(A, B), FMath::Max(A, B));
					if (A == B || (Have.Contains(Key) && Key != Own))
					{
						continue;
					}
					FSeg Ns = G.Copy(S);
					Ns.A = A;
					Ns.B = B;
					Out.Add(MoveTemp(Ns));
					Have.Add(Key);
				}
			}
			G.Segments = MoveTemp(Out);
			for (const FUnbridged& U : Unbridged)
			{
				UE_LOG(LogOpenCity, Warning, TEXT("  roads cross without a junction at (%.0f, %.0f): heights %.1f and %.1f m; needs a bridge or another route"), U.X, U.Y, U.Za, U.Zb);
			}
			UE_LOG(LogOpenCity, Display, TEXT("crossings: %d junction(s) added where roads crossed, %d road end(s) joined to the road they touched, %d unbridged crossing(s) left"),
				Crossings, Touches, Unbridged.Num());
		}
	}

	int32 LimitGrades(FRoadGraph& G, const FTerrainInfo& Info)
	{
		// Hold every road node within its roads' maximum grade of its neighbours (iterated to a fixed
		// point), so roads stop inheriting the hillside's slope; the terrain is then cut and filled to the
		// roads. Bridge ends and the dam crest stay where they are.
		const double Crest = Info.ReservoirLevel + 2.0;
		TSet<int32> Pinned;
		for (const FSeg& S : G.Segments)
		{
			if (!S.Bridge.IsEmpty()) { Pinned.Add(S.A); Pinned.Add(S.B); }
		}
		for (int32 i = 0; i < G.Nodes.Num(); ++i)
		{
			if (std::fabs(G.Nodes[i].Y - DamY) < 2.0 && std::fabs(G.Nodes[i].Z - Crest) < 0.05) { Pinned.Add(i); }
		}
		struct FEdge { int32 A, B; double Rise; };
		TArray<FEdge> Edges;
		for (const FSeg& S : G.Segments)
		{
			const double Run = std::hypot(G.Nodes[S.B].X - G.Nodes[S.A].X, G.Nodes[S.B].Y - G.Nodes[S.A].Y);
			Edges.Add({ S.A, S.B, MaxGrade(S, G) * FMath::Max(Run, 1.0) });
		}
		// Hairpin-loop necks: the two legs keep within their allowed height difference, so the climb the loop
		// no longer makes is taken on the legs rather than pulled back across the neck.
		auto NodeAt = [&G](const FPt& P)
		{
			int32 Best = INDEX_NONE;
			double BestD = 3.0;
			for (int32 i = 0; i < G.Nodes.Num(); ++i)
			{
				const double D = std::hypot(G.Nodes[i].X - P.X, G.Nodes[i].Y - P.Y);
				if (D < BestD) { BestD = D; Best = i; }
			}
			return Best;
		};
		for (const FRoadGraph::FNeck& Neck : G.Necks)
		{
			const int32 A = NodeAt(Neck.A), B = NodeAt(Neck.B);
			if (A != INDEX_NONE && B != INDEX_NONE && A != B)
			{
				Edges.Add({ A, B, Neck.Rise });
			}
		}
		for (int32 It = 0; It < 400; ++It)
		{
			double Moved = 0.0;
			for (const FEdge& E : Edges)
			{
				const double Za = G.Nodes[E.A].Z, Zb = G.Nodes[E.B].Z;
				const double Excess = std::fabs(Zb - Za) - E.Rise;
				if (Excess <= 0.005)
				{
					continue;
				}
				const bool Fa = !Pinned.Contains(E.A), Fb = !Pinned.Contains(E.B);
				if (!(Fa || Fb))
				{
					continue;
				}
				const double Share = Excess / (int32(Fa) + int32(Fb));
				const double Sign = Zb > Za ? 1.0 : -1.0;
				if (Fa) { G.Nodes[E.A].Z = PyRound(Za + Sign * Share, 3); }
				if (Fb) { G.Nodes[E.B].Z = PyRound(Zb - Sign * Share, 3); }
				Moved = FMath::Max(Moved, Excess);
			}
			if (Moved < 0.01)
			{
				break;
			}
		}
		int32 Steep = 0;
		for (const FEdge& E : Edges)
		{
			if (std::fabs(G.Nodes[E.B].Z - G.Nodes[E.A].Z) - E.Rise > 0.05) { ++Steep; }
		}
		return Steep;
	}

	void DropOrphanNodes(FRoadGraph& G)
	{
		// Remove nodes no segment uses any more, and renumber the segments' node indices.
		TSet<int32> UsedSet;
		for (const FSeg& S : G.Segments) { UsedSet.Add(S.A); UsedSet.Add(S.B); }
		TArray<int32> Used = UsedSet.Array();
		Used.Sort();
		TMap<int32, int32> Remap;
		TArray<FNode> Nodes;
		for (int32 New = 0; New < Used.Num(); ++New)
		{
			Remap.Add(Used[New], New);
			Nodes.Add(G.Nodes[Used[New]]);
		}
		G.Nodes = MoveTemp(Nodes);
		for (FSeg& S : G.Segments) { S.A = Remap[S.A]; S.B = Remap[S.B]; }
	}

	void BuildRoads(FRoadGraph& G, const FGridD& Terrain, const FTerrainInfo& Info, FPyRandom& Rng)
	{
		G.Terrain = &Terrain;
		// District grids.
		Grid(G, Rng, { X(60), 240, X(975), 880 }, 95, 0, 8, TEXT("street"), TEXT("PortoVecchio"));
		Grid(G, Rng, { X(60), 880, X(975), 1680 }, 62, 14, 10, TEXT("alley"), TEXT("CittaAlta"), 0.85);
		Grid(G, Rng, { X(1030), 900, X(1980), 1680 }, 125, 0, 6, TEXT("street"), TEXT("LeFornaci"));
		Grid(G, Rng, { X(1160), 160, X(1980), 900 }, 105, -6, 6, TEXT("street"), TEXT("Lungomare"));
		Grid(G, Rng, { X(1030), 160, X(1160), 900 }, 60, 20, 8, TEXT("alley"), TEXT("BorgoMarino"), 0.8);
		Grid(G, Rng, { X(300), -230, X(820), 40 }, 110, 0, 0, TEXT("pier"), TEXT("MoloSalvi"));

		// Arterials.
		G.Path(PlanPts({ { 140, 250 }, { 140, 560 }, { 150, 880 }, { 180, 1200 }, { 220, 1520 } }), TEXT("avenue"));   // west avenue
		G.Path(PlanPts({ { 950, 260 }, { 930, 560 }, { 935, 880 } }), TEXT("avenue"));                                  // port riverside
		G.Path(PlanPts({ { 1200, 170 }, { 1500, 175 }, { 1800, 185 }, { 1960, 200 } }), TEXT("avenue"));                // Lungomare promenade
		G.Path(PlanPts({ { 1080, 960 }, { 1300, 950 }, { 1600, 945 }, { 1950, 950 } }), TEXT("avenue"));                // east avenue
		G.Path(PlanPts({ { 1100, 300 }, { 1095, 600 }, { 1090, 900 }, { 1085, 1250 }, { 1080, 1600 } }), TEXT("avenue")); // east riverside

		// Bridges over the Fiume Ardo (named, with gate tags where they are gates).
		auto Bridge = [&G](const TCHAR* Name, double Y, const TCHAR* Kind, const TCHAR* Gate)
		{
			const double Wv = RiverX(Y), Hw = RiverHalfWidth(Y);
			const int32 A = G.Node(Wv - Hw - 30, Y);
			const int32 B = G.Node(Wv + Hw + 30, Y);
			G.Connect(A, B, Kind, Name, Gate);
		};
		Bridge(TEXT("PonteSanRocco"), 1250, TEXT("avenue"), TEXT(""));
		Bridge(TEXT("PonteMobile"), 350, TEXT("avenue"), TEXT("Gate.PonteMobile"));
		Bridge(TEXT("Passerella"), 800, TEXT("alley"), TEXT(""));             // footbridge (pedestrian: AnnotateNetworks)
		Bridge(TEXT("PonteDelleColline"), 1900, TEXT("street"), TEXT("Gate.Colline"));
		// Swing bridge to Molo Salvi across the Darsena.
		G.Path(PlanPts({ { 560, 260 }, { 560, 30 } }), TEXT("pier"), 6.0, TEXT("PonteGirevole"), TEXT("Gate.Molo"));

		// Coast road: up the west cliffs from the port to the hills, with curves.
		G.Path(PlanPts({ { 90, 300 }, { 70, 700 }, { 75, 1100 }, { 95, 1450 }, { 160, 1700 }, { 300, 1850 }, { 420, 1960 }, { 600, 2030 },
			{ 850, 2000 }, { 1000, 1930 } }), TEXT("coast"));
		// Hills roads and gates. The Villa Salvi road starts on the centro grid itself.
		TArray<FPt> Villa = PlanPts({ { 520, 1660 }, { 540, 1760 }, { 480, 1900 }, { 450, 1960 } });
		{
			int32 Near = 0;
			for (int32 i = 1; i < G.Nodes.Num(); ++i)
			{
				if (Dist(G.Nodes[i].X, G.Nodes[i].Y, Villa[0].X, Villa[0].Y) < Dist(G.Nodes[Near].X, G.Nodes[Near].Y, Villa[0].X, Villa[0].Y)) { Near = i; }
			}
			if (Dist(G.Nodes[Near].X, G.Nodes[Near].Y, Villa[0].X, Villa[0].Y) < 45.0)
			{
				Villa[0] = { G.Nodes[Near].X, G.Nodes[Near].Y };
			}
		}
		G.Path(Villa, TEXT("street"), 6.0, TEXT(""), TEXT("Gate.Colline"));   // to Villa Salvi
		G.Path(PlanPts({ { 1400, 1660 }, { 1380, 1800 }, { 1300, 1900 }, { 1180, 1905 } }), TEXT("street"), 6.0, TEXT(""), TEXT("Gate.Colline"));

		// Monte Ferrale roads, routed over the terrain: the pass from the hills checkpoint up to the dam's
		// east end, then straight along the dam crest; the quarry spur, the radio-mast road and the
		// hermitage road. Routed roads follow the land and switch back by themselves where it gets steep.
		const FGridD Soft = RouteSoft(Terrain);
		const double Crest = Info.ReservoirLevel + 2.0;
		const double Rcx = (Reservoir.X0 + Reservoir.X1) / 2, Rcy = (Reservoir.Y0 + Reservoir.Y1) / 2;
		const double Rax = (Reservoir.X1 - Reservoir.X0) / 2 * 1.2, Ray = (Reservoir.Y1 - Reservoir.Y0) / 2 * 1.2;
		const FTest Wet = [=](double XV, double YV)
		{
			if (std::pow((XV - Rcx) / Rax, 2.0) + std::pow((YV - Rcy) / Ray, 2.0) < 1.0)
			{
				return true;   // the reservoir
			}
			return YV < DamY + 20 && std::fabs(XV - RiverX(YV)) < RiverHalfWidth(YV) + 14;
		};
		const FPt QuarryEnd(X(1700), 2380, true);
		const double Qw = 120.0 / 2 + 15, Qd = 80.0 / 2 + 15;   // La Cava's yard plus a margin
		const FTest WetOrQuarry = [=](double XV, double YV)
		{
			// Only the spur enters the quarry yard: its gravel piles and conveyor stand in it.
			return Wet(XV, YV) || (std::fabs(XV - QuarryEnd.X) < Qw && std::fabs(YV - QuarryEnd.Y) < Qd);
		};
		const FPt DamE(X(1395), DamY, true), DamW(X(870), DamY, true);
		// The climb ends just east of the dam where the land is already at crest height.
		const FPt Abutment(DamE.X + 60.0, DamY);
		FRouteArgs ClimbArgs;
		ClimbArgs.Avoid = WetOrQuarry;
		FRouteResult Climb = RouteRoad(Soft, FPt(X(1300), 1900, true), Abutment, ClimbArgs);   // from the hills road's end node
		check(Climb.bOk);
		// Routed roads merge nodes only within 2 m: hairpin loops pass 9 m from the other leg.
		TArray<FPt> PassPts = Climb.Points;
		PassPts.Append({ DamE, FPt(X(1130), DamY, true), DamW });
		const TArray<int32> PassIds = G.Path(PassPts, TEXT("pass"), 2.0, TEXT(""), TEXT("Gate.Ferrale"));
		GradeBulbs(G, Climb.Bulbs);
		const int32 NClimb = Climb.Points.Num();
		GradeChain(G, { PassIds[NClimb - 1], PassIds[NClimb] }, G.Nodes[PassIds[NClimb - 1]].Z, Crest);
		for (int32 i = NClimb; i < PassIds.Num(); ++i)
		{
			G.Nodes[PassIds[i]].Z = PyRound(Crest, 2);   // along the dam crest
		}
		// Branches start from the pass node from which the link is short but not steep.
		auto BranchFrom = [&](const FPt& Target)
		{
			const double Tz = G.Height(Target.X, Target.Y);
			TArray<int32> Cands(PassIds.GetData(), NClimb);
			TArray<double> Cost;
			for (const int32 i : Cands)
			{
				const double D = Dist(G.Nodes[i].X, G.Nodes[i].Y, Target.X, Target.Y);
				const double Grade = std::fabs(G.Nodes[i].Z - Tz) / FMath::Max(D, 1.0);
				Cost.Add((D >= 150.0 ? D : 1e9) * (1.0 + 10.0 * FMath::Max(0.0, Grade - RouteGradeOk)));
			}
			TArray<int32> Order;
			for (int32 k = 0; k < Cands.Num(); ++k) { Order.Add(k); }
			Order.StableSort([&Cost](int32 L, int32 R) { return Cost[L] < Cost[R]; });
			TArray<int32> Out;
			for (const int32 k : Order) { if (!G.LoopNodes.Contains(Cands[k])) { Out.Add(Cands[k]); } }   // no road joins a hairpin loop
			return Out;
		};
		// The quarry yard has one entrance, in the middle of its north side: the spur arrives there and the
		// radio-mast road leaves from it. Routed roads keep off roads already built.
		const FPt GateN(QuarryEnd.X, QuarryEnd.Y + Qd + 3.0);
		const TArray<int32> QCands = BranchFrom(GateN);
		const FTest NearRoad = [&G](double XV, double YV)   // hairpin loops keep 14 m from roads already built
		{
			for (const FNode& N : G.Nodes)
			{
				if ((N.X - XV) * (N.X - XV) + (N.Y - YV) * (N.Y - YV) < 14.0 * 14.0) { return true; }
			}
			return false;
		};
		auto Tangent = [&G](const TArray<int32>& Ids, int32 Node)
		{
			const int32 K = Ids.IndexOfByKey(Node);
			const FNode& A = G.Nodes[Ids[FMath::Max(0, K - 1)]];
			const FNode& B = G.Nodes[Ids[FMath::Min(Ids.Num() - 1, K + 1)]];
			double L = std::hypot(B.X - A.X, B.Y - A.Y);
			L = L != 0.0 ? L : 1.0;
			return FVector2D((B.X - A.X) / L, (B.Y - A.Y) / L);
		};
		// The spur keeps BranchLoopFree off the pass's hairpin loops: its first 40 m may cross roads (RoadMask),
		// and from a node 36 m before a loop it ran under the loop's neck with no room for the slope between.
		const FTest WetOrQuarryOrLoop = [&](double XV, double YV)
		{
			for (const TArray<FPt>& Loop : Climb.Bulbs)
			{
				for (const FPt& P : Loop) { if (Dist(XV, YV, P.X, P.Y) < BranchLoopFree) { return true; } }
			}
			return WetOrQuarry(XV, YV);
		};
		// The best branch points first; the first from which the spur can leave the pass at BranchMinDeg wins,
		// then at BranchFallbackDeg (the hillside beside the pass is too steep to leave it square anywhere but
		// next to a hairpin loop), and last the best point with no angle rule.
		FRouteResult Spur;
		int32 Q0 = QCands[0];
		auto TrySpur = [&](int32 Cand, double MinDeg, bool bFallback)
		{
			const FPt P(G.Nodes[Cand].X, G.Nodes[Cand].Y);
			FRouteArgs A;
			A.Avoid = WetOrQuarryOrLoop;
			A.LoopAvoid = NearRoad;
			A.KeepOff = RoadMask(G, { P, GateN });
			const FVector2D Sd = Tangent(PassIds, Cand);
			A.bStartDir = true;
			A.SDx = Sd.X;
			A.SDy = Sd.Y;
			A.MinDeg = MinDeg;
			A.bFallback = bFallback;
			return RouteRoad(Soft, P, GateN, A);
		};
		for (const double MinDeg : { BranchMinDeg, BranchFallbackDeg })
		{
			for (int32 c = 0; c < FMath::Min(40, QCands.Num()) && !Spur.bOk; ++c)
			{
				Spur = TrySpur(QCands[c], MinDeg, false);
				Q0 = QCands[c];
			}
			if (Spur.bOk)
			{
				UE_LOG(LogOpenCity, Display, TEXT("quarry spur leaves the pass at %.0f degrees or more"), MinDeg);
				break;
			}
		}
		if (!Spur.bOk)
		{
			Q0 = QCands[0];
			Spur = TrySpur(Q0, BranchMinDeg, true);
		}
		check(Spur.bOk);
		const FPt Q0p(G.Nodes[Q0].X, G.Nodes[Q0].Y);
		UE_LOG(LogOpenCity, Display, TEXT("quarry spur leaves the pass at (%.0f, %.0f)"), Q0p.X, Q0p.Y);
		TArray<FPt> QuarryPts = { Q0p };
		for (int32 k = 1; k < Spur.Points.Num(); ++k) { QuarryPts.Add(Spur.Points[k]); }
		QuarryPts.Add(QuarryEnd);
		const TArray<int32> Quarry = G.Path(QuarryPts, TEXT("pass"), 2.0);
		GradeBulbs(G, Spur.Bulbs);
		// The service road ends at a turning area 70 m below the mast: the ridge rises 150 m in 150 m
		// against the map edge.
		const FPt MastEnd(X(1820), 2610, true);
		FRouteArgs MastArgs;
		MastArgs.Avoid = WetOrQuarry;
		MastArgs.LoopAvoid = NearRoad;
		MastArgs.KeepOff = RoadMask(G, { GateN, MastEnd });
		const FVector2D MSd = Tangent(Quarry, Quarry[Quarry.Num() - 2]);
		MastArgs.bStartDir = true;
		MastArgs.SDx = MSd.X;
		MastArgs.SDy = MSd.Y;
		const FRouteResult MastR = RouteRoad(Soft, GateN, MastEnd, MastArgs);
		check(MastR.bOk);
		const TArray<int32> Mast = G.Path(MastR.Points, TEXT("pass"), 2.0);
		GradeBulbs(G, MastR.Bulbs);
		const FPt HermitageEnd(X(500), 2640, true);
		FRouteArgs HermArgs;
		HermArgs.Avoid = [=](double XV, double YV)   // the first leg may not double back east along the dam
		{
			return Wet(XV, YV) || (Dist(XV, YV, DamW.X, DamW.Y) < 70.0 && XV > DamW.X + 8.0);
		};
		HermArgs.LoopAvoid = NearRoad;
		HermArgs.KeepOff = RoadMask(G, { DamW, HermitageEnd });
		const FRouteResult HermR = RouteRoad(Soft, DamW, HermitageEnd, HermArgs);
		check(HermR.bOk);
		const TArray<int32> Hermitage = G.Path(HermR.Points, TEXT("pass"), 2.0);
		GradeBulbs(G, HermR.Bulbs);
		auto TurningCircle = [&G](const TArray<int32>& Ids)
		{
			// A turning circle at a dead end: a ring beyond the last node, level with it. Traffic can't U-turn
			// on a 7 m mountain road; with the ring the end is no longer a dead end.
			const double R = HairpinR;
			const int32 N = 10;
			const FNode E = G.Nodes[Ids.Last()], P = G.Nodes[Ids[Ids.Num() - 2]];
			const double Dx = E.X - P.X, Dy = E.Y - P.Y;
			double L = std::hypot(Dx, Dy);
			L = L != 0.0 ? L : 1.0;
			const double Cx = E.X + Dx / L * R, Cy = E.Y + Dy / L * R;
			const double A0 = std::atan2(E.Y - Cy, E.X - Cx);
			TArray<int32> Ring = { Ids.Last() };
			for (int32 k = 1; k < N; ++k)
			{
				Ring.Add(G.Node(Cx + R * std::cos(A0 + 2 * Pi * k / N), Cy + R * std::sin(A0 + 2 * Pi * k / N), 1.0));
			}
			for (const int32 i : Ring) { G.Nodes[i].Z = E.Z; }
			for (int32 k = 0; k < Ring.Num(); ++k) { G.Connect(Ring[k], Ring[(k + 1) % Ring.Num()], TEXT("pass")); }
			G.LoopNodes.Append(Ring);
		};
		TurningCircle(Mast);
		TurningCircle(Hermitage);
		UE_LOG(LogOpenCity, Display, TEXT("hairpin loops: %d on the pass (radius %.0f m)"), Climb.Bulbs.Num(), HairpinR);
		UE_LOG(LogOpenCity, Display, TEXT("mountain roads: pass %d nodes, quarry %d, mast %d, hermitage %d"), Climb.Points.Num(), Quarry.Num(), Mast.Num(), Hermitage.Num());

		// Stitch: join every road end that is still loose and the edge nodes of each district to the
		// nearest node of a neighbouring district or grid.
		auto Nearest = [&G](int32 i, double MaxD, bool bOtherDistrict, const TSet<int32>& Exclude)
		{
			const double XV = G.Nodes[i].X, YV = G.Nodes[i].Y;
			const int32 Di = DistrictAt(XV, YV);
			int32 BestJ = INDEX_NONE;
			double Bd = MaxD * MaxD;
			for (int32 j = 0; j < G.Nodes.Num(); ++j)
			{
				if (j == i || Exclude.Contains(j) || G.LoopNodes.Contains(j))
				{
					continue;
				}
				if (bOtherDistrict && DistrictAt(G.Nodes[j].X, G.Nodes[j].Y) == Di)
				{
					continue;
				}
				const double Dd = (G.Nodes[j].X - XV) * (G.Nodes[j].X - XV) + (G.Nodes[j].Y - YV) * (G.Nodes[j].Y - YV);
				if (Dd < Bd && SegmentOnLand(XV, YV, G.Nodes[j].X, G.Nodes[j].Y))
				{
					BestJ = j;
					Bd = Dd;
				}
			}
			return BestJ;
		};
		auto NearestFoot = [&G](int32 i, double MaxD, FPt& OutFoot, double& OutD)
		{
			// Nearest point on the interior of another road's segment (a T-junction onto it).
			const double XV = G.Nodes[i].X, YV = G.Nodes[i].Y;
			bool bFound = false;
			OutD = MaxD;
			for (const FSeg& S : G.Segments)
			{
				if (!S.Bridge.IsEmpty() || S.Kind == TEXT("pier") || S.A == i || S.B == i || G.LoopNodes.Contains(S.A) || G.LoopNodes.Contains(S.B))
				{
					continue;
				}
				const FNode& A = G.Nodes[S.A];
				const FNode& B = G.Nodes[S.B];
				const double Vx = B.X - A.X, Vy = B.Y - A.Y;
				const double L2 = Vx * Vx + Vy * Vy;
				if (L2 < 1.0)
				{
					continue;
				}
				const double T = ((XV - A.X) * Vx + (YV - A.Y) * Vy) / L2;
				if (!(0.1 < T && T < 0.9))
				{
					continue;
				}
				const double Fx = A.X + Vx * T, Fy = A.Y + Vy * T;
				const double D = std::hypot(Fx - XV, Fy - YV);
				if (D < OutD && SegmentOnLand(XV, YV, Fx, Fy))
				{
					OutFoot = { Fx, Fy };
					OutD = D;
					bFound = true;
				}
			}
			return bFound;
		};
		TArray<int32> Deg;
		Deg.Init(0, G.Nodes.Num());
		for (const FSeg& S : G.Segments) { ++Deg[S.A]; ++Deg[S.B]; }
		TSet<int32> PassEnd;
		for (const FSeg& S : G.Segments)
		{
			if (S.Kind == TEXT("pass")) { PassEnd.Add(S.A); PassEnd.Add(S.B); }
		}
		const int32 NNodes = G.Nodes.Num();
		for (int32 i = 0; i < NNodes; ++i)
		{
			if (Deg[i] > 1 || PassEnd.Contains(i))   // loose ends only (mountain road ends stay dead ends)
			{
				continue;
			}
			TSet<int32> Linked;
			for (const FSeg& S : G.Segments)
			{
				if (S.A == i) { Linked.Add(S.B); }
				if (S.B == i) { Linked.Add(S.A); }
			}
			const int32 J = Nearest(i, 120.0, false, Linked);
			// A right-angle T onto the road itself is used when it is shorter than joining the nearest node.
			FPt Foot;
			double Fd;
			const bool bFoot = NearestFoot(i, 120.0, Foot, Fd);
			if (bFoot && (J == INDEX_NONE || Fd + 5.0 < Dist(G.Nodes[i].X, G.Nodes[i].Y, G.Nodes[J].X, G.Nodes[J].Y)))
			{
				const int32 F = G.Node(Foot.X, Foot.Y, 3.0);   // SplitCrossings joins it to the road
				G.Connect(i, F, TEXT("street"));
			}
			else if (J != INDEX_NONE)
			{
				G.Connect(i, J, TEXT("street"));
			}
		}
		// Border links between neighbouring districts, about every 150 m.
		const int32 Snapshot = G.Nodes.Num();
		for (int32 i = 0; i < Snapshot; ++i)
		{
			const int32 J = Nearest(i, 75.0, true, TSet<int32>());
			if (J != INDEX_NONE && Rng.Random() < 0.6)
			{
				G.Connect(i, J, TEXT("street"));
			}
		}

		DropCoincident(G);
		MergeShortSegments(G);
		SplitCrossings(G);
		MergeShortSegments(G);
		SplitCrossings(G);   // a junction node a metre off a road's line can leave one crossing; none after this
		FixShallowForks(G);

		// Gates: every road that crosses between districts is closed by the gate of whichever side unlocks
		// later in the story. Explicit tags (bridges) are kept.
		const TMap<FString, int32> UnlockRank = { { TEXT("CittaAlta"), 0 }, { TEXT("LeFornaci"), 0 }, { TEXT("PortoVecchio"), 1 },
			{ TEXT("LeColline"), 2 }, { TEXT("MonteFerrale"), 3 }, { TEXT("Lungomare"), 4 }, { TEXT("BorgoMarino"), 4 }, { TEXT("MoloSalvi"), 5 } };
		const TMap<FString, FString> DistrictGate = { { TEXT("PortoVecchio"), TEXT("Gate.PortCordon") }, { TEXT("LeColline"), TEXT("Gate.Colline") },
			{ TEXT("MonteFerrale"), TEXT("Gate.Ferrale") }, { TEXT("Lungomare"), TEXT("Gate.RailCrossing") },
			{ TEXT("BorgoMarino"), TEXT("Gate.RailCrossing") }, { TEXT("MoloSalvi"), TEXT("Gate.Molo") } };
		for (FSeg& S : G.Segments)
		{
			if (!S.Gate.IsEmpty())
			{
				continue;
			}
			const int32 Da = DistrictAt(G.Nodes[S.A].X, G.Nodes[S.A].Y), Db = DistrictAt(G.Nodes[S.B].X, G.Nodes[S.B].Y);
			if (Da == Db || Da == INDEX_NONE || Db == INDEX_NONE)
			{
				continue;
			}
			const FString NA = Districts[Da].Name, NB = Districts[Db].Name;
			const FString Later = UnlockRank[NA] >= UnlockRank[NB] ? NA : NB;
			if (UnlockRank[Later] > 0)
			{
				S.Gate = DistrictGate[Later];
			}
		}
	}
}
