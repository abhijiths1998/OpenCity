#pragma once

#include "CoreMinimal.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Engine/DataAsset.h"
#include "Vehicles/OCVehicleModels.h"
#include "OCVehicleData.generated.h"

class UAnimInstance;
class UInputAction;
class UInputMappingContext;
class USkeletalMesh;
class USoundBase;
class UMaterialInterface;
class UStaticMesh;

/** One radio station. The stub plays no audio yet (M10 adds MetaSounds). */
UCLASS(BlueprintType)
class OPENCITY_API UOCRadioStationData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Radio")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, Category = "Radio")
	FText Genre;

	/** Length of the station's loop, used to track playback position. */
	UPROPERTY(EditDefaultsOnly, Category = "Radio", meta = (ClampMin = "1", Units = "s"))
	float LengthSeconds = 1200.f;

	/** Optional until M10. */
	UPROPERTY(EditDefaultsOnly, Category = "Radio")
	TObjectPtr<USoundBase> Sound;
};

/** A static mesh attached to the vehicle for looks only (body, glass, wheels). No collision. */
USTRUCT(BlueprintType)
struct OPENCITY_API FOCVehiclePart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	TObjectPtr<UStaticMesh> Mesh;

	/** Bone or socket on the chassis mesh to follow (wheel bones spin and steer). None = root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FName Bone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FVector Offset = FVector::ZeroVector;

	/** E.g. yaw 180 to mirror a wheel for the right-hand side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	FVector Scale = FVector::OneVector;

	/** Replaces every material on the part when set (e.g. the rain-aware car glass). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part")
	TObjectPtr<UMaterialInterface> MaterialOverride;
};

/** Motorbike-only handling (used by AOCMotorbike, ignored by cars). */
USTRUCT(BlueprintType)
struct OPENCITY_API FOCMotorbikeHandling
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean", meta = (ClampMin = "0", ClampMax = "60", Units = "deg"))
	float MaxLeanDegrees = 35.f;

	/** Speed at which full lean is allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean", meta = (ClampMin = "1", Units = "km/h"))
	float FullLeanSpeedKmh = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lean", meta = (ClampMin = "0"))
	float LeanSpeed = 5.f;

	/** Roll correction strength (angular acceleration per radian of roll). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0"))
	float StabiliserStiffness = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0"))
	float StabiliserDamping = 10.f;

	/** Pulls the turn rate toward speed * tan(steer) / wheelbase so the light chassis follows
	 *  the front wheel instead of spinning out. 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (ClampMin = "0"))
	float YawAssist = 8.f;

	/** Beyond this roll the bike counts as fallen and the stabiliser lets go (use Reset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stability", meta = (Units = "deg"))
	float CrashRollDegrees = 70.f;
};

/**
 * Everything that makes one vehicle model: art, Chaos physics setup, camera, damage, seating.
 * AOCVehicle copies the physics setup into its movement component at spawn, so tuning lives
 * here and not in Blueprints. Assets: /Game/Vehicles/Data/DA_Vehicle_* (oc_m2_setup.py).
 */
UCLASS(BlueprintType)
class OPENCITY_API UOCVehicleData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Info")
	FText DisplayName;

	/** Physics chassis: needs wheel bones and a physics asset. */
	UPROPERTY(EditDefaultsOnly, Category = "Art")
	TObjectPtr<USkeletalMesh> Mesh;

	/** Usually a VehicleAnimationInstance Blueprint that spins and steers the wheel bones. */
	UPROPERTY(EditDefaultsOnly, Category = "Art")
	TSubclassOf<UAnimInstance> AnimClass;

	/** Body, glass and wheel meshes attached to the chassis (the template cars are built this way). */
	UPROPERTY(EditDefaultsOnly, Category = "Art")
	TArray<FOCVehiclePart> VisualParts;

	UPROPERTY(EditDefaultsOnly, Category = "Chassis", meta = (ClampMin = "1", Units = "kg"))
	float Mass = 1500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Chassis", meta = (ClampMin = "0"))
	float DragCoefficient = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Chassis", meta = (ClampMin = "0", Units = "cm"))
	float ChassisWidth = 180.f;

	UPROPERTY(EditDefaultsOnly, Category = "Chassis", meta = (ClampMin = "0", Units = "cm"))
	float ChassisHeight = 140.f;

	UPROPERTY(EditDefaultsOnly, Category = "Chassis")
	bool bOverrideCenterOfMass = false;

	/** Offset from the physics body's default centre of mass (lower = more stable). */
	UPROPERTY(EditDefaultsOnly, Category = "Chassis", meta = (EditCondition = "bOverrideCenterOfMass"))
	FVector CenterOfMassOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Wheels")
	TArray<FChaosWheelSetup> WheelSetups;

	/** Engine torque shape as (RPM, 0-1 of MaxTorque) points; built into EngineSetup.TorqueCurve. */
	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	TArray<FVector2D> TorqueCurvePoints;

	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	FVehicleEngineConfig EngineSetup;

	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	FVehicleTransmissionConfig TransmissionSetup;

	UPROPERTY(EditDefaultsOnly, Category = "Engine")
	FVehicleDifferentialConfig DifferentialSetup;

	UPROPERTY(EditDefaultsOnly, Category = "Steering")
	FVehicleSteeringConfig SteeringSetup;

	/** Steering limit by speed as (km/h, 0-1 of full lock); built into SteeringSetup.SteeringCurve.
	 *  Less lock at speed stops spin-outs. Empty = full lock at all speeds. */
	UPROPERTY(EditDefaultsOnly, Category = "Steering")
	TArray<FVector2D> SteeringCurvePoints;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	FOCVehicleCameraSettings Camera;

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	FOCVehicleDamageSettings Damage;

	/** Where the hidden driver is attached, relative to the vehicle root. */
	UPROPERTY(EditDefaultsOnly, Category = "Seat")
	FVector DriverOffset = FVector(0.f, -40.f, 60.f);

	/** Exit spots in vehicle space, tried in order (driver side first). */
	UPROPERTY(EditDefaultsOnly, Category = "Seat")
	TArray<FVector> ExitOffsets = { FVector(0.f, -180.f, 60.f), FVector(0.f, 180.f, 60.f), FVector(-350.f, 0.f, 60.f), FVector(0.f, 0.f, 250.f) };

	/** Above this speed, exiting brakes first and gets out once slow enough. */
	UPROPERTY(EditDefaultsOnly, Category = "Seat", meta = (ClampMin = "0", Units = "km/h"))
	float MaxExitSpeedKmh = 15.f;

	UPROPERTY(EditDefaultsOnly, Category = "Radio")
	TArray<TObjectPtr<UOCRadioStationData>> RadioStations;

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike")
	FOCMotorbikeHandling Motorbike;
};

/** Input for driving. Asset: /Game/Core/Input/DA_OCVehicleInputConfig. */
UCLASS(BlueprintType)
class OPENCITY_API UOCVehicleInputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Contexts")
	TObjectPtr<UInputMappingContext> VehicleContext;

	UPROPERTY(EditDefaultsOnly, Category = "Contexts")
	int32 VehiclePriority = 0;

	/** Axis1D 0..1. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Throttle;

	/** Axis1D 0..1. Also reverses when stopped (Chaos auto-reverse). */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Brake;

	/** Axis1D -1..1, right positive. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Steer;

	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Handbrake;

	/** Axis2D free look around the vehicle. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Look;

	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Exit;

	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> RadioNext;

	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> RadioPrevious;

	/** Put a flipped or stuck vehicle back on its wheels. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> ResetVehicle;
};
