#pragma once

#include "CoreMinimal.h"
#include "OCCameraPose.generated.h"

/** One third-person camera framing: boom length, shoulder offset and field of view. */
USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCCameraPose
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0", Units = "cm"))
	float ArmLength = 350.f;

	/** Offset at the camera end of the boom. +Y = right shoulder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector SocketOffset = FVector(0.f, 50.f, 60.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "30", ClampMax = "120", Units = "deg"))
	float FieldOfView = 90.f;

	/** Exponential blend toward Target (frame-rate independent). Speed <= 0 snaps. */
	FOCCameraPose InterpTo(const FOCCameraPose& Target, float DeltaSeconds, float Speed) const;

	/** Same pose with the shoulder offset mirrored (right <-> left). */
	FOCCameraPose Mirrored() const;

	bool Equals(const FOCCameraPose& Other, float Tolerance = 0.1f) const;
};
