// Landmarks, retaining walls, buildings, road metadata (INFRA_DESIGN.md), junction control from the
// D.M. 19/4/2006 sight triangles, the water network, and the pipeline that writes the plan files.
#pragma clang fp contract(off)

#include "Layout/OCMareviaLayout.h"

#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "OCLog.h"
#include <cmath>
#include <queue>
#include <vector>

namespace OCLayout
{
	const TArray<FString> BuildingStyles = { TEXT("oldtown"), TEXT("grid90"), TEXT("grid120"), TEXT("beach"), TEXT("village"), TEXT("terminal"), TEXT("hills") };

	namespace
	{
		constexpr double LandmarkRoadMargin = 3.0;   // m kept clear between a landmark's footprint and a road's edge
		constexpr double LandmarkSearch = 150.0;     // m: furthest a landmark moves from its planned spot
		constexpr double StoreyH = 3.2;              // m floor to floor (KIT_SPEC.md)
		constexpr double Bay = 2.0;                  // m: building widths and depths are whole multiples of this
		constexpr double MinFootprint = 4.0;
		constexpr double WallStep = 4.0;             // m: retaining-wall panels are 4 m long
		constexpr double WallCutMin = 2.0;           // m of ground above the road edge before it gets a cut wall
		constexpr double WallFillMin = 2.5;          // m of drop below the road edge before it gets a fill wall
		constexpr double WallGap = 0.9;              // m from the road edge to a wall's road-side face

		FString DistName(int32 D) { return D == INDEX_NONE ? FString() : FString(Districts[D].Name); }
		int32 Ground01(double V, int32 Max) { return int32(FMath::Clamp<int64>(PyRoundInt(V), 0, Max)); }
		double GroundAt(const FGridD& T, double XV, double YV) { return T(Ground01(YV - YMin, H), Ground01(XV, W)); }
	}

	// --- Landmarks ----------------------------------------------------------------------------------

	TArray<FLandmark> PlannedLandmarks()
	{
		struct FRow { const TCHAR* Name; const TCHAR* Dist; double X, Y; double FW, FD; bool FWInt; int32 H; const TCHAR* Kind; };
		const FRow Rows[] = {
			{ TEXT("LaCala"), TEXT("PortoVecchio"), X(700), 275, 24, 16, true, 7, TEXT("ruin") },
			{ TEXT("FishMarket"), TEXT("PortoVecchio"), X(420), 330, 70, 35, true, 9, TEXT("hall") },
			{ TEXT("FerryPier"), TEXT("PortoVecchio"), X(880), 240, 20, 80, true, 2, TEXT("pier") },
			{ TEXT("SalviTerminal"), TEXT("MoloSalvi"), X(520), -120, 60, 40, true, 55, TEXT("tower") },
			{ TEXT("Faro"), TEXT("MoloSalvi"), X(930), -240, 8, 8, true, 28, TEXT("lighthouse") },
			{ TEXT("SantaLucia"), TEXT("CittaAlta"), X(520), 1320, 30, 55, true, 30, TEXT("church") },
			{ TEXT("CastrovanniTownhouse"), TEXT("CittaAlta"), X(380), 1180, 22, 18, true, 14, TEXT("house") },
			{ TEXT("PoliceHQ"), TEXT("CittaAlta"), X(760), 1000, 45, 35, true, 18, TEXT("civic") },
			{ TEXT("OspedaleSanMartino"), TEXT("CittaAlta"), X(250), 1500, 60, 40, true, 22, TEXT("civic") },
			{ TEXT("BeppesGarage"), TEXT("LeFornaci"), X(1450), 1150, 50, 40, true, 8, TEXT("yard") },
			{ TEXT("AbandonedKiln"), TEXT("LeFornaci"), X(1780), 1500, 60, 45, true, 40, TEXT("kiln") },
			{ TEXT("StazioneCentrale"), TEXT("LeFornaci"), X(1500), 980, 120, 30, true, 14, TEXT("station") },
			{ TEXT("ClubMedusa"), TEXT("Lungomare"), X(1450), 250, 40, 30, true, 10, TEXT("club") },
			{ TEXT("HotelRiviera"), TEXT("Lungomare"), X(1750), 300, 40, 25, true, 38, TEXT("hotel") },
			{ TEXT("VillaSalvi"), TEXT("LeColline"), X(450), 1960, 40, 30, true, 12, TEXT("villa") },
			{ TEXT("Lookout"), TEXT("LeColline"), X(250), 2030, 15, 15, true, 3, TEXT("lookout") },
			{ TEXT("LaCava"), TEXT("MonteFerrale"), X(1700), 2380, 120, 80, true, 15, TEXT("quarry") },
			{ TEXT("DigaDelFerrale"), TEXT("MonteFerrale"), X(1130), DamY, X(500), 15, false, 40, TEXT("dam") },
			{ TEXT("EremoDelleAquile"), TEXT("MonteFerrale"), X(500), 2640, 25, 20, true, 12, TEXT("hermitage") },
			{ TEXT("RadioMast"), TEXT("MonteFerrale"), X(1820), 2680, 8, 8, true, 60, TEXT("mast") },
		};
		TArray<FLandmark> Out;
		for (const FRow& R : Rows)
		{
			FLandmark L;
			L.Name = R.Name;
			L.District = R.Dist;
			L.Pos = FPt(R.X, R.Y, true);
			L.FW = R.FW;
			L.FD = R.FD;
			L.FWInt = R.FWInt;
			L.Height = R.H;
			L.Kind = R.Kind;
			Out.Add(L);
		}
		return Out;
	}

	namespace
	{
		// Landmarks whose ground plan includes the road on purpose: the dam carries the pass road on its
		// crest, and the quarry yard is where the pass road's branch ends.
		const TSet<FString> LandmarksOnRoad = { TEXT("dam"), TEXT("quarry"), TEXT("pier"), TEXT("lighthouse") };
		const TSet<FString> ClaimableKinds = { TEXT("alley"), TEXT("street") };

		/** For a landmark too big to fit between streets (hospital, station): the nearest spot where only
		 *  minor, ungated streets cross its footprint and removing them keeps the network connected. */
		FPt ClaimBlock(FRoadGraph& G, const FGridD& T, const FLandmark& L, const FRect& Rect)
		{
			const double Fw = L.FW, Fd = L.FD;
			struct FP { double X, Y, Reach; int32 Seg; };
			TArray<FP> Pts;
			for (int32 i = 0; i < G.Segments.Num(); ++i)
			{
				const FSeg& S = G.Segments[i];
				const FNode& A = G.Nodes[S.A];
				const FNode& B = G.Nodes[S.B];
				const int32 N = FMath::Max(2, int32(std::hypot(B.X - A.X, B.Y - A.Y) / 2.0) + 1);
				for (int32 k = 0; k < N; ++k)
				{
					const double Tk = double(k) / (N - 1);
					Pts.Add({ A.X + (B.X - A.X) * Tk, A.Y + (B.Y - A.Y) * Tk, S.Width / 2.0 + LandmarkRoadMargin, i });
				}
			}
			auto ConnectedWithout = [&G](const TSet<int32>& Drop)
			{
				TArray<int32> Order;
				TMap<int32, TArray<int32>> Adj;
				for (int32 i = 0; i < G.Segments.Num(); ++i)
				{
					if (Drop.Contains(i)) { continue; }
					const FSeg& S = G.Segments[i];
					if (!Adj.Contains(S.A)) { Order.Add(S.A); }
					Adj.FindOrAdd(S.A).Add(S.B);
					if (!Adj.Contains(S.B)) { Order.Add(S.B); }
					Adj.FindOrAdd(S.B).Add(S.A);
				}
				TSet<int32> Seen = { Order[0] };
				TArray<int32> Stack = { Order[0] };
				while (!Stack.IsEmpty())
				{
					for (const int32 M : Adj[Stack.Pop()])
					{
						if (!Seen.Contains(M)) { Seen.Add(M); Stack.Add(M); }
					}
				}
				return Seen.Num() == Adj.Num();
			};
			const double XV = L.Pos.X, YV = L.Pos.Y;
			const int32 NR = int32(std::ceil((LandmarkSearch - 0.0) / 4.0));
			for (int32 ri = 0; ri < NR; ++ri)
			{
				const double R = 0.0 + ri * 4.0;
				TArray<double> Angles;
				if (R > 0)
				{
					const double Step = 360.0 / FMath::Max(1, int32(2 * Pi * R / 4.0));
					const int32 NA = int32(std::ceil((360.0 - 0.0) / Step));
					for (int32 k = 0; k < NA; ++k) { Angles.Add(0.0 + k * Step); }
				}
				else
				{
					Angles.Add(0.0);
				}
				for (const double A : Angles)
				{
					const double Cx = XV + R * std::cos(Radians(A)), Cy = YV + R * std::sin(Radians(A));
					if (!(Rect.X0 + Fw / 2 <= Cx && Cx <= Rect.X1 - Fw / 2 && Rect.Y0 + Fd / 2 <= Cy && Cy <= Rect.Y1 - Fd / 2))
					{
						continue;
					}
					const int32 C0 = int32(Cx - Fw / 2), C1 = int32(Cx + Fw / 2) + 1, R0 = int32(Cy - Fd / 2 - YMin), R1 = int32(Cy + Fd / 2 - YMin) + 1;
					bool bWet = false;
					for (int32 r = FMath::Max(R0, 0); r < FMath::Min(R1, NY) && !bWet; ++r)
					{
						for (int32 c = FMath::Max(C0, 0); c < FMath::Min(C1, NX) && !bWet; ++c) { bWet = T(r, c) < 1.5; }
					}
					if (bWet)
					{
						continue;
					}
					TSet<int32> Drop;
					for (const FP& P : Pts)
					{
						if (std::fabs(P.X - Cx) < Fw / 2 + P.Reach && std::fabs(P.Y - Cy) < Fd / 2 + P.Reach) { Drop.Add(P.Seg); }
					}
					bool bBad = false;
					for (const int32 i : Drop)
					{
						const FSeg& S = G.Segments[i];
						bBad |= !ClaimableKinds.Contains(S.Kind) || !S.Gate.IsEmpty() || !S.Bridge.IsEmpty();
					}
					if (bBad || (Drop.Num() && !ConnectedWithout(Drop)))
					{
						continue;
					}
					TArray<FSeg> Keep;
					for (int32 i = 0; i < G.Segments.Num(); ++i)
					{
						if (!Drop.Contains(i)) { Keep.Add(G.Segments[i]); }
					}
					G.Segments = MoveTemp(Keep);
					UE_LOG(LogOpenCity, Display, TEXT("landmark %s: claimed a block at (%.0f, %.0f), %.0f m from plan, removing %d minor street segment(s)"),
						*L.Name, Cx, Cy, std::hypot(Cx - XV, Cy - YV), Drop.Num());
					return FPt(PyRound(Cx, 1), PyRound(Cy, 1));
				}
			}
			UE_LOG(LogOpenCity, Warning, TEXT("no spot for landmark %s; left on its planned spot (roads run through it)"), *L.Name);
			return L.Pos;
		}
	}

