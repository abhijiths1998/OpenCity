#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Player/OCLocomotion.h"
#include "OCCharacterAnimInstance.generated.h"

class AOCCharacter;
class UCharacterMovementComponent;

/**
 * Animation Blueprint hookup point for OpenCity characters.
 * Reparent an Animation Blueprint to this class to read the values below in its graph.
 * Names are prefixed "OC" so they never clash with variables the Blueprint already has.
 */
UCLASS()
class OPENCITY_API UOCCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** Horizontal speed, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	float OCSpeed = 0.f;

	/** Movement direction relative to facing, -180..180 degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	float OCDirection = 0.f;

	/** Camera pitch relative to the character, -90..90 degrees (for aim offsets). */
	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	float OCAimPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	bool bOCHasAcceleration = false;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	bool bOCInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	bool bOCCrouched = false;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	bool bOCAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	bool bOCSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity")
	EOCGait OCGait = EOCGait::Jog;

private:
	TWeakObjectPtr<AOCCharacter> Character;
	TWeakObjectPtr<UCharacterMovementComponent> Movement;
};
