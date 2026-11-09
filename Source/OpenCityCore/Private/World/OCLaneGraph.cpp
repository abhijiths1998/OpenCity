#include "World/OCLaneGraph.h"

#include "World/OCRoadGraph.h"

namespace OCLanes
{
	/** Room kept clear at a junction (cm): half the widest road plus a stop-line margin. */
	constexpr double JunctionMarginCm = 200.0;
	/** A lane keeps at least this share of its segment; the setbacks share the rest. */
	constexpr double MaxSetbackShare = 0.4;

	FVector Horizontal(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0).GetSafeNormal();
	}

	/** Right of a horizontal heading. Unreal is X east, Y south: facing east, right is +Y. */
	FVector RightOf(const FVector& Forward)
	{
		return FVector(-Forward.Y, Forward.X, 0.0);
	}

	double DistToLine(const FVector& P, const FVector& A, const FVector& B)
	{
		return FMath::Sqrt(FMath::PointDistToSegmentSquared(P, A, B));
	}
}

double FOCLaneGraph::LaneOffsetCm(float RoadWidthCm, int32 LanesPerDirection, int32 Index)
{
	const int32 N = FMath::Max(1, LanesPerDirection);
	const double LaneWidth = RoadWidthCm / (2.0 * N);
	return (Index + 0.5) * LaneWidth;
}

EOCLaneTurn FOCLaneGraph::ClassifyTurn(const FVector& InDir, const FVector& OutDir)
{
	const FVector In = OCLanes::Horizontal(InDir);
	const FVector Out = OCLanes::Horizontal(OutDir);
	// Z of In x Out is positive for a turn to the right (Y points south).
	const double Cross = In.X * Out.Y - In.Y * Out.X;
	const double Deg = FMath::RadiansToDegrees(FMath::Atan2(Cross, FVector::DotProduct(In, Out)));
	if (FMath::Abs(Deg) <= 35.0)
	{
		return EOCLaneTurn::Straight;
	}
	if (FMath::Abs(Deg) >= 145.0)
	{
		return EOCLaneTurn::UTurn;
	}
	return Deg > 0.0 ? EOCLaneTurn::Right : EOCLaneTurn::Left;
}

