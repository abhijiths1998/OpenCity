#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/OCEventSubsystem.h"
#include "OCWantedTypes.h"
#include "OCWantedSubsystem.generated.h"

/** Wanted / heat system (M7): crime events, heat levels 1-5, search zones, decay.
 *
 * Listens to UOCEventSubsystem (Event.Crime.*). A crime raises heat only when the player
 * (or a vehicle the player drives) caused it. Heat is kept as a float so small crimes add
 * up; the level is its whole part. Repeating the same minor crime (gunshots, reckless
 * driving) within CrimeCooldownSeconds refreshes the search zone but adds no heat, so a
 * magazine of shots is one crime, not twelve. Heat holds while a search zone is live and
 * then decays one level every DecaySecondsPerLevel. OnHeatChanged feeds the HUD (M9) and
 * police dispatch. There is no witness model yet: every player crime counts.
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
	virtual TStatId GetStatId() const override;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOCOnHeatChanged, EOCHeatLevel /*NewLevel*/);

	/** Current heat level. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Wanted")
	EOCHeatLevel GetHeatLevel() const { return HeatLevel; }

	/** Raw heat (level = whole part). */
	float GetHeat() const { return Heat; }

	/** Whether the player is currently wanted. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Wanted")
	bool IsWanted() const { return HeatLevel != EOCHeatLevel::None; }

	/** Report a crime: publishes the matching Event.Crime.* tag (heat follows from the event). */
	void ReportCrime(EOCrime Crime, const FVector& Location, const AActor* Instigator,
		float Severity = 1.f);

	/** Player escapes: clear heat and search zones. */
	void ClearHeat();

	/** Live search zones (for the minimap in M9 and dev logging). */
	const TArray<FOCSearchZone>& GetSearchZones() const { return SearchZones; }

	FOCOnHeatChanged OnHeatChanged;

private:
	void OnCrimeEvent(EOCrime Crime, const FOCEvent& Ev);
	void SetHeat(float NewHeat, const TCHAR* Why);
	void UpdateHeat(float DeltaTime);
	void ExpireZones(float Now);

	EOCHeatLevel HeatLevel = EOCHeatLevel::None;
	float Heat = 0.f;

	UPROPERTY()
	TArray<FOCSearchZone> SearchZones;

	/** Seconds of wanted time before heat decays one level (once no search zone is live). */
	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0", Units = "s"))
	float DecaySecondsPerLevel = 15.f;

	/** Heat added per unit of crime severity. */
	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0"))
	float HeatPerSeverity = 1.f;

	/** The same minor crime again within this many seconds adds no heat. */
	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0", Units = "s"))
	float CrimeCooldownSeconds = 5.f;

	/** Search zone radius and lifetime around each crime. */
	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0", Units = "cm"))
	float SearchRadiusCm = 3000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Wanted", meta = (ClampMin = "0", Units = "s"))
	float SearchSeconds = 60.f;

	TWeakObjectPtr<UOCEventSubsystem> Events;
	TMap<EOCrime, float> LastCrimeTime;
	float DecayAccum = 0.f;
};
