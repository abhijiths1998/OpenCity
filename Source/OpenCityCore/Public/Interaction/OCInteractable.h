#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "OCInteractable.generated.h"

class APawn;

UINTERFACE(MinimalAPI, BlueprintType)
class UOCInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything the player can use with the interact button: vehicles (enter), later doors,
 * pickups and mission triggers. The interactor never needs to know the concrete type.
 */
class OPENCITYCORE_API IOCInteractable
{
	GENERATED_BODY()

public:
	/** Whether this pawn can interact right now (e.g. a vehicle with a free seat). */
	virtual bool CanInteract(const APawn* Instigator) const = 0;

	/** Do it. Returns true if something happened. */
	virtual bool Interact(APawn* Instigator) = 0;

	/** Short prompt for the HUD (M9), e.g. "Enter vehicle". */
	virtual FText GetInteractionText() const = 0;
};
