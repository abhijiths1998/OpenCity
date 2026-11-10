#include "World/OCNetworkValidation.h"

#include "World/OCInfraNetworks.h"
#include "World/OCRoadGraph.h"

namespace OCValidate
{
	const FName Profile(TEXT("profile"));
	const FName Width(TEXT("width"));
	const FName Lanes(TEXT("lanes"));
	const FName Gradient(TEXT("gradient"));
	const FName Radius(TEXT("radius"));
	const FName Speed(TEXT("speed"));
	const FName Connectivity(TEXT("connectivity"));
	const FName OneWay(TEXT("oneway"));
	const FName Ztl(TEXT("ztl"));
	const FName Tram(TEXT("tram"));
	const FName Junction(TEXT("junction"));
	const FName Clearance(TEXT("clearance"));
	const FName Crossing(TEXT("crossing"));
	const FName RailGeometry(TEXT("rail_geometry"));
	const FName RailGrade(TEXT("rail_gradient"));
	const FName RailPower(TEXT("rail_power"));
	const FName RailStation(TEXT("rail_station"));
	const FName WaterBody(TEXT("water_body"));
	const FName WaterEdge(TEXT("water_edge"));

	const TSet<FName>& JunctionTypes()
	{
		static const TSet<FName> Types = { TEXT("priority"), TEXT("stop"), TEXT("blind_t"), TEXT("signalized"), TEXT("roundabout"), TEXT("piazza"),
			TEXT("gate_fork"), TEXT("ztl_gate"), TEXT("level_crossing"), TEXT("tram_crossing"), TEXT("bridge_head"), TEXT("interchange") };
		return Types;
	}

	const TSet<FName>& EdgeTreatments()
	{
		static const TSet<FName> Types = { TEXT("quay"), TEXT("embankment_wall"), TEXT("walkway_low"), TEXT("natural_bank"), TEXT("beach_sand"),
			TEXT("beach_pebble"), TEXT("rock_platform"), TEXT("cliff"), TEXT("breakwater"), TEXT("dam"), TEXT("gravel_bed"), TEXT("delta") };
		return Types;
	}

	void Add(FOCNetworkReport& Out, FName Check, bool bError, int32 Segment, int32 Node, const FVector& Where, FString Message)
	{
		FOCNetworkIssue I;
		I.Check = Check;
		I.bError = bError;
		I.Segment = Segment;
		I.Node = Node;
		I.Location = Where;
		I.Message = MoveTemp(Message);
		Out.Issues.Add(MoveTemp(I));
	}

	double PlanLength(const FVector& A, const FVector& B)
	{
		return FVector2D(B.X - A.X, B.Y - A.Y).Size();
	}
}

int32 FOCNetworkReport::NumErrors() const
{
	return Issues.FilterByPredicate([](const FOCNetworkIssue& I) { return I.bError; }).Num();
}

int32 FOCNetworkReport::Num(FName Check) const
{
	return Issues.FilterByPredicate([Check](const FOCNetworkIssue& I) { return I.Check == Check; }).Num();
}

FString FOCNetworkReport::Summary(int32 MaxPerCheck) const
{
	TMap<FName, TPair<int32, int32>> Counts;  // errors, warnings
	TArray<FName> Order;
	for (const FOCNetworkIssue& I : Issues)
	{
		if (!Counts.Contains(I.Check))
		{
			Order.Add(I.Check);
		}
		TPair<int32, int32>& C = Counts.FindOrAdd(I.Check);
		(I.bError ? C.Key : C.Value)++;
	}
	FString S = FString::Printf(TEXT("%d issue(s), %d error(s)\n"), Issues.Num(), NumErrors());
	for (const FName Check : Order)
	{
		const TPair<int32, int32>& C = Counts[Check];
		S += FString::Printf(TEXT("%s: %d error(s), %d warning(s)\n"), *Check.ToString(), C.Key, C.Value);
		int32 Shown = 0;
		for (const FOCNetworkIssue& I : Issues)
		{
			if (I.Check == Check && Shown++ < MaxPerCheck)
			{
				// Layout metres (x east, y north) are easier to find in the tools than cm.
				S += FString::Printf(TEXT("  - %s (at %.0f, %.0f m)\n"), *I.Message, I.Location.X / 100.0, -I.Location.Y / 100.0);
			}
		}
	}
	return S;
}

