#pragma once

#include "CoreMinimal.h"
#include "OCVehicleModels.generated.h"

/** Tuning for crash damage. Speeds are the impact's change in velocity, in km/h. */
USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCVehicleDamageSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1"))
	float MaxHealth = 1000.f;

	/** Impacts below this change in speed do no damage (curb taps, light bumps). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", Units = "km/h"))
	float MinImpactKmh = 12.f;

	/** Health lost per km/h of impact above the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float DamagePerKmh = 12.f;

	/** Engine power multiplier just before the vehicle is wrecked (1 = undamaged). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float MinPerformance = 0.35f;

	/** Hit events within this window count as one crash (Chaos sends several per impact). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", Units = "s"))
	float ImpactMergeWindow = 0.15f;
};

/** Health and performance of one vehicle. Pure logic, unit-tested. */
struct OPENCITYCORE_API FOCVehicleDamageModel
{
	void Reset(const FOCVehicleDamageSettings& InSettings);

	/** Feed one hit. Returns health lost by this hit (0 if ignored). */
	float ApplyImpact(float ImpactKmh, double NowSeconds);

	float GetHealth() const { return Health; }
	float GetHealthFraction() const;
	bool IsWrecked() const { return Health <= 0.f; }

	/** Engine power multiplier: 1 at full health, MinPerformance near zero, 0 when wrecked. */
	float GetPerformance() const;

private:
	FOCVehicleDamageSettings Settings;
	float Health = 1000.f;
	double LastImpactTime = -1000.0;
	float CrashPeakKmh = 0.f;
};

/** One radio station as the tuner sees it. */
struct FOCRadioStationInfo
{
	FString Name;
	float LengthSeconds = 0.f;
};

/**
 * Radio tuning. Stations "play" continuously in world time with a per-station offset, so
 * leaving a car and coming back finds each station further along, as if it never stopped.
 * Index -1 is "off"; cycling goes off -> 0 -> 1 -> ... -> off.
 */
struct OPENCITYCORE_API FOCRadioTuner
{
	void SetStations(TArray<FOCRadioStationInfo> InStations, uint32 Seed);

	int32 Next();
	int32 Previous();
	void Tune(int32 Index);

	int32 GetIndex() const { return Index; }
	bool IsOff() const { return Index < 0; }
	int32 NumStations() const { return Stations.Num(); }
	const FOCRadioStationInfo* GetCurrent() const;

	/** Playback position in seconds on the current station at this world time (0 if off). */
	float GetPosition(double NowSeconds) const;

private:
	TArray<FOCRadioStationInfo> Stations;
	TArray<float> Offsets;
	int32 Index = -1;
};

/** Chase-camera tuning shared by all vehicles; values come from each vehicle's data asset. */
USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCVehicleCameraSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float ArmLength = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector SocketOffset = FVector(0.f, 0.f, 180.f);

	/** Downward tilt of the boom, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	float Pitch = -10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "30", ClampMax = "120", Units = "deg"))
	float FovAtRest = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "30", ClampMax = "120", Units = "deg"))
	float FovAtSpeed = 100.f;

	/** Speed at which FovAtSpeed is reached. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "1", Units = "km/h"))
	float FovFullSpeedKmh = 180.f;

	/** Seconds without look input before the camera swings back behind the vehicle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "s"))
	float RecenterDelay = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0"))
	float RecenterSpeed = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0"))
	float RotationLagSpeed = 8.f;

	/** Field of view for a forward speed (reverse counts as speed too). */
	float FovForSpeed(float SpeedKmh) const;
};
