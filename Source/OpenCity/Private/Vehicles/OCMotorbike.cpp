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
	PrimaryActorTick.bStartWithTickEnabled = false; // ticks while ridden or settling (see Tick)
	PrimaryActorTick.TickGroup = TG_PrePhysics;     // stabiliser torque goes into this frame's physics step

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

const FOCMotorbikeHandling& AOCMotorbike::Handling() const
{
	static const FOCMotorbikeHandling Defaults;
	return VehicleData ? VehicleData->Motorbike : Defaults;
}

void AOCMotorbike::UpdateWheelLayout()
{
	// Visual wheels and the yaw-assist wheelbase follow the physics wheel bones
	// (setups 0/1 = front pair, 2/3 = rear pair), so they can't drift apart.
	if (!VehicleData || VehicleData->WheelSetups.Num() < 4 || !GetMesh()->GetSkeletalMeshAsset())
	{
		return;
	}
	auto BoneLocal = [this](int32 i)
	{
		const FName Bone = VehicleData->WheelSetups[i].BoneName;
		return GetMesh()->GetComponentTransform().InverseTransformPosition(GetMesh()->GetBoneLocation(Bone));
	};
	FrontWheelLocation = (BoneLocal(0) + BoneLocal(1)) * 0.5f;
	RearWheelLocation = (BoneLocal(2) + BoneLocal(3)) * 0.5f;
}

void AOCMotorbike::ApplyArt()
{
	UpdateWheelLayout();
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

void AOCMotorbike::OnDriverEntered()
{
	IdleTime = 0.f;
	SetActorTickEnabled(true);
}

void AOCMotorbike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Stabilise(DeltaSeconds);
	UpdateVisuals(DeltaSeconds);

	// Parked, empty and settled: stop ticking until someone gets on.
	const bool bSettled = !HasDriver() && FMath::Abs(GetSpeedKmh()) < 1.f && FMath::Abs(GetActorRotation().Roll) < 2.f
		&& FMath::IsNearlyZero(LeanDegrees, 0.5f);
	IdleTime = bSettled ? IdleTime + DeltaSeconds : 0.f;
	if (IdleTime > SleepAfterSeconds)
	{
		SetActorTickEnabled(false);
	}
}

void AOCMotorbike::Stabilise(float DeltaSeconds)
{
	USkeletalMeshComponent* M = GetMesh();
	if (!M || !M->IsSimulatingPhysics())
	{
		return;
	}
	const FOCMotorbikeHandling& H = Handling();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	// Roll angle: + when the right side is down (FRotationMatrix: Right = (0, cos R, -sin R)).
	const float RollRad = FMath::Asin(FMath::Clamp(-Right.Z, -1.f, 1.f));
	if (FMath::Abs(FMath::RadiansToDegrees(RollRad)) > H.CrashRollDegrees)
	{
		return; // fallen over: let it lie (reset puts it back up)
	}
	// Angular velocity +X turns Right toward +Z (right side up), i.e. it *reduces* roll.
	// So the spring pushes along +X for positive roll, and damping opposes the roll rate.
	const FVector AngVel = M->GetPhysicsAngularVelocityInRadians();
	const float RollRate = FVector::DotProduct(AngVel, Forward);
	const float Correction = H.StabiliserStiffness * RollRad - H.StabiliserDamping * RollRate;
	M->AddTorqueInRadians(Forward * Correction, NAME_None, /*bAccelChange*/ true);

	// Yaw assist (kinematic bicycle model): yaw rate = v * tan(steer) / wheelbase.
	const UChaosWheeledVehicleMovementComponent* V = GetWheeledMovement();
	const float YawAssist = H.YawAssist;
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

	const FOCMotorbikeHandling& H = Handling();
	const float SpeedFactor = FMath::Clamp(FMath::Abs(GetSpeedKmh()) / H.FullLeanSpeedKmh, 0.f, 1.f);
	const float Target = V->GetSteeringInput() * H.MaxLeanDegrees * SpeedFactor; // + = lean right
	LeanDegrees = FMath::FInterpTo(LeanDegrees, Target, DeltaSeconds, H.LeanSpeed);
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
