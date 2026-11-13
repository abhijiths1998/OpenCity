#include "Vehicles/OCVehicle.h"

#include "Camera/CameraComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "OCLog.h"
#include "TimerManager.h"
#include "Vehicles/OCVehicleComponents.h"
#include "Vehicles/OCVehicleData.h"

#define LOCTEXT_NAMESPACE "OCVehicle"

namespace
{
	constexpr float CmPerSecToKmh = 0.036f;
}

AOCVehicle::AOCVehicle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false; // the Chaos movement component ticks; this actor doesn't need to

	// AWheeledVehiclePawn leaves simulation off (Epic's templates enable it in Blueprint).
	GetMesh()->BodyInstance.bSimulatePhysics = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetMesh());
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 16.f;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ChaseCamera->bUsePawnControlRotation = false;

	CameraRig = CreateDefaultSubobject<UOCVehicleCameraComponent>(TEXT("CameraRig"));
	Damage = CreateDefaultSubobject<UOCVehicleDamageComponent>(TEXT("Damage"));
	Radio = CreateDefaultSubobject<UOCRadioComponent>(TEXT("Radio"));
}

UChaosWheeledVehicleMovementComponent* AOCVehicle::GetWheeledMovement() const
{
	return Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
}

void AOCVehicle::ApplyVehicleData(bool bRebuildPhysics)
{
	if (!VehicleData)
	{
		return;
	}
	USkeletalMeshComponent* M = GetMesh();
	UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement();
	if (!M || !V)
	{
		return;
	}

	V->Mass = VehicleData->Mass;
	V->DragCoefficient = VehicleData->DragCoefficient;
	V->ChassisWidth = VehicleData->ChassisWidth;
	V->ChassisHeight = VehicleData->ChassisHeight;
	V->bEnableCenterOfMassOverride = VehicleData->bOverrideCenterOfMass;
	V->CenterOfMassOverride = VehicleData->CenterOfMassOffset;
	V->WheelSetups = VehicleData->WheelSetups;
	V->EngineSetup = VehicleData->EngineSetup;
	V->TransmissionSetup = VehicleData->TransmissionSetup;
	V->DifferentialSetup = VehicleData->DifferentialSetup;
	V->SteeringSetup = VehicleData->SteeringSetup;
	// S reverses once stopped; while rolling backwards, W brakes. The exit sequence relies on this.
	V->bReverseAsBrake = true;
	V->bThrottleAsBrake = true;

	// Chaos divides by the curve's max value, so an empty curve would break the engine.
	FRichCurve* Curve = V->EngineSetup.TorqueCurve.GetRichCurve();
	Curve->Reset();
	for (const FVector2D& P : VehicleData->TorqueCurvePoints)
	{
		Curve->AddKey(P.X, P.Y);
	}
	if (Curve->GetNumKeys() == 0)
	{
		Curve->AddKey(0.f, 1.f);
		Curve->AddKey(V->EngineSetup.MaxRPM, 1.f);
		UE_LOG(LogOpenCity, Warning, TEXT("%s: %s has no torque curve, using a flat one."), *GetName(), *VehicleData->GetName());
	}
	BaseMaxTorque = V->EngineSetup.MaxTorque;

	// Chaos samples this curve in steps of MaxX/20: fewer than two keys or MaxX <= 0 would
	// loop forever (FVehicleSteeringConfig::FillSteeringSetup), so reject such curves.
	const TArray<FVector2D>& SteerPts = VehicleData->SteeringCurvePoints;
	const bool bValidSteerCurve = SteerPts.Num() >= 2 && SteerPts.Last().X > SteerPts[0].X;
	if (!SteerPts.IsEmpty() && !bValidSteerCurve)
	{
		UE_LOG(LogOpenCity, Warning, TEXT("%s: steering curve needs 2+ points with increasing speed; ignored."), *VehicleData->GetName());
	}
	if (bValidSteerCurve)
	{
		FRichCurve* Steer = V->SteeringSetup.SteeringCurve.GetRichCurve();
		Steer->Reset();
		for (const FVector2D& P : VehicleData->SteeringCurvePoints)
		{
			Steer->AddKey(P.X * 0.621371f, P.Y); // Chaos reads this curve in mph
		}
	}

	if (VehicleData->AnimClass && M->GetAnimClass() != VehicleData->AnimClass)
	{
		M->SetAnimInstanceClass(VehicleData->AnimClass);
	}
	if (VehicleData->Mesh && M->GetSkeletalMeshAsset() != VehicleData->Mesh)
	{
		M->SetSkeletalMeshAsset(VehicleData->Mesh);
	}

	BuildVisualParts();

	// The vehicle physics was created (if at all) before the data was copied: rebuild it.
	// A vehicle spawned at runtime registers its components (creating physics) before any data
	// is copied; Chaos then sees an empty torque curve and switches the engine off for good.
	if (bRebuildPhysics && GetWorld() && GetWorld()->IsGameWorld())
	{
		V->bMechanicalSimEnabled = true;
		V->RecreatePhysicsState();
	}
}

