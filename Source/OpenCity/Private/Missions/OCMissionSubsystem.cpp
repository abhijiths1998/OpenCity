#include "Missions/OCMissionSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Events/OCEventSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "OCLog.h"
#include "Progression/OCProgressionSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Wanted/OCWantedSubsystem.h"

bool UOCMissionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

TStatId UOCMissionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOCMissionSubsystem, STATGROUP_Tickables);
}

void UOCMissionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Events = InWorld.GetSubsystem<UOCEventSubsystem>();
	if (Events.IsValid())
	{
		// Objectives that count things (kills) hear about them here.
		for (const FGameplayTag& Tag : { OCEventTags::Murder(), OCEventTags::Assault(), OCEventTags::Gunshot() })
		{
			Events->Subscribe(Tag, this, [this](const FOCEvent& Ev)
			{
				if (IsMissionActive() && Objectives.IsValidIndex(Current) && Objectives[Current])
				{
					Objectives[Current]->OnEvent(Ev);
				}
			});
		}
	}
}

void UOCMissionSubsystem::Deinitialize()
{
	if (Events.IsValid())
	{
		Events->Unsubscribe(this);
	}
	Super::Deinitialize();
}

void UOCMissionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsMissionActive())
	{
		Step(DeltaTime, GatherContext());
	}
}

FOCObjectiveContext UOCMissionSubsystem::GatherContext() const
{
	FOCObjectiveContext Ctx;
	const UWorld* World = GetWorld();
	if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			Ctx.bHasPlayer = true;
			Ctx.PlayerLocation = Pawn->GetActorLocation();
		}
	}
	if (const UOCWantedSubsystem* Wanted = World ? World->GetSubsystem<UOCWantedSubsystem>() : nullptr)
	{
		Ctx.HeatLevel = (int32)Wanted->GetHeatLevel();
	}
	return Ctx;
}

bool UOCMissionSubsystem::StartMission(UOCMissionData* Mission)
{
	if (!Mission || IsMissionActive())
	{
		return false;
	}
	ActiveMission = Mission;
	Outcome = EOCMissionOutcome::None;
	bCheckpointReached = false;
	Objectives.Reset();
	Required.Reset();
	for (const FOCMissionObjective& Entry : Mission->Objectives)
	{
		if (!Entry.Objective)
		{
			UE_LOG(LogOpenCity, Warning, TEXT("Mission %s: objective %d has no settings, skipped"),
				*Mission->GetName(), Objectives.Num());
			continue;
		}
		// Each run gets its own copy, so the asset's templates never carry play state.
		UOCObjective* Obj = DuplicateObject<UOCObjective>(Entry.Objective, this);
		Obj->Reset();
		if (!Entry.DisplayOverride.IsEmpty())
		{
			Obj->Description = Entry.DisplayOverride;
		}
		Objectives.Add(Obj);
		Required.Add(Entry.bRequired);
	}
	if (Objectives.IsEmpty())
	{
		UE_LOG(LogOpenCity, Warning, TEXT("Mission %s has no objectives"), *Mission->GetName());
		ActiveMission = nullptr;
		return false;
	}
	UE_LOG(LogOpenCity, Log, TEXT("Mission: started %s (%d objectives)"), *Mission->GetName(), Objectives.Num());
	ActivateObjective(0);
	return true;
}

void UOCMissionSubsystem::ActivateObjective(int32 Index)
{
	Current = Index;
	if (!Objectives.IsValidIndex(Index))
	{
		return;
	}
	if (ActiveMission && Index >= ActiveMission->CheckpointObjectiveIndex)
	{
		bCheckpointReached = true;
	}
	Objectives[Index]->Activate();
	UE_LOG(LogOpenCity, Log, TEXT("Mission: objective %d/%d: %s"), Index + 1, Objectives.Num(),
		*Objectives[Index]->Description.ToString());
	OnObjectiveChanged.Broadcast(ActiveMission, Index);
}

