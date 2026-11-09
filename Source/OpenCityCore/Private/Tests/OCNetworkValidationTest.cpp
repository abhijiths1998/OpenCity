#include "Misc/AutomationTest.h"
#include "World/OCInfraNetworks.h"
#include "World/OCLaneGraph.h"
#include "World/OCNetworkValidation.h"
#include "World/OCRoadGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCNetworkTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/**
	 * 0 -- 1 -- 2 straight "quartiere" street (1 -- 2 too wide), 2 -> 3 one-way, a 90-degree bend at 3
	 * with a 52 m radius (passes), 3 -- 4 at 20%, a 90-degree bend at 4 with a 21 m radius (fails),
	 * 5 -- 6 inside the ZTL.
	 */
	const TCHAR* Layout = TEXT(R"({
		"roads": {
			"profiles": {
				"quartiere": {"dm_type": "E", "lane_cm": 300, "lanes_per_dir": 1, "shoulder_right_cm": 50, "parking_cm": 200,
				              "max_grade_pct": 8, "min_radius_m": 51, "design_speed_max_kmh": 50, "paving": "asphalt"},
				"centro": {"lane_cm": 250, "lanes_per_dir": 1, "shoulder_right_cm": 25, "max_grade_pct": 15, "min_radius_m": 0,
				           "design_speed_max_kmh": 30, "paving": "sampietrini"}
			},
			"nodes": [[0,0,0],[100,0,0],[200,0,0],[300,0,0],[300,30,6],[330,30,6],[400,30,6]],
			"segments": [
				{"a":0,"b":1,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"profile":"quartiere","paving":"asphalt","era":"postwar","wear":0.7,"parking":"white"},
				{"a":1,"b":2,"kind":"street","lanes_per_dir":1,"width":12,"speed_kmh":50,"profile":"quartiere"},
				{"a":2,"b":3,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"profile":"quartiere","one_way":true},
				{"a":3,"b":4,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"profile":"quartiere"},
				{"a":4,"b":5,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"profile":"quartiere"},
				{"a":5,"b":6,"kind":"alley","lanes_per_dir":1,"width":5.5,"speed_kmh":30,"profile":"centro","ztl":true,"tram":true,"paving":"sampietrini","era":"medieval"}
			],
			"junctions": [{"node": 4, "type": "roundabout", "planned": true}, {"node": 1, "type": "zebra"}]
		},
		"rail": {"lines": [
			{"id": "Costiera", "kind": "regional", "electrification": "3kV_DC", "points": [[0,0,0],[1000,0,10],[1100,0,30]],
			 "stations": [{"name": "Centrale", "point": 1, "platform_m": 200}, {"name": "Ghost", "point": 9}]},
			{"id": "Tram1", "kind": "tram", "electrification": "3kV_DC", "points": [[0,0,0],[500,0,0]]}
		]},
		"water_network": {"bodies": [
			{"id": "FiumeArdo", "type": "river", "depth_m": 2.5, "flow_mps": 0.8, "centerline": [[0,0],[0,100]],
			 "edges": [{"treatment": "embankment_wall", "side": "both", "from_m": 0, "to_m": 100}]},
			{"id": "Darsena", "type": "port", "depth_m": 6, "flow_mps": 0.5, "edges": [{"treatment": "lava_moat"}]},
			{"id": "Torrente", "type": "torrent", "seasonal": true, "centerline": [[5,5],[9,9]], "edges": [{"treatment": "gravel_bed"}]}
		]}
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadMetadataTest, "OpenCity.Core.World.Networks.RoadMetadata", OCNetworkTest::Flags)

bool FOCRoadMetadataTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph G;
	FString Error;
	TestTrue(TEXT("Parses"), G.LoadFromLayoutJson(OCNetworkTest::Layout, Error));
	const FOCRoadProfile* Q = G.FindProfile(TEXT("quartiere"));
	TestNotNull(TEXT("Profile parsed"), Q);
	if (Q)
	{
		TestEqual(TEXT("DM type"), Q->DMType, FName(TEXT("E")));
		TestEqual(TEXT("Carriageway: 2x300 + 2x50 + 200 parking"), Q->CarriagewayCm(), 900.f);
	}
	const FOCRoadSegment& S0 = G.GetSegments()[0];
	TestEqual(TEXT("Paving"), S0.Paving, FName(TEXT("asphalt")));
	TestEqual(TEXT("Era"), S0.Era, FName(TEXT("postwar")));
	TestEqual(TEXT("Wear"), S0.Wear, 0.7f);
	TestEqual(TEXT("Parking"), S0.Parking, FName(TEXT("white")));
	TestTrue(TEXT("One-way flag"), G.GetSegments()[2].bOneWay);
	TestTrue(TEXT("ZTL flag"), G.GetSegments()[5].bZTL);
	TestTrue(TEXT("Tram flag"), G.GetSegments()[5].bTram);
	TestEqual(TEXT("Junctions"), G.GetJunctions().Num(), 2);
	TestTrue(TEXT("Planned roundabout"), G.GetJunctions()[0].bPlanned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadOneWayTest, "OpenCity.Core.World.Networks.OneWay", OCNetworkTest::Flags)

