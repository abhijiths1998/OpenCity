#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OCWorldStateSettings.generated.h"

class UMaterialParameterCollection;

/** Project Settings > Game > OpenCity World State (DefaultGame.ini): the day/night cycle. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OpenCity World State"))
class OPENCITY_API UOCWorldStateSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** MPC_World: only UOCWorldStateSubsystem writes it (scalars NightAmount, TimeOfDay). */
	UPROPERTY(Config, EditAnywhere, Category = "World State")
	TSoftObjectPtr<UMaterialParameterCollection> WorldParameters;

	UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHour = 16.5f;

	/** Real minutes for a full day; the clock runs in game builds unless -octod= fixes the hour. */
	UPROPERTY(Config, EditAnywhere, Category = "Clock", meta = (ClampMin = "1"))
	float DayLengthMinutes = 48.f;

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	float SunriseHour = 6.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	float SunsetHour = 20.f;

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	float MaxSunElevationDeg = 68.f;

	/** Rotates the whole sun path around the city (degrees). */
	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	float SunPathYawOffset = 0.f;

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	float SunIntensity = 10.f;

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	FLinearColor NoonColor = FLinearColor(1.f, 0.96f, 0.9f);

	UPROPERTY(Config, EditAnywhere, Category = "Sun")
	FLinearColor LowSunColor = FLinearColor(1.f, 0.62f, 0.36f);

	/** Sky light intensity at night (keeps streets readable under a dark sky). */
	UPROPERTY(Config, EditAnywhere, Category = "Night")
	float NightSkyLightIntensity = 2.5f;

	/** Pooled lights moved onto the street lamps nearest the camera at night. */
	UPROPERTY(Config, EditAnywhere, Category = "Night", meta = (ClampMin = "0", ClampMax = "64"))
	int32 StreetLightPool = 24;

	UPROPERTY(Config, EditAnywhere, Category = "Night")
	float StreetLightIntensity = 9000.f;

	UPROPERTY(Config, EditAnywhere, Category = "Night")
	float StreetLightRadius = 1800.f;

	UPROPERTY(Config, EditAnywhere, Category = "Night")
	FLinearColor StreetLightColor = FLinearColor(1.f, 0.66f, 0.34f);

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
