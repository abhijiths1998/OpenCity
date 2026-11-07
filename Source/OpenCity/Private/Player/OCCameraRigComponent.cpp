#include "Player/OCCameraRigComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

UOCCameraRigComponent::UOCCameraRigComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // only while blending
	PrimaryComponentTick.TickGroup = TG_PostPhysics;   // after movement, before the camera is read

	ExplorePose.ArmLength = 350.f;
	ExplorePose.SocketOffset = FVector(0.f, 50.f, 60.f);
	ExplorePose.FieldOfView = 90.f;

	SprintPose.ArmLength = 400.f;
	SprintPose.SocketOffset = FVector(0.f, 40.f, 55.f);
	SprintPose.FieldOfView = 96.f;

	CrouchPose.ArmLength = 300.f;
	CrouchPose.SocketOffset = FVector(0.f, 55.f, 30.f);
	CrouchPose.FieldOfView = 88.f;

	AimPose.ArmLength = 150.f;
	AimPose.SocketOffset = FVector(0.f, 65.f, 55.f);
	AimPose.FieldOfView = 70.f;
}

void UOCCameraRigComponent::Initialize(USpringArmComponent* InBoom, UCameraComponent* InCamera)
{
	Boom = InBoom;
	Camera = InCamera;
	Current = ExplorePose;
	Target = ExplorePose;
	ApplyPose(Current);
}

const FOCCameraPose& UOCCameraRigComponent::PoseFor(EOCGait Gait) const
{
	switch (Gait)
	{
	case EOCGait::Sprint: return SprintPose;
	case EOCGait::Crouch: return CrouchPose;
	case EOCGait::Aim:    return AimPose;
	case EOCGait::Jog:
	default:              return ExplorePose;
	}
}

void UOCCameraRigComponent::SetGait(EOCGait Gait)
{
	CurrentGait = Gait;
	StartBlend();
}

void UOCCameraRigComponent::SwapShoulder()
{
	bLeftShoulder = !bLeftShoulder;
	StartBlend();
}

void UOCCameraRigComponent::StartBlend()
{
	const FOCCameraPose& Base = PoseFor(CurrentGait);
	Target = bLeftShoulder ? Base.Mirrored() : Base;
	if (!Current.Equals(Target))
	{
		SetComponentTickEnabled(true);
	}
}

void UOCCameraRigComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float Speed = CurrentGait == EOCGait::Aim ? AimBlendSpeed : BlendSpeed;
	Current = Current.InterpTo(Target, DeltaTime, Speed);
	if (Current.Equals(Target))
	{
		Current = Target;
		SetComponentTickEnabled(false);
	}
	ApplyPose(Current);
}

void UOCCameraRigComponent::ApplyPose(const FOCCameraPose& Pose)
{
	if (USpringArmComponent* B = Boom.Get())
	{
		B->TargetArmLength = Pose.ArmLength;
		B->SocketOffset = Pose.SocketOffset;
	}
	if (UCameraComponent* C = Camera.Get())
	{
		C->SetFieldOfView(Pose.FieldOfView);
	}
}
