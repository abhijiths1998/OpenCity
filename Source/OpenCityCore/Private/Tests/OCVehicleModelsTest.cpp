#include "Misc/AutomationTest.h"
#include "Vehicles/OCVehicleModels.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCVehicleModelsTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FOCVehicleDamageSettings Settings()
	{
		FOCVehicleDamageSettings S;
		S.MaxHealth = 1000.f;
		S.MinImpactKmh = 10.f;
		S.DamagePerKmh = 10.f;
		S.MinPerformance = 0.4f;
		S.ImpactMergeWindow = 0.2f;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCVehicleDamageTest, "OpenCity.Core.Vehicle.Damage", OCVehicleModelsTest::Flags)

bool FOCVehicleDamageTest::RunTest(const FString& Parameters)
{
	using namespace OCVehicleModelsTest;
	FOCVehicleDamageModel M;
	M.Reset(Settings());

	TestEqual(TEXT("Starts at full health"), M.GetHealth(), 1000.f);
	TestEqual(TEXT("Full performance"), M.GetPerformance(), 1.f);

	TestEqual(TEXT("Below threshold does nothing"), M.ApplyImpact(9.f, 1.0), 0.f);
	TestEqual(TEXT("Damage above threshold"), M.ApplyImpact(30.f, 2.0), 200.f);
	TestEqual(TEXT("Health reduced"), M.GetHealth(), 800.f);
	TestEqual(TEXT("Performance scales with health"), M.GetPerformance(), FMath::Lerp(0.4f, 1.f, 0.8f), 1e-4f);

	// Many hit events for one crash: total equals the crash's peak, however they arrive.
	FOCVehicleDamageModel Burst;
	Burst.Reset(Settings());
	float Total = 0.f;
	for (const float Kmh : { 20.f, 35.f, 35.f, 25.f, 50.f, 40.f })
	{
		Total += Burst.ApplyImpact(Kmh, 5.0); // all in the same instant
	}
	TestEqual(TEXT("One crash = damage of its peak"), Total, (50.f - 10.f) * 10.f);

	// A scrape: many small hits in a row, each within the window of the last.
	FOCVehicleDamageModel Scrape;
	Scrape.Reset(Settings());
	float ScrapeTotal = 0.f;
	for (int32 i = 0; i < 50; ++i)
	{
		ScrapeTotal += Scrape.ApplyImpact(15.f, 10.0 + i * 0.1);
	}
	TestEqual(TEXT("Scraping counts once"), ScrapeTotal, 50.f);

	// Separate crashes outside the window each count.
	FOCVehicleDamageModel Two;
	Two.Reset(Settings());
	const float A = Two.ApplyImpact(30.f, 1.0);
	const float B = Two.ApplyImpact(30.f, 2.0);
	TestEqual(TEXT("Two crashes count twice"), A + B, 400.f);

	// Wreck.
	FOCVehicleDamageModel W;
	W.Reset(Settings());
	TestEqual(TEXT("Damage capped at remaining health"), W.ApplyImpact(500.f, 1.0), 1000.f);
	TestTrue(TEXT("Wrecked at zero"), W.IsWrecked());
	TestEqual(TEXT("Wrecked has no performance"), W.GetPerformance(), 0.f);
	TestEqual(TEXT("No damage after wreck"), W.ApplyImpact(100.f, 5.0), 0.f);

	W.Reset(Settings());
	TestFalse(TEXT("Reset repairs"), W.IsWrecked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCRadioTunerTest, "OpenCity.Core.Vehicle.Radio", OCVehicleModelsTest::Flags)

bool FOCRadioTunerTest::RunTest(const FString& Parameters)
{
	FOCRadioTuner T;
	TestTrue(TEXT("Starts off"), T.IsOff());
	TestEqual(TEXT("Next with no stations stays off"), T.Next(), -1);

	T.SetStations({ { TEXT("A"), 600.f }, { TEXT("B"), 900.f }, { TEXT("C"), 300.f } }, 42u);
	TestEqual(TEXT("Next from off"), T.Next(), 0);
	TestEqual(TEXT("Next"), T.Next(), 1);
	TestEqual(TEXT("Next"), T.Next(), 2);
	TestEqual(TEXT("Next wraps to off"), T.Next(), -1);
	TestEqual(TEXT("Previous from off goes to last"), T.Previous(), 2);
	TestEqual(TEXT("Previous"), T.Previous(), 1);

	T.Tune(0);
	const float P1 = T.GetPosition(1000.0);
	const float P2 = T.GetPosition(1010.0);
	TestTrue(TEXT("Position inside the track"), P1 >= 0.f && P1 < 600.f);
	TestEqual(TEXT("Station keeps playing in world time"), FMath::Fmod(P2 - P1 + 600.f, 600.f), 10.f, 1e-3f);

	// Leave the car for 2 minutes, come back: 120 s later on the same station.
	const float Before = T.GetPosition(5000.0);
	const float After = T.GetPosition(5120.0);
	TestEqual(TEXT("Position advanced while away"), FMath::Fmod(After - Before + 600.f, 600.f), 120.f, 1e-3f);

	// Same seed, same offsets: deterministic.
	FOCRadioTuner U;
	U.SetStations({ { TEXT("A"), 600.f }, { TEXT("B"), 900.f }, { TEXT("C"), 300.f } }, 42u);
	U.Tune(0);
	TestEqual(TEXT("Deterministic per seed"), U.GetPosition(1000.0), P1, 1e-3f);

	T.Tune(7);
	TestTrue(TEXT("Invalid tune turns off"), T.IsOff());
	TestEqual(TEXT("Off has position 0"), T.GetPosition(123.0), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCVehicleCameraTest, "OpenCity.Core.Vehicle.CameraFov", OCVehicleModelsTest::Flags)

bool FOCVehicleCameraTest::RunTest(const FString& Parameters)
{
	FOCVehicleCameraSettings C;
	C.FovAtRest = 80.f;
	C.FovAtSpeed = 100.f;
	C.FovFullSpeedKmh = 200.f;
	TestEqual(TEXT("At rest"), C.FovForSpeed(0.f), 80.f);
	TestEqual(TEXT("Half speed"), C.FovForSpeed(100.f), 90.f);
	TestEqual(TEXT("Clamped above full speed"), C.FovForSpeed(400.f), 100.f);
	TestEqual(TEXT("Reverse counts as speed"), C.FovForSpeed(-100.f), 90.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
