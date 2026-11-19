#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OCPopulationSettings.generated.h"

/** Population rings and AI cadence (M5). Project Settings > Game > OpenCity Population. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OpenCity Population"))
class OPENCITY_API UOCPopulationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Peds on Medium; Low 50%, High 120%, Ultra 150% (ARCHITECTURE.md). */
	UPROPERTY(Config, EditAnywhere, Category = "Population", meta = (ClampMin = "0"))
	int32 MediumBudget = 40;

	/** New peds appear between these distances from the camera (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Population", meta = (Units = "m"))
	float SpawnMinM = 15.f;

	UPROPERTY(Config, EditAnywhere, Category = "Population", meta = (Units = "m"))
	float SpawnMaxM = 200.f;

	/** Inside this distance a ped only appears where the camera can't see it (no pop-in within 150 m). */
	UPROPERTY(Config, EditAnywhere, Category = "Population", meta = (Units = "m"))
	float VisibleSpawnM = 150.f;

	/** Peds further than this and out of view go back to the pool. */
	UPROPERTY(Config, EditAnywhere, Category = "Population", meta = (Units = "m"))
	float DespawnM = 250.f;

	/** AI runs this many times per second per ped (no per-frame AI). */
	UPROPERTY(Config, EditAnywhere, Category = "Population", meta = (ClampMin = "1", Units = "Hz"))
	float AiRateHz = 4.f;

	/** How far a threat is noticed (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Threats", meta = (Units = "m"))
	float ThreatRadiusM = 15.f;

	/** A ped keeps fleeing this long after the last threat (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Threats", meta = (Units = "s"))
	float FleeDurationS = 6.f;

	/** Gunshot radius (m): every ped within this hears it and flees. */
	UPROPERTY(Config, EditAnywhere, Category = "Threats", meta = (Units = "m"))
	float GunshotRadiusM = 25.f;

	/** Hit points of a pedestrian (weapon damage is in the same units). */
	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "1"))
	float PedestrianHealth = 100.f;

	/** A vehicle driven by the player knocks a ped down above this speed (km/h). */
	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "0", Units = "km/h"))
	float VehicleKnockdownKmh = 12.f;

	/** Bodies out of view go back to the pool after this long (s). */
	UPROPERTY(Config, EditAnywhere, Category = "Damage", meta = (ClampMin = "0", Units = "s"))
	float CorpseSeconds = 30.f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
