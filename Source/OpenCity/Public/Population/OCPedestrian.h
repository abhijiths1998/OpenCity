#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Damage/OCDamageable.h"
#include "Population/OCCrowdSim.h"
#include "OCPedestrian.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;

/**
 * A pooled pedestrian (M5): capsule for collision, a simple static mesh for looks (the engine's
 * cylinder placeholder until there is pedestrian art). It never ticks: UOCPopulationSubsystem places
 * it from the crowd sim. Damage goes through IOCDamageable to the subsystem, which owns health in
 * the sim and publishes Event.Crime.Assault / Event.Crime.Murder with the attacker as instigator.
 * The capsule ignores vehicles: the subsystem knocks peds down when the player's car reaches them,
 * so a standing ped never stops a car dead like a bollard.
 */
UCLASS(NotBlueprintable)
class OPENCITY_API AOCPedestrian : public AActor, public IOCDamageable
{
	GENERATED_BODY()

public:
	AOCPedestrian();

	static float CapsuleHalfHeight();

	// IOCDamageable
	virtual bool ApplyDamage(float Amount, const FVector& Impulse, const AActor* Instigator) override;
	virtual float GetHealthFraction() const override { return Health; }
	virtual bool IsDead() const override { return bDead; }

	/** Back to the pool: hidden, no collision, out of the world. */
	void SetPooled(const FTransform& Where);
	void SetActive(bool bActive);

	/** The crowd sim id this actor shows (INDEX_NONE when pooled). */
	int32 SimId = INDEX_NONE;

	/** Place the actor with its feet at FeetLocation. A dead ped keeps its pose. */
	void SetSimPose(const FVector& FeetLocation, const FVector& Forward);
	const FVector& FeetLocation() const { return Feet; }

	/** Called by the population subsystem when health changes (Fraction 0..1). */
	void SetHealthFraction(float Fraction);
	/** Lie down and stop blocking anyone. */
	void Die(double Now);
	double GetDeathTime() const { return DeathTime; }

	/** Current behaviour state (mirrors the sim, for debug drawing). */
	EOCPedState GetState() const { return State; }
	void SetState(EOCPedState In) { State = In; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	float Health = 1.f;
	bool bDead = false;
	bool bInWorld = true;
	double DeathTime = 0.0;
	FVector Feet = FVector::ZeroVector;
	EOCPedState State = EOCPedState::Idle;
};
