#include "World/OCRoadGraph.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

float FOCRoadProfile::CarriagewayCm() const
{
	return 2.f * LanesPerDirection * LaneCm + 2.f * (ShoulderLeftCm + ShoulderRightCm + EmergencyLaneCm) + MedianCm + ParkingCm;
}

const TArray<int32>& FOCRoadGraph::SegmentsAt(int32 Node) const
{
	static const TArray<int32> None;
	return Adjacency.IsValidIndex(Node) ? Adjacency[Node] : None;
}

float FOCRoadSegment::LengthCm(const TArray<FOCRoadNode>& Nodes) const
{
	return Nodes.IsValidIndex(A) && Nodes.IsValidIndex(B)
		? static_cast<float>(FVector::Dist(Nodes[A].Location, Nodes[B].Location)) : 0.f;
}

FVector FOCRoadGraph::LayoutToWorld(double X, double Y, double Z)
{
	return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
}

void FOCRoadGraph::AddNode(const FVector& Location)
{
	Nodes.Add({ Location });
	Adjacency.AddDefaulted();
}

void FOCRoadGraph::AddSegment(const FOCRoadSegment& Segment)
{
	const int32 Index = Segments.Add(Segment);
	if (Adjacency.IsValidIndex(Segment.A) && Adjacency.IsValidIndex(Segment.B))
	{
		Adjacency[Segment.A].Add(Index);
		Adjacency[Segment.B].Add(Index);
	}
}

void FOCRoadGraph::RebuildAdjacency()
{
	Adjacency.Reset();
	Adjacency.SetNum(Nodes.Num());
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		const FOCRoadSegment& S = Segments[i];
		if (Adjacency.IsValidIndex(S.A) && Adjacency.IsValidIndex(S.B))
		{
			Adjacency[S.A].Add(i);
			Adjacency[S.B].Add(i);
		}
	}
}

