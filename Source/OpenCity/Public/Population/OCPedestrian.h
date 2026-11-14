#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Damage/OCDamageable.h"
#include "Population/OCCrowdSim.h"
#include "OCPedestrian.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

/**
 * A pooled pedestrian (M5): capsule for movement/collision, a simple static mesh for looks
 * (no project art dependency — the engine's cylinder placeholder is used unless a mesh is set).
 * It never ticks: UOCPopulationSubsystem places it from the crowd sim at AI rate. Implements
 * IOCDamageable so gunfire and vehicle hits can put it down.
 */
UCLASS(NotBlueprintable)
class OPENCITY_API AOCPedestrian : public AActor, public IOCDamageable
{
	GENERATED_BODY()

public:
	AOCPedestrian();

	// IOCDamageable
	virtual bool ApplyDamage(float Amount, const FVector& Impulse, const AActor* Instigator) override;
	virtual float GetHealthFraction() const override { return Health; }
	virtual bool IsDead() const override { return bDead; }

	/** Dress for a fresh spawn: place out of the world. */
	void SetPooled(const FTransform& Where);
	void SetActive(bool bActive);

	/** The crowd sim id this actor shows (INDEX_NONE when pooled). */
	int32 SimId = INDEX_NONE;

	/** Where the sim says this ped should be right now (game layer applies Z from ground). */
	void SetSimPose(const FVector& Location, const FVector& Forward, double SpeedCmS);
	const FVector& SimLocation() const { return SimLoc; }

	/** Current behaviour state (mirrors the sim, for debug drawing). */
	EOCPedState GetState() const { return State; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	float Health = 1.f;
	bool bDead = false;
	bool bInWorld = true;
	FVector SimLoc = FVector::ZeroVector;
	EOCPedState State = EOCPedState::Idle;
};
