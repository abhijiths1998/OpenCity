#include "Missions/OCMissionSubsystem.h"

#include "Engine/World.h"

bool UOCMissionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCMissionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

void UOCMissionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsMissionActive())
	{
		EvaluateObjectives();
	}
}

void UOCMissionSubsystem::StartMission(TObjectPtr<UOCMissionData> Mission)
{
	if (!Mission || IsMissionActive())
	{
		return;
	}
	ActiveMission = Mission;
	Outcome = EOCMissionOutcome::None;

	// Instantiate objectives from their classes.
	ObjectiveInstances.Reset();
	for (const FOCMissionObjective& Entry : Mission->Objectives)
	{
		if (UOCObjective* Obj = NewObject<UOCObjective>(this, Entry.ObjectiveClass))
		{
			Obj->Description = Entry.DisplayOverride.IsEmpty() ? Obj->Description : Entry.DisplayOverride;
			ObjectiveInstances.Add(Obj);
		}
	}
	CheckpointObjectiveIndex = Mission->CheckpointObjectiveIndex;

	// Activate the first objective (and any non-required leading ones).
	for (UOCObjective* Obj : ObjectiveInstances)
	{
		Obj->Activate();
	}
}

void UOCMissionSubsystem::AbortMission()
{
	Outcome = EOCMissionOutcome::Failed;
	ActiveMission = nullptr;
	for (UOCObjective* Obj : ObjectiveInstances)
	{
		Obj->Reset();
	}
	ObjectiveInstances.Reset();
}

void UOCMissionSubsystem::RestartFromCheckpoint()
{
	if (!ActiveMission) return;
	Outcome = EOCMissionOutcome::None;
	for (int32 i = 0; i < ObjectiveInstances.Num(); ++i)
	{
		ObjectiveInstances[i]->Reset();
		if (i <= CheckpointObjectiveIndex)
		{
			ObjectiveInstances[i]->Activate();
		}
	}
}

void UOCMissionSubsystem::EvaluateObjectives()
{
	if (Outcome != EOCMissionOutcome::None) return;

	bool AllRequiredSucceeded = true;
	bool AnyRequiredFailed = false;
	for (UOCObjective* Obj : ObjectiveInstances)
	{
		const EOCObjectiveResult Result = Obj->Evaluate();
		if (Result == EOCObjectiveResult::Succeed)
		{
			Obj->Succeed();
		}
		else if (Result == EOCObjectiveResult::Fail)
		{
			Obj->Fail();
		}
	}
	for (int32 i = 0; i < ObjectiveInstances.Num(); ++i)
	{
		UOCObjective* Obj = ObjectiveInstances[i];
		if (Obj->IsFailed() && i <= CheckpointObjectiveIndex)
		{
			// Failed before or at checkpoint: restart from checkpoint instead of failing mission.
			RestartFromCheckpoint();
			return;
		}
		if (Obj->IsFailed())
		{
			AnyRequiredFailed = true;
		}
		if (!Obj->IsComplete())
		{
			AllRequiredSucceeded = false;
		}
	}
	if (AnyRequiredFailed)
	{
		Outcome = EOCMissionOutcome::Failed;
		return;
	}
	if (AllRequiredSucceeded)
	{
		Outcome = EOCMissionOutcome::Succeeded;
		return;
	}
}