bool FOCRoadGraph::LoadFromLayoutJson(const FString& JsonText, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}
	const TSharedPtr<FJsonObject>* Roads = nullptr;
	if (!Root->TryGetObjectField(TEXT("roads"), Roads))
	{
		OutError = TEXT("missing \"roads\"");
		return false;
	}

	Nodes.Reset();
	Segments.Reset();
	for (const TSharedPtr<FJsonValue>& V : (*Roads)->GetArrayField(TEXT("nodes")))
	{
		const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
		if (P.Num() < 3)
		{
			OutError = TEXT("node needs [x, y, z]");
			return false;
		}
		Nodes.Add({ LayoutToWorld(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()) });
	}
	for (const TSharedPtr<FJsonValue>& V : (*Roads)->GetArrayField(TEXT("segments")))
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		FOCRoadSegment S;
		S.A = static_cast<int32>(O->GetNumberField(TEXT("a")));
		S.B = static_cast<int32>(O->GetNumberField(TEXT("b")));
		if (!Nodes.IsValidIndex(S.A) || !Nodes.IsValidIndex(S.B))
		{
			OutError = FString::Printf(TEXT("segment refers to missing node (%d, %d)"), S.A, S.B);
			return false;
		}
		S.Kind = FName(*O->GetStringField(TEXT("kind")));
		S.LanesPerDirection = static_cast<int32>(O->GetNumberField(TEXT("lanes_per_dir")));
		S.WidthCm = static_cast<float>(O->GetNumberField(TEXT("width")) * 100.0);
		S.SpeedKmh = static_cast<float>(O->GetNumberField(TEXT("speed_kmh")));
		FString Str;
		if (O->TryGetStringField(TEXT("bridge"), Str)) { S.Bridge = FName(*Str); }
		if (O->TryGetStringField(TEXT("gate"), Str)) { S.Gate = FName(*Str); }
		// Infrastructure metadata (optional; INFRA_DESIGN.md).
		if (O->TryGetStringField(TEXT("profile"), Str)) { S.Profile = FName(*Str); }
		if (O->TryGetStringField(TEXT("paving"), Str)) { S.Paving = FName(*Str); }
		if (O->TryGetStringField(TEXT("era"), Str)) { S.Era = FName(*Str); }
		if (O->TryGetStringField(TEXT("parking"), Str)) { S.Parking = FName(*Str); }
		O->TryGetBoolField(TEXT("one_way"), S.bOneWay);
		O->TryGetBoolField(TEXT("ztl"), S.bZTL);
		O->TryGetBoolField(TEXT("tram"), S.bTram);
		O->TryGetBoolField(TEXT("pedestrian"), S.bPedestrian);
		double Widening = 0.0;
		if (O->TryGetNumberField(TEXT("widening"), Widening)) { S.WideningCm = static_cast<float>(Widening * 100.0); }
		double Wear = S.Wear;
		if (O->TryGetNumberField(TEXT("wear"), Wear)) { S.Wear = static_cast<float>(Wear); }
		Segments.Add(S);
	}

	Profiles.Reset();
	const TSharedPtr<FJsonObject>* ProfileObj = nullptr;
	if ((*Roads)->TryGetObjectField(TEXT("profiles"), ProfileObj))
	{
		for (const auto& Pair : (*ProfileObj)->Values)
		{
			const TSharedPtr<FJsonObject> P = Pair.Value->AsObject();
			if (!P.IsValid())
			{
				continue;
			}
			FOCRoadProfile R;
			R.Id = FName(*Pair.Key);
			FString Str;
			if (P->TryGetStringField(TEXT("dm_type"), Str) && !Str.IsEmpty()) { R.DMType = FName(*Str); }
			if (P->TryGetStringField(TEXT("paving"), Str)) { R.Paving = FName(*Str); }
			P->TryGetBoolField(TEXT("urban"), R.bUrban);
			auto Num = [&P](const TCHAR* Key, float& Out)
			{
				double V;
				if (P->TryGetNumberField(Key, V)) { Out = static_cast<float>(V); }
			};
			double Lanes = R.LanesPerDirection;
			if (P->TryGetNumberField(TEXT("lanes_per_dir"), Lanes)) { R.LanesPerDirection = static_cast<int32>(Lanes); }
			Num(TEXT("lane_cm"), R.LaneCm);
			Num(TEXT("shoulder_left_cm"), R.ShoulderLeftCm);
			Num(TEXT("shoulder_right_cm"), R.ShoulderRightCm);
			Num(TEXT("emergency_lane_cm"), R.EmergencyLaneCm);
			Num(TEXT("median_cm"), R.MedianCm);
			Num(TEXT("footpath_cm"), R.FootpathCm);
			Num(TEXT("parking_cm"), R.ParkingCm);
			Num(TEXT("max_grade_pct"), R.MaxGradePct);
			Num(TEXT("min_radius_m"), R.MinRadiusM);
			Num(TEXT("design_speed_max_kmh"), R.DesignSpeedMaxKmh);
			Profiles.Add(R.Id, R);
		}
	}
	Junctions.Reset();
	const TArray<TSharedPtr<FJsonValue>>* JunctionArr = nullptr;
	if ((*Roads)->TryGetArrayField(TEXT("junctions"), JunctionArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *JunctionArr)
		{
			const TSharedPtr<FJsonObject> J = V->AsObject();
			if (!J.IsValid())
			{
				continue;
			}
			FOCRoadJunction R;
			R.Node = static_cast<int32>(J->GetNumberField(TEXT("node")));
			R.Type = FName(*J->GetStringField(TEXT("type")));
			J->TryGetBoolField(TEXT("planned"), R.bPlanned);
			Junctions.Add(R);
		}
	}
	RebuildAdjacency();
	return true;
}

int32 FOCRoadGraph::FindNearestNode(const FVector& Location) const
{
	int32 Best = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		const double D = FVector::DistSquared(Nodes[i].Location, Location);
		if (D < BestDistSq)
		{
			BestDistSq = D;
			Best = i;
		}
	}
	return Best;
}

