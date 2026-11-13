#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Progression/OCUnlockSchedule.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OCProgressionSubsystem.generated.h"

class APlayerController;
class FOCRoadGraph;
class UOCAreaUnlockData;
class UOCRoadGraphAsset;
struct FActorsInitializedParams;

DECLARE_MULTICAST_DELEGATE_OneParam(FOCOnMilestoneCompleted, FGameplayTag /*Milestone*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOCOnAreaWarning, const FText& /*Message*/);

/**
 * Story progression: the completed milestones and what they open (ARCHITECTURE.md "Story
 * progression and area gates"). Switches each gate's World Partition Data Layers when the world
 * starts and when a milestone completes, so closed gates cost nothing and nothing ticks.
 * Missions (M8) complete milestones; dev builds use oc.dev.unlock. Saved in M10.
 */
UCLASS()
class OPENCITY_API UOCProgressionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Returns false if it was already complete. Opens whatever waited for it. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Progression")
	bool CompleteMilestone(FGameplayTag Milestone);

	UFUNCTION(BlueprintPure, Category = "OpenCity|Progression")
	bool IsMilestoneComplete(FGameplayTag Milestone) const { return Completed.HasTagExact(Milestone); }

	/** Areas and gates without an unlock rule are always open. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Progression")
	bool IsAreaOpen(FGameplayTag Area) const { return Schedule.IsOpen(Area, Completed); }

	/** Dev: complete every milestone in the schedule / forget all progress. */
	void CompleteAllMilestones();
	void ResetProgress();

	const FGameplayTagContainer& GetCompletedMilestones() const { return Completed; }
	TArray<FGameplayTag> GetScheduledMilestones() const { return Schedule.AllMilestones(); }

	/** Closed gates as road graph gate names, for pathing (traffic, mission routes). */
	TSet<FName> GetClosedGates() const { return Schedule.ClosedGates(Completed); }

	FText GetLockedMessage(FGameplayTag Area) const;
	float GetPushBackDelaySeconds() const;

	/** The road network (null if the settings don't name a valid road graph asset). */
	const FOCRoadGraph* GetRoadGraph() const;

	/** The road graph asset (lanes for traffic); null if the settings name none. */
	const UOCRoadGraphAsset* GetRoadGraphAsset() const { return RoadGraph; }

	/**
	 * The road node nearest From that a new game's start can reach with the gates as they are now
	 * (never inside a locked area). OutFacing looks away from From.
	 */
	bool FindOpenRoadNear(const FVector& From, FVector& OutLocation, FRotator& OutFacing) const;

	/**
	 * Fade out, move the player's pawn to the open road nearest From, fade in. With bWreckVehicle
	 * (deep water) a driven vehicle is wrecked and left behind; the driver is moved instead.
	 * Ignored while a return is already under way.
	 */
	void ReturnPlayerToOpenRoad(APlayerController* PC, const FVector& From, bool bWreckVehicle);

	/** Locked-area volumes report their bounds, so returns never land inside a still-locked area. */
	void RegisterAreaBounds(const UObject* Owner, FGameplayTag Area, const FBox& Bounds);
	void UnregisterAreaBounds(const UObject* Owner);

	/** The locked-area warning. HUD (M7) binds OnAreaWarning; until then it is printed on screen. */
	void ShowAreaWarning(const FText& Message);

	FOCOnMilestoneCompleted OnMilestoneCompleted;
	FOCOnAreaWarning OnAreaWarning;

private:
	void HandleWorldActorsInitialized(const FActorsInitializedParams& Params);
	void ApplyToWorld(UWorld* World) const;
	void ApplyToCurrentWorld() const;
	void FinishReturn(TWeakObjectPtr<APlayerController> WeakPC, FVector From, int32 Attempt);

	UPROPERTY()
	TObjectPtr<UOCAreaUnlockData> UnlockData;

	UPROPERTY()
	TObjectPtr<UOCRoadGraphAsset> RoadGraph;

	struct FAreaBounds
	{
		TWeakObjectPtr<const UObject> Owner;
		FGameplayTag Area;
		FBox Bounds;
	};
	TArray<FAreaBounds> AreaBounds;

	FOCUnlockSchedule Schedule;
	FGameplayTagContainer Completed;
	FDelegateHandle WorldInitHandle;
	bool bReturning = false;
};
