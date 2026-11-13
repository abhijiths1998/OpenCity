#include "Misc/AutomationTest.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraph.h"
#include "World/OCTrafficSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCTrafficSimTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A crossroads at node 0 with four 100 m streets ending in dead ends (U-turns there); the south arm is gated. */
	const TCHAR* Crossroads = TEXT(R"({
		"roads": {
			"nodes": [[0,0,0],[100,0,0],[0,100,0],[-100,0,0],[0,-100,0]],
			"segments": [
				{"a":0,"b":1,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":0,"b":2,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":0,"b":3,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":0,"b":4,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50,"gate":"Gate.Test"}
			]
		}
	})");

	/** One straight kilometre (two segments), for free-flow speed. */
	const TCHAR* Straight = TEXT(R"({
		"roads": {
			"nodes": [[0,0,0],[500,0,0],[1000,0,0]],
			"segments": [
				{"a":0,"b":1,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50},
				{"a":1,"b":2,"kind":"street","lanes_per_dir":1,"width":9,"speed_kmh":50}
			]
		}
	})");

	bool Load(FAutomationTestBase& T, const TCHAR* Json, FOCRoadGraph& Roads, FOCLaneGraph& Lanes)
	{
		FString Error;
		if (!T.TestTrue(TEXT("Road graph parses"), Roads.LoadFromLayoutJson(Json, Error)))
		{
			return false;
		}
		Lanes.Build(Roads);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCTrafficSimRulesTest, "OpenCity.Core.World.Traffic.Rules", OCTrafficSimTest::Flags)

bool FOCTrafficSimRulesTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph Roads;
	FOCLaneGraph Lanes;
	if (!OCTrafficSimTest::Load(*this, OCTrafficSimTest::Crossroads, Roads, Lanes))
	{
		return false;
	}
	FOCTrafficSim Sim;
	Sim.Build(Lanes);
	FRandomStream Rng(1974);
	// Three cars per lane, spread out: 24 cars on eight 100 m lanes, all heading somewhere.
	for (int32 l = 0; l < Lanes.NumLanes(); ++l)
	{
		for (int32 k = 0; k < 3; ++k)
		{
			Sim.Spawn(l, 1000.0 + k * 2500.0, 500.0, Rng);
		}
	}
	TestEqual(TEXT("Cars spawned"), Sim.GetCars().Num(), 3 * Lanes.NumLanes());

	const FOCTrafficSim::FParams P;
	int32 Conflicts = 0, Overlaps = 0, Speeding = 0;
	double MinGap = TNumericLimits<double>::Max();
	constexpr double Dt = 1.0 / 30.0;
	for (int32 Step = 0; Step < 30 * 180; ++Step)
	{
		Sim.Step(Dt, Rng);
		Conflicts += Sim.CountConflictingCrossings();
		// Bumper gaps between cars on the same lane.
		TMap<int32, TArray<double>> ByLane;
		for (const FOCTrafficCar& C : Sim.GetCars())
		{
			if (C.Link == INDEX_NONE)
			{
				ByLane.FindOrAdd(C.Lane).Add(C.S);
			}
			const double Limit = (C.Link == INDEX_NONE ? Lanes.GetLanes()[C.Lane].SpeedKmh : 50.f) * 100000.0 / 3600.0 * 1.1;
			Speeding += C.Speed > Limit * 1.01 ? 1 : 0;
		}
		for (TPair<int32, TArray<double>>& E : ByLane)
		{
			E.Value.Sort();
			for (int32 i = 1; i < E.Value.Num(); ++i)
			{
				const double Gap = E.Value[i] - E.Value[i - 1] - P.CarLengthCm;
				MinGap = FMath::Min(MinGap, Gap);
				Overlaps += Gap < 0.0 ? 1 : 0;
			}
		}
	}
	TestEqual(TEXT("Never two cars on conflicting links at once"), Conflicts, 0);
	TestEqual(TEXT("No car runs into the one ahead"), Overlaps, 0);
	TestEqual(TEXT("No car over its limit (temper up to 1.1)"), Speeding, 0);
	AddInfo(FString::Printf(TEXT("smallest bumper gap %.0f cm"), MinGap));

	// No gridlock: every car keeps going.
	double LeastDriven = TNumericLimits<double>::Max();
	for (const FOCTrafficCar& C : Sim.GetCars())
	{
		LeastDriven = FMath::Min(LeastDriven, C.Driven);
	}
	AddInfo(FString::Printf(TEXT("least driven %.0f m in 180 s"), LeastDriven / 100.0));
	TestTrue(TEXT("Every car drove at least 300 m in three minutes"), LeastDriven > 30000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCTrafficSimGatesTest, "OpenCity.Core.World.Traffic.Gates", OCTrafficSimTest::Flags)

bool FOCTrafficSimGatesTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph Roads;
	FOCLaneGraph Lanes;
	if (!OCTrafficSimTest::Load(*this, OCTrafficSimTest::Crossroads, Roads, Lanes))
	{
		return false;
	}
	FOCTrafficSim Sim;
	Sim.Build(Lanes);
	Sim.SetClosedGates({ FName(TEXT("Gate.Test")) });
	FRandomStream Rng(7);
	for (int32 l = 0; l < Lanes.NumLanes(); ++l)
	{
		if (Lanes.GetLanes()[l].Gate.IsNone())
		{
			Sim.Spawn(l, 2000.0, 800.0, Rng);
		}
	}
	int32 Entered = 0;
	for (int32 Step = 0; Step < 30 * 120; ++Step)
	{
		Sim.Step(1.0 / 30.0, Rng);
		for (const FOCTrafficCar& C : Sim.GetCars())
		{
			const int32 Lane = C.Link != INDEX_NONE ? Lanes.GetLinks()[C.Link].ToLane : C.Lane;
			Entered += Lanes.GetLanes()[Lane].Gate == FName(TEXT("Gate.Test")) ? 1 : 0;
		}
	}
	TestEqual(TEXT("No car enters the closed gate's street"), Entered, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCTrafficSimSpeedTest, "OpenCity.Core.World.Traffic.Speed", OCTrafficSimTest::Flags)

bool FOCTrafficSimSpeedTest::RunTest(const FString& Parameters)
{
	FOCRoadGraph Roads;
	FOCLaneGraph Lanes;
	if (!OCTrafficSimTest::Load(*this, OCTrafficSimTest::Straight, Roads, Lanes))
	{
		return false;
	}
	FOCTrafficSim Sim;
	Sim.Build(Lanes);
	FRandomStream Rng(3);
	const int32 Lane = Lanes.GetSegmentLanes(0, 0)[0];
	const int32 Id = Sim.Spawn(Lane, 0.0, 0.0, Rng);
	for (int32 Step = 0; Step < 30 * 20; ++Step)
	{
		Sim.Step(1.0 / 30.0, Rng);
	}
	const FOCTrafficCar* Car = Sim.FindCar(Id);
	if (!TestNotNull(TEXT("Car still there"), Car))
	{
		return false;
	}
	const double Want = 50.0 * 100000.0 / 3600.0 * Car->Temper;
	AddInfo(FString::Printf(TEXT("after 20 s: %.1f km/h (wants %.1f), %.0f m driven"), Car->Speed * 0.036, Want * 0.036, Car->Driven / 100.0));
	TestTrue(TEXT("Reaches within 10% of its desired speed in 20 s"), Car->Speed > 0.9 * Want && Car->Speed <= Want * 1.001);
	// The straight-on link through the middle node keeps the car on a line (no wobble in the pose).
	const FOCTrafficPose Pose = Sim.GetPose(*Car);
	TestTrue(TEXT("Pose stays on the road's line"), FMath::Abs(Pose.Location.Y) < 500.0);
	return true;
}

#endif
