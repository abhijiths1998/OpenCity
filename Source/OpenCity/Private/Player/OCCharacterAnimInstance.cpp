#include "Player/OCCharacterAnimInstance.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Player/OCCharacter.h"

void UOCCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Character = Cast<AOCCharacter>(TryGetPawnOwner());
	Movement = Character.IsValid() ? Character->GetCharacterMovement() : nullptr;
}

void UOCCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AOCCharacter* C = Character.Get();
	const UCharacterMovementComponent* M = Movement.Get();
	if (!C || !M)
	{
		return;
	}

	const FVector Velocity = M->Velocity;
	OCSpeed = Velocity.Size2D();
	bOCHasAcceleration = !M->GetCurrentAcceleration().IsNearlyZero();
	bOCInAir = M->IsFalling();
	bOCCrouched = M->IsCrouching();
	bOCAiming = C->IsAiming();
	bOCSprinting = C->IsSprinting();
	OCGait = C->GetGait();

	const FRotator Facing = C->GetActorRotation();
	OCDirection = OCSpeed > 1.f ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Facing.Yaw) : 0.f;

	const FRotator Delta = (C->GetBaseAimRotation() - Facing).GetNormalized();
	OCAimPitch = FMath::Clamp(Delta.Pitch, -90.f, 90.f);
}
