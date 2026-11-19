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

	/** The objective with its settings (Reach, Eliminate, Survive, Lose Heat). */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Objective")
	TObjectPtr<UOCObjective> Objective;

	/** Overrides the objective's own description. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText DisplayOverride;

	/** A required objective failing fails the mission; an optional one is skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
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

/** Data asset defining one mission: objectives in order, checkpoint, unlocks.
 *  Authored entirely as data (OCMilestone -steps=m8 or the editor); no C++ per mission.
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

	/** Story milestone completed when this mission succeeds (e.g. Story.Mission.Funerale). Areas and
	 *  gates open from it through DA_AreaUnlocks. */
	UPROPERTY(EditDefaultsOnly, Category = "Progression")
	FGameplayTag CompletesMilestone;

	/** Areas this mission opens (informational: DA_AreaUnlocks is the authority; a test checks they agree). */
	UPROPERTY(EditDefaultsOnly, Category = "Progression")
	TArray<FGameplayTag> UnlocksAreas;

	/** Objectives, played one after another. */
	UPROPERTY(EditDefaultsOnly, Category = "Objectives")
	TArray<FOCMissionObjective> Objectives;

	/** Once this objective becomes active it is the checkpoint: a retry after failing resumes here. */
	UPROPERTY(EditDefaultsOnly, Category = "Objectives", meta = (ClampMin = "0"))
	int32 CheckpointObjectiveIndex = 0;
};