void AOCVehicle::BuildVisualParts()
{
	for (UStaticMeshComponent* Old : VisualPartComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	VisualPartComponents.Reset();
	if (!VehicleData)
	{
		return;
	}
	for (const FOCVehiclePart& Part : VehicleData->VisualParts)
	{
		if (!Part.Mesh)
		{
			continue;
		}
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		C->CreationMethod = EComponentCreationMethod::UserConstructionScript; // editor re-runs clean it up
		C->SetStaticMesh(Part.Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetupAttachment(GetMesh(), Part.Bone);
		C->SetRelativeTransform(FTransform(Part.Rotation, Part.Offset, Part.Scale));
		if (Part.MaterialOverride)
		{
			for (int32 i = 0; i < C->GetNumMaterials(); ++i)
			{
				C->SetMaterial(i, Part.MaterialOverride);
			}
		}
		C->RegisterComponent();
		VisualPartComponents.Add(C);
	}
	ApplyPaint();
}

void AOCVehicle::ApplyPaint()
{
	if (!VehicleData)
	{
		return;
	}
	if (PaintColor.R < 0.f)
	{
		// One pick per car: a spawned car keeps its colour and dirt when parts are rebuilt.
		const TArray<FLinearColor>& Palette = VehicleData->PaintColors;
		const FRandomStream Rng(GetTypeHash(GetFName()) ^ static_cast<uint32>(GetUniqueID()));
		PaintColor = Palette.IsEmpty() ? FLinearColor(0.8f, 0.05f, 0.02f, 1.f) : Palette[Rng.RandRange(0, Palette.Num() - 1)];
		Dirt = Rng.FRandRange(VehicleData->DirtRange.X, VehicleData->DirtRange.Y);
	}
	for (UStaticMeshComponent* C : VisualPartComponents)
	{
		if (C)
		{
			C->SetCustomPrimitiveDataVector4(0, FVector4(PaintColor.R, PaintColor.G, PaintColor.B, 1.0));
			C->SetCustomPrimitiveDataFloat(4, Dirt);
			C->SetCustomPrimitiveDataFloat(5, DamageAmount01);
		}
	}
}

void AOCVehicle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVehicleData(/*bRebuildPhysics*/ false); // mesh preview in the editor
}

void AOCVehicle::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyVehicleData(/*bRebuildPhysics*/ true);

	if (VehicleData)
	{
		CameraRig->Initialize(CameraBoom, ChaseCamera, GetVehicleMovementComponent(), VehicleData->Camera);
		Damage->Initialize(GetMesh(), VehicleData->Damage);
		Radio->Initialize(VehicleData->RadioStations);
	}
	else
	{
		UE_LOG(LogOpenCity, Error, TEXT("%s: VehicleData is not set."), *GetName());
	}
}