void FOCLaneGraph::Build(const FOCRoadGraph& Roads)
{
	Lanes.Reset();
	Links.Reset();
	OutLinks.Reset();
	BySegmentDir.Reset();

	const TArray<FOCRoadNode>& Nodes = Roads.GetNodes();
	const TArray<FOCRoadSegment>& Segments = Roads.GetSegments();

	// Segments meeting at each node, and the room each junction needs.
	TArray<TArray<int32>> AtNode;
	AtNode.SetNum(Nodes.Num());
	for (int32 s = 0; s < Segments.Num(); ++s)
	{
		if (Nodes.IsValidIndex(Segments[s].A) && Nodes.IsValidIndex(Segments[s].B) && Segments[s].A != Segments[s].B)
		{
			AtNode[Segments[s].A].Add(s);
			AtNode[Segments[s].B].Add(s);
		}
	}
	TArray<double> Setback;
	Setback.Init(0.0, Nodes.Num());
	for (int32 n = 0; n < Nodes.Num(); ++n)
	{
		double HalfWidest = 0.0;
		for (const int32 s : AtNode[n])
		{
			HalfWidest = FMath::Max(HalfWidest, 0.5 * Segments[s].WidthCm);
		}
		// Junctions keep the crossing clear; bends only need room for the curve.
		Setback[n] = AtNode[n].Num() >= 3 ? HalfWidest + OCLanes::JunctionMarginCm
			: AtNode[n].Num() == 2 ? 0.5 * HalfWidest : 0.0;
	}

	// Lanes: both directions of every two-way segment.
	for (int32 s = 0; s < Segments.Num(); ++s)
	{
		const FOCRoadSegment& Seg = Segments[s];
		if (!Nodes.IsValidIndex(Seg.A) || !Nodes.IsValidIndex(Seg.B) || Seg.A == Seg.B)
		{
			continue;
		}
		// One-way: A to B only. A one-way into a dead end has no U-turn lane; FOCNetworkValidator
		// reports that node as a one-way trap (error), so such data never ships.
		for (int32 Dir = 0; Dir < (Seg.bOneWay ? 1 : 2); ++Dir)
		{
			const int32 From = Dir == 0 ? Seg.A : Seg.B;
			const int32 To = Dir == 0 ? Seg.B : Seg.A;
			const FVector P0 = Nodes[From].Location;
			const FVector P1 = Nodes[To].Location;
			const double Len = FVector::Dist(P0, P1);
			const FVector Along = (P1 - P0).GetSafeNormal();
			const FVector Right = OCLanes::RightOf(OCLanes::Horizontal(P1 - P0));
			const double Max = OCLanes::MaxSetbackShare * Len;
			const FVector S0 = P0 + Along * FMath::Min(Setback[From], Max);
			const FVector S1 = P1 - Along * FMath::Min(Setback[To], Max);

			TArray<int32>& Group = BySegmentDir.Add({ s, From });
			const int32 N = FMath::Max(1, Seg.LanesPerDirection);
			for (int32 i = 0; i < N; ++i)
			{
				FOCLane L;
				L.Segment = s;
				L.FromNode = From;
				L.ToNode = To;
				L.Index = i;
				// Two-way: lanes fill the right half. One-way: they share the whole carriageway.
				const double OffsetCm = Seg.bOneWay ? (i - (N - 1) * 0.5) * Seg.WidthCm / N : LaneOffsetCm(Seg.WidthCm, N, i);
				const FVector Offset = Right * OffsetCm;
				L.Start = S0 + Offset;
				L.End = S1 + Offset;
				L.WidthCm = Seg.bOneWay ? Seg.WidthCm / N : Seg.WidthCm / (2.f * N);
				L.SpeedKmh = Seg.SpeedKmh;
				L.Gate = Seg.Gate;
				Group.Add(Lanes.Add(L));
			}
		}
	}
	OutLinks.SetNum(Lanes.Num());

	auto AddLink = [this](int32 FromLane, int32 ToLane, EOCLaneTurn Turn)
	{
		for (const int32 Existing : OutLinks[FromLane])
		{
			if (Links[Existing].ToLane == ToLane)
			{
				return;
			}
		}
		OutLinks[FromLane].Add(Links.Add({ FromLane, ToLane, Turn }));
	};

	// Links: at each lane's end node, into the lanes that leave that node.
	for (const TPair<TPair<int32, int32>, TArray<int32>>& InGroup : BySegmentDir)
	{
		const TArray<int32>& InLanes = InGroup.Value;
		const FOCLane& First = Lanes[InLanes[0]];
		const int32 Node = First.ToNode;
		const int32 NIn = InLanes.Num();
		const FVector InDir = First.Direction();
		const int32 Degree = AtNode[Node].Num();

		struct FCandidate { const TArray<int32>* OutLanes; EOCLaneTurn Turn; double Straightness; };
		TArray<FCandidate> Candidates;
		for (const int32 s : AtNode[Node])
		{
			// Dead ends turn round onto the same segment; elsewhere U-turns on the same road are not allowed.
			if (s == First.Segment && Degree > 1)
			{
				continue;
			}
			if (const TArray<int32>* Out = BySegmentDir.Find({ s, Node }))
			{
				const FVector OutDir = Lanes[(*Out)[0]].Direction();
				Candidates.Add({ Out, ClassifyTurn(InDir, OutDir), FVector::DotProduct(OCLanes::Horizontal(InDir), OCLanes::Horizontal(OutDir)) });
			}
		}

		auto KeepLanes = [&](const FCandidate& C)
		{
			// Lane i continues in lane i; extra lanes merge into, or split from, the outermost.
			const int32 NOut = C.OutLanes->Num();
			for (int32 i = 0; i < NIn; ++i)
			{
				AddLink(InLanes[i], (*C.OutLanes)[FMath::Min(i, NOut - 1)], C.Turn);
			}
			for (int32 j = NIn; j < NOut; ++j)
			{
				AddLink(InLanes[NIn - 1], (*C.OutLanes)[j], C.Turn);
			}
		};

		for (const FCandidate& C : Candidates)
		{
			// Bends and dead ends just continue, whatever the angle (mountain hairpins).
			if (Degree <= 2 || C.Turn == EOCLaneTurn::Straight)
			{
				KeepLanes(C);
			}
			else if (C.Turn == EOCLaneTurn::Right)
			{
				AddLink(InLanes.Last(), C.OutLanes->Last(), C.Turn);
			}
			else // Left, or a sharp turn into another road
			{
				AddLink(InLanes[0], (*C.OutLanes)[0], C.Turn);
			}
		}

		// No stranded lanes: a lane the turn rules left without a way on takes the straightest exit.
		if (!Candidates.IsEmpty())
		{
			const FCandidate* Best = &Candidates[0];
			for (const FCandidate& C : Candidates)
			{
				Best = C.Straightness > Best->Straightness ? &C : Best;
			}
			for (int32 i = 0; i < NIn; ++i)
			{
				if (OutLinks[InLanes[i]].IsEmpty())
				{
					AddLink(InLanes[i], (*Best->OutLanes)[FMath::Min(i, Best->OutLanes->Num() - 1)], Best->Turn);
				}
			}
		}
	}
}

const TArray<int32>& FOCLaneGraph::GetOutLinks(int32 Lane) const
{
	static const TArray<int32> None;
	return OutLinks.IsValidIndex(Lane) ? OutLinks[Lane] : None;
}

TArray<int32> FOCLaneGraph::GetSegmentLanes(int32 Segment, int32 FromNode) const
{
	const TArray<int32>* Found = BySegmentDir.Find({ Segment, FromNode });
	return Found ? *Found : TArray<int32>();
}

int32 FOCLaneGraph::FindNearestLane(const FVector& Location, const FVector& Forward, const TSet<FName>& ClosedGates) const
{
	const FVector Heading = OCLanes::Horizontal(Forward);
	int32 Best = INDEX_NONE;
	double BestDist = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		const FOCLane& L = Lanes[i];
		if (!L.Gate.IsNone() && ClosedGates.Contains(L.Gate))
		{
			continue;
		}
		if (!Heading.IsZero() && FVector::DotProduct(Heading, OCLanes::Horizontal(L.End - L.Start)) < 0.0)
		{
			continue;
		}
		const double D = OCLanes::DistToLine(Location, L.Start, L.End);
		if (D < BestDist)
		{
			BestDist = D;
			Best = i;
		}
	}
	return Best;
}
