#include "Vehicles/OCVehicleComponents.h"

#include "Camera/CameraComponent.h"
#include "ChaosVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "OCLog.h"
#include "Vehicles/OCVehicleData.h"

// --- Camera -------------------------------------------------------------------

UOCVehicleCameraComponent::UOCVehicleCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UOCVehicleCameraComponent::Initialize(USpringArmComponent* InBoom, UCameraComponent* InCamera,
	UChaosVehicleMovementComponent* InMovement, const FOCVehicleCameraSettings& InSettings)
{
	Boom = InBoom;
	Camera = InCamera;
	Movement = InMovement;
	Settings = InSettings;

	if (USpringArmComponent* B = Boom.Get())
	{
		B->TargetArmLength = Settings.ArmLength;
		B->SocketOffset = Settings.SocketOffset;
		B->bEnableCameraRotationLag = true;
		B->CameraRotationLagSpeed = Settings.RotationLagSpeed;
	}
	if (UCameraComponent* C = Camera.Get())
	{
		C->SetFieldOfView(Settings.FovAtRest);
	}
	LookOffset = FVector2D::ZeroVector;
	ApplyBoomRotation();
}

void UOCVehicleCameraComponent::AddLookInput(const FVector2D& DeltaDegrees)
{
	LookOffset.X = FRotator::NormalizeAxis(LookOffset.X + DeltaDegrees.X);
	LookOffset.Y = FMath::Clamp(LookOffset.Y + DeltaDegrees.Y, -40.f, 30.f);
	TimeSinceLook = 0.f;
	ApplyBoomRotation();
}

void UOCVehicleCameraComponent::SetDriving(bool bDriving)
{
	SetComponentTickEnabled(bDriving);
	if (!bDriving)
	{
		LookOffset = FVector2D::ZeroVector;
		ApplyBoomRotation();
	}
}

void UOCVehicleCameraComponent::ApplyBoomRotation()
{
	if (USpringArmComponent* B = Boom.Get())
	{
		B->SetRelativeRotation(FRotator(Settings.Pitch + LookOffset.Y, LookOffset.X, 0.f));
	}
}

void UOCVehicleCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (const UChaosVehicleMovementComponent* M = Movement.Get())
	{
		const float Kmh = M->GetForwardSpeed() * 0.036f; // cm/s -> km/h
		if (UCameraComponent* C = Camera.Get())
		{
			C->SetFieldOfView(FMath::FInterpTo(C->FieldOfView, Settings.FovForSpeed(Kmh), DeltaTime, 3.f));
		}
	}

	TimeSinceLook += DeltaTime;
	if (TimeSinceLook > Settings.RecenterDelay && !LookOffset.IsNearlyZero(0.05))
	{
		LookOffset = FMath::Vector2DInterpTo(LookOffset, FVector2D::ZeroVector, DeltaTime, Settings.RecenterSpeed);
		ApplyBoomRotation();
	}
}

// --- Damage -------------------------------------------------------------------

UOCVehicleDamageComponent::UOCVehicleDamageComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // driven by hit events
}

void UOCVehicleDamageComponent::Initialize(UPrimitiveComponent* InBody, const FOCVehicleDamageSettings& InSettings)
{
	Model.Reset(InSettings);
	if (UPrimitiveComponent* Old = Body.Get())
	{
		Old->OnComponentHit.RemoveDynamic(this, &UOCVehicleDamageComponent::HandleHit);
	}
	Body = InBody;
	if (InBody)
	{
		InBody->SetNotifyRigidBodyCollision(true);
		InBody->OnComponentHit.AddUniqueDynamic(this, &UOCVehicleDamageComponent::HandleHit);
	}
}

void UOCVehicleDamageComponent::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const float Mass = HitComponent ? HitComponent->GetMass() : 0.f;
	if (Mass <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	// Impulse (kg*cm/s) over mass is the change in velocity this hit caused.
	const float DeltaVKmh = NormalImpulse.Size() / Mass * 0.036f;
	ApplyImpact(DeltaVKmh);
}

void UOCVehicleDamageComponent::ApplyImpact(float ImpactKmh)
{
	const bool bWasWrecked = Model.IsWrecked();
	const float Damage = Model.ApplyImpact(ImpactKmh, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	if (Damage <= 0.f)
	{
		return;
	}
	UE_LOG(LogOpenCity, Log, TEXT("%s: impact %.0f km/h, damage %.0f, health %.0f%%"), *GetNameSafe(GetOwner()),
		ImpactKmh, Damage, Model.GetHealthFraction() * 100.f);
	OnDamaged.Broadcast(Damage, Model.GetHealthFraction());
	if (!bWasWrecked && Model.IsWrecked())
	{
		OnWrecked.Broadcast();
	}
}

// --- Radio --------------------------------------------------------------------

UOCRadioComponent::UOCRadioComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOCRadioComponent::Initialize(const TArray<TObjectPtr<UOCRadioStationData>>& InStations)
{
	Stations.Reset();
	TArray<FOCRadioStationInfo> Infos;
	for (UOCRadioStationData* S : InStations)
	{
		if (S)
		{
			Stations.Add(S);
			Infos.Add({ S->DisplayName.ToString(), S->LengthSeconds });
		}
	}
	// Seed from the owner's name so each car is consistent but cars differ.
	Tuner.SetStations(MoveTemp(Infos), GetTypeHash(GetNameSafe(GetOwner())));
	RememberedIndex = Stations.IsEmpty() ? -1 : 0;
}

void UOCRadioComponent::PowerOn()
{
	bPowered = true;
	Tuner.Tune(RememberedIndex);
	Announce();
}

void UOCRadioComponent::PowerOff()
{
	if (bPowered)
	{
		RememberedIndex = Tuner.GetIndex();
	}
	bPowered = false;
}

void UOCRadioComponent::Next()
{
	if (bPowered)
	{
		Tuner.Next();
		RememberedIndex = Tuner.GetIndex();
		Announce();
	}
}

void UOCRadioComponent::Previous()
{
	if (bPowered)
	{
		Tuner.Previous();
		RememberedIndex = Tuner.GetIndex();
		Announce();
	}
}

void UOCRadioComponent::Announce() const
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FString Text = TEXT("Radio off");
	if (const FOCRadioStationInfo* S = Tuner.GetCurrent())
	{
		const int32 Pos = FMath::FloorToInt32(Tuner.GetPosition(Now));
		Text = FString::Printf(TEXT("Radio: %s  %02d:%02d"), *S->Name, Pos / 60, Pos % 60);
	}
	UE_LOG(LogOpenCity, Log, TEXT("%s: %s"), *GetNameSafe(GetOwner()), *Text);
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(/*Key*/ 7101, 3.f, FColor::Cyan, Text); // stub until the M9 HUD
	}
#endif
}
