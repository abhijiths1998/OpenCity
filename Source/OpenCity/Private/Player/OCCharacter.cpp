#include "Player/OCCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "OCLog.h"
#include "Player/OCCameraRigComponent.h"
#include "Player/OCInteractionComponent.h"
#include "Player/OCInputConfig.h"

AOCCharacter::AOCCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false; // event-driven; see UpdateLocomotion

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	// Mannequin import orientation: feet at capsule bottom, facing +X.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f), FRotator(0.f, -90.f, 0.f));

	// Explore mode: character turns toward movement, camera orbits freely.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 540.f, 0.f);
	Move->JumpZVelocity = 600.f;
	Move->AirControl = 0.35f;
	Move->MaxWalkSpeed = LocomotionSettings.JogSpeed;
	Move->MaxWalkSpeedCrouched = LocomotionSettings.CrouchSpeed;
	Move->MinAnalogWalkSpeed = 20.f;
	Move->BrakingDecelerationWalking = 2000.f;
	Move->GetNavAgentPropertiesRef().bCanCrouch = true;
	Move->SetCrouchedHalfHeight(60.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true; // keeps the camera out of walls
	CameraBoom->ProbeSize = 12.f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 18.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	CameraRig = CreateDefaultSubobject<UOCCameraRigComponent>(TEXT("CameraRig"));
	Interaction = CreateDefaultSubobject<UOCInteractionComponent>(TEXT("Interaction"));
}

void AOCCharacter::ApplyArt()
{
	if (BodyMesh && GetMesh()->GetSkeletalMeshAsset() != BodyMesh)
	{
		GetMesh()->SetSkeletalMeshAsset(BodyMesh);
	}
	if (BodyAnimClass && GetMesh()->GetAnimClass() != BodyAnimClass)
	{
		GetMesh()->SetAnimInstanceClass(BodyAnimClass);
	}
}

void AOCCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyArt(); // so the editor viewport shows the mesh too
}

void AOCCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyArt();
	GetCharacterMovement()->MaxWalkSpeedCrouched = LocomotionSettings.CrouchSpeed;
	CameraRig->Initialize(CameraBoom, FollowCamera);
	UpdateLocomotion();
}

void AOCCharacter::AddMappingContext(const UInputMappingContext* Context, int32 Priority) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Context || !PC || !PC->GetLocalPlayer())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Subsystem->AddMappingContext(Context, Priority);
	}
}

void AOCCharacter::RemoveMappingContext(const UInputMappingContext* Context) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Context || !PC || !PC->GetLocalPlayer())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Subsystem->RemoveMappingContext(Context);
	}
}

void AOCCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (InputConfig)
	{
		AddMappingContext(InputConfig->OnFootContext, InputConfig->OnFootPriority);
		if (Intent.bWantsAim)
		{
			AddMappingContext(InputConfig->AimingContext, InputConfig->AimingPriority);
		}
		UE_LOG(LogOpenCity, Log, TEXT("%s: possessed by %s, on-foot input context %s"), *GetName(),
			*GetNameSafe(GetController()), *GetNameSafe(InputConfig->OnFootContext));
	}
}

void AOCCharacter::UnPossessed()
{
	if (InputConfig)
	{
		RemoveMappingContext(InputConfig->AimingContext);
		RemoveMappingContext(InputConfig->OnFootContext);
	}
	// Don't keep running or aiming without a driver.
	Intent.Reset();
	UpdateLocomotion();
	Super::UnPossessed();
}

void AOCCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogOpenCity, Error, TEXT("%s: needs an EnhancedInputComponent (check DefaultInput.ini)."), *GetName());
		return;
	}
	if (!InputConfig)
	{
		UE_LOG(LogOpenCity, Error, TEXT("%s: InputConfig is not set (BP_OCPlayerCharacter)."), *GetName());
		return;
	}

	const UOCInputConfig& C = *InputConfig;
	if (C.Move)
	{
		Input->BindAction(C.Move, ETriggerEvent::Triggered, this, &AOCCharacter::HandleMove);
		Input->BindAction(C.Move, ETriggerEvent::Completed, this, &AOCCharacter::HandleMoveStopped);
	}
	if (C.Look)
	{
		Input->BindAction(C.Look, ETriggerEvent::Triggered, this, &AOCCharacter::HandleLook);
	}
	if (C.Jump)
	{
		Input->BindAction(C.Jump, ETriggerEvent::Started, this, &AOCCharacter::HandleJumpStarted);
		Input->BindAction(C.Jump, ETriggerEvent::Completed, this, &AOCCharacter::HandleJumpStopped);
	}
	if (C.Sprint)
	{
		Input->BindAction(C.Sprint, ETriggerEvent::Started, this, &AOCCharacter::HandleSprintStarted);
		Input->BindAction(C.Sprint, ETriggerEvent::Completed, this, &AOCCharacter::HandleSprintStopped);
	}
	if (C.SprintToggle)
	{
		Input->BindAction(C.SprintToggle, ETriggerEvent::Started, this, &AOCCharacter::HandleSprintToggle);
	}
	if (C.Crouch)
	{
		Input->BindAction(C.Crouch, ETriggerEvent::Started, this, &AOCCharacter::HandleCrouch);
	}
	if (C.Aim)
	{
		Input->BindAction(C.Aim, ETriggerEvent::Started, this, &AOCCharacter::HandleAimStarted);
		Input->BindAction(C.Aim, ETriggerEvent::Completed, this, &AOCCharacter::HandleAimStopped);
	}
	if (C.SwapShoulder)
	{
		Input->BindAction(C.SwapShoulder, ETriggerEvent::Started, this, &AOCCharacter::HandleSwapShoulder);
	}
	if (C.Interact)
	{
		Input->BindAction(C.Interact, ETriggerEvent::Started, this, &AOCCharacter::HandleInteract);
	}
}

