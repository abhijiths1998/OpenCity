#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "Vehicles/OCVehicleModels.h"
#include "OCVehicleComponents.generated.h"

class UCameraComponent;
class UChaosVehicleMovementComponent;
class UOCRadioStationData;
class UPrimitiveComponent;
class USpringArmComponent;

/**
 * Chase camera: speed-based field of view and free look that swings back behind the
 * vehicle after a delay. Ticks only while the vehicle has a player driver.
 */
UCLASS(ClassGroup = (OpenCity))
class OPENCITY_API UOCVehicleCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOCVehicleCameraComponent();

	void Initialize(USpringArmComponent* InBoom, UCameraComponent* InCamera, UChaosVehicleMovementComponent* InMovement,
		const FOCVehicleCameraSettings& InSettings);

	/** Free-look input (yaw, pitch), degrees. */
	void AddLookInput(const FVector2D& DeltaDegrees);

	/** Start or stop following the vehicle (only while a player drives). */
	void SetDriving(bool bDriving);

	// Tick is needed: FOV follows speed and free look recenters every frame while driving.
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ApplyBoomRotation();

	TWeakObjectPtr<USpringArmComponent> Boom;
	TWeakObjectPtr<UCameraComponent> Camera;
	TWeakObjectPtr<UChaosVehicleMovementComponent> Movement;
	FOCVehicleCameraSettings Settings;
	FVector2D LookOffset = FVector2D::ZeroVector;
	float TimeSinceLook = 0.f;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOCOnVehicleDamaged, float /*Damage*/, float /*HealthFraction*/);
DECLARE_MULTICAST_DELEGATE(FOCOnVehicleWrecked);

/** Turns physics hits into damage (FOCVehicleDamageModel) and reports performance changes. */
UCLASS(ClassGroup = (OpenCity))
class OPENCITY_API UOCVehicleDamageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOCVehicleDamageComponent();

	/** Start listening to hits on the physics body. */
	void Initialize(UPrimitiveComponent* InBody, const FOCVehicleDamageSettings& InSettings);

	float GetHealthFraction() const { return Model.GetHealthFraction(); }
	float GetPerformance() const { return Model.GetPerformance(); }
	bool IsWrecked() const { return Model.IsWrecked(); }

	/** For tests and debugging: apply an impact directly. */
	void ApplyImpact(float ImpactKmh);

	FOCOnVehicleDamaged OnDamaged;
	FOCOnVehicleWrecked OnWrecked;

private:
	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	TWeakObjectPtr<UPrimitiveComponent> Body;
	FOCVehicleDamageModel Model;
};

/** Radio stub: tunes stations and remembers the station per vehicle. No audio until M10. */
UCLASS(ClassGroup = (OpenCity))
class OPENCITY_API UOCRadioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOCRadioComponent();

	void Initialize(const TArray<TObjectPtr<UOCRadioStationData>>& InStations);

	/** Driver got in: resume the last station (stations keep playing in world time). */
	void PowerOn();
	void PowerOff();
	void Next();
	void Previous();

	const FOCRadioTuner& GetTuner() const { return Tuner; }

private:
	void Announce() const;

	UPROPERTY()
	TArray<TObjectPtr<UOCRadioStationData>> Stations;

	FOCRadioTuner Tuner;
	int32 RememberedIndex = 0;
	bool bPowered = false;
};
