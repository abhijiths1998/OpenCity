#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "OCPlayerController.generated.h"

/**
 * OpenCity player controller. Each possessed pawn (character, later vehicles) adds and removes
 * its own input mapping contexts; the controller clears leftovers when possession changes.
 */
UCLASS()
class OPENCITY_API AOCPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