int32 FOCRoadGraph::FindNearestNodePlanar(const FVector& Location) const
{
	int32 Best = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		const double D = FVector::DistSquared2D(Nodes[i].Location, Location);
		if (D < BestDistSq && !Adjacency[i].IsEmpty())
		{
			BestDistSq = D;
			Best = i;
		}
	}
	return Best;
}

double FOCRoadGraph::SegmentTime(const FOCRoadSegment& S) const
{
	const double SpeedCmPerSec = FMath::Max(1.0, S.SpeedKmh / 0.036);
	return S.LengthCm(Nodes) / SpeedCmPerSec;
}

TArray<int32> FOCRoadGraph::FindPath(int32 Start, int32 Goal, const TSet<FName>& ClosedGates) const
{
	TArray<int32> Path;
	if (!Nodes.IsValidIndex(Start) || !Nodes.IsValidIndex(Goal))
	{
		return Path;
	}

	// A* on travel time; the heuristic assumes the fastest road anywhere (admissible).
	double MaxSpeed = 1.0;
	for (const FOCRoadSegment& S : Segments)
	{
		MaxSpeed = FMath::Max(MaxSpeed, static_cast<double>(S.SpeedKmh) / 0.036);
	}
	auto Heuristic = [&](int32 N) { return FVector::Dist(Nodes[N].Location, Nodes[Goal].Location) / MaxSpeed; };

	TArray<double> Cost;
	Cost.Init(TNumericLimits<double>::Max(), Nodes.Num());
	TArray<int32> Came;
	Came.Init(INDEX_NONE, Nodes.Num());
	struct FOpen { double F; int32 Node; bool operator<(const FOpen& O) const { return F < O.F; } };
	TArray<FOpen> Open;
	Cost[Start] = 0.0;
	Open.HeapPush({ Heuristic(Start), Start });

	while (!Open.IsEmpty())
	{
		FOpen Top;
		Open.HeapPop(Top);
		const int32 N = Top.Node;
		if (N == Goal)
		{
			for (int32 At = Goal; At != INDEX_NONE; At = Came[At])
			{
				Path.Insert(At, 0);
			}
			return Path;
		}
		if (Top.F > Cost[N] + Heuristic(N) + KINDA_SMALL_NUMBER)
		{
			continue; // stale heap entry
		}
		for (const int32 SegIdx : Adjacency[N])
		{
			const FOCRoadSegment& S = Segments[SegIdx];
			if ((!S.Gate.IsNone() && ClosedGates.Contains(S.Gate)) || !S.AllowsFrom(N))
			{
				continue;
			}
			const int32 Next = S.A == N ? S.B : S.A;
			const double NewCost = Cost[N] + SegmentTime(S);
			if (NewCost < Cost[Next])
			{
				Cost[Next] = NewCost;
				Came[Next] = N;
				Open.HeapPush({ NewCost + Heuristic(Next), Next });
			}
		}
	}
	return Path;
}

