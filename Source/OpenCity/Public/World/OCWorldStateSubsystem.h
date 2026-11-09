#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/OCSunModel.h"
#include "OCWorldStateSubsystem.generated.h"

class ADirectionalLight;
class ASkyLight;
class USpotLightComponent;
class UMaterialParameterCollection;
class UOCWeatherPreset;
class AExponentialHeightFog;
class ASkyAtmosphere;

/**
 * Time of day: moves the sun, tints it, dims the sky light at night, writes MPC_World (the only
 * writer: NightAmount, TimeOfDay) and, at night, moves a small pool of warm spot lights onto the
 * street lamps nearest the camera. Runs on a 10 Hz timer, no Tick. Settings: UOCWorldStateSettings.
 *
 * Command line: -octod=<hour> fixes the hour (the clock doesn't run). Console: oc.tod <hour>,
 * oc.tod.run 0|1.
 */
UCLASS()
class OPENCITY_API UOCWorldStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	float GetTimeOfDay() const { return Hour; }
	void SetTimeOfDay(float NewHour);
	void SetClockRunning(bool bRun) { bRunning = bRun; }
	float GetNightAmount() const { return Night; }

	/** Blend to a weather preset over Seconds (default: the settings' blend time). */
	void SetWeather(const UOCWeatherPreset* Preset, float Seconds = -1.f);
	/** A preset from the settings by name ("Rain" or "DA_Weather_Rain"); null if unknown. */
	const UOCWeatherPreset* FindWeather(const FString& Name) const;
	float GetWetness() const { return Wetness; }
	float GetRain() const { return Current.Rain; }

private:
	void Step();
	void Apply();
	void UpdateStreetLights();

	/** Blendable weather values (a snapshot of a preset). */
	struct FWeather
	{
		float Sun = 1.f, Sky = 1.f, Fog = 1.f, Rayleigh = 1.f, Mie = 1.f, Rain = 0.f, WetTarget = 0.f;
		static FWeather From(const UOCWeatherPreset* P);
		static FWeather Lerp(const FWeather& A, const FWeather& B, float T);
	};
	void StepWeather(float Dt);

	FOCSunModel Sun;
	FWeather From, To, Current;
	float BlendTime = 0.f, BlendLength = 0.f, Wetness = 0.f;
	float BaseFogDensity = -1.f;
	TWeakObjectPtr<AExponentialHeightFog> Fog;
	TWeakObjectPtr<ASkyAtmosphere> Atmosphere;
	TWeakObjectPtr<ADirectionalLight> SunLight;
	TWeakObjectPtr<ASkyLight> SkyLight;
	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Parameters;
	UPROPERTY()
	TArray<TObjectPtr<USpotLightComponent>> Lamps;
	FTimerHandle Timer;
	float Hour = 12.f;
	float Night = 0.f;
	float LastSkyCaptureHour = -100.f;
	float DayMinutes = 48.f;
	int32 StepCount = 0;
	bool bRunning = true;
};