double FOCNetworkValidator::PlanRadiusCm(const FVector& A, const FVector& B, const FVector& C)
{
	const FVector2D P(A.X, A.Y), Q(B.X, B.Y), R(C.X, C.Y);
	const double a = (Q - R).Size(), b = (P - R).Size(), c = (P - Q).Size();
	const double Cross = FMath::Abs((Q.X - P.X) * (R.Y - P.Y) - (Q.Y - P.Y) * (R.X - P.X));
	return Cross < 1e-6 ? TNumericLimits<double>::Max() : a * b * c / (2.0 * Cross);
}

void FOCNetworkValidator::ValidateRoads(const FOCRoadGraph& Roads, const FOptions& Options, FOCNetworkReport& Out)
{
	using namespace OCValidate;
	const TArray<FOCRoadNode>& Nodes = Roads.GetNodes();
	const TArray<FOCRoadSegment>& Segs = Roads.GetSegments();

	for (int32 s = 0; s < Segs.Num(); ++s)
	{
		const FOCRoadSegment& S = Segs[s];
		const FVector PA = Nodes[S.A].Location, PB = Nodes[S.B].Location;
		const FVector Mid = (PA + PB) * 0.5;
		const FOCRoadProfile* P = Roads.FindProfile(S.Profile);
		if (!P)
		{
			Add(Out, Profile, true, s, INDEX_NONE, Mid, FString::Printf(TEXT("segment %d: unknown profile '%s'"), s, *S.Profile.ToString()));
			continue;
		}
		const float Want = P->CarriagewayCm();
		if (Want > 0.f && FMath::Abs(S.WidthCm - S.WideningCm - Want) > Options.WidthTolerance * Want)
		{
			Add(Out, Width, false, s, INDEX_NONE, Mid, FString::Printf(TEXT("segment %d (%s): %.1f m wide, profile carriageway %.1f m"),
				s, *S.Profile.ToString(), S.WidthCm / 100.f, Want / 100.f));
		}
		if (!S.bOneWay && S.LanesPerDirection != P->LanesPerDirection)
		{
			Add(Out, Lanes, false, s, INDEX_NONE, Mid, FString::Printf(TEXT("segment %d (%s): %d lane(s) per direction, profile %d"),
				s, *S.Profile.ToString(), S.LanesPerDirection, P->LanesPerDirection));
		}
		const double Plan = PlanLength(PA, PB);
		if (Plan > 100.0 && S.Bridge.IsNone())
		{
			const double GradePct = FMath::Abs(PB.Z - PA.Z) / Plan * 100.0;
			if (GradePct > P->MaxGradePct + Options.GradeTolerancePct)
			{
				Add(Out, Gradient, GradePct > P->MaxGradePct * 1.5, s, INDEX_NONE, Mid, FString::Printf(TEXT("segment %d (%s): %.1f%% grade, limit %.0f%%"),
					s, *S.Profile.ToString(), GradePct, P->MaxGradePct));
			}
		}
		if (S.SpeedKmh > P->DesignSpeedMaxKmh + 0.5f)
		{
			Add(Out, Speed, false, s, INDEX_NONE, Mid, FString::Printf(TEXT("segment %d (%s): %.0f km/h above the profile's %.0f"),
				s, *S.Profile.ToString(), S.SpeedKmh, P->DesignSpeedMaxKmh));
		}
		if (S.bZTL && S.SpeedKmh > 30.5f)
		{
			Add(Out, Ztl, false, s, INDEX_NONE, Mid, FString::Printf(TEXT("segment %d: ZTL street at %.0f km/h (Zona 30 expected)"), s, S.SpeedKmh));
		}
	}

	// Radius: through-nodes (two segments, same profile) where the road bends.
	for (int32 n = 0; n < Nodes.Num(); ++n)
	{
		const TArray<int32>& At = Roads.SegmentsAt(n);
		if (At.Num() != 2)
		{
			continue;
		}
		const FOCRoadSegment& S0 = Segs[At[0]];
		const FOCRoadSegment& S1 = Segs[At[1]];
		const FOCRoadProfile* P = Roads.FindProfile(S0.Profile);
		if (!P || S0.Profile != S1.Profile || P->MinRadiusM <= 0.f)
		{
			continue;
		}
		const FVector Prev = Nodes[S0.A == n ? S0.B : S0.A].Location;
		const FVector Here = Nodes[n].Location;
		const FVector Next = Nodes[S1.A == n ? S1.B : S1.A].Location;
		const FVector2D In = FVector2D(Here - Prev).GetSafeNormal(), OutDir = FVector2D(Next - Here).GetSafeNormal();
		const double TurnDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(In, OutDir), -1.0, 1.0)));
		if (TurnDeg < Options.MinTurnDeg)
		{
			continue;
		}
		// Nodes sit ~25 m apart, so a hairpin's three-point circle is far wider than the bend the
		// road really makes: there the apex radius is half the distance between the two legs.
		const double R = (TurnDeg > 120.0 ? FMath::Min(PlanRadiusCm(Prev, Here, Next), PlanLength(Prev, Next) * 0.5)
			: PlanRadiusCm(Prev, Here, Next)) / 100.0;
		if (R < P->MinRadiusM)
		{
			Add(Out, Radius, R < P->MinRadiusM * 0.5, INDEX_NONE, n, Here, FString::Printf(TEXT("node %d (%s): %.0f m radius, minimum %.0f m (turn %.0f deg)"),
				n, *S0.Profile.ToString(), R, P->MinRadiusM, TurnDeg));
		}
	}

	// Clearance between stretches of road that are close in plan but not neighbours along the road
	// (stacked hairpin legs, a street passing above another): the land between their edges must
	// have room for a slope of MaxSlope, or it becomes a wall.
	{
		TMap<FIntPoint, TArray<int32>> Grid;  // 50 m cells
		const double CellCm = 5000.0;
		auto CellOf = [CellCm](const FVector& P) { return FIntPoint(FMath::FloorToInt32(P.X / CellCm), FMath::FloorToInt32(P.Y / CellCm)); };
		for (int32 n = 0; n < Nodes.Num(); ++n)
		{
			if (!Roads.SegmentsAt(n).IsEmpty())
			{
				Grid.FindOrAdd(CellOf(Nodes[n].Location)).Add(n);
			}
		}
		auto HalfWidth = [&](int32 n)
		{
			double W = 0.0;
			for (const int32 si : Roads.SegmentsAt(n))
			{
				W = FMath::Max(W, 0.5 * Segs[si].WidthCm);
			}
			return W;
		};
		auto NearAlongRoad = [&](int32 From, int32 To, int32 Hops)
		{
			TArray<int32> Frontier = { From };
			TSet<int32> Seen = { From };
			for (int32 h = 0; h < Hops && !Frontier.IsEmpty(); ++h)
			{
				TArray<int32> Next;
				for (const int32 N : Frontier)
				{
					for (const int32 si : Roads.SegmentsAt(N))
					{
						const int32 M = Segs[si].A == N ? Segs[si].B : Segs[si].A;
						if (M == To)
						{
							return true;
						}
						if (!Seen.Contains(M))
						{
							Seen.Add(M);
							Next.Add(M);
						}
					}
				}
				Frontier = MoveTemp(Next);
			}
			return false;
		};
		for (const TPair<FIntPoint, TArray<int32>>& Cell : Grid)
		{
			for (const int32 n : Cell.Value)
			{
				for (int32 dx = -1; dx <= 1; ++dx)
				{
					for (int32 dy = -1; dy <= 1; ++dy)
					{
						const TArray<int32>* Other = Grid.Find(Cell.Key + FIntPoint(dx, dy));
						for (const int32 m : Other ? *Other : TArray<int32>())
						{
							if (m <= n)
							{
								continue;
							}
							const double Dz = FMath::Abs(Nodes[n].Location.Z - Nodes[m].Location.Z);
							if (Dz < 200.0)
							{
								continue;
							}
							const double Gap = PlanLength(Nodes[n].Location, Nodes[m].Location) - HalfWidth(n) - HalfWidth(m);
							const double Need = Dz / Options.MaxTerrainSlope;
							if (Gap < Need && !NearAlongRoad(n, m, 3))
							{
								Add(Out, Clearance, Gap < Need * 0.5, INDEX_NONE, n, Nodes[n].Location,
									FString::Printf(TEXT("nodes %d and %d: %.0f m apart edge to edge, %.0f m height difference needs %.0f m (wall)"),
										n, m, FMath::Max(0.0, Gap) / 100.0, Dz / 100.0, Need / 100.0));
							}
						}
					}
				}
			}
		}
	}

	// Roads that cross in plan without sharing a node: no junction in the lane graph, two road
	// meshes on top of each other, and walls or kerbs running across the other road (a tour drive
	// got stuck on the viale's kerb where an alley ran through it). The layout generator turns
	// such crossings into junctions (split_crossings); only a bridge with headroom may cross.
	{
		TMap<FIntPoint, TArray<int32>> Grid;  // 50 m cells covered by each segment's bounding box
		const double CellCm = 5000.0;
		for (int32 s = 0; s < Segs.Num(); ++s)
		{
			const FVector& PA = Nodes[Segs[s].A].Location;
			const FVector& PB = Nodes[Segs[s].B].Location;
			const int32 X0 = FMath::FloorToInt32(FMath::Min(PA.X, PB.X) / CellCm), X1 = FMath::FloorToInt32(FMath::Max(PA.X, PB.X) / CellCm);
			const int32 Y0 = FMath::FloorToInt32(FMath::Min(PA.Y, PB.Y) / CellCm), Y1 = FMath::FloorToInt32(FMath::Max(PA.Y, PB.Y) / CellCm);
			for (int32 x = X0; x <= X1; ++x)
			{
				for (int32 y = Y0; y <= Y1; ++y)
				{
					Grid.FindOrAdd(FIntPoint(x, y)).Add(s);
				}
			}
		}
		auto Side = [](const FVector& O, const FVector& A, const FVector& B)
		{
			return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
		};
		TSet<uint64> Seen;
		for (const TPair<FIntPoint, TArray<int32>>& Cell : Grid)
		{
			const TArray<int32>& L = Cell.Value;
			for (int32 i = 0; i < L.Num(); ++i)
			{
				for (int32 j = i + 1; j < L.Num(); ++j)
				{
					const int32 p = FMath::Min(L[i], L[j]), q = FMath::Max(L[i], L[j]);
					bool bSeen = false;
					Seen.Add((uint64(p) << 32) | uint64(q), &bSeen);
					if (bSeen)
					{
						continue;
					}
					const FOCRoadSegment& SP = Segs[p];
					const FOCRoadSegment& SQ = Segs[q];
					if (SP.A == SQ.A || SP.A == SQ.B || SP.B == SQ.A || SP.B == SQ.B)
					{
						continue;
					}
					const FVector& A = Nodes[SP.A].Location;
					const FVector& B = Nodes[SP.B].Location;
					const FVector& C = Nodes[SQ.A].Location;
					const FVector& D = Nodes[SQ.B].Location;
					const double D1 = Side(C, D, A), D2 = Side(C, D, B), D3 = Side(A, B, C), D4 = Side(A, B, D);
					if (!(((D1 > 0) != (D2 > 0)) && ((D3 > 0) != (D4 > 0))) || D1 == D2 || D3 == D4)
					{
						continue;
					}
					const double T = D1 / (D1 - D2), U = D3 / (D3 - D4);
					const FVector At = FMath::Lerp(A, B, T);
					const double Dz = FMath::Abs(FMath::Lerp(A.Z, B.Z, T) - FMath::Lerp(C.Z, D.Z, U));
					if ((!SP.Bridge.IsNone() || !SQ.Bridge.IsNone()) && Dz >= Options.BridgeClearanceCm)
					{
						continue;
					}
					Add(Out, Crossing, true, p, INDEX_NONE, At,
						FString::Printf(TEXT("segments %d (%s) and %d (%s) cross with no junction, %.1f m apart in height"),
							p, *SP.Profile.ToString(), q, *SQ.Profile.ToString(), Dz / 100.0));
				}
			}
		}
	}

	// Connectivity with every gate open: undirected reach, then directed both ways for one-way streets.
	if (Nodes.IsValidIndex(Options.StartNode))
	{
		const TBitArray<> Reach = Roads.ReachableFrom(Options.StartNode);
		// Reverse reach: who can get back to the start (only differs when one-way segments exist).
		TBitArray<> Back(false, Nodes.Num());
		TArray<int32> Stack = { Options.StartNode };
		Back[Options.StartNode] = true;
		while (!Stack.IsEmpty())
		{
			const int32 N = Stack.Pop();
			for (const int32 si : Roads.SegmentsAt(N))
			{
				const FOCRoadSegment& S = Segs[si];
				const int32 Prev = S.A == N ? S.B : S.A;
				if (S.AllowsFrom(Prev) && !Back[Prev])
				{
					Back[Prev] = true;
					Stack.Add(Prev);
				}
			}
		}
		for (int32 n = 0; n < Nodes.Num(); ++n)
		{
			if (Roads.SegmentsAt(n).IsEmpty())
			{
				continue;
			}
			if (!Reach[n])
			{
				Add(Out, Connectivity, true, INDEX_NONE, n, Nodes[n].Location, FString::Printf(TEXT("node %d can't be reached from the start"), n));
			}
			else if (!Back[n])
			{
				Add(Out, OneWay, true, INDEX_NONE, n, Nodes[n].Location, FString::Printf(TEXT("node %d is a one-way trap: no way back to the start"), n));
			}
		}
	}

	// ZTL boundaries need a gate, and tram tracks must form simple lines.
	TSet<int32> Gates;
	for (const FOCRoadJunction& J : Roads.GetJunctions())
	{
		if (!Nodes.IsValidIndex(J.Node))
		{
			Add(Out, Junction, true, INDEX_NONE, J.Node, FVector::ZeroVector, FString::Printf(TEXT("junction on missing node %d"), J.Node));
			continue;
		}
		if (!JunctionTypes().Contains(J.Type))
		{
			Add(Out, Junction, true, INDEX_NONE, J.Node, Nodes[J.Node].Location, FString::Printf(TEXT("node %d: unknown junction type '%s'"), J.Node, *J.Type.ToString()));
		}
		const int32 Degree = Roads.SegmentsAt(J.Node).Num();
		if ((J.Type == FName(TEXT("roundabout")) || J.Type == FName(TEXT("signalized"))) && Degree < 3)
		{
			Add(Out, Junction, false, INDEX_NONE, J.Node, Nodes[J.Node].Location, FString::Printf(TEXT("node %d: %s with only %d road(s)"), J.Node, *J.Type.ToString(), Degree));
		}
		if (J.Type == FName(TEXT("ztl_gate")))
		{
			Gates.Add(J.Node);
		}
	}
	for (int32 n = 0; n < Nodes.Num(); ++n)
	{
		int32 In = 0, OutZ = 0, TramDegree = 0;
		for (const int32 si : Roads.SegmentsAt(n))
		{
			(Segs[si].bZTL ? In : OutZ)++;
			TramDegree += Segs[si].bTram ? 1 : 0;
		}
		if (In > 0 && OutZ > 0 && !Gates.Contains(n))
		{
			Add(Out, Ztl, false, INDEX_NONE, n, Nodes[n].Location, FString::Printf(TEXT("node %d: ZTL boundary without a ztl_gate"), n));
		}
		if (TramDegree > 2)
		{
			Add(Out, Tram, false, INDEX_NONE, n, Nodes[n].Location, FString::Printf(TEXT("node %d: %d tram tracks meet (branch)"), n, TramDegree));
		}
	}
}

