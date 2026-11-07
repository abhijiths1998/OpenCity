#include "Misc/AutomationTest.h"
#include "Player/OCLocomotion.h"
#include "Camera/OCCameraPose.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCLocomotionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FOCLocomotionInput Make(FVector2D Move, bool bSprint = false, bool bAim = false, bool bCrouched = false)
	{
		FOCLocomotionInput In;
		In.MoveInput = Move;
		In.bWantsSprint = bSprint;
		In.bWantsAim = bAim;
		In.bIsCrouched = bCrouched;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLocomotionGaitTest, "OpenCity.Core.Locomotion.Gait", OCLocomotionTest::Flags)

bool FOCLocomotionGaitTest::RunTest(const FString& Parameters)
{
	using namespace OCLocomotionTest;
	const FOCLocomotionSettings S;
	const FVector2D Forward(0.0, 1.0), Back(0.0, -1.0), Right(1.0, 0.0), Diagonal(0.7, 0.7), None(0.0, 0.0);

	TestEqual(TEXT("Default is jog"), FOCLocomotionRules::Resolve(Make(Forward), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("Sprint forward"), FOCLocomotionRules::Resolve(Make(Forward, true), S).Gait, EOCGait::Sprint);
	TestEqual(TEXT("Sprint forward-diagonal"), FOCLocomotionRules::Resolve(Make(Diagonal, true), S).Gait, EOCGait::Sprint);
	TestEqual(TEXT("No sprint backwards"), FOCLocomotionRules::Resolve(Make(Back, true), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("No sprint sideways"), FOCLocomotionRules::Resolve(Make(Right, true), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("No sprint standing still"), FOCLocomotionRules::Resolve(Make(None, true), S).Gait, EOCGait::Jog);

	TestEqual(TEXT("Aim blocks sprint"), FOCLocomotionRules::Resolve(Make(Forward, true, true), S).Gait, EOCGait::Aim);
	TestEqual(TEXT("Aim while crouched stays crouched"), FOCLocomotionRules::Resolve(Make(Forward, false, true, true), S).Gait, EOCGait::Crouch);
	TestEqual(TEXT("Crouched"), FOCLocomotionRules::Resolve(Make(Forward, false, false, true), S).Gait, EOCGait::Crouch);

	const FOCLocomotionDecision SprintFromCrouch = FOCLocomotionRules::Resolve(Make(Forward, true, false, true), S);
	TestEqual(TEXT("Sprint from crouch sprints"), SprintFromCrouch.Gait, EOCGait::Sprint);
	TestTrue(TEXT("Sprint from crouch stands up"), SprintFromCrouch.bShouldUncrouch);
	TestFalse(TEXT("Plain sprint does not request uncrouch"), FOCLocomotionRules::Resolve(Make(Forward, true), S).bShouldUncrouch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLocomotionSpeedTest, "OpenCity.Core.Locomotion.Speed", OCLocomotionTest::Flags)

bool FOCLocomotionSpeedTest::RunTest(const FString& Parameters)
{
	using namespace OCLocomotionTest;
	FOCLocomotionSettings S;
	S.JogSpeed = 400.f;
	S.SprintSpeed = 700.f;
	S.CrouchSpeed = 150.f;
	S.AimSpeed = 250.f;

	TestEqual(TEXT("Jog speed"), FOCLocomotionRules::Resolve(Make({0.0, 1.0}), S).MaxSpeed, 400.f);
	TestEqual(TEXT("Sprint speed"), FOCLocomotionRules::Resolve(Make({0.0, 1.0}, true), S).MaxSpeed, 700.f);
	TestEqual(TEXT("Crouch speed"), FOCLocomotionRules::Resolve(Make({0.0, 1.0}, false, false, true), S).MaxSpeed, 150.f);
	TestEqual(TEXT("Aim speed"), FOCLocomotionRules::Resolve(Make({0.0, 1.0}, false, true), S).MaxSpeed, 250.f);

	S.SprintForwardThreshold = 0.9f;
	TestEqual(TEXT("Threshold respected"), FOCLocomotionRules::Resolve(Make({0.7, 0.7}, true), S).Gait, EOCGait::Jog);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLocomotionSprintToggleTest, "OpenCity.Core.Locomotion.SprintToggle", OCLocomotionTest::Flags)

bool FOCLocomotionSprintToggleTest::RunTest(const FString& Parameters)
{
	using namespace OCLocomotionTest;
	const FOCLocomotionSettings S;
	TestTrue(TEXT("Stopping clears toggle"), FOCLocomotionRules::Resolve(Make({0.0, 0.0}, true), S).bShouldClearSprintToggle);
	TestFalse(TEXT("Moving keeps toggle"), FOCLocomotionRules::Resolve(Make({0.0, 1.0}, true), S).bShouldClearSprintToggle);
	TestFalse(TEXT("Moving backwards keeps toggle"), FOCLocomotionRules::Resolve(Make({0.0, -1.0}, true), S).bShouldClearSprintToggle);
	TestFalse(TEXT("No toggle, nothing to clear"), FOCLocomotionRules::Resolve(Make({0.0, 0.0}), S).bShouldClearSprintToggle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCCameraPoseTest, "OpenCity.Core.Camera.Pose", OCLocomotionTest::Flags)

bool FOCCameraPoseTest::RunTest(const FString& Parameters)
{
	FOCCameraPose A;
	A.ArmLength = 350.f;
	A.SocketOffset = FVector(0.f, 50.f, 60.f);
	A.FieldOfView = 90.f;

	FOCCameraPose B;
	B.ArmLength = 150.f;
	B.SocketOffset = FVector(0.f, 70.f, 50.f);
	B.FieldOfView = 70.f;

	TestTrue(TEXT("Zero speed snaps"), A.InterpTo(B, 0.016f, 0.f).Equals(B));

	const FOCCameraPose Step = A.InterpTo(B, 0.016f, 10.f);
	TestTrue(TEXT("Moves toward target arm"), Step.ArmLength < A.ArmLength && Step.ArmLength > B.ArmLength);
	TestTrue(TEXT("Moves toward target FOV"), Step.FieldOfView < A.FieldOfView && Step.FieldOfView > B.FieldOfView);

	// Converges within one simulated second at 60 fps.
	FOCCameraPose P = A;
	for (int32 i = 0; i < 60; ++i)
	{
		P = P.InterpTo(B, 1.f / 60.f, 10.f);
	}
	TestTrue(TEXT("Converges"), P.Equals(B, 0.5f));

	// Frame-rate independence: 30 fps and 120 fps end close together after 0.25 s.
	FOCCameraPose P30 = A, P120 = A;
	for (int32 i = 0; i < 8; ++i) { P30 = P30.InterpTo(B, 1.f / 30.f, 10.f); }
	for (int32 i = 0; i < 30; ++i) { P120 = P120.InterpTo(B, 1.f / 120.f, 10.f); }
	TestTrue(TEXT("Roughly frame-rate independent"), FMath::Abs(P30.ArmLength - P120.ArmLength) < 15.f);

	const FOCCameraPose M = A.Mirrored();
	TestEqual(TEXT("Mirror flips shoulder"), M.SocketOffset.Y, -50.0);
	TestEqual(TEXT("Mirror keeps height"), M.SocketOffset.Z, 60.0);
	TestTrue(TEXT("Mirror twice is identity"), M.Mirrored().Equals(A));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
