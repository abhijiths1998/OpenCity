#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Missions/OCObjective.h"
#include "Missions/OCMissionData.h"
#include "OCMissionSubsystem.generated.h"

/** Active mission state machine: objectives, triggers, checkpoints, fail/restart.
 *
 * The mission subsystem owns the active mission and its objective instances.
 * Objectives report Succeed/Fail via Evaluate(); the subsystem decides mission
 * outcome (all required succeed = success; any required fail = failure → restart
 * from checkpoint or mission failed).
 */
UCLASS()
class OPENCITY_API UOCMissionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }

	/** Start a mission from a data asset. */
	void StartMission(TObjectPtr<UOCMissionData> Mission);

	/** Abort the current mission (failure). */
	void AbortMission();

	/** Restart from the last checkpoint. */
	void RestartFromCheckpoint();

	/** The active mission data (nullptr if none). */
	TObjectPtr<UOCMissionData> GetActiveMission() const { return ActiveMission; }

	/** Current mission outcome. */
	EOCMissionOutcome GetOutcome() const { return Outcome; }

	/** Whether a mission is currently active. */
	bool IsMissionActive() const { return ActiveMission != nullptr && Outcome == EOCMissionOutcome::None; }

private:
	void EvaluateObjectives();
	void OnObjectiveStateChanged(UOCObjective* Objective);

	TObjectPtr<UOCMissionData> ActiveMission;
	TArray<UOCObjective*> ObjectiveInstances;
	int32 CheckpointObjectiveIndex = 0;
	EOCMissionOutcome Outcome = EOCMissionOutcome::None;
};
