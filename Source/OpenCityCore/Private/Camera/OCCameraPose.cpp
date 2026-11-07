#include "Camera/OCCameraPose.h"

FOCCameraPose FOCCameraPose::InterpTo(const FOCCameraPose& Target, float DeltaSeconds, float Speed) const
{
	if (Speed <= 0.f)
	{
		return Target;
	}
	FOCCameraPose Out;
	Out.ArmLength = FMath::FInterpTo(ArmLength, Target.ArmLength, DeltaSeconds, Speed);
	Out.SocketOffset = FMath::VInterpTo(SocketOffset, Target.SocketOffset, DeltaSeconds, Speed);
	Out.FieldOfView = FMath::FInterpTo(FieldOfView, Target.FieldOfView, DeltaSeconds, Speed);
	return Out;
}

FOCCameraPose FOCCameraPose::Mirrored() const
{
	FOCCameraPose Out = *this;
	Out.SocketOffset.Y = -SocketOffset.Y;
	return Out;
}

bool FOCCameraPose::Equals(const FOCCameraPose& Other, float Tolerance) const
{
	return FMath::IsNearlyEqual(ArmLength, Other.ArmLength, Tolerance)
		&& SocketOffset.Equals(Other.SocketOffset, Tolerance)
		&& FMath::IsNearlyEqual(FieldOfView, Other.FieldOfView, Tolerance);
}
