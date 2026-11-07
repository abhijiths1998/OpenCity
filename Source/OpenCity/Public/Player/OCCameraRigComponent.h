#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/OCCameraPose.h"
#include "Player/OCLocomotion.h"
#include "OCCameraRigComponent.generated.h"

class UCameraComponent;
class USpringArmComponent;

/**
 * Third-person camera framing: blends the spring arm and camera between poses
 * (explore, sprint, crouch, aim) and handles the shoulder side.
 * Ticks only while a blend is in progress.
 */
UCLASS(ClassGroup = (OpenCity), meta = (BlueprintSpawnableComponent))
class OPENCITY_API UOCCameraRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOCCameraRigComponent();

	/** Must be called once by the owner before use. */
	void Initialize(USpringArmComponent* InBoom, UCameraComponent* InCamera);

	/** Pick the pose for this gait and start blending to it. */
	void SetGait(EOCGait Gait);

	/** Swap between right and left shoulder. */
	void SwapShoulder();

	bool IsLeftShoulder() const { return bLeftShoulder; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Camera")
	FOCCameraPose ExplorePose;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FOCCameraPose SprintPose;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FOCCameraPose CrouchPose;

	UPROPERTY(EditAnywhere, Category = "Camera")
	FOCCameraPose AimPose;

	/** Higher is snappier. */
	UPROPERTY(EditAnywhere, Category = "Camera", meta = (ClampMin = "0"))
	float BlendSpeed = 8.f;

	UPROPERTY(EditAnywhere, Category = "Camera", meta = (ClampMin = "0"))
	float AimBlendSpeed = 14.f;

private:
	const FOCCameraPose& PoseFor(EOCGait Gait) const;
	void StartBlend();
	void ApplyPose(const FOCCameraPose& Pose);

	TWeakObjectPtr<USpringArmComponent> Boom;
	TWeakObjectPtr<UCameraComponent> Camera;
	FOCCameraPose Current;
	FOCCameraPose Target;
	EOCGait CurrentGait = EOCGait::Jog;
	bool bLeftShoulder = false;
};
