#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/OCLocomotion.h"
#include "OCCharacter.generated.h"

class UAnimInstance;
class UCameraComponent;
class UInputMappingContext;
class USkeletalMesh;
class UOCCameraRigComponent;
class UOCInputConfig;
class USpringArmComponent;
struct FInputActionValue;

/**
 * Third-person on-foot player. Kept small: it maps input to movement and wires components.
 * Camera framing lives in UOCCameraRigComponent; gait rules in FOCLocomotionRules (OpenCityCore).
 * Art (mesh, anim class) and tuning are set in the Blueprint subclass BP_OCPlayerCharacter.
 * No Tick: locomotion is re-evaluated only when input or movement state changes.
 */
UCLASS()
class OPENCITY_API AOCCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AOCCharacter(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "OpenCity|Locomotion")
	EOCGait GetGait() const { return Gait; }

	UFUNCTION(BlueprintPure, Category = "OpenCity|Locomotion")
	bool IsAiming() const { return bWantsAim; }

	UFUNCTION(BlueprintPure, Category = "OpenCity|Locomotion")
	bool IsSprinting() const { return Gait == EOCGait::Sprint; }

	/** Last move input, X = right, Y = forward. */
	FVector2D GetMoveInput() const { return MoveInput; }

	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UOCCameraRigComponent> CameraRig;

	UPROPERTY(EditDefaultsOnly, Category = "OpenCity|Input")
	TObjectPtr<UOCInputConfig> InputConfig;

	UPROPERTY(EditDefaultsOnly, Category = "OpenCity|Locomotion")
	FOCLocomotionSettings LocomotionSettings;

	/** Art hookup, set in the Blueprint subclass. */
	UPROPERTY(EditDefaultsOnly, Category = "OpenCity|Art")
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY(EditDefaultsOnly, Category = "OpenCity|Art")
	TSubclassOf<UAnimInstance> BodyAnimClass;

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

private:
	void ApplyArt();
	void AddMappingContext(const UInputMappingContext* Context, int32 Priority) const;
	void RemoveMappingContext(const UInputMappingContext* Context) const;
	void UpdateLocomotion();

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveStopped(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStarted(const FInputActionValue& Value);
	void HandleJumpStopped(const FInputActionValue& Value);
	void HandleSprintStarted(const FInputActionValue& Value);
	void HandleSprintStopped(const FInputActionValue& Value);
	void HandleSprintToggle(const FInputActionValue& Value);
	void HandleCrouch(const FInputActionValue& Value);
	void HandleAimStarted(const FInputActionValue& Value);
	void HandleAimStopped(const FInputActionValue& Value);
	void HandleSwapShoulder(const FInputActionValue& Value);

	FVector2D MoveInput = FVector2D::ZeroVector;
	EOCGait Gait = EOCGait::Jog;
	bool bSprintHeld = false;
	bool bSprintToggled = false;
	bool bWantsAim = false;
};