void FOCNetworkValidator::ValidateRail(const FOCRailNetwork& Rail, FOCNetworkReport& Out)
{
	using namespace OCValidate;
	const TArray<FOCRailLine>& Lines = Rail.GetLines();
	for (int32 l = 0; l < Lines.Num(); ++l)
	{
		const FOCRailLine& L = Lines[l];
		const FVector Where = L.Points.IsEmpty() ? FVector::ZeroVector : L.Points[0];
		if (L.Points.Num() < 2)
		{
			Add(Out, RailGeometry, true, l, INDEX_NONE, Where, FString::Printf(TEXT("line %s: needs at least two points"), *L.Id.ToString()));
			continue;
		}
		if (L.GaugeMm != 1435)
		{
			Add(Out, RailGeometry, false, l, INDEX_NONE, Where, FString::Printf(TEXT("line %s: %d mm gauge (Italian standard 1435)"), *L.Id.ToString(), L.GaugeMm));
		}
		const bool bTram = L.Kind == FName(TEXT("tram"));
		const FName WantPower = bTram ? FName(TEXT("750V_DC")) : (L.Kind == FName(TEXT("regional")) ? FName(TEXT("3kV_DC")) : NAME_None);
		if (!WantPower.IsNone() && L.Electrification != WantPower && !(bTram && L.Electrification == FName(TEXT("600V_DC"))))
		{
			Add(Out, RailPower, false, l, INDEX_NONE, Where, FString::Printf(TEXT("line %s (%s): %s, expected %s"), *L.Id.ToString(), *L.Kind.ToString(),
				*L.Electrification.ToString(), *WantPower.ToString()));
		}
		for (int32 i = 1; i < L.Points.Num(); ++i)
		{
			const double Plan = PlanLength(L.Points[i - 1], L.Points[i]);
			if (Plan < 100.0)
			{
				continue;
			}
			const double GradePct = FMath::Abs(L.Points[i].Z - L.Points[i - 1].Z) / Plan * 100.0;
			if (GradePct > L.GradeLimitPct())
			{
				Add(Out, RailGrade, true, l, i, L.Points[i], FString::Printf(TEXT("line %s: %.1f%% grade between points %d and %d, limit %.1f%%"),
					*L.Id.ToString(), GradePct, i - 1, i, L.GradeLimitPct()));
			}
		}
		for (const FOCRailStation& S : L.Stations)
		{
			if (!L.Points.IsValidIndex(S.PointIndex))
			{
				Add(Out, RailStation, true, l, INDEX_NONE, Where, FString::Printf(TEXT("line %s: station %s on missing point %d"), *L.Id.ToString(), *S.Name.ToString(), S.PointIndex));
			}
		}
	}
}

