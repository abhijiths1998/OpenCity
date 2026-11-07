#pragma once

#include "CoreMinimal.h"
#include "Vehicles/OCVehicle.h"
#include "OCMotorbike.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Motorbike on a hidden, narrow four-wheel Chaos chassis (standard Chaos vehicles cannot
 * balance on two wheels). A roll stabiliser keeps the chassis upright; the visible bike sits
 * on LeanPivot, which leans into turns, while wheel and fork meshes follow the real wheels.
 */
UCLASS()
class OPENCITY_API AOCMotorbike : public AOCVehicle
{
	GENERATED_BODY()

public:
	AOCMotorbike(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	// Tick is needed: the roll stabiliser and visual lean are continuous physics corrections.
	virtual void Tick(float DeltaSeconds) override;

	/** Current visual lean in degrees (+ = right side down). */
	float GetLeanDegrees() const { return LeanDegrees; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<USceneComponent> LeanPivot;

	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<USceneComponent> ForkPivot;

	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<USceneComponent> FrontHub;

	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<UStaticMeshComponent> FrontWheel;

	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<USceneComponent> RearHub;

	UPROPERTY(VisibleAnywhere, Category = "Motorbike")
	TObjectPtr<UStaticMeshComponent> RearWheel;

	/** Grey-box art, set in BP_OCVehicle_Bike. */
	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art")
	TObjectPtr<UStaticMesh> BodyMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art")
	TObjectPtr<UStaticMesh> WheelMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art")
	FVector BodyScale = FVector(1.6f, 0.3f, 0.45f);

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art", meta = (Units = "cm"))
	float WheelRadius = 32.f;

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art", meta = (Units = "cm"))
	float WheelWidth = 12.f;

	/** Wheel centres relative to the vehicle root (match the chassis wheel bones). */
	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art")
	FVector FrontWheelLocation = FVector(70.f, 0.f, 32.f);

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Art")
	FVector RearWheelLocation = FVector(-70.f, 0.f, 32.f);

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Lean", meta = (ClampMin = "0", ClampMax = "60", Units = "deg"))
	float MaxLeanDegrees = 35.f;

	/** Speed at which full lean is allowed. */
	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Lean", meta = (ClampMin = "1", Units = "km/h"))
	float FullLeanSpeedKmh = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Lean", meta = (ClampMin = "0"))
	float LeanSpeed = 5.f;

	/** Flip if the bike leans out of turns. */
	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Lean")
	float LeanSign = 1.f;

	/** Roll correction strength (angular acceleration per radian of roll). */
	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Stability", meta = (ClampMin = "0"))
	float StabiliserStiffness = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Stability", meta = (ClampMin = "0"))
	float StabiliserDamping = 10.f;

	/** Beyond this roll the bike counts as crashed and the stabiliser lets go (use Reset). */
	UPROPERTY(EditDefaultsOnly, Category = "Motorbike|Stability", meta = (Units = "deg"))
	float CrashRollDegrees = 70.f;

private:
	void ApplyArt();
	void Stabilise(float DeltaSeconds);
	void UpdateVisuals(float DeltaSeconds);

	float LeanDegrees = 0.f;
};