void AOCVehicle::BeginPlay()
{
	Super::BeginPlay();
	Damage->OnDamaged.AddUObject(this, &AOCVehicle::HandleDamaged);
	Damage->OnWrecked.AddUObject(this, &AOCVehicle::HandleWrecked);
	Park();

	if (const UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement())
	{
		UE_LOG(LogOpenCity, Log, TEXT("%s: %s ready, %d wheels, mass %.0f kg, max torque %.0f Nm, physics %d"), *GetName(),
			VehicleData ? *VehicleData->DisplayName.ToString() : TEXT("?"), V->Wheels.Num(), V->Mass, BaseMaxTorque,
			GetMesh()->IsSimulatingPhysics());
		// Wheel bone positions in vehicle space: checks imported chassis axes (front = +X).
		for (const FChaosWheelSetup& W : V->WheelSetups)
		{
			const FVector Local = GetActorTransform().InverseTransformPosition(GetMesh()->GetBoneLocation(W.BoneName));
			UE_LOG(LogOpenCity, Log, TEXT("%s:   wheel %s at %s"), *GetName(), *W.BoneName.ToString(), *Local.ToCompactString());
		}
	}
}

void AOCVehicle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PendingExitTimer);
	// Destroyed with someone inside (not a level teardown): put the driver back in the world.
	if (HasDriver() && EndPlayReason == EEndPlayReason::Destroyed)
	{
		FVector Spot;
		ExitAt(FindExitSpot(Spot) ? Spot : GetActorLocation() + FVector(0.f, 0.f, 200.f));
	}
	Super::EndPlay(EndPlayReason);
}

float AOCVehicle::GetGroundSpeedKmh() const
{
	// Exit gating uses the real speed: a car sliding sideways has ~0 forward speed.
	return GetVelocity().Size() * CmPerSecToKmh;
}

void AOCVehicle::HandleDriverDestroyed(AActor* DestroyedActor)
{
	UE_LOG(LogOpenCity, Warning, TEXT("%s: driver was destroyed while driving"), *GetName());
	Driver.Reset();
	GetWorldTimerManager().ClearTimer(PendingExitTimer);
}

float AOCVehicle::GetSpeedKmh() const
{
	const UChaosVehicleMovementComponent* V = GetVehicleMovementComponent();
	return V ? V->GetForwardSpeed() * CmPerSecToKmh : 0.f;
}

bool AOCVehicle::IsWrecked() const
{
	return Damage && Damage->IsWrecked();
}

void AOCVehicle::Park()
{
	if (UChaosVehicleMovementComponent* V = GetVehicleMovementComponent())
	{
		V->SetThrottleInput(0.f);
		V->SetBrakeInput(0.f);
		V->SetSteeringInput(0.f);
		V->SetHandbrakeInput(true);
	}
}

// --- Entering and exiting ------------------------------------------------------

bool AOCVehicle::CanInteract(const APawn* Instigator) const
{
	return Instigator && !HasDriver() && !IsWrecked() && Instigator->IsPlayerControlled();
}

FText AOCVehicle::GetInteractionText() const
{
	return LOCTEXT("Enter", "Enter vehicle");
}

APawn* AOCVehicle::EjectDriverTo(const FVector& Location)
{
	APawn* D = Driver.Get();
	if (D)
	{
		ExitAt(Location);
	}
	return D;
}

void AOCVehicle::Wreck()
{
	Damage->ApplyImpact(1.0e5f); // damage is capped at the remaining health
}

bool AOCVehicle::Interact(APawn* Instigator)
{
	if (!CanInteract(Instigator))
	{
		return false;
	}
	APlayerController* PC = Cast<APlayerController>(Instigator->GetController());
	if (!PC)
	{
		return false;
	}

	Driver = Instigator;
	Instigator->OnDestroyed.AddUniqueDynamic(this, &AOCVehicle::HandleDriverDestroyed);
	if (ACharacter* C = Cast<ACharacter>(Instigator))
	{
		C->GetCharacterMovement()->StopMovementImmediately();
		C->GetCharacterMovement()->DisableMovement();
	}
	Instigator->SetActorEnableCollision(false);
	Instigator->SetActorHiddenInGame(true);
	Instigator->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (VehicleData)
	{
		Instigator->SetActorRelativeLocation(VehicleData->DriverOffset);
	}

	PC->Possess(this);
	CameraRig->SetDriving(true);
	Radio->PowerOn();
	if (UChaosVehicleMovementComponent* V = GetVehicleMovementComponent())
	{
		V->SetHandbrakeInput(false);
	}
	OnDriverEntered();
	UE_LOG(LogOpenCity, Log, TEXT("%s: %s got in"), *GetName(), *Instigator->GetName());
	return true;
}

