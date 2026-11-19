#include "Misc/AutomationTest.h"
#include "Missions/OCMissionData.h"
#include "Missions/OCMissionSubsystem.h"
#include "Missions/OCObjective.h"
#include "Events/OCEventSubsystem.h"
#include "Progression/OCAreaUnlockData.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCMissionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FOCObjectiveContext At(const FVector& Where, int32 Heat = 0)
	{
		FOCObjectiveContext Ctx;
		Ctx.bHasPlayer = true;
		Ctx.PlayerLocation = Where;
		Ctx.HeatLevel = Heat;
		return Ctx;
	}

	UOCObjectiveReach* Reach(UObject* Outer, const FVector& Where, float Radius = 1000.f)
	{
		UOCObjectiveReach* R = NewObject<UOCObjectiveReach>(Outer);
		R->Location = Where;
		R->RadiusCm = Radius;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCObjectiveReachTest, "OpenCity.Core.Missions.Objective.Reach", OCMissionTest::Flags)

bool FOCObjectiveReachTest::RunTest(const FString& Parameters)
{
	using namespace OCMissionTest;
	UOCObjectiveReach* Obj = Reach(GetTransientPackage(), FVector(10000, 0, 0));
	TestEqual(TEXT("Inactive does nothing"), Obj->Advance(0.1f, At(FVector(10000, 0, 0))), EOCObjectiveResult::None);

	Obj->Activate();
	TestEqual(TEXT("Far away: still running"), Obj->Advance(0.1f, At(FVector::ZeroVector)), EOCObjectiveResult::None);
	TestEqual(TEXT("On a bridge 30 m above: not there"), Obj->Advance(0.1f, At(FVector(10000, 0, 3000))), EOCObjectiveResult::None);
	TestEqual(TEXT("No player: nothing"), Obj->Advance(0.1f, FOCObjectiveContext()), EOCObjectiveResult::None);
	TestEqual(TEXT("Inside the radius: done"), Obj->Advance(0.1f, At(FVector(10500, 300, 100))), EOCObjectiveResult::Succeed);
	TestTrue(TEXT("Succeeded state"), Obj->IsComplete());
	TestEqual(TEXT("Done objectives stay done"), Obj->Advance(0.1f, At(FVector::ZeroVector)), EOCObjectiveResult::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCObjectiveLimitsTest, "OpenCity.Core.Missions.Objective.Limits", OCMissionTest::Flags)

bool FOCObjectiveLimitsTest::RunTest(const FString& Parameters)
{
	using namespace OCMissionTest;
	UOCObjectiveReach* Timed = Reach(GetTransientPackage(), FVector(100000, 0, 0));
	Timed->TimeLimitSeconds = 10.f;
	Timed->Activate();
	TestEqual(TEXT("Within the limit"), Timed->Advance(9.f, At(FVector::ZeroVector)), EOCObjectiveResult::None);
	TestEqual(TEXT("Past the limit fails"), Timed->Advance(2.f, At(FVector::ZeroVector)), EOCObjectiveResult::Fail);
	TestTrue(TEXT("Failed state"), Timed->IsFailed());
	TestFalse(TEXT("Has a reason"), Timed->GetFailReason().IsEmpty());

	Timed->Reset();
	TestTrue(TEXT("Reset to inactive"), Timed->State == EOCObjectiveState::Inactive);
	TestEqual(TEXT("Reset clears the clock"), Timed->ActiveSeconds, 0.f);

	UOCObjectiveReach* Quiet = Reach(GetTransientPackage(), FVector(100000, 0, 0));
	Quiet->FailAtHeatLevel = 1;
	Quiet->Activate();
	TestEqual(TEXT("No heat: running"), Quiet->Advance(1.f, At(FVector::ZeroVector, 0)), EOCObjectiveResult::None);
	TestEqual(TEXT("Wanted: failed"), Quiet->Advance(1.f, At(FVector::ZeroVector, 1)), EOCObjectiveResult::Fail);

	// Arriving wins over the limit in the same step.
	UOCObjectiveReach* Late = Reach(GetTransientPackage(), FVector::ZeroVector);
	Late->TimeLimitSeconds = 1.f;
	Late->Activate();
	TestEqual(TEXT("Arrival on the last frame counts"), Late->Advance(2.f, At(FVector::ZeroVector)), EOCObjectiveResult::Succeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCObjectiveSurviveTest, "OpenCity.Core.Missions.Objective.Survive", OCMissionTest::Flags)

bool FOCObjectiveSurviveTest::RunTest(const FString& Parameters)
{
	using namespace OCMissionTest;
	UOCObjectiveSurvive* Obj = NewObject<UOCObjectiveSurvive>(GetTransientPackage());
	Obj->DurationSeconds = 5.f;
	Obj->Activate();
	for (int32 i = 0; i < 4; ++i)
	{
		TestEqual(TEXT("Holding out"), Obj->Advance(1.f, At(FVector::ZeroVector)), EOCObjectiveResult::None);
	}
	TestEqual(TEXT("Time is up: done"), Obj->Advance(1.f, At(FVector::ZeroVector)), EOCObjectiveResult::Succeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCObjectiveEliminateTest, "OpenCity.Core.Missions.Objective.Eliminate", OCMissionTest::Flags)

bool FOCObjectiveEliminateTest::RunTest(const FString& Parameters)
{
	using namespace OCMissionTest;
	UOCObjectiveEliminate* Obj = NewObject<UOCObjectiveEliminate>(GetTransientPackage());
	Obj->KillCount = 2;
	Obj->Activate();
	TestEqual(TEXT("No kills: running"), Obj->Advance(1.f, At(FVector::ZeroVector)), EOCObjectiveResult::None);

	// A death nobody (or an NPC) caused is not the player's kill.
	FOCEvent Ev;
	Ev.Tag = OCEventTags::Murder();
	Obj->OnEvent(Ev);
	TestEqual(TEXT("Kills by others don't count"), Obj->Kills, 0);

	Obj->Kills = 2;
	TestEqual(TEXT("Enough kills: done"), Obj->Advance(1.f, At(FVector::ZeroVector)), EOCObjectiveResult::Succeed);
	Obj->Reset();
	TestEqual(TEXT("Reset clears kills"), Obj->Kills, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCMissionFlowTest, "OpenCity.Core.Missions.Flow", OCMissionTest::Flags)

bool FOCMissionFlowTest::RunTest(const FString& Parameters)
{
	using namespace OCMissionTest;
	const FVector A(10000, 0, 0), B(20000, 0, 0), C(30000, 0, 0);

	UOCMissionData* Mission = NewObject<UOCMissionData>(GetTransientPackage());
	FOCMissionObjective E0; E0.Objective = Reach(Mission, A);
	FOCMissionObjective E1; E1.Objective = Reach(Mission, B); E1.Objective->TimeLimitSeconds = 30.f;
	FOCMissionObjective E2; E2.Objective = Reach(Mission, C); E2.bRequired = false; E2.Objective->TimeLimitSeconds = 5.f;
	Mission->Objectives = { E0, E1, E2 };
	Mission->CheckpointObjectiveIndex = 1;

	UOCMissionSubsystem* M = NewObject<UOCMissionSubsystem>(GetTransientPackage());
	TestFalse(TEXT("Empty mission refused"), M->StartMission(NewObject<UOCMissionData>(GetTransientPackage())));
	TestTrue(TEXT("Starts"), M->StartMission(Mission));
	TestFalse(TEXT("A second start is refused while running"), M->StartMission(Mission));
	TestEqual(TEXT("First objective active"), M->GetCurrentObjectiveIndex(), 0);

	M->Step(1.f, At(FVector::ZeroVector));
	TestEqual(TEXT("Still on objective 1"), M->GetCurrentObjectiveIndex(), 0);
	M->Step(1.f, At(A));
	TestEqual(TEXT("Reached A: objective 2"), M->GetCurrentObjectiveIndex(), 1);

	M->Step(31.f, At(A));
	TestEqual(TEXT("Required objective timed out: mission failed"), M->GetOutcome(), EOCMissionOutcome::Failed);
	TestTrue(TEXT("Retry"), M->RetryFromCheckpoint());
	TestEqual(TEXT("Retry resumes at the checkpoint, not the start"), M->GetCurrentObjectiveIndex(), 1);
	TestEqual(TEXT("Running again"), M->GetOutcome(), EOCMissionOutcome::None);

	M->Step(1.f, At(B));
	TestEqual(TEXT("Reached B: optional objective 3"), M->GetCurrentObjectiveIndex(), 2);
	M->Step(6.f, At(B));
	TestEqual(TEXT("Optional objective failing still ends in success"), M->GetOutcome(), EOCMissionOutcome::Succeeded);

	TestTrue(TEXT("Asset templates carry no play state"),
		Mission->Objectives[0].Objective->State == EOCObjectiveState::Inactive && Mission->Objectives[1].Objective->ActiveSeconds == 0.f);

	// Standing on several targets at once completes them in one step.
	UOCMissionSubsystem* M2 = NewObject<UOCMissionSubsystem>(GetTransientPackage());
	UOCMissionData* Same = NewObject<UOCMissionData>(GetTransientPackage());
	FOCMissionObjective S0; S0.Objective = Reach(Same, A);
	FOCMissionObjective S1; S1.Objective = Reach(Same, A);
	Same->Objectives = { S0, S1 };
	M2->StartMission(Same);
	M2->Step(0.1f, At(A));
	TestEqual(TEXT("Chained objectives finish together"), M2->GetOutcome(), EOCMissionOutcome::Succeeded);

	// Forced completion goes through the same path.
	UOCMissionSubsystem* M3 = NewObject<UOCMissionSubsystem>(GetTransientPackage());
	M3->StartMission(Mission);
	M3->ForceComplete();
	TestEqual(TEXT("Forced: succeeded"), M3->GetOutcome(), EOCMissionOutcome::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCMissionAssetsTest, "OpenCity.Core.Missions.Data.StoryAssets", OCMissionTest::Flags)

bool FOCMissionAssetsTest::RunTest(const FString& Parameters)
{
	const UOCAreaUnlockData* Unlocks = LoadObject<UOCAreaUnlockData>(nullptr, TEXT("/Game/Core/Progression/DA_AreaUnlocks.DA_AreaUnlocks"));
	TestNotNull(TEXT("DA_AreaUnlocks loads"), Unlocks);

	for (const TCHAR* Name : { TEXT("Funerale"), TEXT("StradaCostiera"), TEXT("LaFornace") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Missions/DA_Mission_%s.DA_Mission_%s"), Name, Name);
		const UOCMissionData* Mission = LoadObject<UOCMissionData>(nullptr, *Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Name), Mission))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s completes a milestone"), Name), Mission->CompletesMilestone.IsValid());
		TestTrue(*FString::Printf(TEXT("%s has objectives"), Name), Mission->Objectives.Num() > 0);
		TestTrue(*FString::Printf(TEXT("%s checkpoint is an objective"), Name), Mission->Objectives.IsValidIndex(Mission->CheckpointObjectiveIndex));
		for (int32 i = 0; i < Mission->Objectives.Num(); ++i)
		{
			const UOCObjective* Obj = Mission->Objectives[i].Objective;
			TestNotNull(*FString::Printf(TEXT("%s objective %d has settings"), Name, i), Obj);
			if (const UOCObjectiveReach* R = Cast<UOCObjectiveReach>(Obj))
			{
				TestFalse(*FString::Printf(TEXT("%s objective %d has a place"), Name, i), R->Location.IsNearlyZero());
			}
		}
		// What the mission says it opens must be what the unlock data opens with its milestone.
		for (const FGameplayTag& Area : Mission->UnlocksAreas)
		{
			const FOCAreaUnlock* Rule = Unlocks ? Unlocks->Areas.FindByPredicate([&Area](const FOCAreaUnlock& U) { return U.Area == Area; }) : nullptr;
			TestTrue(*FString::Printf(TEXT("%s: %s opens with this mission"), Name, *Area.ToString()),
				Rule && Rule->RequiredMilestone == Mission->CompletesMilestone);
		}
	}
	return true;
}

#endif
