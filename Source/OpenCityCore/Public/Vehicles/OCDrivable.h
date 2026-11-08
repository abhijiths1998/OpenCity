#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OCDrivable.generated.h"

class APawn;

UINTERFACE(MinimalAPI)
class UOCDrivable : public UInterface
{
	GENERATED_BODY()
};

/**
 * World systems (deep water, later missions and police) act on vehicles through this, never
 * through the vehicle classes themselves.
 */
class OPENCITYCORE_API IOCDrivable
{
	GENERATED_BODY()

public:
	/** The pawn driving, if any. */
	virtual APawn* GetDriverPawn() const = 0;

	/** Put the driver out at Location now (no exit animation or speed check). Returns the driver. */
	virtual APawn* EjectDriverTo(const FVector& Location) = 0;

	/** Destroy the vehicle's drivability (e.g. sunk in deep water). */
	virtual void Wreck() = 0;
};