bool AOCVehicle::FindExitSpot(FVector& OutLocation) const
{
	const APawn* D = Driver.Get();
	const ACharacter* C = Cast<ACharacter>(D);
	if (!D || !VehicleData)
	{
		return false;
	}
	const float Radius = C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 42.f;
	const float HalfHeight = C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OCVehicleExit), false, this);
	Params.AddIgnoredActor(D);

	const FTransform VehicleXf = GetActorTransform();
	const FRotator Yaw(0.f, VehicleXf.Rotator().Yaw, 0.f); // spots stay upright if the vehicle is tilted
	const FVector Seat = VehicleXf.GetLocation() + Yaw.RotateVector(VehicleData->DriverOffset);
	UWorld* World = GetWorld();
	for (const FVector& Local : VehicleData->ExitOffsets)
	{
		const FVector Candidate = VehicleXf.GetLocation() + Yaw.RotateVector(Local) + FVector(0.f, 0.f, HalfHeight * 0.5f);

		// 1. Nothing solid between the seat and the spot (no stepping through thin walls or rails).
		if (World->LineTraceTestByChannel(Seat, Candidate, ECC_Visibility, Params))
		{
			continue;
		}
		// 2. Ground within reach below (no stepping off a ledge or bridge).
		FHitResult Ground;
		if (!World->LineTraceSingleByChannel(Ground, Candidate, Candidate - FVector(0.f, 0.f, HalfHeight + 150.f), ECC_Visibility, Params))
		{
			continue;
		}
		// 3. The capsule fits, standing on that ground.
		const FVector Standing = Ground.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 2.f);
		if (World->OverlapBlockingTestByChannel(Standing, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
		{
			continue;
		}
		OutLocation = Standing;
		return true;
	}
	return false;
}

void AOCVehicle::HandleExit(const FInputActionValue& Value)
{
	RequestExit();
}

void AOCVehicle::RequestExit()
{
	if (!HasDriver())
	{
		return;
	}
	const float MaxKmh = VehicleData ? VehicleData->MaxExitSpeedKmh : 15.f;
	if (GetGroundSpeedKmh() <= MaxKmh)
	{
		ForceExit();
		return;
	}
	// Too fast to step out: brake hard, then get out once slow enough.
	ApplyExitBraking();
	GetWorldTimerManager().SetTimer(PendingExitTimer, this, &AOCVehicle::CheckPendingExit, 0.1f, true);
}

void AOCVehicle::ApplyExitBraking()
{
	UChaosVehicleMovementComponent* V = GetVehicleMovementComponent();
	if (!V)
	{
		return;
	}
	// Brake means "reverse" once moving backwards (auto-reverse), so when rolling backwards
	// press throttle instead, which Chaos treats as a brake (bThrottleAsBrake).
	const bool bForward = V->GetForwardSpeed() >= 0.f;
	V->SetBrakeInput(bForward ? 1.f : 0.f);
	V->SetThrottleInput(bForward ? 0.f : 1.f);
	V->SetHandbrakeInput(true);
}

void AOCVehicle::CheckPendingExit()
{
	const float MaxKmh = VehicleData ? VehicleData->MaxExitSpeedKmh : 15.f;
	if (!HasDriver())
	{
		GetWorldTimerManager().ClearTimer(PendingExitTimer);
		return;
	}
	if (GetGroundSpeedKmh() <= MaxKmh)
	{
		GetWorldTimerManager().ClearTimer(PendingExitTimer);
		ForceExit();
		return;
	}
	ApplyExitBraking(); // re-evaluate direction every check
}

