#pragma once

#include "CoreMinimal.h"
#include "GameplayTags.h"
#include "Subsystems/WorldSubsystem.h"
#include "OCEventSubsystem.generated.h"

/** Payload carried with every gameplay event. */
USTRUCT(BlueprintType)
struct OPENCITY_API FOCEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FGameplayTag Tag;            // e.g. Event.Crime.Murder, Event.Crime.Gunshot

	UPROPERTY()
	FVector Location;            // where it happened (world cm)

	UPROPERTY()
	TWeakObjectPtr<AActor> Instigator; // who caused it (may be null)

	UPROPERTY()
	float Severity = 1.f;        // 0..1, how serious (used for heat/witness checks)
};

/** Tag-based gameplay events: crimes, deaths, objective signals.
 *
 * Features publish events (weapon hit → Event.Crime.Murder, gunshot →
 * Event.Crime.Gunshot, pedestrian death → Event.Crime.Murder) and subsystems
 * subscribe: UOCWantedSubsystem raises heat, UOCPopulationSubsystem panics
 * nearby peds, UOCMissionSubsystem fails protected-target objectives.
 * No feature casts to another feature's concrete class; everything goes
 * through tags and this subsystem (ARCHITECTURE.md section 6).
 */
UCLASS()
class OPENCITY_API UOCEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Publish an event to all listeners. Safe to call from any system. */
	void Publish(const FGameplayTag& Tag, const FVector& Location, const AActor* Instigator,
		float Severity = 1.f);

	/** Subscribe to events matching Tag (exact tag match). Handle receives a copy of the event. */
	void Subscribe(const FGameplayTag& Tag, TFunction<void(const FOCEvent&)> Handle);

private:
	TMap<FGameplayTag, TArray<TFunction<void(const FOCEvent&)>>> Listeners;
};