	TArray<FLandmark> SettleLandmarks(const FGridD& T, FRoadGraph& G)
	{
		// Move each landmark to the nearest spot (to its planned position, in its district) where its
		// footprint plus a margin is clear of every road corridor and of water.
		FGridB Blocked(NX, NY, 0);
		for (int32 r = 0; r < NY; ++r)
		{
			const double Y = double(r + YMin);
			const double Rx = RiverX(Y), Rhw = RiverHalfWidth(Y) + 4.0;
			for (int32 c = 0; c < NX; ++c)
			{
				Blocked(r, c) = (T(r, c) < 1.5 || (std::fabs(double(c) - Rx) <= Rhw && r + YMin < DamY + 30)) ? 1 : 0;
			}
		}
		for (const FSeg& S : G.Segments)
		{
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double Reach = S.Width / 2.0 + LandmarkRoadMargin;
			const int32 X0 = int32(FMath::Max(0.0, FMath::Min(A.X, B.X) - Reach - 2)), X1 = int32(FMath::Min(double(W), FMath::Max(A.X, B.X) + Reach + 2)) + 1;
			const int32 Y0 = int32(FMath::Max(0.0, FMath::Min(A.Y, B.Y) - Reach - 2 - YMin)), Y1 = int32(FMath::Min(double(H), FMath::Max(A.Y, B.Y) + Reach + 2 - YMin)) + 1;
			const float Ax = float(A.X), Ay = float(A.Y), Dx = float(B.X - A.X), Dy = float(B.Y - A.Y);
			const float Den = float(FMath::Max((B.X - A.X) * (B.X - A.X) + (B.Y - A.Y) * (B.Y - A.Y), 1e-6));
			const float ReachF = float(Reach);
			for (int32 r = Y0; r < Y1; ++r)
			{
				const float YY = float(r) + float(YMin);
				for (int32 c = X0; c < X1; ++c)
				{
					const float XX = float(c);
					float Tt = ((XX - Ax) * Dx + (YY - Ay) * Dy) / Den;
					Tt = Tt < 0.0f ? 0.0f : (Tt > 1.0f ? 1.0f : Tt);
					if (std::hypot(XX - (Ax + Tt * Dx), YY - (Ay + Tt * Dy)) < ReachF) { Blocked(r, c) = 1; }
				}
			}
		}
		// Candidate offsets in order of distance (ties keep generation order).
		const double Step = 2.0;
		const int32 Rings = int32(LandmarkSearch / Step);
		TArray<FVector2D> Cands;
		for (int32 i = -Rings; i <= Rings; ++i) { for (int32 j = -Rings; j <= Rings; ++j) { Cands.Add(FVector2D(i * Step, j * Step)); } }
		Cands.StableSort([](const FVector2D& A, const FVector2D& B) { return A.X * A.X + A.Y * A.Y < B.X * B.X + B.Y * B.Y; });
		TArray<FLandmark> Placed;
		for (FLandmark L : PlannedLandmarks())
		{
			if (LandmarksOnRoad.Contains(L.Kind))
			{
				Placed.Add(L);
				continue;
			}
			const FRect& Rect = Districts[DistrictIndex(*L.District)].Rect;
			bool bFound = false;
			FPt Best;
			for (const FVector2D& O : Cands)
			{
				if (O.X * O.X + O.Y * O.Y > LandmarkSearch * LandmarkSearch)
				{
					break;
				}
				const double Cx = L.Pos.X + O.X, Cy = L.Pos.Y + O.Y;
				if (!(Rect.X0 + L.FW / 2 <= Cx && Cx <= Rect.X1 - L.FW / 2 && Rect.Y0 + L.FD / 2 <= Cy && Cy <= Rect.Y1 - L.FD / 2))
				{
					continue;
				}
				const int32 C0 = int32(Cx - L.FW / 2), C1 = int32(Cx + L.FW / 2) + 1;
				const int32 R0 = int32(Cy - L.FD / 2 - YMin), R1 = int32(Cy + L.FD / 2 - YMin) + 1;
				if (C0 < 0 || R0 < 0 || C1 > W || R1 > H)
				{
					continue;
				}
				bool bAny = false;
				for (int32 r = R0; r < R1 && !bAny; ++r) { for (int32 c = C0; c < C1 && !bAny; ++c) { bAny = Blocked(r, c) != 0; } }
				if (!bAny)
				{
					Best = FPt(PyRound(Cx, 1), PyRound(Cy, 1));
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				const FPt P = ClaimBlock(G, T, L, Rect);
				L.bMoved = !(P == L.Pos) || !P.IntY;
				L.Pos = P;
				Placed.Add(L);
				continue;
			}
			if (!(Best == L.Pos))
			{
				UE_LOG(LogOpenCity, Display, TEXT("landmark %s: moved %.0f m off the roads to (%s, %s)"), *L.Name,
					std::hypot(Best.X - L.Pos.X, Best.Y - L.Pos.Y), *PyRepr(Best.X), *PyRepr(Best.Y));
			}
			L.Pos = Best;   // a rounded float tuple from here on
			L.bMoved = true;
			Placed.Add(L);
		}
		return Placed;
	}

	// --- Retaining walls ------------------------------------------------------------------------------

	TArray<FWallRun> RetainingWalls(const FGridD& T, const FRoadGraph& G)
	{
		// Retaining walls where the ground right beside a road edge jumps (INFRA_DESIGN.md Phase 3): fill
		// walls carry a road over a drop (their top is a 1 m parapet), cut walls hold back ground rising at
		// the edge. Stone on old roads, the coast road and the pass; concrete elsewhere.
		TArray<int32> Degree;
		Degree.Init(0, G.Nodes.Num());
		for (const FSeg& S : G.Segments) { ++Degree[S.A]; ++Degree[S.B]; }
		TArray<double> HalfW;
		double MaxHalf = 0.0;
		for (const FSeg& S : G.Segments)
		{
			HalfW.Add(S.Width / 2.0 + (SidewalkKinds.Contains(S.Kind) ? SidewalkW : 0.0));
			MaxHalf = FMath::Max(MaxHalf, HalfW.Last());
		}
		TMap<FIntPoint, TSet<int32>> Grid;   // 50 m cells: a wall may not stand on another road
		for (int32 i = 0; i < G.Segments.Num(); ++i)
		{
			const FNode& A = G.Nodes[G.Segments[i].A];
			const FNode& B = G.Nodes[G.Segments[i].B];
			const int32 Num = FMath::Max(2, int32(Dist(A.X, A.Y, B.X, B.Y) / 25.0) + 1);
			const double StepT = 1.0 / (Num - 1);
			for (int32 k = 0; k < Num; ++k)
			{
				const double Tk = k == Num - 1 ? 1.0 : k * StepT + 0.0;
				Grid.FindOrAdd({ int32(PyFloorDiv(A.X + (B.X - A.X) * Tk, 50)), int32(PyFloorDiv(A.Y + (B.Y - A.Y) * Tk, 50)) }).Add(i);
			}
		}
		auto OnOtherRoad = [&](double XV, double YV, int32 Own, double Z)
		{
			// A wall here would stand on another road at about its height (a junction). A road metres below
			// or above is what the wall separates it from (stacked hairpin legs): keep the wall.
			const int32 OA = G.Segments[Own].A, OB = G.Segments[Own].B;
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					const TSet<int32>* Cell = Grid.Find({ int32(PyFloorDiv(XV, 50)) + Dx, int32(PyFloorDiv(YV, 50)) + Dy });
					for (const int32 i : Cell ? *Cell : TSet<int32>())
					{
						const FSeg& S = G.Segments[i];
						if (i == Own || S.A == OA || S.A == OB || S.B == OA || S.B == OB)
						{
							continue;   // the same road's neighbouring segments are not another road
						}
						const FNode& A = G.Nodes[S.A];
						const FNode& B = G.Nodes[S.B];
						const double Vx = B.X - A.X, Vy = B.Y - A.Y;
						const double Tt = FMath::Max(0.0, FMath::Min(1.0, ((XV - A.X) * Vx + (YV - A.Y) * Vy) / FMath::Max(Vx * Vx + Vy * Vy, 1e-9)));
						if (std::hypot(XV - A.X - Tt * Vx, YV - A.Y - Tt * Vy) < HalfW[i] + 1.0 && std::fabs(A.Z + (B.Z - A.Z) * Tt - Z) < 1.5)
						{
							return true;
						}
					}
				}
			}
			return false;
		};
		auto OnRoadSurface = [&](double XV, double YV, double Margin = 0.2)
		{
			// True if (x, y) lies on any road's carriageway (any height, any road, its own included).
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					const TSet<int32>* Cell = Grid.Find({ int32(PyFloorDiv(XV, 50)) + Dx, int32(PyFloorDiv(YV, 50)) + Dy });
					for (const int32 i : Cell ? *Cell : TSet<int32>())
					{
						const FSeg& S = G.Segments[i];
						const FNode& A = G.Nodes[S.A];
						const FNode& B = G.Nodes[S.B];
						const double Vx = B.X - A.X, Vy = B.Y - A.Y;
						const double Tt = FMath::Max(0.0, FMath::Min(1.0, ((XV - A.X) * Vx + (YV - A.Y) * Vy) / FMath::Max(Vx * Vx + Vy * Vy, 1e-9)));
						if (std::hypot(XV - A.X - Tt * Vx, YV - A.Y - Tt * Vy) < S.Width / 2.0 + Margin)
						{
							return true;
						}
					}
				}
			}
			return false;
		};
		struct FSample { int32 Seg; double Side; FString Kind, Material; double X, Y, Top, Bottom, D, Rx, Ry; };
		TArray<FSample> Samples;
		for (int32 i = 0; i < G.Segments.Num(); ++i)
		{
			const FSeg& S = G.Segments[i];
			if (!S.Bridge.IsEmpty() || S.Kind == TEXT("pier"))
			{
				continue;
			}
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double Length = Dist(A.X, A.Y, B.X, B.Y);
			if (Length < WallStep)
			{
				continue;
			}
			const double Ux = (B.X - A.X) / Length, Uy = (B.Y - A.Y) / Length;
			const double ClearA = Degree[S.A] > 2 ? MaxHalf + 4.0 : 0.0;
			const double ClearB = Degree[S.B] > 2 ? MaxHalf + 4.0 : 0.0;
			const FString District = DistName(DistrictAt((A.X + B.X) / 2.0, (A.Y + B.Y) / 2.0));
			const FString Material = (S.Kind == TEXT("pass") || S.Kind == TEXT("coast") || District == TEXT("CittaAlta")) ? TEXT("Stone") : TEXT("Concrete");
			for (double D = ClearA; D <= Length - ClearB; D += WallStep)
			{
				const double Zr = A.Z + (B.Z - A.Z) * D / Length;
				for (const double Side : { -1.0, 1.0 })
				{
					const double Rx = -Uy * Side, Ry = Ux * Side;
					// Walls stand 0.9 m beyond the edge (a shoulder to the parapet, as on real mountain roads).
					const double Ex = A.X + Ux * D + Rx * (HalfW[i] + WallGap), Ey = A.Y + Uy * D + Ry * (HalfW[i] + WallGap);
					const double Hp = GroundAt(T, Ex + Rx * 1.5, Ey + Ry * 1.5);
					double Dz = Hp - Zr;
					// A drop a few metres out is still a drop a car can fall into.
					const double Low = FMath::Min(Hp, FMath::Min(GroundAt(T, Ex + Rx * 3.0, Ey + Ry * 3.0), GroundAt(T, Ex + Rx * 4.5, Ey + Ry * 4.5)));
					double Foot = Hp;
					if (Dz <= WallCutMin && Low - Zr < -WallFillMin)
					{
						Dz = Low - Zr;
						Foot = FMath::Min(Hp, Zr);
					}
					FString Kind;
					double Top, Bottom;
					if (Dz > WallCutMin)
					{
						Kind = TEXT("cut"); Top = Hp + 0.3; Bottom = Zr - 0.3;
					}
					else if (Dz < -WallFillMin)
					{
						Kind = TEXT("fill"); Top = Zr + 1.0; Bottom = Foot - 0.5;   // a 1 m parapet
					}
					else
					{
						continue;
					}
					if (OnOtherRoad(Ex + Rx * 0.25, Ey + Ry * 0.25, i, Zr))
					{
						continue;
					}
					const double Half = WallStep / 2.0;   // the panel reaches 2 m along the road each way
					bool bOnRoad = false;
					for (const double F : { -1.0, 0.0, 1.0 }) { bOnRoad |= OnRoadSurface(Ex + Ux * F * Half, Ey + Uy * F * Half); }
					if (bOnRoad)
					{
						continue;
					}
					Samples.Add({ i, Side, Kind, Material, Ex, Ey, Top, Bottom, D, PyRound(Rx, 4), PyRound(Ry, 4) });
				}
			}
		}
		// A cut wall where another road's fill wall already holds the step is a double wall: drop it.
		TMap<FIntPoint, TArray<int32>> Fills;
		for (int32 k = 0; k < Samples.Num(); ++k)
		{
			if (Samples[k].Kind == TEXT("fill"))
			{
				Fills.FindOrAdd({ int32(PyFloorDiv(Samples[k].X, 10)), int32(PyFloorDiv(Samples[k].Y, 10)) }).Add(k);
			}
		}
		auto NearFill = [&](const FSample& S)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					const TArray<int32>* Cell = Fills.Find({ int32(PyFloorDiv(S.X, 10)) + Dx, int32(PyFloorDiv(S.Y, 10)) + Dy });
					for (const int32 f : Cell ? *Cell : TArray<int32>())
					{
						if (Samples[f].Seg != S.Seg && Dist(Samples[f].X, Samples[f].Y, S.X, S.Y) < 4.0) { return true; }
					}
				}
			}
			return false;
		};
		TArray<FSample> Kept;
		for (const FSample& S : Samples)
		{
			if (S.Kind == TEXT("fill") || !NearFill(S)) { Kept.Add(S); }
		}
		// Consecutive samples (same segment, side and type, 4 m apart) form a run.
		Kept.StableSort([](const FSample& A, const FSample& B)
		{
			if (A.Seg != B.Seg) { return A.Seg < B.Seg; }
			if (A.Side != B.Side) { return A.Side < B.Side; }
			if (A.Kind != B.Kind) { return A.Kind < B.Kind; }
			return A.D < B.D;
		});
		TArray<FWallRun> Out;
		{
			int32 CurSeg = INDEX_NONE;
			double CurSide = 0.0, Last = 0.0;
			FString CurKind;
			TArray<FWallRun> Runs;
			for (const FSample& S : Kept)
			{
				if (Runs.IsEmpty() || CurSeg != S.Seg || CurSide != S.Side || CurKind != S.Kind || S.D - Last > WallStep + 0.01)
				{
					FWallRun R;
					R.Material = S.Material;
					R.Type = S.Kind;
					R.OutX = S.Rx;
					R.OutY = S.Ry;
					Runs.Add(R);
					CurSeg = S.Seg;
					CurSide = S.Side;
					CurKind = S.Kind;
				}
				Runs.Last().Points.Add({ PyRound(S.X, 2), PyRound(S.Y, 2), PyRound(S.Top, 2), PyRound(S.Bottom, 2) });
				Last = S.D;
			}
			for (FWallRun& R : Runs)
			{
				if (R.Points.Num() >= 2) { Out.Add(MoveTemp(R)); }
			}
		}
		// Runs stop short of each node, and on the outside of a bend the two segments' wall lines diverge:
		// join run ends that meet (same type, within 6 m) with a bridging panel, or a car finds the gap.
		struct FEnd { int32 K; bool bLast; };
		TArray<FEnd> Ends;
		for (int32 k = 0; k < Out.Num(); ++k) { Ends.Add({ k, false }); Ends.Add({ k, true }); }
		auto EndPt = [&Out](const FEnd& E) -> const TArray<double>& { return E.bLast ? Out[E.K].Points.Last() : Out[E.K].Points[0]; };
		TSet<int32> Joined;   // index into Ends
		TArray<FWallRun> Bridges;
		for (int32 i = 0; i < Ends.Num(); ++i)
		{
			int32 BestE = INDEX_NONE;
			double BestD = 0.0;
			const FWallRun& Rk = Out[Ends[i].K];
			for (int32 e2 = i + 1; e2 < Ends.Num(); ++e2)
			{
				const FWallRun& R2 = Out[Ends[e2].K];
				if (Ends[e2].K == Ends[i].K || R2.Type != Rk.Type || Joined.Contains(e2))
				{
					continue;
				}
				const TArray<double>& P = EndPt(Ends[i]);
				const TArray<double>& Q = EndPt(Ends[e2]);
				const double D = Dist(P[0], P[1], Q[0], Q[1]);
				if (0.05 < D && D < 6.0 && Rk.OutX * R2.OutX + Rk.OutY * R2.OutY > 0.5 && (BestE == INDEX_NONE || D < BestD))
				{
					BestE = e2;
					BestD = D;
				}
			}
			if (BestE == INDEX_NONE || Joined.Contains(i))
			{
				continue;
			}
			const TArray<double> P = EndPt(Ends[i]);
			const TArray<double> Q = EndPt(Ends[BestE]);
			bool bAcross = false;
			for (const double F : { 0.25, 0.5, 0.75 }) { bAcross |= OnRoadSurface(P[0] + (Q[0] - P[0]) * F, P[1] + (Q[1] - P[1]) * F); }
			if (bAcross)
			{
				continue;   // a bridging panel across a road mouth
			}
			Joined.Add(i);
			Joined.Add(BestE);
			const FWallRun& R2 = Out[Ends[BestE].K];
			const double Ox = Rk.OutX + R2.OutX, Oy = Rk.OutY + R2.OutY;
			const double N = std::hypot(Ox, Oy);
			FWallRun Br;
			Br.Material = Rk.Material;
			Br.Type = Rk.Type;
			Br.OutX = PyRound(Ox / N, 4);
			Br.OutY = PyRound(Oy / N, 4);
			Br.Points.Add(P);
			Br.Points.Add({ Q[0], Q[1], FMath::Max(P[2], Q[2]), FMath::Min(P[3], Q[3]) });
			Bridges.Add(MoveTemp(Br));
		}
		Out.Append(Bridges);
		return Out;
	}

	TArray<FWallRun> DropWallsInBuildings(const TArray<FWallRun>& Runs, const TArray<FBuilding>& Buildings, double Grow)
	{
		// Split wall runs where they would pass through a building footprint (+ Grow m).
		auto Inside = [&](double XV, double YV)
		{
			for (const FBuilding& B : Buildings)
			{
				if (std::fabs(XV - B.PX) > 40 || std::fabs(YV - B.PY) > 40)
				{
					continue;
				}
				const double C = std::cos(Radians(B.Yaw)), S = std::sin(Radians(B.Yaw));
				const double U = (XV - B.PX) * C + (YV - B.PY) * S, V = -(XV - B.PX) * S + (YV - B.PY) * C;
				if (std::fabs(U) <= B.SW / 2 + Grow && std::fabs(V) <= B.SD / 2 + Grow)
				{
					return true;
				}
			}
			return false;
		};
		TArray<FWallRun> Out;
		for (const FWallRun& R : Runs)
		{
			FWallRun Cur = R;
			Cur.Points.Reset();
			for (const TArray<double>& P : R.Points)
			{
				if (Inside(P[0], P[1]))
				{
					if (Cur.Points.Num() >= 2) { Out.Add(Cur); }
					Cur.Points.Reset();
				}
				else
				{
					Cur.Points.Add(P);
				}
			}
			if (Cur.Points.Num() >= 2) { Out.Add(Cur); }
		}
		return Out;
	}

	// --- Buildings ------------------------------------------------------------------------------------

	namespace
	{
		struct FBuildParams { double W0, W1, D0, D1, H0, H1, Gap0, Gap1, Slope, Set0, Set1, Extra; };
		const FBuildParams& Params(const FString& Style)
		{
			// w, d, h ranges (m), gap range along the road, slope limit (m), setback from the pavement edge, extra
			static const TMap<FString, FBuildParams> P = {
				{ TEXT("oldtown"), { 7, 12, 9, 14, 9, 18, 0, 0.3, 8, 0.2, 0.4, 0 } },
				{ TEXT("grid90"), { 10, 20, 12, 20, 10, 24, 0, 1.0, 5, 0.2, 0.6, 0 } },
				{ TEXT("grid120"), { 20, 45, 20, 35, 7, 14, 3, 8, 4, 1.0, 3, 0 } },
				{ TEXT("beach"), { 15, 30, 12, 20, 12, 30, 1, 4, 5, 0.3, 1.0, 0 } },
				{ TEXT("village"), { 6, 10, 8, 12, 6, 10, 0, 1.5, 6, 0.2, 0.8, 0 } },
				{ TEXT("terminal"), { 25, 50, 15, 30, 8, 16, 6, 15, 3, 1.5, 3, 0 } },
				{ TEXT("hills"), { 12, 20, 10, 16, 6, 9, 25, 60, 6, 1.5, 3, 6 } },
			};
			return P[Style];
		}
	}

	FRectMask RectMask(double CX, double CY, double Wd, double Dp, double Yaw, double Grow);

	TArray<FBuilding> PlaceBuildings(const FGridD& T, const FGridF& Best, const FRoadGraph& G, FPyRandom& Rng, const TArray<FLandmark>& Landmarks, FString& OutError)
	{
		FGridB Blocked(NX, NY, 0);
		for (int32 r = 0; r < NY; ++r)
		{
			const double Y = double(r + YMin);
			const double Rx = RiverX(Y), Rhw = RiverHalfWidth(Y) + 6.0;
			for (int32 c = 0; c < NX; ++c)
			{
				bool bOk = T(r, c) >= 1.5 && !(std::fabs(double(c) - Rx) <= Rhw && r + YMin < DamY + 30);
				// Frontages may come to 0.3 m from the pavement edge (best = m past road + pavement + 1 m).
				Blocked(r, c) = (!bOk || Best(r, c) < -0.7f) ? 1 : 0;
			}
		}
		for (const FRect& R : { Darsena, Reservoir })
		{
			for (int32 r = FMath::Max(0, int32(R.Y0 - YMin)); r < FMath::Min(NY, int32(R.Y1 - YMin) + 1); ++r)
			{
				for (int32 c = FMath::Max(0, int32(R.X0)); c < FMath::Min(NX, int32(R.X1) + 1); ++c) { Blocked(r, c) = 1; }
			}
		}
		for (const FLandmark& L : Landmarks)
		{
			for (int32 r = FMath::Max(0, int32(L.Pos.Y - L.FD / 2 - 8 - YMin)); r < FMath::Min(NY, int32(L.Pos.Y + L.FD / 2 + 8 - YMin) + 2); ++r)
			{
				for (int32 c = FMath::Max(0, int32(L.Pos.X - L.FW / 2 - 8)); c < FMath::Min(NX, int32(L.Pos.X + L.FW / 2 + 8) + 2); ++c) { Blocked(r, c) = 1; }
			}
		}
		// Junction corners and sharp bends: a car turning there cuts the corner, and at 5.5 m alley junctions
		// and bends with facades on the carriageway edge it hit the building on it. Keep CornerClear past the
		// widest carriageway clear round every junction and every bend of BendDeg or more (a corner set back;
		// roads with pavements already are).
		{
			constexpr double CornerClear = 3.5;
			constexpr double BendDeg = 45.0;
			struct FAt { TArray<int32> Others; double Half = 0.0; };
			TMap<int32, FAt> AtNode;   // node -> the nodes it joins, widest half carriageway
			for (const FSeg& S : G.Segments)
			{
				FAt& Ea = AtNode.FindOrAdd(S.A);
				Ea.Others.Add(S.B);
				Ea.Half = FMath::Max(Ea.Half, S.Width / 2.0);
				FAt& Eb = AtNode.FindOrAdd(S.B);
				Eb.Others.Add(S.A);
				Eb.Half = FMath::Max(Eb.Half, S.Width / 2.0);
			}
			for (const TPair<int32, FAt>& E : AtNode)
			{
				const FNode& N = G.Nodes[E.Key];
				bool bCorner = E.Value.Others.Num() >= 3;
				if (E.Value.Others.Num() == 2)
				{
					const FNode& P = G.Nodes[E.Value.Others[0]];
					const FNode& Q = G.Nodes[E.Value.Others[1]];
					const double In = std::atan2(N.Y - P.Y, N.X - P.X), Out = std::atan2(Q.Y - N.Y, Q.X - N.X);
					const double Turn = std::fabs(std::remainder(Out - In, 2.0 * PI));
					bCorner = Turn >= FMath::DegreesToRadians(BendDeg);
				}
				if (!bCorner)
				{
					continue;
				}
				const double R = E.Value.Half + CornerClear;
				for (int32 r = FMath::Max(0, int32(N.Y - R - YMin)); r < FMath::Min(NY, int32(N.Y + R - YMin) + 2); ++r)
				{
					for (int32 c = FMath::Max(0, int32(N.X - R)); c < FMath::Min(NX, int32(N.X + R) + 2); ++c)
					{
						if (std::hypot(c - N.X, r + YMin - N.Y) < R) { Blocked(r, c) = 1; }
					}
				}
			}
		}
		// Bridges are not in Best (their corridors aren't graded): keep their decks and approaches clear.
		for (const FSeg& S : G.Segments)
		{
			if (S.Bridge.IsEmpty())
			{
				continue;
			}
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double Reach = S.Width / 2.0 + 6.0;
			const int32 X0 = int32(FMath::Max(0.0, FMath::Min(A.X, B.X) - Reach - 6)), X1 = int32(FMath::Min(double(W), FMath::Max(A.X, B.X) + Reach + 6)) + 1;
			const int32 Y0 = int32(FMath::Max(0.0, FMath::Min(A.Y, B.Y) - Reach - 6 - YMin)), Y1 = int32(FMath::Min(double(H), FMath::Max(A.Y, B.Y) + Reach + 6 - YMin)) + 1;
			const float Ax = float(A.X), Ay = float(A.Y), Dx = float(B.X - A.X), Dy = float(B.Y - A.Y);
			const float Den = float((B.X - A.X) * (B.X - A.X) + (B.Y - A.Y) * (B.Y - A.Y));
			for (int32 r = Y0; r < Y1; ++r)
			{
				const float YY = float(r) + float(YMin);
				for (int32 c = X0; c < X1; ++c)
				{
					const float XX = float(c);
					float Tt = ((XX - Ax) * Dx + (YY - Ay) * Dy) / Den;
					Tt = Tt < -0.05f ? -0.05f : (Tt > 1.05f ? 1.05f : Tt);
					if (std::hypot(XX - (Ax + Tt * Dx), YY - (Ay + Tt * Dy)) < float(Reach)) { Blocked(r, c) = 1; }
				}
			}
		}
		FGridB Occ(NX, NY, 0);
		TArray<FBuilding> Out;
		for (const FSeg& Seg : G.Segments)
		{
			if (!Seg.Bridge.IsEmpty())
			{
				continue;
			}
			const FNode& A = G.Nodes[Seg.A];
			const FNode& B = G.Nodes[Seg.B];
			const int32 Di = DistrictAt((A.X + B.X) / 2, (A.Y + B.Y) / 2);
			if (Seg.Kind == TEXT("pier") && DistName(Di) != TEXT("MoloSalvi"))
			{
				continue;   // the terminal's own road grid is pier segments
			}
			if (Di == INDEX_NONE || FString(Districts[Di].Style) == TEXT("mountain"))
			{
				continue;
			}
			const FString Style = Districts[Di].Style;
			const FBuildParams& P = Params(Style);
			const FRect& Rect = Districts[Di].Rect;
			const double L = std::hypot(B.X - A.X, B.Y - A.Y);
			if (L < 1e-6)
			{
				continue;
			}
			const double Ux = (B.X - A.X) / L, Uy = (B.Y - A.Y) / L;
			// Rasterise exactly what gets stored (rounded), so the self-check sees the same masks.
			const double YawDeg = PyRound(Degrees(std::atan2(Uy, Ux)), 1);
			const double Yaw = Radians(YawDeg);
			const double Edge = Seg.Width / 2 + (SidewalkKinds.Contains(Seg.Kind) ? SidewalkW : 0.0) + 0.3;
			for (const int32 Side : { 1, -1 })
			{
				double S = Rng.Uniform(0, 3);
				while (S < L)
				{
					// Footprints snap to the facade kit's 2 m bay grid, min 4 m.
					const double Wb = FMath::Max(MinFootprint, Bay * PyRoundInt(Rng.Uniform(P.W0, P.W1) / Bay));
					const double Db = FMath::Max(MinFootprint, Bay * PyRoundInt(Rng.Uniform(P.D0, P.D1) / Bay));
					const double Gap = Rng.Uniform(P.Gap0, P.Gap1);
					if (S + Wb > L)
					{
						break;
					}
					const double Off = Edge + Rng.Uniform(P.Set0, P.Set1) + P.Extra + Db / 2;
					const double Cx = PyRound(A.X + Ux * (S + Wb / 2) - Uy * Off * Side, 2);
					const double Cy = PyRound(A.Y + Uy * (S + Wb / 2) + Ux * Off * Side, 2);
					bool bOk = Rect.X0 <= Cx && Cx <= Rect.X1 && Rect.Y0 <= Cy && Cy <= Rect.Y1;
					FRectMask M;
					double GMin = 0.0;
					if (bOk)
					{
						M = RectMask(Cx, Cy, Wb, Db, Yaw);
						if (!M.Valid())
						{
							bOk = false;
						}
						else
						{
							bool bAny = false, bBlocked = false;
							double GMax = -1e18;
							GMin = 1e18;
							for (int32 r = M.Y0; r < M.Y1; ++r)
							{
								for (int32 c = M.X0; c < M.X1; ++c)
								{
									if (M.At(r, c))
									{
										bAny = true;
										bBlocked |= Blocked(r, c) != 0;
										GMax = FMath::Max(GMax, T(r, c));
										GMin = FMath::Min(GMin, T(r, c));
									}
								}
							}
							if (!bAny || bBlocked || GMax - GMin > P.Slope)
							{
								bOk = false;
							}
							else
							{
								const double Gm = FMath::Min(Gap * 0.5, 1.0);
								const FRectMask M2 = Gm > 0 ? RectMask(Cx, Cy, Wb, Db, Yaw, Gm) : M;
								for (int32 r = M2.Y0; r < M2.Y1 && bOk; ++r)
								{
									for (int32 c = M2.X0; c < M2.X1 && bOk; ++c)
									{
										if (M2.At(r, c) && Occ(r, c)) { bOk = false; }
									}
								}
							}
						}
					}
					if (bOk)
					{
						for (int32 r = M.Y0; r < M.Y1; ++r) { for (int32 c = M.X0; c < M.X1; ++c) { if (M.At(r, c)) { Occ(r, c) = 1; } } }
						const double Ci = T(int32(PyRoundInt(Cy - YMin)), int32(PyRoundInt(Cx)));
						const int32 Storeys = FMath::Max<int32>(1, int32(PyRoundInt(Rng.Uniform(P.H0, P.H1) / StoreyH)));
						FBuilding Bd;
						Bd.PX = PyRound(Cx, 2);
						Bd.PY = PyRound(Cy, 2);
						Bd.SW = Wb;
						Bd.SD = Db;
						Bd.Yaw = PyRound(Degrees(Yaw), 1);
						Bd.Base = PyRound(GMin - 1.0, 2);
						Bd.Storeys = Storeys;
						Bd.Top = PyRound(Ci + Storeys * StoreyH, 2);
						Bd.Style = BuildingStyles.IndexOfByKey(Style);
						Bd.District = Districts[Di].Name;
						Out.Add(Bd);
						S += Wb + Gap;
					}
					else
					{
						S += FMath::Max(3.0, Wb * 0.5);
					}
				}
			}
		}
		// Self-check: re-rasterise the stored (rounded) records.
		TGrid<int16> Chk(NX, NY, 0);
		for (const FBuilding& Bd : Out)
		{
			const FRectMask M = RectMask(Bd.PX, Bd.PY, Bd.SW, Bd.SD, Radians(Bd.Yaw));
			for (int32 r = M.Y0; r < M.Y1; ++r)
			{
				for (int32 c = M.X0; c < M.X1; ++c)
				{
					if (!M.At(r, c)) { continue; }
					if (++Chk(r, c) > 1) { OutError = TEXT("buildings overlap"); }
					if (Best(r, c) < -0.7f - 0.15f) { OutError = TEXT("building inside road clearance"); }
				}
			}
		}
		return Out;
	}

	namespace
	{
		// Landmarks whose main volume is built from the building kit: kind -> (offset of the main volume as a
		// fraction of (w, d), its size fraction, storeys (0 = from height), building style).
		struct FKitLm { double Ox, Oy, Sx, Sy; int32 Storeys; const TCHAR* Style; };
		const TMap<FString, FKitLm>& KitLandmarks()
		{
			static const TMap<FString, FKitLm> M = {
				{ TEXT("church"), { 0.0, -0.125, 1.0, 0.75, 4, TEXT("oldtown") } },
				{ TEXT("house"), { 0.0, 0.0, 1.0, 1.0, 4, TEXT("oldtown") } },
				{ TEXT("civic"), { 0.0, 0.0, 1.0, 1.0, 0, TEXT("grid90") } },
				{ TEXT("hall"), { 0.0, 0.0, 1.0, 1.0, 2, TEXT("grid90") } },
				{ TEXT("club"), { 0.0, 0.0, 1.0, 1.0, 3, TEXT("beach") } },
				{ TEXT("hotel"), { 0.0, 0.0, 1.0, 1.0, 0, TEXT("beach") } },
				{ TEXT("station"), { 0.0, 0.0, 0.4, 1.0, 4, TEXT("grid120") } },
				{ TEXT("villa"), { -0.2, 0.0, 0.6, 1.0, 3, TEXT("hills") } },
			};
			return M;
		}
	}

	TArray<FBuilding> LandmarkBuildings(const FGridD& T, const TArray<FLandmark>& Landmarks)
	{
		TArray<FBuilding> Out;
		for (const FLandmark& L : Landmarks)
		{
			const FKitLm* K = KitLandmarks().Find(L.Kind);
			if (!K)
			{
				continue;
			}
			const double Cx = L.Pos.X + K->Ox * L.FW, Cy = L.Pos.Y + K->Oy * L.FD;
			const double Wb = FMath::Max(MinFootprint, double(PyRoundInt(L.FW * K->Sx / Bay)) * Bay);
			const double Db = FMath::Max(MinFootprint, double(PyRoundInt(L.FD * K->Sy / Bay)) * Bay);
			const int32 N = K->Storeys ? K->Storeys : FMath::Max<int32>(1, int32(PyRoundInt(L.Height / StoreyH)));
			const int32 R0 = int32(Cy - Db / 2 - YMin), R1 = int32(Cy + Db / 2 - YMin) + 1;
			const int32 C0 = int32(Cx - Wb / 2), C1 = int32(Cx + Wb / 2) + 1;
			double GMin = 1e18;
			for (int32 r = R0; r < FMath::Min(R1, NY); ++r) { for (int32 c = C0; c < FMath::Min(C1, NX); ++c) { GMin = FMath::Min(GMin, T(r, c)); } }
			const double Ci = T(int32(PyRoundInt(Cy - YMin)), int32(PyRoundInt(Cx)));
			FBuilding Bd;
			Bd.PX = PyRound(Cx, 2);
			Bd.PY = PyRound(Cy, 2);
			Bd.SW = Wb;
			Bd.SD = Db;
			Bd.Yaw = 0.0;
			Bd.Base = PyRound(GMin - 1.0, 2);
			Bd.Top = PyRound(Ci + N * StoreyH, 2);
			Bd.Storeys = N;
			Bd.Style = BuildingStyles.IndexOfByKey(K->Style);
			Bd.District = L.District;
			Bd.Landmark = L.Name;
			Out.Add(Bd);
		}
		return Out;
	}

	// --- Infrastructure metadata (INFRA_DESIGN.md) --------------------------------------------------

	namespace
	{
		bool InCore(double XV, double YV)
		{
			// Città Alta's walled core: an ellipse round Santa Lucia.
			const double Cx = 700.0, Cy = 1230.0, Rx = 400.0, Ry = 290.0;
			return std::pow((XV - Cx) / Rx, 2.0) + std::pow((YV - Cy) / Ry, 2.0) <= 1.0;
		}

		struct FClass { const TCHAR* Profile; const TCHAR* Paving; const TCHAR* Era; const TCHAR* Parking; bool bZtl; };
		FClass ClassifySegment(const FSeg& S, double Mx, double My, const FString& District, bool bDeadEnd)
		{
			// Profile, paving, era, parking and ZTL for one segment, from its geometry family and place.
			const bool bCore = District == TEXT("CittaAlta") && InCore(Mx, My);
			if (S.Bridge == TEXT("Passerella")) { return { TEXT("passerella"), TEXT("stone_slab"), TEXT("c19"), TEXT("none"), false }; }
			if (S.Kind == TEXT("pier")) { return { TEXT("banchina_porto"), TEXT("concrete"), TEXT("postwar"), TEXT("none"), false }; }
			if (S.Kind == TEXT("coast")) { return { TEXT("costiera"), TEXT("asphalt"), TEXT("postwar"), TEXT("none"), false }; }
			if (S.Kind == TEXT("pass")) { return { TEXT("montana_tornanti"), TEXT("asphalt"), TEXT("postwar"), TEXT("none"), false }; }
			if (S.Kind == TEXT("avenue"))
			{
				if ((District == TEXT("Lungomare") || District == TEXT("BorgoMarino")) && My < 300) { return { TEXT("lungomare"), TEXT("asphalt"), TEXT("c19"), TEXT("blue"), false }; }
				return { TEXT("viale"), TEXT("asphalt"), TEXT("c19"), TEXT("blue"), false };
			}
			if (S.Kind == TEXT("alley"))
			{
				if (bCore) { return { TEXT("centro_storico"), TEXT("lastricato"), TEXT("medieval"), TEXT("none"), true }; }
				if (District == TEXT("CittaAlta")) { return { TEXT("centro_storico"), TEXT("sampietrini"), TEXT("c19"), TEXT("none"), false }; }
				// Borgo Marino is an old fishing village (STORY.md): its 5.5 m lanes are a village core, not post-war
				// local streets under the DM F minimum of 6.5 m (46 width warnings). Asphalt, wall lanterns.
				if (District == TEXT("BorgoMarino")) { return { TEXT("centro_storico"), TEXT("asphalt"), TEXT("c19"), TEXT("none"), false }; }
				return { TEXT("urbana_locale"), TEXT("asphalt"), TEXT("postwar"), TEXT("white"), false };
			}
			if (bCore) { return { TEXT("centro_storico"), TEXT("sampietrini"), TEXT("medieval"), TEXT("none"), true }; }
			if (District == TEXT("CittaAlta")) { return { TEXT("urbana_quartiere"), TEXT("asphalt"), TEXT("c19"), TEXT("blue"), false }; }
			if (District == TEXT("LeColline") && bDeadEnd) { return { TEXT("strada_bianca"), TEXT("gravel_white"), TEXT("c19"), TEXT("none"), false }; }
			if (District == TEXT("LeColline") || District == TEXT("MonteFerrale")) { return { TEXT("locale_f2"), TEXT("asphalt"), TEXT("postwar"), TEXT("none"), false }; }
			if (District == TEXT("Lungomare")) { return { TEXT("urbana_quartiere"), TEXT("asphalt"), TEXT("modern"), TEXT("blue"), false }; }
			if (District == TEXT("BorgoMarino")) { return { TEXT("urbana_quartiere"), TEXT("asphalt"), TEXT("c19"), TEXT("white"), false }; }
			return { TEXT("urbana_quartiere"), TEXT("asphalt"), TEXT("postwar"), TEXT("white"), false };
		}

		TSet<int32> TramRoute(const FRoadGraph& G, const TArray<FLandmark>& Landmarks)
		{
			// Planned tram (Phase 7): Stazione Centrale to Città Alta's civic centre over Ponte San Rocco,
			// preferring avenues, then streets; alleys only where nothing else connects.
			auto NearestNode = [&G](const FPt& P)
			{
				int32 BestN = 0;
				double Bd = TNumericLimits<double>::Max();
				for (int32 i = 0; i < G.Nodes.Num(); ++i)
				{
					const double D = (G.Nodes[i].X - P.X) * (G.Nodes[i].X - P.X) + (G.Nodes[i].Y - P.Y) * (G.Nodes[i].Y - P.Y);
					if (D < Bd) { Bd = D; BestN = i; }
				}
				return BestN;
			};
			FPt Station, Police;
			for (const FLandmark& L : Landmarks)
			{
				if (L.Name == TEXT("StazioneCentrale")) { Station = L.Pos; }
				if (L.Name == TEXT("PoliceHQ")) { Police = L.Pos; }
			}
			const int32 A = NearestNode(Station), B = NearestNode(Police);
			TMap<int32, TArray<TPair<int32, int32>>> Adj;
			for (int32 i = 0; i < G.Segments.Num(); ++i)
			{
				const FSeg& S = G.Segments[i];
				if ((S.Kind == TEXT("avenue") || S.Kind == TEXT("street") || S.Kind == TEXT("alley")) && S.Gate.IsEmpty())
				{
					Adj.FindOrAdd(S.A).Add({ S.B, i });
					Adj.FindOrAdd(S.B).Add({ S.A, i });
				}
			}
			TMap<int32, double> Dists = { { A, 0.0 } };
			TMap<int32, TPair<int32, int32>> Came;
			struct FOpen { double D; int32 N; bool operator>(const FOpen& O) const { return D != O.D ? D > O.D : N > O.N; } };
			std::priority_queue<FOpen, std::vector<FOpen>, std::greater<FOpen>> Todo;
			Todo.push({ 0.0, A });
			while (!Todo.empty())
			{
				const FOpen Top = Todo.top();
				Todo.pop();
				if (Top.N == B)
				{
					break;
				}
				if (Top.D > Dists[Top.N])
				{
					continue;
				}
				for (const TPair<int32, int32>& E : Adj.FindRef(Top.N))
				{
					const FString& K = G.Segments[E.Value].Kind;
					const double Mult = K == TEXT("avenue") ? 0.6 : (K == TEXT("street") ? 1.0 : 3.0);
					const double Nd = Top.D + Dist(G.Nodes[Top.N].X, G.Nodes[Top.N].Y, G.Nodes[E.Key].X, G.Nodes[E.Key].Y) * Mult;
					const double* Old = Dists.Find(E.Key);
					if (Nd < (Old ? *Old : 1e18))
					{
						Dists.Add(E.Key, Nd);
						Came.Add(E.Key, { Top.N, E.Value });
						Todo.push({ Nd, E.Key });
					}
				}
			}
			TSet<int32> Segs;
			int32 N = B;
			while (const TPair<int32, int32>* C = Came.Find(N))
			{
				Segs.Add(C->Value);
				N = C->Key;
			}
			return Segs;
		}
	}

	bool AnnotateNetworks(FRoadGraph& G, const TArray<FLandmark>& Landmarks, FPyRandom& Rng, const FJsonPtr& Profiles, TArray<FJunction>& OutJunctions, FString& OutError)
	{
		static const TSet<FString> PedestrianProfiles = { TEXT("passerella"), TEXT("vicolo") };
		static const TMap<FString, double> EraWear = { { TEXT("roman"), 0.85 }, { TEXT("medieval"), 0.8 }, { TEXT("c19"), 0.6 }, { TEXT("postwar"), 0.65 }, { TEXT("modern"), 0.3 } };
		TArray<int32> Degree;
		Degree.Init(0, G.Nodes.Num());
		for (const FSeg& S : G.Segments) { ++Degree[S.A]; ++Degree[S.B]; }
		const TSet<int32> Tram = TramRoute(G, Landmarks);
		for (int32 i = 0; i < G.Segments.Num(); ++i)
		{
			FSeg& S = G.Segments[i];
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double Mx = (A.X + B.X) / 2.0, My = (A.Y + B.Y) / 2.0;
			const FClass C = ClassifySegment(S, Mx, My, DistName(DistrictAt(Mx, My)), FMath::Min(Degree[S.A], Degree[S.B]) == 1);
			const FJsonPtr Prof = Profiles->Get(C.Profile);
			if (!Prof)
			{
				OutError = FString::Printf(TEXT("segment %d: profile %s missing from road_profiles.json"), i, C.Profile);
				return false;
			}
			S.bAnnotated = true;
			S.Profile = C.Profile;
			S.Paving = C.Paving;
			S.Era = C.Era;
			S.Parking = C.Parking;
			S.bZtl = C.bZtl;
			S.bPedestrian = PedestrianProfiles.Contains(S.Profile);
			S.bTram = Tram.Contains(i);
			S.Wear = PyRound(FMath::Min(1.0, FMath::Max(0.0, EraWear[S.Era] + Rng.Uniform(-0.15, 0.15))), 2);
			// Italian limits: the profile's design maximum (urban 50), Zona 30 inside the walls.
			const int32 Design = int32(Prof->Get(TEXT("design_speed_max_kmh"))->AsNumber());
			S.Speed = FMath::Min3(S.Speed, Design, S.bZtl ? 30 : 999);
			// A road wider than its profile's carriageway (a 9 m street that became a hill track or a walled-core
			// street) narrows to it. Only narrowing: its graded corridor and building set-backs were laid out wider.
			auto Cm = [&Prof](const TCHAR* Key) { const FJsonPtr V = Prof->Get(Key); return V ? V->AsNumber() : 0.0; };
			const double Carriageway = (2.0 * Cm(TEXT("lanes_per_dir")) * Cm(TEXT("lane_cm"))
				+ 2.0 * (Cm(TEXT("shoulder_left_cm")) + Cm(TEXT("shoulder_right_cm")) + Cm(TEXT("emergency_lane_cm")))
				+ Cm(TEXT("median_cm")) + Cm(TEXT("parking_cm"))) / 100.0;
			if (S.Bridge.IsEmpty() && Carriageway > 0.0 && S.Width - S.Widening > Carriageway * 1.12) { S.Width = Carriageway + S.Widening; }   // curve widening stays
		}
		// Junction types, in first-appearance order of the nodes.
		TArray<int32> Order;
		TMap<int32, TArray<int32>> ByNode;
		for (int32 i = 0; i < G.Segments.Num(); ++i)
		{
			for (const int32 N : { G.Segments[i].A, G.Segments[i].B })
			{
				if (!ByNode.Contains(N)) { Order.Add(N); }
				ByNode.FindOrAdd(N).Add(i);
			}
		}
		int32 Piazza = 0;   // Santa Lucia
		{
			double Bd = TNumericLimits<double>::Max();
			for (int32 i = 0; i < G.Nodes.Num(); ++i)
			{
				const double D = std::pow(G.Nodes[i].X - 702, 2.0) + std::pow(G.Nodes[i].Y - 1224, 2.0);
				if (D < Bd) { Bd = D; Piazza = i; }
			}
		}
		constexpr double SignalSpacing = 180.0;   // m: corso junctions get signals at most this often
		TArray<int32> Signals;
		for (const int32 N : Order)
		{
			const TArray<int32>& Segs = ByNode[N];
			const FString District = DistName(DistrictAt(G.Nodes[N].X, G.Nodes[N].Y));
			bool bZtlT = false, bZtlF = false, bBrT = false, bBrF = false, bStreet = false;
			int32 Avenues = 0;
			for (const int32 i : Segs)
			{
				const FSeg& S = G.Segments[i];
				(S.bZtl ? bZtlT : bZtlF) = true;
				(!S.Bridge.IsEmpty() ? bBrT : bBrF) = true;
				Avenues += S.Kind == TEXT("avenue") ? 1 : 0;
				bStreet |= S.Kind == TEXT("street");
			}
			FString Kind;
			if (bZtlT && bZtlF)
			{
				Kind = TEXT("ztl_gate");
			}
			else if (bBrT && bBrF)
			{
				Kind = TEXT("bridge_head");
			}
			else if (N == Piazza && Segs.Num() >= 3)
			{
				Kind = TEXT("piazza");
			}
			else if (Segs.Num() < 3)
			{
				continue;
			}
			else if (Avenues >= 3 && (District == TEXT("PortoVecchio") || District == TEXT("LeFornaci") || District == TEXT("Lungomare")))
			{
				Kind = TEXT("roundabout");   // two avenues crossing in the periphery
			}
			else
			{
				bool bSpaced = true;
				for (const int32 M : Signals)
				{
					bSpaced &= Dist(G.Nodes[N].X, G.Nodes[N].Y, G.Nodes[M].X, G.Nodes[M].Y) > SignalSpacing;
				}
				if (Avenues >= 3 || (Avenues == 2 && Segs.Num() >= 4 && bStreet && bSpaced))
				{
					Kind = TEXT("signalized");   // corso junctions: a street crossing an avenue
					Signals.Add(N);
				}
				else if (Segs.Num() == 3 && (District == TEXT("CittaAlta") || District == TEXT("LeColline") || District == TEXT("BorgoMarino") || District == TEXT("MonteFerrale")))
				{
					Kind = TEXT("blind_t");
				}
				else
				{
					Kind = TEXT("priority");
				}
			}
			OutJunctions.Add({ N, Kind });
		}
		return true;
	}

	void SightTriangles(const FRoadGraph& G, const TArray<FBuilding>& Buildings, TArray<FJunction>& Junctions)
	{
		// D.M. 19/4/2006 section 4.6 at every priority and blind-T junction: the long leg along the through
		// road is D = v t (12 s under give way, 6 s under STOP, +1 s per point of minor-arm grade over 2%),
		// the short leg 20 m back from the through road's edge (give way) or 3 m behind the stop line (STOP).
		// Give-way triangles clear -> "priority"; STOP triangles clear -> "stop"; neither -> "blind_t".
		FGridB Occ(NX, NY, 0);
		for (const FBuilding& B : Buildings)
		{
			const FRectMask M = RectMask(B.PX, B.PY, B.SW, B.SD, Radians(B.Yaw));
			for (int32 r = M.Y0; r < M.Y1; ++r) { for (int32 c = M.X0; c < M.X1; ++c) { if (M.At(r, c)) { Occ(r, c) = 1; } } }
		}
		auto Blocked = [&Occ](const FPt& A, const FPt& B, const FPt& C)
		{
			const int32 N = FMath::Max(2, int32(FMath::Max3(Dist(A, B), Dist(A, C), Dist(B, C)) / 1.5));
			for (int32 i = 0; i <= N; ++i)
			{
				for (int32 j = 0; j <= N - i; ++j)
				{
					const double U = double(i) / N, V = double(j) / N;
					const double XV = A.X + (B.X - A.X) * U + (C.X - A.X) * V, YV = A.Y + (B.Y - A.Y) * U + (C.Y - A.Y) * V;
					const int64 Xi = PyRoundInt(XV), Yi = PyRoundInt(YV - YMin);
					if (0 <= Xi && Xi <= W && 0 <= Yi && Yi <= H && Occ(int32(Yi), int32(Xi))) { return true; }
				}
			}
			return false;
		};
		TMap<int32, TArray<int32>> ByNode;
		for (int32 i = 0; i < G.Segments.Num(); ++i)
		{
			ByNode.FindOrAdd(G.Segments[i].A).Add(i);
			ByNode.FindOrAdd(G.Segments[i].B).Add(i);
		}
		auto Rank = [&G](int32 Si) { return RoadKind(G.Segments[Si].Kind).Rank; };
		int32 Counts[3] = { 0, 0, 0 };
		for (FJunction& J : Junctions)
		{
			if (J.Type != TEXT("priority") && J.Type != TEXT("blind_t"))
			{
				continue;
			}
			const int32 N = J.Node;
			TArray<int32> Segs;
			for (const int32 i : ByNode.FindRef(N)) { if (!G.Segments[i].bPedestrian) { Segs.Add(i); } }
			if (Segs.Num() < 3)
			{
				continue;
			}
			const FNode& Nd = G.Nodes[N];
			auto Away = [&](int32 Si)
			{
				const FSeg& S = G.Segments[Si];
				const FNode& O = G.Nodes[S.A == N ? S.B : S.A];
				const double Dx = O.X - Nd.X, Dy = O.Y - Nd.Y;
				double L = std::hypot(Dx, Dy);
				L = L != 0.0 ? L : 1.0;
				return FVector2D(Dx / L, Dy / L);
			};
			// The through road: the most nearly straight pair of arms of the highest rank, then the widest.
			int32 BI = INDEX_NONE, BK = INDEX_NONE;
			double Key[3] = { 0, 0, 0 };
			for (int32 i = 0; i < Segs.Num(); ++i)
			{
				for (int32 k = i + 1; k < Segs.Num(); ++k)
				{
					const FVector2D A = Away(Segs[i]), B = Away(Segs[k]);
					const double Straight = -(A.X * B.X + A.Y * B.Y);
					const double Kk[3] = { double(FMath::Min(Rank(Segs[i]), Rank(Segs[k]))), FMath::Min(G.Segments[Segs[i]].Width, G.Segments[Segs[k]].Width), Straight };
					const bool bBetter = BI == INDEX_NONE || (Kk[0] != Key[0] ? Kk[0] > Key[0] : (Kk[1] != Key[1] ? Kk[1] > Key[1] : Kk[2] > Key[2]));
					if (Straight > 0.5 && bBetter)
					{
						BI = i; BK = k;
						FMemory::Memcpy(Key, Kk, sizeof(Kk));
					}
				}
			}
			if (BI == INDEX_NONE)   // a Y: the two widest arms are the through road
			{
				TArray<int32> Ord;
				for (int32 i = 0; i < Segs.Num(); ++i) { Ord.Add(i); }
				Ord.StableSort([&](int32 L, int32 R)
				{
					const int32 RL = Rank(Segs[L]), RR = Rank(Segs[R]);
					return RL != RR ? RL > RR : G.Segments[Segs[L]].Width > G.Segments[Segs[R]].Width;
				});
				BI = Ord[0];
				BK = Ord[1];
			}
			TArray<int32> Through = { Segs[BI], Segs[BK] }, Minor;
			for (int32 i = 0; i < Segs.Num(); ++i) { if (i != BI && i != BK) { Minor.Add(Segs[i]); } }
			const double V = FMath::Max(G.Segments[Through[0]].Speed, G.Segments[Through[1]].Speed) / 3.6;
			const double Half = FMath::Max(G.Segments[Through[0]].Width, G.Segments[Through[1]].Width) / 2.0;
			int32 Verdict = 0;   // 0 priority, 1 stop, 2 blind_t
			for (const int32 Mi : Minor)
			{
				const FSeg& S = G.Segments[Mi];
				const FNode& O = G.Nodes[S.A == N ? S.B : S.A];
				double Length = Dist(O.X, O.Y, Nd.X, Nd.Y);
				Length = Length != 0.0 ? Length : 1.0;
				const double Extra = FMath::Max(0.0, std::fabs(O.Z - Nd.Z) / Length * 100.0 - 2.0);
				const FVector2D Md = Away(Mi);
				int32 Kind = 2;
				const double Ts[2] = { 12.0, 6.0 }, Backs[2] = { 20.0, 3.0 };
				for (int32 m = 0; m < 2; ++m)
				{
					const FPt Obs(Nd.X + Md.X * (Half + Backs[m]), Nd.Y + Md.Y * (Half + Backs[m]));
					bool bClear = true;
					for (const int32 Th : Through)
					{
						const FVector2D Td = Away(Th);
						const FPt Far(Nd.X + Td.X * V * (Ts[m] + Extra), Nd.Y + Td.Y * V * (Ts[m] + Extra));
						if (Blocked(Obs, FPt(Nd.X, Nd.Y), Far))
						{
							bClear = false;
							break;
						}
					}
					if (bClear)
					{
						Kind = m;
						break;
					}
				}
				Verdict = FMath::Max(Verdict, Kind);
			}
			static const TCHAR* Names[3] = { TEXT("priority"), TEXT("stop"), TEXT("blind_t") };
			J.Type = Names[Verdict];
			++Counts[Verdict];
		}
		UE_LOG(LogOpenCity, Display, TEXT("sight triangles (D.M. 19/4/2006 4.6): %d junctions clear for give way, %d need STOP, %d need STOP and a mirror"),
			Counts[0], Counts[1], Counts[2]);
	}

	// --- Output ---------------------------------------------------------------------------------------

	namespace
	{
		FJsonPtr Num(double V, bool bInt) { return bInt ? FJson::MakeInt(int64(V)) : FJson::MakeFloat(V); }
		FJsonPtr Flt(double V) { return FJson::MakeFloat(V); }
		FJsonPtr Int(int64 V) { return FJson::MakeInt(V); }
		FJsonPtr Str(const FString& V) { return FJson::MakeString(V); }
		FJsonPtr StrOrNull(const FString& V) { return V.IsEmpty() ? FJson::MakeNull() : FJson::MakeString(V); }
		FJsonPtr Arr(std::initializer_list<FJsonPtr> Items)
		{
			FJsonPtr A = FJson::MakeArray();
			for (const FJsonPtr& I : Items) { A->Add(I); }
			return A;
		}
		FJsonPtr RectJson(const FRect& R) { return Arr({ Flt(R.X0), Int(int64(R.Y0)), Flt(R.X1), Int(int64(R.Y1)) }); }
		FJsonPtr PtJson(const FPt& P) { return Arr({ Flt(P.X), Num(P.Y, P.IntY) }); }

		FJsonPtr WallJson(const FWallRun& R)
		{
			FJsonPtr O = FJson::MakeObject();
			O->Set(TEXT("material"), Str(R.Material));
			O->Set(TEXT("type"), Str(R.Type));
			O->Set(TEXT("outward"), Arr({ Flt(R.OutX), Flt(R.OutY) }));
			if (R.bTerrace)
			{
				O->Set(TEXT("terrace"), FJson::MakeBool(true));
			}
			FJsonPtr Pts = FJson::MakeArray();
			for (const TArray<double>& P : R.Points) { Pts->Add(Arr({ Flt(P[0]), Flt(P[1]), Flt(P[2]), Flt(P[3]) })); }
			O->Set(TEXT("points"), Pts);
			return O;
		}

		FJsonPtr WaterNetwork(const FTerrainInfo& Info)
		{
			// Water bodies with depth, flow and edge treatments (INFRA_DESIGN.md "Water"). Edge ranges are
			// metres along the centreline (rivers) or along x (coast and basins).
			TArray<double> Along = { 0.0 };
			double Total = 0.0;
			for (int32 i = 0; i + 1 < River.Num(); ++i)
			{
				Total += Dist(River[i], River[i + 1]);
				Along.Add(Total);
			}
			auto AtY = [&](double Yv)
			{
				for (int32 i = 0; i + 1 < River.Num(); ++i)
				{
					if (River[i].Y <= Yv && Yv <= River[i + 1].Y)
					{
						return PyRound(Along[i] + (Along[i + 1] - Along[i]) * (Yv - River[i].Y) / FMath::Max(River[i + 1].Y - River[i].Y, 1e-6), 1);
					}
				}
				return PyRound(Total, 1);
			};
			auto Edge = [](const TCHAR* T, const TCHAR* Side, FJsonPtr From, FJsonPtr To)
			{
				FJsonPtr E = FJson::MakeObject();
				E->Set(TEXT("treatment"), Str(T));
				E->Set(TEXT("side"), Str(Side));
				E->Set(TEXT("from_m"), From);
				E->Set(TEXT("to_m"), To);
				return E;
			};
			auto Body = [](const TCHAR* Id, const TCHAR* Type, double Depth, double Flow, double Level, FJsonPtr Edges)
			{
				FJsonPtr B = FJson::MakeObject();
				B->Set(TEXT("id"), Str(Id));
				B->Set(TEXT("type"), Str(Type));
				B->Set(TEXT("depth_m"), Flt(Depth));
				B->Set(TEXT("flow_mps"), Flt(Flow));
				B->Set(TEXT("level_m"), Flt(Level));
				B->Set(TEXT("edges"), Edges);
				return B;
			};
			FJsonPtr Bodies = FJson::MakeArray();
			Bodies->Add(Body(TEXT("GolfoDiMarevia"), TEXT("sea"), 30.0, 0.0, SeaLevel, Arr({
				Edge(TEXT("breakwater"), TEXT("all"), Flt(Molo.X0), Flt(Breakwater[1].X)),
				Edge(TEXT("quay"), TEXT("all"), Flt(Darsena.X0), Flt(X(1000))),
				Edge(TEXT("delta"), TEXT("all"), Flt(X(1000) - 40), Flt(X(1160))),
				Edge(TEXT("beach_sand"), TEXT("all"), Flt(X(1160)), Int(W)) })));
			Bodies->Add(Body(TEXT("Darsena"), TEXT("port"), 6.0, 0.0, SeaLevel, Arr({ Edge(TEXT("quay"), TEXT("all"), Flt(Darsena.X0), Flt(Darsena.X1)) })));
			{
				FJsonPtr R = Body(TEXT("FiumeArdo"), TEXT("river"), 2.5, 0.8, SeaLevel, Arr({
					Edge(TEXT("embankment_wall"), TEXT("both"), Flt(AtY(40)), Flt(AtY(1650))),
					Edge(TEXT("walkway_low"), TEXT("both"), Flt(AtY(300)), Flt(AtY(1500))),
					Edge(TEXT("natural_bank"), TEXT("both"), Flt(AtY(1650)), Flt(AtY(2040))),
					Edge(TEXT("cliff"), TEXT("both"), Flt(AtY(2040)), Flt(PyRound(Total, 1))) }));
				// Key order as written by the original: id, type, depth_m, flow_mps, level_m, rises_with_weather, centerline, edges.
				FJsonPtr Edges = R->Get(TEXT("edges"));
				R->Keys.RemoveAt(5);
				R->Items.RemoveAt(5);
				R->Set(TEXT("rises_with_weather"), FJson::MakeBool(true));
				FJsonPtr C = FJson::MakeArray();
				for (const FPt& P : River) { C->Add(PtJson(P)); }
				R->Set(TEXT("centerline"), C);
				R->Set(TEXT("edges"), Edges);
				Bodies->Add(R);
			}
			Bodies->Add(Body(TEXT("FociArdo"), TEXT("estuary"), 1.0, 0.3, SeaLevel, Arr({ Edge(TEXT("delta"), TEXT("both"), Flt(0.0), Flt(AtY(40))) })));
			Bodies->Add(Body(TEXT("LagoFerrale"), TEXT("reservoir"), 25.0, 0.0, Info.ReservoirLevel, Arr({
				Edge(TEXT("dam"), TEXT("south"), Flt(Reservoir.X0), Flt(Reservoir.X1)),
				Edge(TEXT("natural_bank"), TEXT("north"), Flt(Reservoir.X0), Flt(Reservoir.X1)) })));
			FJsonPtr O = FJson::MakeObject();
			O->Set(TEXT("bodies"), Bodies);
			return O;
		}
	}

	bool GenerateMarevia(const FString& OutDir, const FString& ProfilesPath, FString& OutError)
	{
		FString ProfilesText;
		if (!FFileHelper::LoadFileToString(ProfilesText, *ProfilesPath))
		{
			OutError = TEXT("cannot read ") + ProfilesPath;
			return false;
		}
		FString ParseError;
		const FJsonPtr AllProfiles = FJson::Parse(ProfilesText, ParseError);
		if (!AllProfiles)
		{
			OutError = TEXT("road_profiles.json: ") + ParseError;
			return false;
		}
		FJsonPtr Profiles = FJson::MakeObject();
		for (int32 i = 0; i < AllProfiles->Keys.Num(); ++i)
		{
			if (!AllProfiles->Keys[i].StartsWith(TEXT("_"))) { Profiles->Set(AllProfiles->Keys[i], AllProfiles->Items[i]); }
		}

		FPyRandom Rng(Seed);
		FNpRandom NpRng(Seed);
		FTerrainInfo Info;
		FGridD Terrain = MakeHeightmap(NpRng, Info);
		FRoadGraph G;
		BuildRoads(G, Terrain, Info, Rng);
		TArray<FLandmark> Landmarks = SettleLandmarks(Terrain, G);   // may remove minor streets under big landmarks
		DropOrphanNodes(G);
		const int32 StillSteep = LimitGrades(G, Info);
		const FGridF Best = FlattenRoads(Terrain, G);
		RelaxSlopes(Terrain, Best);
		const int32 ClampIters = ClampSlopes(Terrain, Best, Info);
		SinkJunctions(Terrain, G);
		const TArray<FWallRun> TerraceWalls = Terraces(Terrain, Best, Landmarks);
		for (FNode& N : G.Nodes)   // the road surface sits 20 cm above the flattened ground
		{
			N.Z = PyRound(FMath::Max(Terrain(int32(PyRoundInt(N.Y - YMin)), int32(PyRoundInt(N.X))) + 0.2, 1.0), 2);
		}
		FPyRandom AnnRng(Seed + 11);
		TArray<FJunction> Junctions;
		if (!AnnotateNetworks(G, Landmarks, AnnRng, Profiles, Junctions, OutError))
		{
			return false;
		}
		TArray<FWallRun> Walls = RetainingWalls(Terrain, G);
		FPyRandom BldRng(Seed + 7);
		TArray<FBuilding> Buildings = PlaceBuildings(Terrain, Best, G, BldRng, Landmarks, OutError);
		if (!OutError.IsEmpty())
		{
			return false;
		}
		Buildings.Append(LandmarkBuildings(Terrain, Landmarks));
		Walls.Append(DropWallsInBuildings(TerraceWalls, Buildings));
		SightTriangles(G, Buildings, Junctions);

		// Connectivity: report pieces of the road network that can't be reached from the start.
		int32 Unreachable = 0;
		{
			TArray<TArray<int32>> Adj;
			Adj.SetNum(G.Nodes.Num());
			for (const FSeg& S : G.Segments) { Adj[S.A].AddUnique(S.B); Adj[S.B].AddUnique(S.A); }
			int32 Start = 0;
			double Bd = TNumericLimits<double>::Max();
			for (int32 i = 0; i < G.Nodes.Num(); ++i)
			{
				const double D = std::pow(G.Nodes[i].X - X(520), 2.0) + std::pow(G.Nodes[i].Y - 1290, 2.0);
				if (D < Bd) { Bd = D; Start = i; }
			}
			TSet<int32> Seen = { Start };
			TArray<int32> Stack = { Start };
			while (!Stack.IsEmpty())
			{
				for (const int32 N : Adj[Stack.Pop()]) { if (!Seen.Contains(N)) { Seen.Add(N); Stack.Add(N); } }
			}
			for (int32 i = 0; i < Adj.Num(); ++i) { Unreachable += (!Seen.Contains(i) && Adj[i].Num()) ? 1 : 0; }
		}

		// --- The plan file ---
		FJsonPtr L = FJson::MakeObject();
		L->Set(TEXT("version"), Int(1));
		L->Set(TEXT("seed"), Int(Seed));
		L->Set(TEXT("units"), Str(TEXT("metres; x east, y north; heights above sea level")));
		L->Set(TEXT("size"), Arr({ Int(W), Int(H) }));
		L->Set(TEXT("origin"), Arr({ Int(0), Int(YMin) }));
		L->Set(TEXT("height_range"), Arr({ Flt(HeightMin), Flt(HeightMax) }));
		L->Set(TEXT("sea_level"), Flt(SeaLevel));
		{
			FJsonPtr Ds = FJson::MakeArray();
			for (const FDistrict& D : Districts)
			{
				FJsonPtr O = FJson::MakeObject();
				O->Set(TEXT("name"), Str(D.Name));
				O->Set(TEXT("rect"), RectJson(D.Rect));
				O->Set(TEXT("unlock"), Str(D.Unlock));
				O->Set(TEXT("style"), Str(D.Style));
				Ds->Add(O);
			}
			L->Set(TEXT("districts"), Ds);
		}
		{
			FJsonPtr Wt = FJson::MakeObject();
			FJsonPtr Rv = FJson::MakeObject();
			Rv->Set(TEXT("name"), Str(TEXT("FiumeArdo")));
			FJsonPtr C = FJson::MakeArray();
			for (const FPt& P : River) { C->Add(PtJson(P)); }
			Rv->Set(TEXT("centerline"), C);
			Rv->Set(TEXT("half_width_lowland"), Flt(25.0));
			Rv->Set(TEXT("dam_y"), Int(DamY));
			Rv->Set(TEXT("depth"), Flt(2.5));
			FJsonPtr Prof = FJson::MakeArray();
			for (const FTerrainInfo::FProfilePt& P : Info.RiverProfile) { Prof->Add(Arr({ Flt(P.Y), Flt(P.X), Flt(P.Bed) })); }
			Rv->Set(TEXT("profile_y_x_bed"), Prof);
			Wt->Set(TEXT("river"), Rv);
			Wt->Set(TEXT("reservoir_level"), Flt(Info.ReservoirLevel));
			Wt->Set(TEXT("dam_height"), Flt(Info.DamHeight));
			Wt->Set(TEXT("darsena"), RectJson(Darsena));
			Wt->Set(TEXT("reservoir"), RectJson(Reservoir));
			Wt->Set(TEXT("molo"), RectJson(Molo));
			Wt->Set(TEXT("breakwater"), Arr({ PtJson(Breakwater[0]), PtJson(Breakwater[1]) }));
			L->Set(TEXT("water"), Wt);
		}
		{
			FJsonPtr Ls = FJson::MakeArray();
			for (const FLandmark& Lm : Landmarks)
			{
				FJsonPtr O = FJson::MakeObject();
				O->Set(TEXT("name"), Str(Lm.Name));
				O->Set(TEXT("district"), Str(Lm.District));
				O->Set(TEXT("pos"), Lm.bMoved ? Arr({ Flt(Lm.Pos.X), Flt(Lm.Pos.Y) }) : PtJson(Lm.Pos));
				O->Set(TEXT("footprint"), Arr({ Num(Lm.FW, Lm.FWInt), Num(Lm.FD, Lm.FDInt) }));
				O->Set(TEXT("height"), Int(Lm.Height));
				O->Set(TEXT("kind"), Str(Lm.Kind));
				O->Set(TEXT("ground_z"), Flt(PyRound(GroundAt(Terrain, Lm.Pos.X, Lm.Pos.Y), 2)));
				Ls->Add(O);
			}
			L->Set(TEXT("landmarks"), Ls);
		}
		{
			struct FGateRow { const TCHAR* Tag; const TCHAR* Opens; const TCHAR* While; };
			const FGateRow Gates[] = {
				{ TEXT("Gate.PortCordon"), TEXT("Story.Mission.Funerale"), TEXT("police cordon at the fire scene") },
				{ TEXT("Gate.Colline"), TEXT("Story.Mission.StradaCostiera"), TEXT("private-road checkpoint") },
				{ TEXT("Gate.Ferrale"), TEXT("Story.Mission.LaCava"), TEXT("rockfall barrier on the pass road") },
				{ TEXT("Gate.PonteMobile"), TEXT("Story.Mission.LaFornace"), TEXT("lifting bridge kept raised") },
				{ TEXT("Gate.RailCrossing"), TEXT("Story.Mission.LaFornace"), TEXT("level crossing closed, freight train parked") },
				{ TEXT("Gate.Molo"), TEXT("Story.Act2.Start"), TEXT("swing bridge turned open") },
			};
			FJsonPtr Gs = FJson::MakeArray();
			for (const FGateRow& Gr : Gates)
			{
				FJsonPtr O = FJson::MakeObject();
				O->Set(TEXT("tag"), Str(Gr.Tag));
				O->Set(TEXT("opens_with"), Str(Gr.Opens));
				O->Set(TEXT("while_locked"), Str(Gr.While));
				Gs->Add(O);
			}
			L->Set(TEXT("gates"), Gs);
		}
		{
			FJsonPtr Rd = FJson::MakeObject();
			FJsonPtr Kinds = FJson::MakeObject();
			for (const FKind& K : RoadKinds) { Kinds->Set(K.Name, Arr({ Int(K.Lanes), Flt(K.Width), Int(K.Speed) })); }
			Rd->Set(TEXT("kinds"), Kinds);
			Rd->Set(TEXT("sidewalk_kinds"), Arr({ Str(TEXT("avenue")), Str(TEXT("street")) }));
			Rd->Set(TEXT("sidewalk_width"), Flt(SidewalkW));
			FJsonPtr Ns = FJson::MakeArray();
			for (const FNode& N : G.Nodes) { Ns->Add(Arr({ Flt(N.X), Flt(N.Y), Flt(N.Z) })); }
			Rd->Set(TEXT("nodes"), Ns);
			FJsonPtr Ss = FJson::MakeArray();
			for (const FSeg& S : G.Segments)
			{
				FJsonPtr O = FJson::MakeObject();
				O->Set(TEXT("a"), Int(S.A));
				O->Set(TEXT("b"), Int(S.B));
				O->Set(TEXT("kind"), Str(S.Kind));
				O->Set(TEXT("lanes_per_dir"), Int(S.Lanes));
				O->Set(TEXT("width"), Flt(S.Width));
				O->Set(TEXT("speed_kmh"), Int(S.Speed));
				O->Set(TEXT("shape"), FJson::MakeArray());
				O->Set(TEXT("bridge"), StrOrNull(S.Bridge));
				O->Set(TEXT("gate"), StrOrNull(S.Gate));
				if (S.bWidening) { O->Set(TEXT("widening"), Flt(S.Widening)); }
				O->Set(TEXT("profile"), Str(S.Profile));
				O->Set(TEXT("paving"), Str(S.Paving));
				O->Set(TEXT("era"), Str(S.Era));
				O->Set(TEXT("parking"), Str(S.Parking));
				O->Set(TEXT("ztl"), FJson::MakeBool(S.bZtl));
				O->Set(TEXT("one_way"), FJson::MakeBool(false));
				O->Set(TEXT("pedestrian"), FJson::MakeBool(S.bPedestrian));
				O->Set(TEXT("tram"), FJson::MakeBool(S.bTram));
				O->Set(TEXT("wear"), Flt(S.Wear));
				Ss->Add(O);
			}
			Rd->Set(TEXT("segments"), Ss);
			Rd->Set(TEXT("profiles"), Profiles);
			FJsonPtr Js = FJson::MakeArray();
			for (const FJunction& J : Junctions)
			{
				FJsonPtr O = FJson::MakeObject();
				O->Set(TEXT("node"), Int(J.Node));
				O->Set(TEXT("type"), Str(J.Type));
				O->Set(TEXT("planned"), FJson::MakeBool(true));
				Js->Add(O);
			}
			Rd->Set(TEXT("junctions"), Js);
			L->Set(TEXT("roads"), Rd);
		}
		{
			FJsonPtr Ws = FJson::MakeArray();
			for (const FWallRun& R : Walls) { Ws->Add(WallJson(R)); }
			L->Set(TEXT("retaining_walls"), Ws);
		}
		{
			// Rail (regional line, tram, funicular) is designed in INFRA_DESIGN.md and waits for approval.
			FJsonPtr Rail = FJson::MakeObject();
			Rail->Set(TEXT("lines"), FJson::MakeArray());
			L->Set(TEXT("rail"), Rail);
		}
		L->Set(TEXT("water_network"), WaterNetwork(Info));
		{
			FJsonPtr St = FJson::MakeArray();
			for (const FString& S : BuildingStyles) { St->Add(Str(S)); }
			L->Set(TEXT("building_styles"), St);
		}
		{
			FJsonPtr Bs = FJson::MakeArray();
			for (const FBuilding& B : Buildings)
			{
				FJsonPtr O = FJson::MakeObject();
				O->Set(TEXT("pos"), Arr({ Flt(B.PX), Flt(B.PY) }));
				O->Set(TEXT("size"), Arr({ Flt(B.SW), Flt(B.SD) }));
				O->Set(TEXT("yaw"), Flt(B.Yaw));
				O->Set(TEXT("base"), Flt(B.Base));
				if (B.Landmark.IsEmpty())
				{
					O->Set(TEXT("storeys"), Int(B.Storeys));
					O->Set(TEXT("top"), Flt(B.Top));
				}
				else
				{
					O->Set(TEXT("top"), Flt(B.Top));
					O->Set(TEXT("storeys"), Int(B.Storeys));
				}
				O->Set(TEXT("style"), Int(B.Style));
				O->Set(TEXT("district"), Str(B.District));
				if (!B.Landmark.IsEmpty()) { O->Set(TEXT("landmark"), Str(B.Landmark)); }
				Bs->Add(O);
			}
			L->Set(TEXT("buildings"), Bs);
		}
		{
			FJsonPtr St = FJson::MakeObject();
			St->Set(TEXT("pos"), Arr({ Flt(X(520)), Int(1290) }));
			St->Set(TEXT("district"), Str(TEXT("CittaAlta")));
			St->Set(TEXT("ground_z"), Flt(PyRound(GroundAt(Terrain, X(520), 1290), 2)));
			L->Set(TEXT("start"), St);
		}
		IFileManager::Get().MakeDirectory(*OutDir, true);
		if (!FFileHelper::SaveStringToFile(L->Write(), *(OutDir / TEXT("marevia_layout.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = TEXT("cannot write marevia_layout.json");
			return false;
		}

		// 16-bit heightmap, north up: HeightMin = 0, HeightMax = 65535.
		FImage Hm(NX, NY, ERawImageFormat::G16, EGammaSpace::Linear);
		uint16* Px = Hm.AsG16().GetData();
		for (int32 r = 0; r < NY; ++r)
		{
			for (int32 c = 0; c < NX; ++c)
			{
				double V = (Terrain(r, c) - HeightMin) / (HeightMax - HeightMin) * 65535.0;
				V = V < 0.0 ? 0.0 : (V > 65535.0 ? 65535.0 : V);
				Px[int64(NY - 1 - r) * NX + c] = uint16(V);
			}
		}
		if (!FImageUtils::SaveImageByExtension(*(OutDir / TEXT("marevia_height.png")), Hm))
		{
			OutError = TEXT("cannot write marevia_height.png");
			return false;
		}
		if (!RenderPreview(OutDir / TEXT("marevia_preview.png"), Terrain, G, Buildings, Landmarks))
		{
			OutError = TEXT("cannot write marevia_preview.png");
			return false;
		}

		// Summary.
		double WallM = 0.0;
		int32 Fill = 0, Cut = 0;
		for (const FWallRun& R : Walls)
		{
			WallM += WallStep * (R.Points.Num() - 1);
			(R.Type == TEXT("fill") ? Fill : Cut) += 1;
		}
		UE_LOG(LogOpenCity, Display, TEXT("retaining walls: %d runs, %.0f m (%d fill, %d cut)"), Walls.Num(), WallM, Fill, Cut);
		TMap<FString, int32> Kinds;
		int32 Gated = 0, Bridges = 0;
		for (const FSeg& S : G.Segments)
		{
			Kinds.FindOrAdd(S.Kind) += 1;
			Gated += S.Gate.IsEmpty() ? 0 : 1;
			Bridges += S.Bridge.IsEmpty() ? 0 : 1;
		}
		FString KindStr;
		for (const TPair<FString, int32>& K : Kinds) { KindStr += FString::Printf(TEXT("%s %d, "), *K.Key, K.Value); }
		UE_LOG(LogOpenCity, Display, TEXT("nodes %d, segments %d (%s), gated %d, bridges %d, unreachable nodes %d, junctions %d"),
			G.Nodes.Num(), G.Segments.Num(), *KindStr, Gated, Bridges, Unreachable, Junctions.Num());
		UE_LOG(LogOpenCity, Display, TEXT("buildings %d; slope clamp: %d passes; %d road segment(s) still over their grade limit"), Buildings.Num(), ClampIters, StillSteep);
		return true;
	}
}
