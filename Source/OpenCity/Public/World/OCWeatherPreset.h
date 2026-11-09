#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OCWeatherPreset.generated.h"

/**
 * One weather state (DA_Weather_Clear / Cloudy / Rain, made by Content/Python/oc_weather.py).
 * UOCWorldStateSubsystem blends between presets; materials read RainAmount and Wetness from MPC_World.
 */
UCLASS(BlueprintType)
class OPENCITY_API UOCWeatherPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Multiplies the sun's intensity (clouds block the sun). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0"))
	float SunScale = 1.f;

	/** Multiplies the sky light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0"))
	float SkyLightScale = 1.f;

	/** Multiplies the height fog density. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0"))
	float FogScale = 1.f;

	/** Sky atmosphere Rayleigh scale: lower = greyer, overcast sky. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0"))
	float RayleighScale = 1.f;

	/** Sky atmosphere Mie scale: higher = hazier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0"))
	float MieScale = 1.f;

	/** 0..1: falling rain (streaks, puddle ripples, splashes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float RainAmount = 0.f;

	/** 0..1: how wet surfaces get under this weather (reached over time, see the settings). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float WetnessTarget = 0.f;
};