bool FOCRoadOneWayTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph G;
	FString Error;
	G.LoadFromLayoutJson(OCNetworkTest::Layout, Error);
	TestEqual(TEXT("Forward along the one-way street"), G.FindPath(0, 6).Num(), 7);
	TestEqual(TEXT("No way back against it"), G.FindPath(6, 0).Num(), 0);
	TestEqual(TEXT("Reachable from the far end: only beyond the one-way"), G.CountReachable(6), 4);

	FOCLaneGraph L;
	L.Build(G);
	int32 OneWayLanes = 0;
	for (const FOCLane& Lane : L.GetLanes())
	{
		if (Lane.Segment == 2)
		{
			++OneWayLanes;
			TestEqual(TEXT("One-way lane runs A to B"), Lane.FromNode, 2);
			TestEqual(TEXT("One-way lane uses the whole width"), Lane.WidthCm, 900.f);
		}
	}
	TestEqual(TEXT("One lane on the one-way segment"), OneWayLanes, 1);

	// A footbridge across the square: walkers only.
	FOCRoadSegment Foot;
	Foot.A = 0;
	Foot.B = 3;
	Foot.bPedestrian = true;
	G.AddSegment(Foot);
	TestEqual(TEXT("Cars still drive round, not over the footbridge"), G.FindPath(0, 3).Num(), 4);
	TestEqual(TEXT("Still no way back past the one-way"), G.FindPath(3, 0).Num(), 0);
	FOCLaneGraph L2;
	L2.Build(G);
	TestEqual(TEXT("No lanes on the footbridge"), L2.GetSegmentLanes(G.NumSegments() - 1, 0).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRoadValidatorTest, "OpenCity.Core.World.Networks.ValidateRoads", OCNetworkTest::Flags)

bool FOCRoadValidatorTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph G;
	FString Error;
	G.LoadFromLayoutJson(OCNetworkTest::Layout, Error);
	FOCNetworkReport R;
	FOCNetworkValidator::ValidateRoads(G, FOCNetworkValidator::FOptions(), R);
	TestEqual(TEXT("Width: segment 1 is 12 m against a 9 m profile"), R.Num(TEXT("width")), 1);
	TestEqual(TEXT("Gradient: 6 m over 30 m on an 8% street"), R.Num(TEXT("gradient")), 1);
	TestEqual(TEXT("Radius: the sharp bend at node 4"), R.Num(TEXT("radius")), 1);
	TestEqual(TEXT("ZTL: boundary at node 5 has no gate"), R.Num(TEXT("ztl")), 1);
	TestEqual(TEXT("One-way: nodes past the one-way can't get back"), R.Num(TEXT("oneway")), 4);
	TestEqual(TEXT("Junction: unknown type 'zebra'"), R.Num(TEXT("junction")), 2);  // + roundabout with 2 roads
	TestEqual(TEXT("Connectivity: everything reachable from 0"), R.Num(TEXT("connectivity")), 0);
	TestTrue(TEXT("Radius of three collinear points is huge"), FOCNetworkValidator::PlanRadiusCm(FVector(0), FVector(100, 0, 0), FVector(200, 0, 0)) > 1e9);
	TestEqual(TEXT("Radius of a quarter circle"), FOCNetworkValidator::PlanRadiusCm(FVector(100, 0, 0), FVector(0, 100, 0), FVector(-100, 0, 0)), 100.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRailWaterValidatorTest, "OpenCity.Core.World.Networks.ValidateRailWater", OCNetworkTest::Flags)

bool FOCRailWaterValidatorTest::RunTest(const FString& Parameters)
{
	FString Error;
	FOCRailNetwork Rail;
	TestTrue(TEXT("Rail parses"), Rail.LoadFromLayoutJson(OCNetworkTest::Layout, Error));
	TestEqual(TEXT("Two lines"), Rail.GetLines().Num(), 2);
	TestEqual(TEXT("Stations"), Rail.GetLines()[0].Stations.Num(), 2);
	TestEqual(TEXT("Platform cm"), Rail.GetLines()[0].Stations[0].PlatformCm, 20000.f);
	FOCNetworkReport R;
	FOCNetworkValidator::ValidateRail(Rail, R);
	TestEqual(TEXT("Rail grade: 20 m over 100 m"), R.Num(TEXT("rail_gradient")), 1);
	TestEqual(TEXT("Tram on 3 kV"), R.Num(TEXT("rail_power")), 1);
	TestEqual(TEXT("Station on a missing point"), R.Num(TEXT("rail_station")), 1);

	FOCWaterNetwork Water;
	TestTrue(TEXT("Water parses"), Water.LoadFromLayoutJson(OCNetworkTest::Layout, Error));
	TestEqual(TEXT("Three bodies"), Water.GetBodies().Num(), 3);
	TestNotNull(TEXT("Find by id"), Water.FindBody(TEXT("Darsena")));
	FOCNetworkReport W;
	FOCNetworkValidator::ValidateWater(Water, W);
	TestEqual(TEXT("Port with flow"), W.Num(TEXT("water_body")), 1);
	TestEqual(TEXT("Unknown edge treatment"), W.Num(TEXT("water_edge")), 1);

	FOCRailNetwork Empty;
	TestTrue(TEXT("No rail section is an empty network"), Empty.LoadFromLayoutJson(TEXT("{\"roads\":{}}"), Error));
	TestEqual(TEXT("Empty"), Empty.GetLines().Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