void UOCMissionSubsystem::Step(float DeltaSeconds, const FOCObjectiveContext& Ctx)
{
	// Several objectives can complete in one step (e.g. already standing at the next Reach spot).
	for (int32 Guard = 0; IsMissionActive() && Objectives.IsValidIndex(Current) && Guard < Objectives.Num(); ++Guard)
	{
		UOCObjective* Obj = Objectives[Current];
		const EOCObjectiveResult Result = Obj->Advance(Guard == 0 ? DeltaSeconds : 0.f, Ctx);
		if (Result == EOCObjectiveResult::None)
		{
			return;
		}
		if (Result == EOCObjectiveResult::Fail && Required[Current])
		{
			Finish(EOCMissionOutcome::Failed, FString::Printf(TEXT("objective %d failed: %s"), Current + 1, *Obj->GetFailReason()));
			return;
		}
		// Succeeded, or an optional objective failed: move on.
		if (Current + 1 >= Objectives.Num())
		{
			Finish(EOCMissionOutcome::Succeeded, TEXT("all objectives done"));
			return;
		}
		ActivateObjective(Current + 1);
	}
}

void UOCMissionSubsystem::Finish(EOCMissionOutcome InOutcome, const FString& Why)
{
	Outcome = InOutcome;
	UE_LOG(LogOpenCity, Log, TEXT("Mission: %s %s (%s)"), *GetNameSafe(ActiveMission),
		InOutcome == EOCMissionOutcome::Succeeded ? TEXT("succeeded") : TEXT("failed"), *Why);

	if (InOutcome == EOCMissionOutcome::Succeeded && ActiveMission && ActiveMission->CompletesMilestone.IsValid())
	{
		const UWorld* W = GetWorld();
		if (const UGameInstance* GI = W ? W->GetGameInstance() : nullptr)
		{
			if (UOCProgressionSubsystem* Prog = GI->GetSubsystem<UOCProgressionSubsystem>())
			{
				Prog->CompleteMilestone(ActiveMission->CompletesMilestone);
			}
		}
	}
	OnMissionEnded.Broadcast(ActiveMission, InOutcome);
}

void UOCMissionSubsystem::AbortMission()
{
	if (IsMissionActive())
	{
		Finish(EOCMissionOutcome::Failed, TEXT("aborted"));
	}
}

bool UOCMissionSubsystem::RetryFromCheckpoint()
{
	if (!ActiveMission || Outcome != EOCMissionOutcome::Failed)
	{
		return false;
	}
	const int32 From = bCheckpointReached ? FMath::Clamp(ActiveMission->CheckpointObjectiveIndex, 0, Objectives.Num() - 1) : 0;
	for (int32 i = From; i < Objectives.Num(); ++i)
	{
		Objectives[i]->Reset();
	}
	Outcome = EOCMissionOutcome::None;
	UE_LOG(LogOpenCity, Log, TEXT("Mission: retry %s from objective %d"), *ActiveMission->GetName(), From + 1);
	ActivateObjective(From);
	return true;
}

void UOCMissionSubsystem::ForceComplete()
{
	while (IsMissionActive() && Objectives.IsValidIndex(Current))
	{
		Objectives[Current]->Succeed();
		if (Current + 1 >= Objectives.Num())
		{
			Finish(EOCMissionOutcome::Succeeded, TEXT("forced"));
			return;
		}
		ActivateObjective(Current + 1);
	}
}

FString UOCMissionSubsystem::Describe() const
{
	if (!ActiveMission)
	{
		return TEXT("none");
	}
	const TCHAR* OutcomeText = Outcome == EOCMissionOutcome::None ? TEXT("running")
		: Outcome == EOCMissionOutcome::Succeeded ? TEXT("succeeded") : TEXT("failed");
	const UOCObjective* Obj = GetCurrentObjective();
	return FString::Printf(TEXT("%s %s, objective %d/%d (%s, %.0f s)%s"), *ActiveMission->GetName(), OutcomeText,
		Current + 1, Objectives.Num(), Obj ? *Obj->Description.ToString() : TEXT("-"), Obj ? Obj->ActiveSeconds : 0.f,
		bCheckpointReached ? TEXT(" [checkpoint]") : TEXT(""));
}
