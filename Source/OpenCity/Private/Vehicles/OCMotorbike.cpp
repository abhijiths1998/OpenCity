#include "Vehicles/OCMotorbike.h"

#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AOCMotorbike::AOCMotorbike(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics; // stabiliser torque goes into this frame's physics step

	LeanPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LeanPivot"));
	LeanPivot->SetupAttachment(GetMesh());

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(LeanPivot);

	ForkPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ForkPivot"));
	ForkPivot->SetupAttachment(LeanPivot);
	FrontHub = CreateDefaultSubobject<USceneComponent>(TEXT("FrontHub"));
	FrontHub->SetupAttachment(ForkPivot);
	FrontWheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontWheel"));
	FrontWheel->SetupAttachment(FrontHub);

	RearHub = CreateDefaultSubobject<USceneComponent>(TEXT("RearHub"));
	RearHub->SetupAttachment(LeanPivot);
	RearWheel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RearWheel"));
	RearWheel->SetupAttachment(RearHub);

	// Visual parts only: physics is the hidden chassis.
	for (UStaticMeshComponent* C : { Body.Get(), FrontWheel.Get(), RearWheel.Get() })
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
	}
}

void AOCMotorbike::ApplyArt()
{
	// The chassis mesh is invisible; its children (the visible bike) stay visible.
	GetMesh()->SetVisibility(false, /*bPropagateToChildren*/ false);

	Body->SetStaticMesh(BodyMesh);
	Body->SetRelativeLocation(FVector(0.f, 0.f, WheelRadius + BodyScale.Z * 50.f));
	Body->SetRelativeScale3D(BodyScale);

	// Engine cylinders are 100 cm across along Z: turn the axle to Y and scale to size.
	const FVector WheelScale(WheelRadius / 50.f, WheelRadius / 50.f, WheelWidth / 100.f);
	ForkPivot->SetRelativeLocation(FrontWheelLocation);
	FrontWheel->SetStaticMesh(WheelMesh);
	FrontWheel->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
	FrontWheel->SetRelativeScale3D(WheelScale);
	RearHub->SetRelativeLocation(RearWheelLocation);
	RearWheel->SetStaticMesh(WheelMesh);
	RearWheel->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
	RearWheel->SetRelativeScale3D(WheelScale);
}

void AOCMotorbike::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyArt();
}

void AOCMotorbike::BeginPlay()
{
	Super::BeginPlay();
	ApplyArt();
}

void AOCMotorbike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Stabilise(DeltaSeconds);
	UpdateVisuals(DeltaSeconds);
}

void AOCMotorbike::Stabilise(float DeltaSeconds)
{
	USkeletalMeshComponent* M = GetMesh();
	if (!M || !M->IsSimulatingPhysics())
	{
		return;
	}
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	// Roll angle: how far the right side has dropped (+) or risen (-).
	const float RollRad = FMath::Asin(FMath::Clamp(-Right.Z, -1.f, 1.f));
	if (FMath::Abs(FMath::RadiansToDegrees(RollRad)) > CrashRollDegrees)
	{
		return; // fallen over: let it lie (reset puts it back up)
	}
	const FVector AngVel = M->GetPhysicsAngularVelocityInRadians();
	const float RollRate = FVector::DotProduct(AngVel, Forward);
	const float Correction = -(StabiliserStiffness * RollRad + StabiliserDamping * RollRate);
	M->AddTorqueInRadians(Forward * Correction, NAME_None, /*bAccelChange*/ true);

	// Yaw assist (kinematic bicycle model): yaw rate = v * tan(steer) / wheelbase.
	const UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement();
	if (YawAssist > 0.f && V && V->Wheels.Num() >= 3 && V->Wheels[0])
	{
		const FVector Up = GetActorUpVector();
		const float Wheelbase = FMath::Max(10.f, (FrontWheelLocation - RearWheelLocation).Size());
		const float SteerRad = FMath::DegreesToRadians(V->Wheels[0]->GetSteerAngle());
		const float DesiredYawRate = V->GetForwardSpeed() * FMath::Tan(SteerRad) / Wheelbase;
		const float YawRate = FVector::DotProduct(AngVel, Up);
		M->AddTorqueInRadians(Up * (YawAssist * (DesiredYawRate - YawRate)), NAME_None, /*bAccelChange*/ true);
	}
}

void AOCMotorbike::UpdateVisuals(float DeltaSeconds)
{
	UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement(); // GetSteeringInput() is non-const
	if (!V)
	{
		return;
	}

	const float SpeedFactor = FMath::Clamp(FMath::Abs(GetSpeedKmh()) / FullLeanSpeedKmh, 0.f, 1.f);
	const float Target = LeanSign * V->GetSteeringInput() * MaxLeanDegrees * SpeedFactor;
	LeanDegrees = FMath::FInterpTo(LeanDegrees, Target, DeltaSeconds, LeanSpeed);
	LeanPivot->SetRelativeRotation(FRotator(0.f, 0.f, LeanDegrees));

	// Wheels 0/1 are front (steered), 2/3 rear; use one wheel of each pair.
	const TArray<TObjectPtr<UChaosVehicleWheel>>& Wheels = V->Wheels;
	if (Wheels.Num() >= 3 && Wheels[0] && Wheels[2])
	{
		ForkPivot->SetRelativeRotation(FRotator(0.f, Wheels[0]->GetSteerAngle(), 0.f));
		FrontHub->SetRelativeRotation(FRotator(-Wheels[0]->GetRotationAngle(), 0.f, 0.f));
		RearHub->SetRelativeRotation(FRotator(-Wheels[2]->GetRotationAngle(), 0.f, 0.f));
	}
}