bool AOCVehicle::ForceExit()
{
	FVector Spot;
	if (!HasDriver())
	{
		return false;
	}
	if (!FindExitSpot(Spot))
	{
		UE_LOG(LogOpenCity, Log, TEXT("%s: no free exit spot"), *GetName());
		const bool bWasBrakingToExit = GetWorldTimerManager().IsTimerActive(PendingExitTimer);
		GetWorldTimerManager().ClearTimer(PendingExitTimer);
		if (bWasBrakingToExit)
		{
			// Hand the controls back: held keys send Triggered again, released ones stay at 0.
			if (UChaosVehicleMovementComponent* V = GetVehicleMovementComponent())
			{
				V->SetBrakeInput(0.f);
				V->SetThrottleInput(0.f);
				V->SetHandbrakeInput(false);
			}
		}
		return false;
	}
	ExitAt(Spot);
	return true;
}

void AOCVehicle::ExitAt(const FVector& Location)
{
	APawn* D = Driver.Get();
	AController* PC = GetController();
	Driver.Reset();
	if (D)
	{
		D->OnDestroyed.RemoveDynamic(this, &AOCVehicle::HandleDriverDestroyed);
	}
	CameraRig->SetDriving(false);
	Radio->PowerOff();
	Park();
	OnDriverExited();
	if (!D)
	{
		return;
	}

	D->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	D->SetActorLocationAndRotation(Location, FRotator(0.f, GetActorRotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	D->SetActorHiddenInGame(false);
	D->SetActorEnableCollision(true);
	if (ACharacter* C = Cast<ACharacter>(D))
	{
		C->GetCharacterMovement()->SetMovementMode(MOVE_Falling); // lands on its own
	}
	if (PC)
	{
		PC->Possess(D);
		PC->SetControlRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
	}
	UE_LOG(LogOpenCity, Log, TEXT("%s: %s got out"), *GetName(), *D->GetName());
}

void AOCVehicle::UnPossessed()
{
	if (InputConfig)
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (ULocalPlayer* LP = PC->GetLocalPlayer())
			{
				if (UEnhancedInputLocalPlayerSubsystem* S = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				{
					S->RemoveMappingContext(InputConfig->VehicleContext);
				}
			}
		}
	}
	Super::UnPossessed();
}

// --- Input --------------------------------------------------------------------

void AOCVehicle::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!InputConfig || !PC || !PC->GetLocalPlayer())
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* S = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		S->AddMappingContext(InputConfig->VehicleContext, InputConfig->VehiclePriority);
	}
	UE_LOG(LogOpenCity, Log, TEXT("%s: driven by %s, input context %s"), *GetName(), *GetNameSafe(PC),
		*GetNameSafe(InputConfig->VehicleContext));
}

void AOCVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input || !InputConfig)
	{
		UE_LOG(LogOpenCity, Error, TEXT("%s: needs an EnhancedInputComponent and InputConfig."), *GetName());
		return;
	}
	const UOCVehicleInputConfig& C = *InputConfig;
	if (C.Throttle)
	{
		Input->BindAction(C.Throttle, ETriggerEvent::Triggered, this, &AOCVehicle::HandleThrottle);
		Input->BindAction(C.Throttle, ETriggerEvent::Completed, this, &AOCVehicle::HandleThrottle);
	}
	if (C.Brake)
	{
		Input->BindAction(C.Brake, ETriggerEvent::Triggered, this, &AOCVehicle::HandleBrake);
		Input->BindAction(C.Brake, ETriggerEvent::Completed, this, &AOCVehicle::HandleBrake);
	}
	if (C.Steer)
	{
		Input->BindAction(C.Steer, ETriggerEvent::Triggered, this, &AOCVehicle::HandleSteer);
		Input->BindAction(C.Steer, ETriggerEvent::Completed, this, &AOCVehicle::HandleSteer);
	}
	if (C.Handbrake)
	{
		Input->BindAction(C.Handbrake, ETriggerEvent::Started, this, &AOCVehicle::HandleHandbrakeStarted);
		Input->BindAction(C.Handbrake, ETriggerEvent::Completed, this, &AOCVehicle::HandleHandbrakeStopped);
	}
	if (C.Look)
	{
		Input->BindAction(C.Look, ETriggerEvent::Triggered, this, &AOCVehicle::HandleLook);
	}
	if (C.Exit)
	{
		Input->BindAction(C.Exit, ETriggerEvent::Started, this, &AOCVehicle::HandleExit);
	}
	if (C.RadioNext)
	{
		Input->BindAction(C.RadioNext, ETriggerEvent::Started, this, &AOCVehicle::HandleRadioNext);
	}
	if (C.RadioPrevious)
	{
		Input->BindAction(C.RadioPrevious, ETriggerEvent::Started, this, &AOCVehicle::HandleRadioPrevious);
	}
	if (C.ResetVehicle)
	{
		Input->BindAction(C.ResetVehicle, ETriggerEvent::Started, this, &AOCVehicle::HandleReset);
	}
}

