#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "OCLockedAreaVolume.generated.h"

class UBoxComponent;

/**
 * Covers one area that a story gate keeps closed (STORY.md "Unlocking the city"). A player who
 * gets in anyway (wading, off-road) is warned; still inside after the push-back delay, they are
 * faded back to the nearest open road. Overlap events and one timer only; no Tick.
 */
UCLASS()
class OPENCITY_API AOCLockedAreaVolume : public AActor
{
	GENERATED_BODY()

public:
	AOCLockedAreaVolume();

	/** Area.<District>; open per UOCProgressionSubsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
	FGameplayTag Area;

	UBoxComponent* GetBox() const { return Box; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);

	/** The player's current pawn, if it is inside (it changes when they enter or leave a vehicle). */
	APawn* PlayerPawnInside() const;
	void CheckStillInside();
	void PushBack();

	UPROPERTY(VisibleAnywhere, Category = "Area")
	TObjectPtr<UBoxComponent> Box;

	TWeakObjectPtr<APlayerController> Intruder;
	FTimerHandle PushBackTimer;
};
