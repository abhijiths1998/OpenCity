#include "Misc/AutomationTest.h"
#include "World/OCSunModel.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCSunModelTest, "OpenCity.Core.World.SunModel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOCSunModelTest::RunTest(const FString& Parameters)
{
	FOCSunModel Sun;
	TestEqual(TEXT("On the horizon at sunrise"), Sun.ElevationDeg(6.5f), 0.f, 1e-3f);
	TestEqual(TEXT("On the horizon at sunset"), Sun.ElevationDeg(20.f), 0.f, 1e-3f);
	TestEqual(TEXT("Highest midway"), Sun.ElevationDeg(13.25f), 68.f, 1e-3f);
	TestTrue(TEXT("Below the horizon at midnight"), Sun.ElevationDeg(0.f) < -20.f);
	TestTrue(TEXT("Below the horizon at 4:00"), Sun.ElevationDeg(4.f) < 0.f);
	TestEqual(TEXT("Rises in the east"), Sun.AzimuthDeg(6.5f), 90.f, 1e-3f);
	TestEqual(TEXT("South at midday"), Sun.AzimuthDeg(13.25f), 180.f, 1e-3f);
	TestEqual(TEXT("Sets in the west"), Sun.AzimuthDeg(20.f), 270.f, 1e-3f);
	TestEqual(TEXT("Wraps hours"), FOCSunModel::Wrap(25.5f), 1.5f, 1e-4f);
	TestEqual(TEXT("Wraps negative hours"), FOCSunModel::Wrap(-1.f), 23.f, 1e-4f);
	TestEqual(TEXT("Day"), FOCSunModel::NightAmount(20.f), 0.f, 1e-4f);
	TestEqual(TEXT("Night"), FOCSunModel::NightAmount(-20.f), 1.f, 1e-4f);
	TestEqual(TEXT("Twilight halfway"), FOCSunModel::NightAmount(0.f), 0.5f, 1e-4f);
	return true;
}

#endif
