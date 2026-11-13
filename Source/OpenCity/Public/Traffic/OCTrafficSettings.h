#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OCTrafficSettings.generated.h"

class UOCVehicleData;

/** Project Settings > Game > OpenCity Traffic (DefaultGame.ini). Population rings from ARCHITECTURE.md section 8. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OpenCity Traffic"))
class OPENCITY_API UOCTrafficSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Models traffic picks from (each with its own paint palette). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic")
	TArray<TSoftObjectPtr<UOCVehicleData>> Models;

	/** Cars on Medium; Low 50%, High 120%, Ultra 150% (ARCHITECTURE.md, "Traffic and crowd"). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (ClampMin = "0"))
	int32 MediumBudget = 24;

	/** New cars appear between these distances from the camera (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float SpawnMinM = 60.f;

	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float SpawnMaxM = 200.f;

	/** Inside this distance a car only appears where the camera can't see it (no pop-in within 150 m). */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float VisibleSpawnM = 150.f;

	/** Cars further than this and out of view go back to the pool. */
	UPROPERTY(Config, EditAnywhere, Category = "Traffic", meta = (Units = "m"))
	float DespawnM = 250.f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
