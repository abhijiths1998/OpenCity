#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OCDeepWaterVolume.generated.h"

class UBoxComponent;

/**
 * Water too deep to wade (no swimming in v1): the player is faded back to the nearest open road
 * on the bank; a vehicle that goes in is wrecked and left there. Placed per water body by
 * the world build with its top a little below the surface. Overlap events only; no Tick.
 */
UCLASS()
class OPENCITY_API AOCDeepWaterVolume : public AActor
{
	GENERATED_BODY()

public:
	AOCDeepWaterVolume();

	UBoxComponent* GetBox() const { return Box; }

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere, Category = "Water")
	TObjectPtr<UBoxComponent> Box;
};
