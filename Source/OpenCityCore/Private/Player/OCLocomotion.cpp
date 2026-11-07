#include "Player/OCLocomotion.h"

namespace
{
	constexpr double MoveDeadZone = 0.1;
}

float FOCLocomotionRules::SpeedFor(EOCGait Gait, const FOCLocomotionSettings& Settings)
{
	switch (Gait)
	{
	case EOCGait::Sprint: return Settings.SprintSpeed;
	case EOCGait::Crouch: return Settings.CrouchSpeed;
	case EOCGait::Aim:    return Settings.AimSpeed;
	case EOCGait::Jog:
	default:              return Settings.JogSpeed;
	}
}

FOCLocomotionDecision FOCLocomotionRules::Resolve(const FOCLocomotionInput& Input, const FOCLocomotionSettings& Settings)
{
	FOCLocomotionDecision Out;
	const bool bForwardEnough = Input.MoveInput.Y >= Settings.SprintForwardThreshold;

	if (Input.bWantsAim)
	{
		Out.Gait = Input.bIsCrouched ? EOCGait::Crouch : EOCGait::Aim;
		Out.bAiming = true;
		Out.bFaceCamera = true;
	}
	else if (Input.bWantsSprint && bForwardEnough)
	{
		// From a crouch, stay crouched until the stand-up actually happens (it can be blocked).
		Out.Gait = Input.bIsCrouched ? EOCGait::Crouch : EOCGait::Sprint;
		Out.bShouldUncrouch = Input.bIsCrouched;
	}
	else if (Input.bIsCrouched)
	{
		Out.Gait = EOCGait::Crouch;
	}
	else
	{
		Out.Gait = EOCGait::Jog;
	}

	Out.MaxSpeed = SpeedFor(Out.Gait, Settings);
	return Out;
}

void FOCLocomotionIntent::SetMoveInput(const FVector2D& NewMoveInput)
{
	const bool bWasMoving = !MoveInput.IsNearlyZero(MoveDeadZone);
	MoveInput = NewMoveInput;
	if (bWasMoving && MoveInput.IsNearlyZero(MoveDeadZone))
	{
		bSprintToggled = false;
	}
}

EOCCrouchRequest FOCLocomotionIntent::PressCrouch(bool bIsCrouched, bool bIsFalling)
{
	BufferedJumpTime = -1.0;
	if (bIsCrouched)
	{
		return EOCCrouchRequest::Uncrouch;
	}
	if (bIsFalling)
	{
		return EOCCrouchRequest::None;
	}
	bSprintHeld = false;
	bSprintToggled = false;
	return EOCCrouchRequest::Crouch;
}

EOCJumpRequest FOCLocomotionIntent::PressJump(bool bIsCrouched, double NowSeconds)
{
	if (bIsCrouched)
	{
		BufferedJumpTime = NowSeconds;
		return EOCJumpRequest::StandUpThenJump;
	}
	return EOCJumpRequest::Jump;
}

bool FOCLocomotionIntent::ConsumeBufferedJump(double NowSeconds, float WindowSeconds)
{
	const bool bValid = BufferedJumpTime >= 0.0 && NowSeconds - BufferedJumpTime <= WindowSeconds;
	BufferedJumpTime = -1.0;
	return bValid;
}

void FOCLocomotionIntent::Reset()
{
	*this = FOCLocomotionIntent();
}

FOCLocomotionInput FOCLocomotionIntent::ToInput(bool bIsCrouched) const
{
	FOCLocomotionInput In;
	In.MoveInput = MoveInput;
	In.bWantsSprint = bSprintHeld || bSprintToggled;
	In.bWantsAim = bWantsAim;
	In.bIsCrouched = bIsCrouched;
	return In;
}
