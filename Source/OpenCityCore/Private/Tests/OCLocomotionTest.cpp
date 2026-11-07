#include "Misc/AutomationTest.h"
#include "Player/OCLocomotion.h"
#include "Camera/OCCameraPose.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCLocomotionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	const FVector2D MoveFwd(0.0, 1.0), MoveBack(0.0, -1.0), MoveRight(1.0, 0.0), MoveDiag(0.7, 0.7), MoveNone(0.0, 0.0);

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

	TestEqual(TEXT("Default is jog"), FOCLocomotionRules::Resolve(Make(MoveFwd), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("Sprint forward"), FOCLocomotionRules::Resolve(Make(MoveFwd, true), S).Gait, EOCGait::Sprint);
	TestEqual(TEXT("Sprint forward-diagonal"), FOCLocomotionRules::Resolve(Make(MoveDiag, true), S).Gait, EOCGait::Sprint);
	TestEqual(TEXT("No sprint backwards"), FOCLocomotionRules::Resolve(Make(MoveBack, true), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("No sprint sideways"), FOCLocomotionRules::Resolve(Make(MoveRight, true), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("No sprint standing still"), FOCLocomotionRules::Resolve(Make(MoveNone, true), S).Gait, EOCGait::Jog);
	TestEqual(TEXT("Crouched"), FOCLocomotionRules::Resolve(Make(MoveFwd, false, false, true), S).Gait, EOCGait::Crouch);

	const FOCLocomotionDecision Aim = FOCLocomotionRules::Resolve(Make(MoveFwd, true, true), S);
	TestEqual(TEXT("Aim blocks sprint"), Aim.Gait, EOCGait::Aim);
	TestTrue(TEXT("Aim uses aim camera"), Aim.bAiming);
	TestTrue(TEXT("Aim faces camera"), Aim.bFaceCamera);

	const FOCLocomotionDecision CrouchAim = FOCLocomotionRules::Resolve(Make(MoveFwd, false, true, true), S);
	TestEqual(TEXT("Aim while crouched stays crouched"), CrouchAim.Gait, EOCGait::Crouch);
	TestTrue(TEXT("Aim while crouched still uses aim camera"), CrouchAim.bAiming);
	TestTrue(TEXT("Aim while crouched faces camera"), CrouchAim.bFaceCamera);

	const FOCLocomotionDecision Jog = FOCLocomotionRules::Resolve(Make(MoveFwd), S);
	TestFalse(TEXT("Jog does not aim"), Jog.bAiming);
	TestFalse(TEXT("Jog faces movement"), Jog.bFaceCamera);

	// Sprint from a crouch: stays crouched (truthfully) until the stand-up happens.
	const FOCLocomotionDecision FromCrouch = FOCLocomotionRules::Resolve(Make(MoveFwd, true, false, true), S);
	TestEqual(TEXT("Sprint from crouch is still crouched"), FromCrouch.Gait, EOCGait::Crouch);
	TestEqual(TEXT("Sprint from crouch keeps crouch speed"), FromCrouch.MaxSpeed, S.CrouchSpeed);
	TestTrue(TEXT("Sprint from crouch requests stand-up"), FromCrouch.bShouldUncrouch);
	TestFalse(TEXT("Plain sprint does not request stand-up"), FOCLocomotionRules::Resolve(Make(MoveFwd, true), S).bShouldUncrouch);
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

	TestEqual(TEXT("Jog speed"), FOCLocomotionRules::Resolve(Make(MoveFwd), S).MaxSpeed, 400.f);
	TestEqual(TEXT("Sprint speed"), FOCLocomotionRules::Resolve(Make(MoveFwd, true), S).MaxSpeed, 700.f);
	TestEqual(TEXT("Crouch speed"), FOCLocomotionRules::Resolve(Make(MoveFwd, false, false, true), S).MaxSpeed, 150.f);
	TestEqual(TEXT("Aim speed"), FOCLocomotionRules::Resolve(Make(MoveFwd, false, true), S).MaxSpeed, 250.f);

	S.SprintForwardThreshold = 0.9f;
	TestEqual(TEXT("Threshold respected"), FOCLocomotionRules::Resolve(Make(MoveDiag, true), S).Gait, EOCGait::Jog);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLocomotionIntentSprintTest, "OpenCity.Core.Locomotion.Intent.SprintToggle", OCLocomotionTest::Flags)

bool FOCLocomotionIntentSprintTest::RunTest(const FString& Parameters)
{
	using namespace OCLocomotionTest;
	const FOCLocomotionSettings S;

	// Toggle pressed while standing still survives until the player moves, then sprints.
	FOCLocomotionIntent I;
	I.PressSprintToggle();
	TestTrue(TEXT("Toggle armed while idle"), I.bSprintToggled);
	I.SetMoveInput(MoveFwd);
	TestEqual(TEXT("Armed toggle sprints once moving"), FOCLocomotionRules::Resolve(I.ToInput(false), S).Gait, EOCGait::Sprint);

	// Stopping ends the toggle.
	I.SetMoveInput(MoveNone);
	TestFalse(TEXT("Stopping clears toggle"), I.bSprintToggled);
	I.SetMoveInput(MoveFwd);
	TestEqual(TEXT("After stop, back to jog"), FOCLocomotionRules::Resolve(I.ToInput(false), S).Gait, EOCGait::Jog);

	// Small stick wobble around zero while idle does not clear an armed toggle.
	FOCLocomotionIntent J;
	J.PressSprintToggle();
	J.SetMoveInput(FVector2D(0.05, 0.0));
	J.SetMoveInput(MoveNone);
	TestTrue(TEXT("Dead-zone wobble keeps toggle"), J.bSprintToggled);

	// Pressing again turns it off.
	J.PressSprintToggle();
	TestFalse(TEXT("Second press turns toggle off"), J.bSprintToggled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLocomotionIntentCrouchTest, "OpenCity.Core.Locomotion.Intent.Crouch", OCLocomotionTest::Flags)

bool FOCLocomotionIntentCrouchTest::RunTest(const FString& Parameters)
{
	using namespace OCLocomotionTest;
	const FOCLocomotionSettings S;

	// Shift + W held, then C: crouch must win and must not bounce back up.
	FOCLocomotionIntent I;
	I.SetMoveInput(MoveFwd);
	I.SetSprintHeld(true);
	TestEqual(TEXT("Crouch while sprinting requests crouch"), I.PressCrouch(false, false), EOCCrouchRequest::Crouch);
	const FOCLocomotionDecision AfterCrouch = FOCLocomotionRules::Resolve(I.ToInput(true), S);
	TestEqual(TEXT("Crouched after crouch press"), AfterCrouch.Gait, EOCGait::Crouch);
	TestFalse(TEXT("Held sprint no longer stands you up"), AfterCrouch.bShouldUncrouch);

	// Re-pressing sprint from the crouch is a deliberate stand-up.
	I.SetSprintHeld(true);
	TestTrue(TEXT("Re-pressed sprint requests stand-up"), FOCLocomotionRules::Resolve(I.ToInput(true), S).bShouldUncrouch);

	// Toggle-sprint is also cancelled by crouching.
	FOCLocomotionIntent T;
	T.SetMoveInput(MoveFwd);
	T.PressSprintToggle();
	T.PressCrouch(false, false);
	TestFalse(TEXT("Crouch cancels toggled sprint"), T.bSprintToggled);

	TestEqual(TEXT("Crouch while crouched stands up"), T.PressCrouch(true, false), EOCCrouchRequest::Uncrouch);
	TestEqual(TEXT("No crouch in the air"), T.PressCrouch(false, true), EOCCrouchRequest::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCLocomotionIntentJumpTest, "OpenCity.Core.Locomotion.Intent.Jump", OCLocomotionTest::Flags)

bool FOCLocomotionIntentJumpTest::RunTest(const FString& Parameters)
{
	using namespace OCLocomotionTest;
	const float Window = 0.3f;

	FOCLocomotionIntent I;
	TestEqual(TEXT("Standing jump"), I.PressJump(false, 10.0), EOCJumpRequest::Jump);
	TestFalse(TEXT("Nothing buffered for a standing jump"), I.ConsumeBufferedJump(10.1, Window));

	TestEqual(TEXT("Crouched jump stands up first"), I.PressJump(true, 20.0), EOCJumpRequest::StandUpThenJump);
	TestTrue(TEXT("Buffered jump fires on quick stand-up"), I.ConsumeBufferedJump(20.1, Window));
	TestFalse(TEXT("Buffered jump fires only once"), I.ConsumeBufferedJump(20.15, Window));

	// Stand-up blocked by a low roof, completes seconds later: no surprise jump.
	I.PressJump(true, 30.0);
	TestFalse(TEXT("Late stand-up does not jump"), I.ConsumeBufferedJump(33.0, Window));

	// Crouch pressed after a buffered jump cancels it.
	I.PressJump(true, 40.0);
	I.PressCrouch(true, false);
	TestFalse(TEXT("Crouch press cancels buffered jump"), I.ConsumeBufferedJump(40.1, Window));
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

	FOCCameraPose P = A;
	for (int32 i = 0; i < 60; ++i)
	{
		P = P.InterpTo(B, 1.f / 60.f, 10.f);
	}
	TestTrue(TEXT("Converges within a second"), P.Equals(B, 0.5f));

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
