#include "Player/OCLocomotion.h"

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

	const bool bHasMoveInput = !Input.MoveInput.IsNearlyZero(0.1);
	const bool bForwardEnough = Input.MoveInput.Y >= Settings.SprintForwardThreshold;

	if (Input.bWantsAim)
	{
		Out.Gait = Input.bIsCrouched ? EOCGait::Crouch : EOCGait::Aim;
	}
	else if (Input.bWantsSprint && bForwardEnough)
	{
		Out.Gait = EOCGait::Sprint;
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

	Out.bShouldClearSprintToggle = Input.bWantsSprint && !bHasMoveInput;
	Out.MaxSpeed = SpeedFor(Out.Gait, Settings);
	return Out;
}
