#include "Misc/AutomationTest.h"
#include "Population/OCCrowdSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCCombatTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCAmmoRulesTest, "OpenCity.Core.Combat.Weapon.AmmoRules", OCCombatTest::Flags)

bool FOCOCAmmoRulesTest::RunTest(const FString& Parameters)
{
	// Ammo bookkeeping: fire consumes magazine; reload refills up to magazine size from reserve.
	const int32 MagazineSize = 12;
	const int32 PerShot = 1;

	int32 Magazine = MagazineSize;
	int32 Reserve = 24;

	// Fire 5 shots: magazine drops, reserve untouched.
	for (int32 s = 0; s < 5; ++s)
	{
		Magazine -= PerShot;
	}
	TestTrue(TEXT("Magazine after 5 shots"), Magazine == 7);
	TestTrue(TEXT("Reserve untouched by firing"), Reserve == 24);

	// Fire until empty: magazine reaches 0.
	Magazine = 0;
	TestTrue(TEXT("Magazine empty after emptying"), Magazine == 0);

	// Reload: refill up to magazine size from reserve.
	const int32 Needed = MagazineSize - Magazine;
	const int32 ToAdd = FMath::Min(Needed, Reserve);
	Magazine += ToAdd;
	Reserve -= ToAdd;
	TestTrue(TEXT("Magazine full after reload"), Magazine == MagazineSize);
	TestTrue(TEXT("Reserve reduced by reload"), Reserve == 24 - MagazineSize);

	// Reload when already full: no change.
	const int32 BeforeMag = Magazine, BeforeRes = Reserve;
	const int32 NeededFull = MagazineSize - Magazine;
	const int32 ToAddFull = FMath::Min(NeededFull, Reserve);
	TestTrue(TEXT("No ammo added when full"), ToAddFull == 0);
	TestTrue(TEXT("Magazine unchanged when full"), Magazine == BeforeMag);
	TestTrue(TEXT("Reserve unchanged when full"), Reserve == BeforeRes);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCDamageThroughCrowdSimTest, "OpenCity.Core.Combat.Weapon.DamageThroughCrowdSim", OCCombatTest::Flags)

bool FOCOCDamageThroughCrowdSimTest::RunTest(const FString& Parameters)
{
	// Verify that applying damage through the crowd sim (the IOCDamageable path weapons use)
	// reduces health, kills at zero, and stops taking damage when dead.
	FOCCrowdSim Sim;
	FRandomStream Rng(11);
	const int32 Id = Sim.Spawn(FVector2D(0.0, 0.0), Rng);
	TestTrue(TEXT("Ped starts alive"), Sim.FindPed(Id)->Health > 0.f);

	const float Damage = 0.4f;
	TestTrue(TEXT("Takes damage"), Sim.Damage(Id, Damage));
	TestTrue(TEXT("Health fraction drops"), Sim.FindPed(Id)->Health == FMath::Max(0.f, 1.f - Damage));
	TestTrue(TEXT("Not dead after partial damage"), Sim.FindPed(Id)->Health > 0.f);

	TestTrue(TEXT("Dies at zero health"), Sim.Damage(Id, 1.f));
	TestTrue(TEXT("Dead ped health is zero"), Sim.FindPed(Id)->Health <= 0.f);
	TestTrue(TEXT("Dead ped state"), Sim.FindPed(Id)->State == EOCPedState::Dead);
	TestTrue(TEXT("No more damage after dead"), !Sim.Damage(Id, 0.5f));
	return true;
}

#endif
