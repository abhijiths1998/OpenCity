#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OCDamageable.generated.h"

class AActor;

/**
 * The only entry point for damage: GAS actors, light pedestrians, vehicles and props all
 * implement it, and nothing else takes a hit (ARCHITECTURE.md section 3). The caller never
 * needs to know the concrete type or whether damage went through GAS. Implemented as pure
 * virtual C++ (like IOCDrivable); death notifications go through UOCEventSubsystem in M7.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UOCDamageable : public UInterface
{
	GENERATED_BODY()
};

class OPENCITYCORE_API IOCDamageable
{
	GENERATED_BODY()

public:
	/** Apply Amount damage (any units the implementer likes). Returns true if something changed. */
	virtual bool ApplyDamage(float Amount, const FVector& Impulse, const AActor* Instigator) = 0;

	/** Current health fraction 0..1. */
	virtual float GetHealthFraction() const = 0;

	/** Whether this actor is dead or wrecked and no longer takes damage. */
	virtual bool IsDead() const = 0;
};
