#include "Misc/AutomationTest.h"
#include "Combat/OCAmmo.h"
#include "Population/OCCrowdSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCCombatTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCAmmoRulesTest, "OpenCity.Core.Combat.Weapon.AmmoRules", OCCombatTest::Flags)

bool FOCOCAmmoRulesTest::RunTest(const FString& Parameters)
{
	FOCAmmo Ammo;
	Ammo.Equip(12, 1, 24);
	TestEqual(TEXT("Equip fills the magazine"), Ammo.InMagazine, 12);
	TestEqual(TEXT("Equip sets the reserve"), Ammo.Reserve, 24);
	TestFalse(TEXT("Full magazine cannot reload"), Ammo.CanReload());

	for (int32 s = 0; s < 5; ++s)
	{
		TestTrue(TEXT("Shot consumed"), Ammo.Consume());
	}
	TestEqual(TEXT("Magazine after 5 shots"), Ammo.InMagazine, 7);
	TestEqual(TEXT("Reserve untouched by firing"), Ammo.Reserve, 24);

	while (Ammo.Consume()) {}
	TestEqual(TEXT("Magazine empty"), Ammo.InMagazine, 0);
	TestFalse(TEXT("Empty magazine cannot fire"), Ammo.CanFire());
	TestFalse(TEXT("Consume on empty spends nothing"), Ammo.Consume());

	TestEqual(TEXT("Reload moves a full magazine"), Ammo.Reload(), 12);
	TestEqual(TEXT("Reserve reduced by reload"), Ammo.Reserve, 12);
	TestEqual(TEXT("Reload when full moves nothing"), Ammo.Reload(), 0);

	// Partial reserve: the last reload only tops up what is left.
	Ammo.Equip(12, 1, 5);
	for (int32 s = 0; s < 8; ++s) { Ammo.Consume(); }
	TestEqual(TEXT("Partial reload moves the whole reserve"), Ammo.Reload(), 5);
	TestEqual(TEXT("Magazine after partial reload"), Ammo.InMagazine, 9);
	TestFalse(TEXT("No reserve, no reload"), Ammo.CanReload());

	// Multi-round shots (e.g. a shotgun) need the full cost in the magazine.
	Ammo.Equip(3, 2, 0);
	TestTrue(TEXT("First 2-round shot"), Ammo.Consume());
	TestFalse(TEXT("1 round left cannot pay a 2-round shot"), Ammo.Consume());
	TestEqual(TEXT("Failed shot spends nothing"), Ammo.InMagazine, 1);

	Ammo.AddReserve(500, 120);
	TestEqual(TEXT("Reserve capped by MaxReserve"), Ammo.Reserve, 120);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCDamageThroughCrowdSimTest, "OpenCity.Core.Combat.Weapon.DamageThroughCrowdSim", OCCombatTest::Flags)

bool FOCOCDamageThroughCrowdSimTest::RunTest(const FString& Parameters)
{
	// Applying damage through the crowd sim (what AOCPedestrian::ApplyDamage forwards to)
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
