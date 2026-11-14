#include "Misc/AutomationTest.h"
#include "Population/OCCrowdSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCPopulationTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCCrowdSimFleeTest, "OpenCity.Core.Population.Crowd.Flee", OCPopulationTest::Flags)

bool FOCCrowdSimFleeTest::RunTest(const FString& Parameters)
{
	FOCCrowdSim Sim;
	FRandomStream Rng(2024);
	// One ped at (0,0), a threat right on top of it at (100,0): it must flee away.
	const int32 Id = Sim.Spawn(FVector2D(0.0, 0.0), Rng);
	FOCThreat T;
	T.Pos = FVector2D(100.0, 0.0);
	T.RadiusCm = 1500.0;
	T.Weight = 1.f;
	Sim.AddThreat(T);
	const double Now = 10.0;
	// Several steps so the ped fully turns away from the threat (turn rate is capped), re-adding
	// the threat each step as the caller does each AI step.
	for (int32 s = 0; s < 40; ++s)
	{
		Sim.AddThreat(T);
		Sim.Step(0.25, Now, Rng);
	}
	const FOCPedestrian* P = Sim.FindPed(Id);
	if (!TestNotNull(TEXT("Ped exists"), P)) return false;
	TestTrue(TEXT("Ped is fleeing"), P->State == EOCPedState::Flee);
	// Fleeing away from a threat to the east means heading west (negative X).
	TestTrue(TEXT("Fleeing away from the threat"), P->Dir.X < -0.7);
	TestTrue(TEXT("At flee speed"), P->Speed > 400.0);
	TestTrue(TEXT("Fleeing until later"), P->FleeUntil > Now);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCCrowdSimDamageTest, "OpenCity.Core.Population.Crowd.Damage", OCPopulationTest::Flags)

bool FOCCrowdSimDamageTest::RunTest(const FString& Parameters)
{
	FOCCrowdSim Sim;
	FRandomStream Rng(7);
	const int32 Id = Sim.Spawn(FVector2D(0.0, 0.0), Rng);
	TestTrue(TEXT("Takes damage"), Sim.Damage(Id, 0.6f));
	TestTrue(TEXT("Dies at zero health"), Sim.Damage(Id, 0.6f));
	const FOCPedestrian* P = Sim.FindPed(Id);
	TestTrue(TEXT("Dead ped stops"), P && P->State == EOCPedState::Dead && P->Speed == 0.0);
	TestTrue(TEXT("No more damage after dead"), !Sim.Damage(Id, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCCrowdSimIdleWanderTest, "OpenCity.Core.Population.Crowd.IdleWander", OCPopulationTest::Flags)

bool FOCCrowdSimIdleWanderTest::RunTest(const FString& Parameters)
{
	FOCCrowdSim Sim;
	FRandomStream Rng(3);
	const int32 Id = Sim.Spawn(FVector2D(0.0, 0.0), Rng);
	for (int32 s = 0; s < 160; ++s) Sim.Step(0.25, s * 0.25, Rng); // 40 s of idle/wander
	const FOCPedestrian* P = Sim.FindPed(Id);
	if (!TestNotNull(TEXT("Ped exists"), P)) return false;
	const FVector2D Start = FVector2D(0.0, 0.0);
	// No threats: wandering should move it clear of spawn over 40 s.
	TestTrue(TEXT("Ped wandered away from spawn"), FVector2D::DistSquared(P->Pos, Start) > FMath::Square(100.0));
	return true;
}

#endif
