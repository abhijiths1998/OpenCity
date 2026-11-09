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
	const FName RailGeometry(TEXT("rail_geometry"));
	const FName RailGrade(TEXT("rail_gradient"));
	const FName RailPower(TEXT("rail_power"));
	const FName RailStation(TEXT("rail_station"));
	const FName WaterBody(TEXT("water_body"));
	const FName WaterEdge(TEXT("water_edge"));

	const TSet<FName>& JunctionTypes()
	{
		static const TSet<FName> Types = { TEXT("priority"), TEXT("blind_t"), TEXT("signalized"), TEXT("roundabout"), TEXT("piazza"),
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
		if (Want > 0.f && FMath::Abs(S.WidthCm - Want) > Options.WidthTolerance * Want)
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
		const double R = PlanRadiusCm(Prev, Here, Next) / 100.0;
		if (R < P->MinRadiusM)
		{
			Add(Out, Radius, R < P->MinRadiusM * 0.5, INDEX_NONE, n, Here, FString::Printf(TEXT("node %d (%s): %.0f m radius, minimum %.0f m (turn %.0f deg)"),
				n, *S0.Profile.ToString(), R, P->MinRadiusM, TurnDeg));
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
