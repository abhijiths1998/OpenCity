#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OCObjective.generated.h"

struct FOCEvent;

/** Objective state. */
UENUM(BlueprintType)
enum class EOCObjectiveState : uint8
{
	Inactive,   // not yet started
	Active,     // player is working on it
	Succeeded,  // completed
	Failed,     // failed
};

/** Result of evaluating an objective. */
UENUM(BlueprintType)
enum class EOCObjectiveResult : uint8
{
	None,       // still in current state
	Succeed,    // transition to Succeeded
	Fail,       // transition to Failed
};

/** What an objective may look at when it is evaluated (filled by UOCMissionSubsystem). */
struct FOCObjectiveContext
{
	bool bHasPlayer = false;
	FVector PlayerLocation = FVector::ZeroVector; // world cm (the possessed pawn: on foot or in a car)
	int32 HeatLevel = 0;                          // wanted level 0..5
};

/**
 * One objective within a mission: pure, data-driven logic (M8). Instanced inside a UOCMissionData
 * asset, so each mission sets its own location, counts and limits; UOCMissionSubsystem copies the
 * templates when a mission starts and drives the copies (one active at a time, in order).
 *
 * Every objective can also fail on a time limit or on drawing police attention.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType, CollapseCategories)
class OPENCITY_API UOCObjective : public UObject
{
	GENERATED_BODY()

public:
	/** Shown on the HUD (M9). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText Description;

	/** Fail if not done within this many seconds of becoming active (0 = no limit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0", Units = "s"))
	float TimeLimitSeconds = 0.f;

	/** Fail as soon as the wanted level reaches this (0 = police don't matter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0", ClampMax = "5"))
	int32 FailAtHeatLevel = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Objective")
	EOCObjectiveState State = EOCObjectiveState::Inactive;

	/** Seconds since Activate (driven by Advance). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Objective")
	float ActiveSeconds = 0.f;

	void Activate();
	void Succeed();
	void Fail();
	/** Back to inactive with progress cleared (restart). */
	void Reset();

	/** Advance time and decide: limits first, then the subclass rule. Applies the transition. */
	EOCObjectiveResult Advance(float DeltaSeconds, const FOCObjectiveContext& Ctx);

	/** A gameplay event while active (kills for Eliminate). */
	virtual void OnEvent(const FOCEvent& Ev) {}

	bool IsActive() const { return State == EOCObjectiveState::Active; }
	bool IsComplete() const { return State == EOCObjectiveState::Succeeded; }
	bool IsFailed() const { return State == EOCObjectiveState::Failed; }

	/** Why the last failure happened (log / HUD). */
	const FString& GetFailReason() const { return FailReason; }

protected:
	/** The objective's own rule; called only while active. */
	virtual EOCObjectiveResult Evaluate(const FOCObjectiveContext& Ctx) { return EOCObjectiveResult::None; }
	/** Clear subclass progress on Reset. */
	virtual void ResetProgress() {}

	FString FailReason;
};

/** Get to a place: succeeds when the player is within Radius of Location (any vehicle or on foot). */
UCLASS(meta = (DisplayName = "Reach"))
class OPENCITY_API UOCObjectiveReach : public UOCObjective
{
	GENERATED_BODY()

public:
	/** World location (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FVector Location = FVector::ZeroVector;

	/** Horizontal radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "100", Units = "cm"))
	float RadiusCm = 1500.f;

	/** How far above or below Location still counts (cm): a bridge over the spot doesn't. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "100", Units = "cm"))
	float HeightToleranceCm = 1500.f;

protected:
	virtual EOCObjectiveResult Evaluate(const FOCObjectiveContext& Ctx) override;
};

/** Take people down: succeeds after KillCount player kills (within Radius of Location if Radius > 0). */
UCLASS(meta = (DisplayName = "Eliminate"))
class OPENCITY_API UOCObjectiveEliminate : public UOCObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "1"))
	int32 KillCount = 1;

	/** Where kills count (cm); RadiusCm 0 = anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0", Units = "cm"))
	float RadiusCm = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Objective")
	int32 Kills = 0;

	virtual void OnEvent(const FOCEvent& Ev) override;

protected:
	virtual EOCObjectiveResult Evaluate(const FOCObjectiveContext& Ctx) override;
	virtual void ResetProgress() override { Kills = 0; }
};

/** Hold out: succeeds after DurationSeconds active (TimeLimit / heat can still fail it). */
UCLASS(meta = (DisplayName = "Survive"))
class OPENCITY_API UOCObjectiveSurvive : public UOCObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0", Units = "s"))
	float DurationSeconds = 10.f;

protected:
	virtual EOCObjectiveResult Evaluate(const FOCObjectiveContext& Ctx) override;
};

/** Shake the police: succeeds while the wanted level is 0 (put it after the objective that draws heat). */
UCLASS(meta = (DisplayName = "Lose Heat"))
class OPENCITY_API UOCObjectiveLoseHeat : public UOCObjective
{
	GENERATED_BODY()

protected:
	virtual EOCObjectiveResult Evaluate(const FOCObjectiveContext& Ctx) override;
};