void FOCNetworkValidator::ValidateWater(const FOCWaterNetwork& Water, FOCNetworkReport& Out)
{
	using namespace OCValidate;
	const TArray<FOCWaterBody>& Bodies = Water.GetBodies();
	static const TSet<FName> Types = { TEXT("sea"), TEXT("port"), TEXT("river"), TEXT("torrent"), TEXT("reservoir"), TEXT("estuary"), TEXT("canal") };
	static const TSet<FName> Still = { TEXT("sea"), TEXT("port"), TEXT("reservoir") };
	for (int32 b = 0; b < Bodies.Num(); ++b)
	{
		const FOCWaterBody& B = Bodies[b];
		const FVector Where = B.Centerline.IsEmpty() ? FVector::ZeroVector : B.Centerline[0];
		if (!Types.Contains(B.Type))
		{
			Add(Out, WaterBody, true, b, INDEX_NONE, Where, FString::Printf(TEXT("%s: unknown water type '%s'"), *B.Id.ToString(), *B.Type.ToString()));
		}
		if (B.DepthM <= 0.f && !B.bSeasonal)
		{
			Add(Out, WaterBody, true, b, INDEX_NONE, Where, FString::Printf(TEXT("%s: depth %.1f m"), *B.Id.ToString(), B.DepthM));
		}
		if (Still.Contains(B.Type) && B.FlowMps > 0.f)
		{
			Add(Out, WaterBody, false, b, INDEX_NONE, Where, FString::Printf(TEXT("%s: %s with %.1f m/s flow"), *B.Id.ToString(), *B.Type.ToString(), B.FlowMps));
		}
		if ((B.Type == FName(TEXT("river")) || B.Type == FName(TEXT("torrent"))) && B.Centerline.Num() < 2)
		{
			Add(Out, WaterBody, true, b, INDEX_NONE, Where, FString::Printf(TEXT("%s: a %s needs a centreline"), *B.Id.ToString(), *B.Type.ToString()));
		}
		if (B.Edges.IsEmpty())
		{
			Add(Out, WaterEdge, false, b, INDEX_NONE, Where, FString::Printf(TEXT("%s: no edge treatment"), *B.Id.ToString()));
		}
		for (const FOCWaterEdge& E : B.Edges)
		{
			if (!EdgeTreatments().Contains(E.Treatment))
			{
				Add(Out, WaterEdge, true, b, INDEX_NONE, Where, FString::Printf(TEXT("%s: unknown edge treatment '%s'"), *B.Id.ToString(), *E.Treatment.ToString()));
			}
		}
	}
}