TArray<int32> FOCRoadGraph::FindPathNoUTurn(int32 Start, int32 ArrivedFrom, int32 Goal, const TSet<FName>& ClosedGates) const
{
	TArray<int32> Path;
	if (!Nodes.IsValidIndex(Start) || !Nodes.IsValidIndex(Goal))
	{
		return Path;
	}
	// Search over (node, previous node) states: the same node reached from different roads may
	// continue differently. Same A* on travel time as FindPath.
	double MaxSpeed = 1.0;
	for (const FOCRoadSegment& S : Segments)
	{
		MaxSpeed = FMath::Max(MaxSpeed, static_cast<double>(S.SpeedKmh) / 0.036);
	}
	auto Heuristic = [&](int32 N) { return FVector::Dist(Nodes[N].Location, Nodes[Goal].Location) / MaxSpeed; };
	const int64 Stride = int64(Nodes.Num()) + 1;
	auto Key = [Stride](int32 N, int32 Prev) { return int64(N) * Stride + int64(Prev + 1); };
	TMap<int64, double> Cost;
	TMap<int64, int64> Came;
	struct FOpen { double F; int32 Node; int32 Prev; bool operator<(const FOpen& O) const { return F < O.F; } };
	TArray<FOpen> Open;
	Cost.Add(Key(Start, ArrivedFrom), 0.0);
	Open.HeapPush({ Heuristic(Start), Start, ArrivedFrom });
	while (!Open.IsEmpty())
	{
		FOpen Top;
		Open.HeapPop(Top);
		const int64 K = Key(Top.Node, Top.Prev);
		const double G = Cost[K];
		if (Top.F > G + Heuristic(Top.Node) + KINDA_SMALL_NUMBER)
		{
			continue; // stale heap entry
		}
		if (Top.Node == Goal)
		{
			for (int64 At = K;;)
			{
				Path.Insert(int32(At / Stride), 0);
				const int64* Back = Came.Find(At);
				if (!Back)
				{
					break;
				}
				At = *Back;
			}
			return Path;
		}
		const bool bDeadEnd = Adjacency[Top.Node].Num() <= 1;
		for (const int32 SegIdx : Adjacency[Top.Node])
		{
			const FOCRoadSegment& S = Segments[SegIdx];
			if ((!S.Gate.IsNone() && ClosedGates.Contains(S.Gate)) || !S.AllowsFrom(Top.Node))
			{
				continue;
			}
			const int32 Next = S.A == Top.Node ? S.B : S.A;
			if (Next == Top.Prev && !bDeadEnd)
			{
				continue; // no U-turn except at a dead end
			}
			const int64 NK = Key(Next, Top.Node);
			const double NewCost = G + SegmentTime(S);
			const double* Old = Cost.Find(NK);
			if (!Old || NewCost < *Old)
			{
				Cost.Add(NK, NewCost);
				Came.Add(NK, K);
				Open.HeapPush({ NewCost + Heuristic(Next), Next, Top.Node });
			}
		}
	}
	return Path;
}

double FOCRoadGraph::PathTimeSeconds(const TArray<int32>& Path) const
{
	double T = 0.0;
	for (int32 i = 0; i + 1 < Path.Num(); ++i)
	{
		for (const int32 SegIdx : Adjacency[Path[i]])
		{
			const FOCRoadSegment& S = Segments[SegIdx];
			if ((S.A == Path[i] && S.B == Path[i + 1]) || (S.B == Path[i] && S.A == Path[i + 1]))
			{
				T += SegmentTime(S);
				break;
			}
		}
	}
	return T;
}

TBitArray<> FOCRoadGraph::ReachableFrom(int32 Start, const TSet<FName>& ClosedGates) const
{
	TBitArray<> Seen(false, Nodes.Num());
	if (!Nodes.IsValidIndex(Start))
	{
		return Seen;
	}
	TArray<int32> Stack = { Start };
	Seen[Start] = true;
	while (!Stack.IsEmpty())
	{
		const int32 N = Stack.Pop();
		for (const int32 SegIdx : Adjacency[N])
		{
			const FOCRoadSegment& S = Segments[SegIdx];
			if ((!S.Gate.IsNone() && ClosedGates.Contains(S.Gate)) || !S.AllowsFrom(N))
			{
				continue;
			}
			const int32 Next = S.A == N ? S.B : S.A;
			if (!Seen[Next])
			{
				Seen[Next] = true;
				Stack.Add(Next);
			}
		}
	}
	return Seen;
}

int32 FOCRoadGraph::CountReachable(int32 Start, const TSet<FName>& ClosedGates) const
{
	return ReachableFrom(Start, ClosedGates).CountSetBits();
}

int32 FOCRoadGraph::FindNearestNode(const FVector& Location, const TBitArray<>& Allowed) const
{
	int32 Best = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Nodes.Num() && i < Allowed.Num(); ++i)
	{
		const double D = FVector::DistSquared(Nodes[i].Location, Location);
		if (Allowed[i] && D < BestDistSq)
		{
			BestDistSq = D;
			Best = i;
		}
	}
	return Best;
}