void AOCCharacter::UpdateLocomotion()
{
	const FOCLocomotionDecision D = FOCLocomotionRules::Resolve(Intent.ToInput(bIsCrouched), LocomotionSettings);
	if (D.bShouldUncrouch)
	{
		// Stand-up happens on the next movement update (or never, under a low roof);
		// OnEndCrouch re-runs this and the gait becomes Sprint then.
		UnCrouch();
	}

	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (D.Gait != EOCGait::Crouch)
	{
		Move->MaxWalkSpeed = D.MaxSpeed; // crouched speed comes from MaxWalkSpeedCrouched
	}

	// Aiming faces the camera direction (strafe); otherwise turn toward movement.
	bUseControllerRotationYaw = D.bFaceCamera;
	Move->bOrientRotationToMovement = !D.bFaceCamera;

	Gait = D.Gait;
	CameraRig->SetState(D.Gait, D.bAiming);
}

void AOCCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	UpdateLocomotion();
}

void AOCCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	UpdateLocomotion();
}

void AOCCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	UpdateLocomotion();
	if (Intent.ConsumeBufferedJump(GetWorld()->GetTimeSeconds(), LocomotionSettings.JumpAfterStandUpWindow))
	{
		Jump();
	}
}

void AOCCharacter::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Move = Value.Get<FVector2D>();
	Intent.SetMoveInput(Move);
	if (Controller)
	{
		const FRotator YawOnly(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FRotationMatrix M(YawOnly);
		AddMovementInput(M.GetUnitAxis(EAxis::X), Move.Y);
		AddMovementInput(M.GetUnitAxis(EAxis::Y), Move.X);
	}
	UpdateLocomotion();
}

void AOCCharacter::HandleMoveStopped(const FInputActionValue& Value)
{
	Intent.SetMoveInput(FVector2D::ZeroVector);
	UpdateLocomotion();
}

void AOCCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Look = Value.Get<FVector2D>();
	AddControllerYawInput(Look.X);
	AddControllerPitchInput(Look.Y);
}

void AOCCharacter::HandleJumpStarted(const FInputActionValue& Value)
{
	if (Intent.PressJump(bIsCrouched, GetWorld()->GetTimeSeconds()) == EOCJumpRequest::StandUpThenJump)
	{
		UnCrouch(); // the buffered jump fires in OnEndCrouch
		return;
	}
	Jump();
}

void AOCCharacter::HandleJumpStopped(const FInputActionValue& Value)
{
	StopJumping();
}

void AOCCharacter::HandleSprintStarted(const FInputActionValue& Value)
{
	Intent.SetSprintHeld(true);
	UpdateLocomotion();
}

void AOCCharacter::HandleSprintStopped(const FInputActionValue& Value)
{
	Intent.SetSprintHeld(false);
	UpdateLocomotion();
}

void AOCCharacter::HandleSprintToggle(const FInputActionValue& Value)
{
	Intent.PressSprintToggle();
	UpdateLocomotion();
}

void AOCCharacter::HandleCrouch(const FInputActionValue& Value)
{
	switch (Intent.PressCrouch(bIsCrouched, GetCharacterMovement()->IsFalling()))
	{
	case EOCCrouchRequest::Crouch:   Crouch(); break;
	case EOCCrouchRequest::Uncrouch: UnCrouch(); break;
	case EOCCrouchRequest::None:     break;
	}
	UpdateLocomotion();
}

void AOCCharacter::HandleAimStarted(const FInputActionValue& Value)
{
	Intent.SetAim(true);
	if (InputConfig)
	{
		AddMappingContext(InputConfig->AimingContext, InputConfig->AimingPriority);
	}
	UpdateLocomotion();
}

void AOCCharacter::HandleAimStopped(const FInputActionValue& Value)
{
	Intent.SetAim(false);
	if (InputConfig)
	{
		RemoveMappingContext(InputConfig->AimingContext);
	}
	UpdateLocomotion();
}

void AOCCharacter::HandleSwapShoulder(const FInputActionValue& Value)
{
	CameraRig->SwapShoulder();
}

void AOCCharacter::HandleInteract(const FInputActionValue& Value)
{
	Interaction->TryInteract();
}
