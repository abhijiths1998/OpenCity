#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OCInteractionComponent.generated.h"

/**
 * Finds the nearest IOCInteractable around the owner pawn and uses it.
 * Queried only when the interact button is pressed (no Tick); the HUD prompt (M9)
 * will reuse FindBest() on a slow timer.
 */
UCLASS(ClassGroup = (OpenCity), meta = (BlueprintSpawnableComponent))
class OPENCITY_API UOCInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOCInteractionComponent();

	/** Nearest actor implementing IOCInteractable that accepts the owner right now. */
	AActor* FindBest() const;

	/** Interact with FindBest(). Returns true if something happened. */
	bool TryInteract();

protected:
	UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float Radius = 300.f;
};
