#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCLaneGraphTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/**
	 * A crossroads at node 0: a 2+2-lane avenue east-west (nodes 3 west, 1 east), 1+1-lane
	 * streets north (2) and south (4). Plus a hairpin bend: 4 -> 5 -> 6 doubles back.
	 */
	const TCHAR* Crossroads = TEXT(R"({
		"roads": {
			"nodes": [[0,0,0],[100,0,0],[0,100,0],[-100,0,0],[0,-100,0],[10,-200,0],[20,-100,0]],
			"segments": [
				{"a":0,"b":1,"kind":"avenue","lanes_per_dir":2,"width":14,"speed_kmh":60},
				{"a":0,"b":2,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":0,"b":3,"kind":"avenue","lanes_per_dir":2,"width":14,"speed_kmh":60},
				{"a":0,"b":4,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"gate":"Gate.Test"},
				{"a":4,"b":5,"kind":"pass","lanes_per_dir":1,"width":7,"speed_kmh":50},
				{"a":5,"b":6,"kind":"pass","lanes_per_dir":1,"width":7,"speed_kmh":50}
			]
		}
	})");

	int32 LinkTo(const FOCLaneGraph& G, int32 From, int32 To)
	{
		for (const int32 L : G.GetOutLinks(From))
		{
			if (G.GetLinks()[L].ToLane == To)
			{
				return L;
			}
		}
		return INDEX_NONE;
	}

	bool LinksToSegment(const FOCLaneGraph& G, int32 From, int32 Segment)
	{
		for (const int32 L : G.GetOutLinks(From))
		{
			if (G.GetLanes()[G.GetLinks()[L].ToLane].Segment == Segment)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLaneGraphGeometryTest, "OpenCity.Core.World.Lanes.Geometry", OCLaneGraphTest::Flags)

bool FOCLaneGraphGeometryTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Inner lane of a 14 m two-lane road"), FOCLaneGraph::LaneOffsetCm(1400.f, 2, 0), 175.0);
	TestEqual(TEXT("Outer lane of a 14 m two-lane road"), FOCLaneGraph::LaneOffsetCm(1400.f, 2, 1), 525.0);
	TestEqual(TEXT("Single lane of a 9 m street"), FOCLaneGraph::LaneOffsetCm(900.f, 1, 0), 225.0);

	// Unreal axes: X east, Y south. Heading east, south is to the right.
	const FVector East(1, 0, 0), South(0, 1, 0), North(0, -1, 0), West(-1, 0, 0);
	TestTrue(TEXT("East then south is a right turn"), FOCLaneGraph::ClassifyTurn(East, South) == EOCLaneTurn::Right);
	TestTrue(TEXT("East then north is a left turn"), FOCLaneGraph::ClassifyTurn(East, North) == EOCLaneTurn::Left);
	TestTrue(TEXT("East then east is straight"), FOCLaneGraph::ClassifyTurn(East, East) == EOCLaneTurn::Straight);
	TestTrue(TEXT("East then west is a U-turn"), FOCLaneGraph::ClassifyTurn(East, West) == EOCLaneTurn::UTurn);

	FOCRoadGraph Roads;
	FString Error;
	TestTrue(TEXT("Parses"), Roads.LoadFromLayoutJson(OCLaneGraphTest::Crossroads, Error));
	FOCLaneGraph G;
	G.Build(Roads);
	TestEqual(TEXT("Lane count"), G.NumLanes(), 2 * (2 + 1 + 2 + 1 + 1 + 1));

	// Eastbound avenue lanes (0 -> 1) sit right of the centre line (south, +Y), inner then outer.
	const TArray<int32> East01 = G.GetSegmentLanes(0, 0);
	if (!TestEqual(TEXT("Eastbound lanes"), East01.Num(), 2))
	{
		return false;
	}
	const FOCLane& Inner = G.GetLanes()[East01[0]];
	const FOCLane& Outer = G.GetLanes()[East01[1]];
	TestEqual(TEXT("Inner lane offset"), Inner.Start.Y, 175.0, 0.01);
	TestEqual(TEXT("Outer lane offset"), Outer.Start.Y, 525.0, 0.01);
	TestTrue(TEXT("Heads east"), Inner.Direction().Equals(East, 1e-6));
	// Crossroads: half the widest road (7 m) + 2 m kept clear. Dead end: none.
	TestEqual(TEXT("Junction setback"), Inner.Start.X, 900.0, 0.01);
	TestEqual(TEXT("Dead end: no setback"), Inner.End.X, 10000.0, 0.01);
	const TArray<int32> West10 = G.GetSegmentLanes(0, 1);
	TestTrue(TEXT("Westbound lanes on the north side"), G.GetLanes()[West10[0]].Start.Y < 0.0);
	TestEqual(TEXT("Lane gate"), G.GetLanes()[G.GetSegmentLanes(3, 0)[0]].Gate, FName(TEXT("Gate.Test")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLaneGraphLinksTest, "OpenCity.Core.World.Lanes.Links", OCLaneGraphTest::Flags)

bool FOCLaneGraphLinksTest::RunTest(const FString& Parameters)
{
	using namespace OCLaneGraphTest;
	FOCRoadGraph Roads;
	FString Error;
	Roads.LoadFromLayoutJson(Crossroads, Error);
	FOCLaneGraph G;
	G.Build(Roads);

	// Arriving eastbound from the west arm (segment 2, node 3 -> 0).
	const TArray<int32> In = G.GetSegmentLanes(2, 3);
	const TArray<int32> EastOut = G.GetSegmentLanes(0, 0);
	const TArray<int32> NorthOut = G.GetSegmentLanes(1, 0);
	const TArray<int32> SouthOut = G.GetSegmentLanes(3, 0);
	TestTrue(TEXT("Inner lane goes straight in its lane"), LinkTo(G, In[0], EastOut[0]) != INDEX_NONE);
	TestTrue(TEXT("Outer lane goes straight in its lane"), LinkTo(G, In[1], EastOut[1]) != INDEX_NONE);
	TestEqual(TEXT("No lane change across the junction"), LinkTo(G, In[0], EastOut[1]), INDEX_NONE);

	const int32 Right = LinkTo(G, In[1], SouthOut[0]);
	TestTrue(TEXT("Right turn from the outer lane"), Right != INDEX_NONE && G.GetLinks()[Right].Turn == EOCLaneTurn::Right);
	TestFalse(TEXT("No right turn from the inner lane"), LinksToSegment(G, In[0], 3));
	const int32 Left = LinkTo(G, In[0], NorthOut[0]);
	TestTrue(TEXT("Left turn from the inner lane"), Left != INDEX_NONE && G.GetLinks()[Left].Turn == EOCLaneTurn::Left);
	TestFalse(TEXT("No left turn from the outer lane"), LinksToSegment(G, In[1], 1));
	TestFalse(TEXT("No U-turn at a junction"), LinksToSegment(G, In[0], 2) || LinksToSegment(G, In[1], 2));

	// Heading south from the north street, east is a left turn: onto the avenue's inner lane.
	const TArray<int32> FromNorth = G.GetSegmentLanes(1, 2);
	TestTrue(TEXT("Street turns left onto the avenue's inner lane"), LinkTo(G, FromNorth[0], EastOut[0]) != INDEX_NONE);

	// Dead end at the east arm: turn round onto the same road.
	const TArray<int32> Back = G.GetSegmentLanes(0, 1);
	TestTrue(TEXT("Dead end turns round"), LinkTo(G, EastOut[0], Back[0]) != INDEX_NONE);

	// Hairpin bend (4 -> 5 -> 6 doubles back): still one continuous lane, not a forbidden U-turn.
	const TArray<int32> Up = G.GetSegmentLanes(4, 4);
	const TArray<int32> Down = G.GetSegmentLanes(5, 5);
	TestTrue(TEXT("Hairpin continues"), LinkTo(G, Up[0], Down[0]) != INDEX_NONE);

	// Every lane has a way on.
	for (int32 i = 0; i < G.NumLanes(); ++i)
	{
		TestFalse(FString::Printf(TEXT("Lane %d has a successor"), i), G.GetOutLinks(i).IsEmpty());
	}

	// Nearest lane respects heading and gates.
	const FVector OnEastbound(5000.0, 200.0, 0.0);
	TestEqual(TEXT("Nearest eastbound lane"), G.FindNearestLane(OnEastbound, FVector(1, 0, 0)), EastOut[0]);
	TestTrue(TEXT("Heading west picks a westbound lane"),
		G.GetLanes()[G.FindNearestLane(OnEastbound, FVector(-1, 0, 0))].FromNode == 1);
	const FVector OnSouthArm(0.0, 5000.0, 0.0);
	TestEqual(TEXT("Closed gate skipped"), G.GetLanes()[G.FindNearestLane(OnSouthArm, FVector::ZeroVector, { FName(TEXT("Gate.Test")) })].Gate, FName());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLaneGraphMareviaTest, "OpenCity.Core.World.Lanes.MareviaLayout", OCLaneGraphTest::Flags)

bool FOCLaneGraphMareviaTest::RunTest(const FString& Parameters)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("SourceArt/World/marevia_layout.json"))))
	{
		AddError(TEXT("Missing SourceArt/World/marevia_layout.json: run scripts/make_layout.sh"));
		return false;
	}
	FOCRoadGraph Roads;
	FString Error;
	if (!TestTrue(TEXT("Layout parses"), Roads.LoadFromLayoutJson(Text, Error)))
	{
		return false;
	}
	FOCLaneGraph G;
	G.Build(Roads);

	int32 Expected = 0;
	for (const FOCRoadSegment& S : Roads.GetSegments())
	{
		// Footbridges and vicoli carry no traffic; one-way streets only run A to B.
		Expected += S.bPedestrian ? 0 : (S.bOneWay ? 1 : 2) * FMath::Max(1, S.LanesPerDirection);
	}
	TestEqual(TEXT("Both directions of every two-way lane"), G.NumLanes(), Expected);

	int32 Stranded = 0, WrongSide = 0, Reversed = 0;
	for (int32 i = 0; i < G.NumLanes(); ++i)
	{
		const FOCLane& L = G.GetLanes()[i];
		Stranded += G.GetOutLinks(i).IsEmpty() ? 1 : 0;
		const FVector Node = Roads.GetNodes()[L.FromNode].Location;
		const FVector Fwd = FVector(Roads.GetNodes()[L.ToNode].Location - Node).GetSafeNormal2D();
		WrongSide += FVector::DotProduct(L.Start - Node, FVector(-Fwd.Y, Fwd.X, 0.0)) <= 0.0 ? 1 : 0;
		Reversed += FVector::DotProduct(L.End - L.Start, Fwd) <= 0.0 ? 1 : 0;
	}
	TestEqual(TEXT("No stranded lanes"), Stranded, 0);
	TestEqual(TEXT("Right-hand traffic everywhere"), WrongSide, 0);
	TestEqual(TEXT("Setbacks never flip a lane"), Reversed, 0);
	AddInfo(FString::Printf(TEXT("Marevia lanes: %d lanes, %d links"), G.NumLanes(), G.NumLinks()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
