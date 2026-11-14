#pragma once

#include "CoreMinimal.h"
#include "GameplayTags.h"
#include "UObject/Object.h"
#include "OCObjective.generated.h"

/** Objective state. */
UENUM(BlueprintType)
enum class EOCObjectiveState : uint8
{
	Inactive,   // not yet started
	Active,     // player is working on it
	Succeeded,  // completed
	Failed,     // failed (can be retried from checkpoint)
};

/** Result of evaluating an objective this frame. */
UENUM(BlueprintType)
enum class EOCObjectiveResult : uint8
{
	None,       // still in current state
	Succeed,    // transition to Succeeded
	Fail,       // transition to Failed
};

/** One objective within a mission: pure, data-driven logic.
 *
 * Subclasses implement Evaluate() to decide Succeed/Fail based on game state.
 * The mission subsystem drives them; no Tick here — they're evaluated on
 * relevant events (enter trigger, kill, reach checkpoint, timer).
 */
UCLASS(Abstract, BlueprintType)
class OPENCITY_API UOCObjective : public UObject
{
	GENERATED_BODY()

public:
	UOCObjective() = default;

	/** Human-readable description (shown on the HUD, M9). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	FText Description;

	/** Current state. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	EOCObjectiveState State = EOCObjectiveState::Inactive;

	/** Start this objective. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Activate();

	/** Mark succeeded (with optional fail reason text). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Succeed();

	/** Mark failed. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Fail();

	/** Reset to inactive (used when restarting from a checkpoint). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Reset();

	/** Evaluate this objective now; returns Succeed/Fail/None. Override in subclasses. */
	UFUNCTION(BlueprintNativeEvent, Category = "Objective")
	EOCObjectiveResult Evaluate();

	/** Whether the objective is currently active and can succeed/fail. */
	bool IsActive() const { return State == EOCObjectiveState::Active; }
	bool IsComplete() const { return State == EOCObjectiveState::Succeeded; }
	bool IsFailed() const { return State == EOCObjectiveState::Failed; }

protected:
	virtual EOCObjectiveResult Evaluate_Implementation() { return EOCObjectiveResult::None; }
};

/** Reach a location (trigger volume): objective completes when the player enters. */
UCLASS()
class OPENCITY_API UOCObjectiveReach : public UOCObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	FGameplayTag TriggerTag;

	virtual EOCObjectiveResult Evaluate_Implementation() override;
};

/** Eliminate a target (pedestrian/vehicle): objective completes when the target is dead. */
UCLASS()
class OPENCITY_API UOCObjectiveEliminate : public UOCObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective")
	TObjectPtr<class AActor> Target;

	virtual EOCObjectiveResult Evaluate_Implementation() override;
};

/** Survive for a duration: objective completes after the timer elapses. */
UCLASS()
class OPENCITY_API UOCObjectiveSurvive : public UOCObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0", Units = "s"))
	float DurationSeconds = 10.f;

	float ElapsedSeconds = 0.f;

	virtual EOCObjectiveResult Evaluate_Implementation() override;
};
