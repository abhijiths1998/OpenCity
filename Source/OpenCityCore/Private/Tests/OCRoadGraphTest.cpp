#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/OCRoadGraph.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCRoadGraphTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A square A-B-C-D with a slow alley A-C diagonal and a gated bridge B-D. */
	const TCHAR* SmallLayout = TEXT(R"({
		"roads": {
			"nodes": [[0,0,1],[100,0,1],[100,100,1],[0,100,1]],
			"segments": [
				{"a":0,"b":1,"kind":"avenue","lanes_per_dir":2,"width":14,"speed_kmh":60},
				{"a":1,"b":2,"kind":"avenue","lanes_per_dir":2,"width":14,"speed_kmh":60},
				{"a":2,"b":3,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":3,"b":0,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":0,"b":2,"kind":"alley","lanes_per_dir":1,"width":5,"speed_kmh":10},
				{"a":1,"b":3,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"bridge":"PonteTest","gate":"Gate.Test"}
			]
		}
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadGraphLoadTest, "OpenCity.Core.World.RoadGraph.Load", OCRoadGraphTest::Flags)

bool FOCRoadGraphLoadTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph G;
	FString Error;
	TestTrue(TEXT("Parses"), G.LoadFromLayoutJson(OCRoadGraphTest::SmallLayout, Error));
	TestEqual(TEXT("Nodes"), G.NumNodes(), 4);
	TestEqual(TEXT("Segments"), G.NumSegments(), 6);
	// Metres, x east / y north  ->  cm, X east / Y south.
	TestEqual(TEXT("Axis mapping"), G.GetNodes()[2].Location, FVector(10000.0, -10000.0, 100.0));
	TestEqual(TEXT("Width in cm"), G.GetSegments()[0].WidthCm, 1400.f);
	TestEqual(TEXT("Gate tag"), G.GetSegments()[5].Gate, FName(TEXT("Gate.Test")));
	TestEqual(TEXT("Bridge name"), G.GetSegments()[5].Bridge, FName(TEXT("PonteTest")));
	TestTrue(TEXT("No gate on plain road"), G.GetSegments()[0].Gate.IsNone());

	FOCRoadGraph Bad;
	TestFalse(TEXT("Rejects garbage"), Bad.LoadFromLayoutJson(TEXT("{nope"), Error));
	TestFalse(TEXT("Rejects missing node"), Bad.LoadFromLayoutJson(TEXT(R"({"roads":{"nodes":[[0,0,0]],"segments":[{"a":0,"b":5,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50}]}})"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadGraphPathTest, "OpenCity.Core.World.RoadGraph.Path", OCRoadGraphTest::Flags)

bool FOCRoadGraphPathTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph G;
	FString Error;
	G.LoadFromLayoutJson(OCRoadGraphTest::SmallLayout, Error);

	TestEqual(TEXT("Nearest node"), G.FindNearestNode(FVector(9000.0, -9500.0, 0.0)), 2);

	// 0 -> 2: the 10 km/h diagonal is shorter but slower than going round at 60 km/h.
	const TArray<int32> P = G.FindPath(0, 2);
	TestEqual(TEXT("Fastest, not shortest"), P, TArray<int32>({ 0, 1, 2 }));
	TestEqual(TEXT("Travel time"), G.PathTimeSeconds(P), 2.0 * 100.0 / (60.0 / 3.6), 1e-6);

	// 1 -> 3: the gated bridge is the direct route when open...
	TestEqual(TEXT("Uses open bridge"), G.FindPath(1, 3), TArray<int32>({ 1, 3 }));
	// ...and is avoided when its gate is closed.
	const TSet<FName> Closed = { FName(TEXT("Gate.Test")) };
	const TArray<int32> Around = G.FindPath(1, 3, Closed);
	TestEqual(TEXT("Avoids closed gate (length)"), Around.Num(), 3);
	TestFalse(TEXT("Avoids closed gate (no direct hop)"), Around.Num() == 2);

	TestEqual(TEXT("Path to self"), G.FindPath(2, 2), TArray<int32>({ 2 }));
	TestTrue(TEXT("Invalid node gives empty path"), G.FindPath(0, 99).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadGraphReachTest, "OpenCity.Core.World.RoadGraph.Reachability", OCRoadGraphTest::Flags)

bool FOCRoadGraphReachTest::RunTest(const FString& Parameters)
{
	// An island reachable only over a gated bridge: 0-1 mainland, 1-2 bridge (gated), 2-3 island.
	FOCRoadGraph G;
	for (int32 i = 0; i < 4; ++i)
	{
		G.AddNode(FVector(i * 1000.0, 0.0, 0.0));
	}
	FOCRoadSegment S;
	S.SpeedKmh = 50.f;
	S.A = 0; S.B = 1; G.AddSegment(S);
	S.A = 1; S.B = 2; S.Gate = TEXT("Gate.Island"); G.AddSegment(S);
	S.A = 2; S.B = 3; S.Gate = NAME_None; G.AddSegment(S);

	TestEqual(TEXT("All reachable when open"), G.CountReachable(0), 4);
	TestEqual(TEXT("Island cut off when gate closed"), G.CountReachable(0, { FName(TEXT("Gate.Island")) }), 2);
	TestTrue(TEXT("No route to island"), G.FindPath(0, 3, { FName(TEXT("Gate.Island")) }).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadGraphMareviaTest, "OpenCity.Core.World.RoadGraph.MareviaLayout", OCRoadGraphTest::Flags)

bool FOCRoadGraphMareviaTest::RunTest(const FString& Parameters)
{
	// The real generated layout: every road connects, and the story gates actually lock areas.
	const FString File = FPaths::ProjectDir() / TEXT("SourceArt/World/marevia_layout.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		AddError(FString::Printf(TEXT("Missing %s: run python3 -I Tools/make_marevia_layout.py"), *File));
		return false;
	}
	FOCRoadGraph G;
	FString Error;
	if (!TestTrue(TEXT("Layout parses"), G.LoadFromLayoutJson(Text, Error)))
	{
		AddError(Error);
		return false;
	}
	const int32 Start = G.FindNearestNode(FOCRoadGraph::LayoutToWorld(520.0, 1290.0, 0.0)); // Città Alta
	TestEqual(TEXT("Whole network connected with all gates open"), G.CountReachable(Start), G.NumNodes());

	const TSet<FName> AllClosed = { TEXT("Gate.PortCordon"), TEXT("Gate.Colline"), TEXT("Gate.Ferrale"),
		TEXT("Gate.PonteMobile"), TEXT("Gate.RailCrossing"), TEXT("Gate.Molo") };
	const int32 NewGame = G.CountReachable(Start, AllClosed);
	TestTrue(TEXT("A new game reaches only part of the city"), NewGame > 0 && NewGame < G.NumNodes());

	// No leaks: with every gate closed, no road in a locked district may be reachable.
	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
	TArray<FBox2D> Locked;
	for (const TSharedPtr<FJsonValue>& D : Root->GetArrayField(TEXT("districts")))
	{
		const TSharedPtr<FJsonObject>& O = D->AsObject();
		if (!O->GetStringField(TEXT("unlock")).IsEmpty())
		{
			const TArray<TSharedPtr<FJsonValue>>& R = O->GetArrayField(TEXT("rect"));
			const FVector A = FOCRoadGraph::LayoutToWorld(R[0]->AsNumber(), R[1]->AsNumber(), 0.0);
			const FVector B = FOCRoadGraph::LayoutToWorld(R[2]->AsNumber(), R[3]->AsNumber(), 0.0);
			Locked.Add(FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y))));
		}
	}
	int32 Leaks = 0;
	for (int32 i = 0; i < G.NumNodes(); ++i)
	{
		if (G.FindPath(Start, i, AllClosed).IsEmpty())
		{
			continue;
		}
		const FVector2D P(G.GetNodes()[i].Location);
		for (const FBox2D& Box : Locked)
		{
			if (Box.IsInside(P))
			{
				++Leaks;
				AddError(FString::Printf(TEXT("Gate leak: node %d at %s is in a locked district"), i, *P.ToString()));
				break;
			}
		}
	}
	TestEqual(TEXT("No gate leaks at new game"), Leaks, 0);

	// The Lungomare promenade is behind the Ponte Mobile and the rail crossing at the start.
	const int32 Promenade = G.FindNearestNode(FOCRoadGraph::LayoutToWorld(1500.0, 175.0, 0.0));
	TestTrue(TEXT("Lungomare locked at start"), G.FindPath(Start, Promenade, AllClosed).IsEmpty());
	TSet<FName> AfterLaFornace = AllClosed;
	AfterLaFornace.Remove(TEXT("Gate.PonteMobile"));
	AfterLaFornace.Remove(TEXT("Gate.RailCrossing"));
	AfterLaFornace.Remove(TEXT("Gate.PortCordon"));
	TestFalse(TEXT("Lungomare open after La Fornace"), G.FindPath(Start, Promenade, AfterLaFornace).IsEmpty());
	AddInfo(FString::Printf(TEXT("Marevia roads: %d nodes, %d segments, %d reachable at new game"),
		G.NumNodes(), G.NumSegments(), NewGame));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
