#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/OCEventSubsystem.h"
#include "OCWantedTypes.h"
#include "OCWantedSubsystem.generated.h"

/** Wanted / heat system (M7): crime events, witnesses, heat levels 1-5,
 * search zones, police dispatch requests.
 *
 * Listens to UOCEventSubsystem (Event.Crime.*), runs a witness check per crime,
 * raises heat when witnessed, maintains search zones that lapse over time, and
 * broadcasts OnHeatChanged so the HUD (M9) and police dispatch can react.
 * Police spawning itself goes through UOCPopulationSubsystem / UOCTrafficSubsystem
 * pooled actors (never casts to their concrete classes).
 */
UCLASS()
class OPENCITY_API UOCWantedSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOCOnHeatChanged, EOCHeatLevel /*NewLevel*/);

	/** Current heat level. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Wanted")
	EOCHeatLevel GetHeatLevel() const { return HeatLevel; }

	/** Whether the player is currently wanted. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Wanted")
	bool IsWanted() const { return HeatLevel != EOCHeatLevel::None; }

	/** Report a crime (called by weapons, impacts, pedestrian deaths). */
	void ReportCrime(EOCrime Crime, const FVector& Location, const AActor* Instigator,
		float Severity = 1.f);

	/** Player escapes: clear heat and search zones. */
	void ClearHeat();

	FOCOnHeatChanged OnHeatChanged;

private:
	void OnCrimeEvent(const FOCEvent& Ev);
	void UpdateHeat(float DeltaTime);
	void ExpireZones(float Now);

	UPROPERTY()
	EOCHeatLevel HeatLevel = EOCHeatLevel::None;

	UPROPERTY()
	TArray<FOCSearchZone> SearchZones;

	/** Seconds of wanted time before heat decays one level (while out of sight). */
	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0", Units = "s"))
	float DecaySecondsPerLevel = 15.f;

	/** Heat added per crime severity. */
	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0"))
	float HeatPerSeverity = 1.f;

	TWeakObjectPtr<UOCEventSubsystem> Events;

	float DecayAccum = 0.f;
};
