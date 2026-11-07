#include "World/OCRoadGraph.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
		Segments.Add(S);
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
			if (!S.Gate.IsNone() && ClosedGates.Contains(S.Gate))
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

int32 FOCRoadGraph::CountReachable(int32 Start, const TSet<FName>& ClosedGates) const
{
	if (!Nodes.IsValidIndex(Start))
	{
		return 0;
	}
	TBitArray<> Seen(false, Nodes.Num());
	TArray<int32> Stack = { Start };
	Seen[Start] = true;
	int32 Count = 1;
	while (!Stack.IsEmpty())
	{
		const int32 N = Stack.Pop();
		for (const int32 SegIdx : Adjacency[N])
		{
			const FOCRoadSegment& S = Segments[SegIdx];
			if (!S.Gate.IsNone() && ClosedGates.Contains(S.Gate))
			{
				continue;
			}
			const int32 Next = S.A == N ? S.B : S.A;
			if (!Seen[Next])
			{
				Seen[Next] = true;
				++Count;
				Stack.Push(Next);
			}
		}
	}
	return Count;
}
