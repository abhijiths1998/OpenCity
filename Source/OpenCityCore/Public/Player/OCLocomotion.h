#pragma once

#include "CoreMinimal.h"
#include "OCLocomotion.generated.h"

/** Movement style the character is in. Drives max speed, camera pose and animation. */
UENUM(BlueprintType)
enum class EOCGait : uint8
{
	Jog,
	Sprint,
	Crouch,
	Aim,
};

/** Max ground speeds per gait, in cm/s. Tuned in the character Blueprint. */
USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCLocomotionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0", Units = "cm/s"))
	float JogSpeed = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0", Units = "cm/s"))
	float SprintSpeed = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0", Units = "cm/s"))
	float CrouchSpeed = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0", Units = "cm/s"))
	float AimSpeed = 250.f;

	/** Forward stick/key amount (0-1) needed to sprint. Sideways and backwards never sprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0", ClampMax = "1"))
	float SprintForwardThreshold = 0.5f;
};

/** What the player is asking for this moment. MoveInput: X = right, Y = forward, each -1..1. */
struct FOCLocomotionInput
{
	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bWantsSprint = false;
	bool bWantsAim = false;
	bool bIsCrouched = false;
};

/** Result of applying the rules. */
struct FOCLocomotionDecision
{
	EOCGait Gait = EOCGait::Jog;
	float MaxSpeed = 0.f;
	/** Sprinting from a crouch stands the character up. */
	bool bShouldUncrouch = false;
	/** Toggle-sprint (gamepad) ends when the player stops moving. */
	bool bShouldClearSprintToggle = false;
};

/**
 * Locomotion rules. Pure logic with no engine state, so it is unit-tested.
 *   Aim beats everything (and blocks sprint); aiming while crouched stays crouched.
 *   Sprint needs mostly-forward input and stands up from a crouch.
 *   Otherwise crouch if crouched, else jog.
 */
class OPENCITYCORE_API FOCLocomotionRules
{
public:
	static FOCLocomotionDecision Resolve(const FOCLocomotionInput& Input, const FOCLocomotionSettings& Settings);
	static float SpeedFor(EOCGait Gait, const FOCLocomotionSettings& Settings);
};
