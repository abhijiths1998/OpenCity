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
	FVector Location = FVector::ZeroVector; // where it happened (world cm)

	UPROPERTY()
	TWeakObjectPtr<AActor> Instigator; // who caused it (may be null)

	UPROPERTY()
	float Severity = 1.f;        // 0..1, how serious (used for heat/witness checks)
};

/** The crime event tags (Config/DefaultGameplayTags.ini). Looked up once, never by string at call sites. */
namespace OCEventTags
{
	OPENCITY_API const FGameplayTag& Gunshot();
	OPENCITY_API const FGameplayTag& Assault();
	OPENCITY_API const FGameplayTag& Murder();
	OPENCITY_API const FGameplayTag& VehicleTheft();
	OPENCITY_API const FGameplayTag& RecklessDriving();
}

/** Tag-based gameplay events: crimes, deaths, objective signals.
 *
 * Features publish events (gunshot → Event.Crime.Gunshot, pedestrian death →
 * Event.Crime.Murder) and subsystems subscribe: UOCWantedSubsystem raises heat,
 * UOCPopulationSubsystem panics nearby peds, UOCMissionSubsystem counts kills.
 * No feature casts to another feature's concrete class; everything goes
 * through tags and this subsystem (ARCHITECTURE.md section 6).
 */
UCLASS()
class OPENCITY_API UOCEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/** Publish an event to all listeners. Safe to call from any system, including from a listener. */
	void Publish(const FGameplayTag& Tag, const FVector& Location, const AActor* Instigator,
		float Severity = 1.f);

	/** Subscribe to events matching Tag exactly. Owner identifies the listener for Unsubscribe. */
	void Subscribe(const FGameplayTag& Tag, const void* Owner, TFunction<void(const FOCEvent&)> Handle);

	/** Drop every subscription made by Owner (call from the owner's Deinitialize). */
	void Unsubscribe(const void* Owner);

	/** Whether Actor is the player pawn, or was fired/driven by it (follows instigator and owner links). */
	static bool IsPlayerInstigated(const AActor* Actor);

private:
	struct FListener
	{
		const void* Owner = nullptr;
		TFunction<void(const FOCEvent&)> Handle;
	};
	TMap<FGameplayTag, TArray<FListener>> Listeners;
};
