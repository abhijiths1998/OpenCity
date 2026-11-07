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

	/** A jump pressed while crouched fires when standing up completes within this time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0", Units = "s"))
	float JumpAfterStandUpWindow = 0.3f;
};

/** Snapshot passed to the rules. MoveInput: X = right, Y = forward, each -1..1. */
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
	/** What the character is actually doing now (a pending stand-up still counts as crouched). */
	EOCGait Gait = EOCGait::Jog;
	float MaxSpeed = 0.f;
	/** Aiming, including aiming while crouched: camera uses the aim pose. */
	bool bAiming = false;
	/** Character faces the camera direction (strafe) instead of its movement direction. */
	bool bFaceCamera = false;
	/** Sprint was requested from a crouch: stand up; gait becomes Sprint once standing. */
	bool bShouldUncrouch = false;
};

/**
 * Locomotion rules. Pure logic with no engine state, so it is unit-tested.
 *   Aim beats everything and blocks sprint; aiming while crouched stays crouched.
 *   Sprint needs mostly-forward input; from a crouch it first requests a stand-up.
 *   Otherwise crouch if crouched, else jog.
 */
class OPENCITYCORE_API FOCLocomotionRules
{
public:
	static FOCLocomotionDecision Resolve(const FOCLocomotionInput& Input, const FOCLocomotionSettings& Settings);
	static float SpeedFor(EOCGait Gait, const FOCLocomotionSettings& Settings);
};

enum class EOCCrouchRequest : uint8 { None, Crouch, Uncrouch };
enum class EOCJumpRequest : uint8 { Jump, StandUpThenJump };

/**
 * The player's movement intent, updated by input events. Holds all input policy
 * (toggles, cancels, buffered jump) so the character only forwards events.
 */
struct OPENCITYCORE_API FOCLocomotionIntent
{
	/** Stopping all movement ends a toggled (gamepad) sprint. */
	void SetMoveInput(const FVector2D& NewMoveInput);
	void SetSprintHeld(bool bHeld) { bSprintHeld = bHeld; }
	void PressSprintToggle() { bSprintToggled = !bSprintToggled; }
	void SetAim(bool bAim) { bWantsAim = bAim; }

	/** Crouch toggles; crouching cancels any sprint (held or toggled) and a buffered jump. */
	EOCCrouchRequest PressCrouch(bool bIsCrouched, bool bIsFalling);

	/** Jumping from a crouch stands up first and buffers the jump. */
	EOCJumpRequest PressJump(bool bIsCrouched, double NowSeconds);

	/** True once if a buffered jump is still within the window. */
	bool ConsumeBufferedJump(double NowSeconds, float WindowSeconds);

	void Reset();

	FOCLocomotionInput ToInput(bool bIsCrouched) const;

	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bSprintHeld = false;
	bool bSprintToggled = false;
	bool bWantsAim = false;

private:
	double BufferedJumpTime = -1.0;
};
