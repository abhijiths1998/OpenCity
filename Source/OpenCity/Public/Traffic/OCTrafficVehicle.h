#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OCTrafficVehicle.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UOCVehicleData;

/**
 * A traffic car (M5): kinematic, no physics and no tick. UOCTrafficSubsystem moves it along the lane graph
 * and pools it. It looks like the model's drivable car (the same body, glass and wheel parts, placed at
 * the chassis bones' rest pose, painted through the same custom primitive data) and blocks like one.
 */
UCLASS(NotBlueprintable)
class OPENCITY_API AOCTrafficVehicle : public AActor
{
	GENERATED_BODY()

public:
	AOCTrafficVehicle();

	/** Dress the car as Model with this paint (rebuilds the parts only when the model changes). */
	void SetModel(UOCVehicleData* Model, const FLinearColor& Paint, float Dirt);
	UOCVehicleData* GetModel() const { return Model; }

	/** Out of the world (pooled) or in it. */
	void SetActive(bool bActive);
	bool IsActive() const { return bInWorld; }

	/** The sim car this actor shows (INDEX_NONE when pooled). */
	int32 SimId = INDEX_NONE;

	/** Length of the collision box (cm), for the obstacle probe. */
	float GetHalfLength() const;

	/** The box centre in the chassis frame (the chassis origin sits on the ground between the wheels). */
	FVector GetCentreFromChassis() const { return CentreFromChassis; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UOCVehicleData> Model;

	bool bInWorld = true;
	FVector CentreFromChassis = FVector(0.0, 0.0, 70.0);
};
