#include "Misc/AutomationTest.h"
#include "Missions/OCObjective.h"
#include "Missions/OCMissionData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCMissionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCObjectiveSurviveTest, "OpenCity.Core.Missions.Objective.Survive", OCMissionTest::Flags)

bool FOCOCObjectiveSurviveTest::RunTest(const FString& Parameters)
{
	UOCObjectiveSurvive* Obj = NewObject<UOCObjectiveSurvive>();
	Obj->DurationSeconds = 1.0f;

	// Inactive: evaluate does nothing.
	TestEqual(TEXT("Inactive returns None"), Obj->Evaluate(), EOCObjectiveResult::None);

	Obj->Activate();
	TestTrue(TEXT("Active"), Obj->IsActive());
	TestEqual(TEXT("Active before duration returns None"), Obj->Evaluate(), EOCObjectiveResult::None);

	// Advance time past duration.
	Obj->ElapsedSeconds = 2.0f;
	TestEqual(TEXT("After duration returns Succeed"), Obj->Evaluate(), EOCObjectiveResult::Succeed);
	Obj->Succeed();
	TestTrue(TEXT("Succeeded state"), Obj->IsComplete());

	// Reset returns to inactive.
	Obj->Reset();
	TestTrue(TEXT("Reset to inactive"), Obj->State == EOCObjectiveState::Inactive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCObjectiveEliminateTest, "OpenCity.Core.Missions.Objective.Eliminate", OCMissionTest::Flags)

bool FOCOCObjectiveEliminateTest::RunTest(const FString& Parameters)
{
	UOCObjectiveEliminate* Obj = NewObject<UOCObjectiveEliminate>();
	Obj->Activate();

	// Null target: nothing to eliminate → Succeed.
	TestEqual(TEXT("Null target returns Succeed"), Obj->Evaluate(), EOCObjectiveResult::Succeed);

	// Valid living target: None.
	AActor* Dummy = NewObject<AActor>();
	Obj->Target = Dummy;
	TestEqual(TEXT("Living target returns None"), Obj->Evaluate(), EOCObjectiveResult::None);

	// Cleared target: Succeed.
	Obj->Target = nullptr;
	TestEqual(TEXT("Cleared target returns Succeed"), Obj->Evaluate(), EOCObjectiveResult::Succeed);
	Obj->Succeed();
	TestTrue(TEXT("Eliminate succeeded state"), Obj->IsComplete());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCOCMissionDataTest, "OpenCity.Core.Missions.Data.Validation", OCMissionTest::Flags)

bool FOCOCMissionDataTest::RunTest(const FString& Parameters)
{
	// Mission data asset validation (pure data checks).
	UOCMissionData* Mission = NewObject<UOCMissionData>();
	Mission->CompletesMilestone = FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Mission.Funerale")));
	Mission->UnlocksAreas.Add(FGameplayTag::RequestGameplayTag(FName(TEXT("Area.PortoVecchio"))));
	Mission->CheckpointObjectiveIndex = 1;

	FOCMissionObjective ObjEntry;
	ObjEntry.bRequired = true;
	Mission->Objectives.Add(ObjEntry);

	TestTrue(TEXT("Completes milestone"), Mission->CompletesMilestone.IsValid());
	TestTrue(TEXT("Unlocks area"), Mission->UnlocksAreas.Num() == 1);
	TestEqual(TEXT("Checkpoint index"), Mission->CheckpointObjectiveIndex, 1);
	TestEqual(TEXT("One objective"), Mission->Objectives.Num(), 1);
	TestTrue(TEXT("Objective required"), Mission->Objectives[0].bRequired);
	return true;
}

#endif