void AOCVehicle::HandleThrottle(const FInputActionValue& Value)
{
	if (GetWorldTimerManager().IsTimerActive(PendingExitTimer))
	{
		return; // braking to get out
	}
	GetVehicleMovementComponent()->SetThrottleInput(IsWrecked() ? 0.f : Value.Get<float>());
}

void AOCVehicle::HandleBrake(const FInputActionValue& Value)
{
	if (!GetWorldTimerManager().IsTimerActive(PendingExitTimer))
	{
		GetVehicleMovementComponent()->SetBrakeInput(Value.Get<float>());
	}
}

void AOCVehicle::HandleSteer(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetSteeringInput(Value.Get<float>());
}

void AOCVehicle::HandleHandbrakeStarted(const FInputActionValue& Value)
{
	GetVehicleMovementComponent()->SetHandbrakeInput(true);
}

void AOCVehicle::HandleHandbrakeStopped(const FInputActionValue& Value)
{
	if (!GetWorldTimerManager().IsTimerActive(PendingExitTimer))
	{
		GetVehicleMovementComponent()->SetHandbrakeInput(false);
	}
}

void AOCVehicle::HandleLook(const FInputActionValue& Value)
{
	CameraRig->AddLookInput(Value.Get<FVector2D>());
}

void AOCVehicle::HandleRadioNext(const FInputActionValue& Value)
{
	Radio->Next();
}

void AOCVehicle::HandleRadioPrevious(const FInputActionValue& Value)
{
	Radio->Previous();
}

void AOCVehicle::HandleReset(const FInputActionValue& Value)
{
	ResetUpright();
}

void AOCVehicle::ResetUpright()
{
	// Back on the wheels, facing the same way, slightly above where it was.
	const FVector Up = GetActorLocation() + FVector(0.f, 0.f, 120.f);
	SetActorLocationAndRotation(Up, FRotator(0.f, GetActorRotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

// --- Damage -------------------------------------------------------------------

void AOCVehicle::HandleDamaged(float DamageAmount, float HealthFraction)
{
	DamageAmount01 = FMath::Clamp(1.f - HealthFraction, 0.f, 1.f);  // dull, scraped paint (M_Master_CarPaint)
	ApplyPaint();
	if (UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement())
	{
		V->SetMaxEngineTorque(BaseMaxTorque * Damage->GetPerformance());
	}
}

void AOCVehicle::HandleWrecked()
{
	UE_LOG(LogOpenCity, Log, TEXT("%s: wrecked"), *GetName());
	DamageAmount01 = 1.f;
	ApplyPaint();
	if (UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement())
	{
		V->SetMaxEngineTorque(0.f);
		V->SetThrottleInput(0.f);
	}
	Radio->PowerOff();
}

#undef LOCTEXT_NAMESPACE

void AOCVehicle::SetPaint(const FLinearColor& Color, float InDirt)
{
	PaintColor = Color;
	Dirt = InDirt;
	ApplyPaint();
}

void AOCVehicle::PoolPark()
{
	bParked = true;
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->SetSimulatePhysics(false);
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorLocation(FVector(0.0, 0.0, -100000.0), false, nullptr, ETeleportType::ResetPhysics);
}

void AOCVehicle::PoolUnpark(const FTransform& Where, const FVector& Velocity)
{
	bParked = false;
	SetActorLocationAndRotation(Where.GetLocation(), Where.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->SetSimulatePhysics(true);
		M->SetPhysicsLinearVelocity(Velocity);
		M->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		M->WakeAllRigidBodies();
	}
}
