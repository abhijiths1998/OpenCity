#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Missions/OCObjective.h"
#include "Missions/OCMissionData.h"
#include "OCMissionSubsystem.generated.h"

class UOCEventSubsystem;

/**
 * The active mission (M8): copies the mission's objective templates, runs them one at a time in
 * order, and decides the outcome. All required objectives done = success (completes the story
 * milestone, which opens areas). A required objective failing = mission failed; RetryFromCheckpoint
 * then resumes at the checkpoint if it was reached, otherwise from the start.
 */
UCLASS()
class OPENCITY_API UOCMissionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOCOnMissionEnded, const UOCMissionData* /*Mission*/, EOCMissionOutcome /*Outcome*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOCOnObjectiveChanged, const UOCMissionData* /*Mission*/, int32 /*ObjectiveIndex*/);

	/** Start a mission (ignored while another is running). Returns whether it started. */
	bool StartMission(UOCMissionData* Mission);

	/** Give up the current mission (counts as failed). */
	void AbortMission();

	/** After a failure: resume from the checkpoint if it was reached, else from the start. */
	bool RetryFromCheckpoint();

	/** Dev/debug: succeed every remaining objective now (same path as playing it out). */
	void ForceComplete();

	/** Advance objectives by DeltaSeconds now (Tick does this every frame; tests call it directly). */
	void Step(float DeltaSeconds, const FOCObjectiveContext& Ctx);

	UOCMissionData* GetActiveMission() const { return ActiveMission; }
	EOCMissionOutcome GetOutcome() const { return Outcome; }
	bool IsMissionActive() const { return ActiveMission != nullptr && Outcome == EOCMissionOutcome::None; }
	int32 GetCurrentObjectiveIndex() const { return Current; }
	const UOCObjective* GetCurrentObjective() const { return Objectives.IsValidIndex(Current) ? Objectives[Current].Get() : nullptr; }
	FString Describe() const;

	FOCOnMissionEnded OnMissionEnded;
	FOCOnObjectiveChanged OnObjectiveChanged;

private:
	void ActivateObjective(int32 Index);
	void Finish(EOCMissionOutcome InOutcome, const FString& Why);
	FOCObjectiveContext GatherContext() const;

	UPROPERTY(Transient)
	TObjectPtr<UOCMissionData> ActiveMission;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UOCObjective>> Objectives;

	TArray<bool> Required;
	int32 Current = INDEX_NONE;
	bool bCheckpointReached = false;
	EOCMissionOutcome Outcome = EOCMissionOutcome::None;
	TWeakObjectPtr<UOCEventSubsystem> Events;
};
