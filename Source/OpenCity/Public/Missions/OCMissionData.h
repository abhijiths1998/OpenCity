#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTags.h"
#include "Missions/OCObjective.h"
#include "OCMissionData.generated.h"

/** One mission objective entry in a mission definition. */
USTRUCT(BlueprintType)
struct OPENCITY_API FOCMissionObjective
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	TSubclassOf<UOCObjective> ObjectiveClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	FText DisplayOverride; // overrides the objective asset's description

	/** Whether this objective must be completed to finish the mission. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	bool bRequired = true;
};

/** How a mission ends. */
UENUM(BlueprintType)
enum class EOCMissionOutcome : uint8
{
	None,
	Succeeded,
	Failed,
};

/** Data asset defining one mission: objectives, success/fail conditions, unlocks.
 *  Authored entirely in the editor; no C++ needed per mission.
 */
UCLASS(BlueprintType)
class OPENCITY_API UOCMissionData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Info")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, Category = "Info")
	FText Description;

	/** Story milestone completed when this mission succeeds (e.g. Story.Mission.Funerale). */
	UPROPERTY(EditDefaultsOnly, Category = "Progression")
	FGameplayTag CompletesMilestone;

	/** Areas unlocked on success (e.g. Area.PortoVecchio). */
	UPROPERTY(EditDefaultsOnly, Category = "Progression")
	TArray<FGameplayTag> UnlocksAreas;

	/** Objectives in order. All required objectives must succeed for the mission to succeed;
	 *  any required objective failing fails the mission (unless checkpoint restart). */
	UPROPERTY(EditDefaultsOnly, Category = "Objectives")
	TArray<FOCMissionObjective> Objectives;

	/** Checkpoint objective index: when reached, player can restart from here on failure. */
	UPROPERTY(EditDefaultsOnly, Category = "Objectives", meta = (ClampMin = "0"))
	int32 CheckpointObjectiveIndex = 0;
};
