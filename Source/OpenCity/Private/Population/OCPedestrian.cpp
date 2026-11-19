#include "Population/OCPedestrian.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Population/OCPopulationSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace OCPopulation
{
	constexpr float CapsuleRadius = 34.f;   // person-sized
	constexpr float HalfHeight = 88.f;
	constexpr float BodyHeight = 2.f * HalfHeight;
	// The engine cylinder is 100 x 100 x 100 cm with its pivot at the centre.
	constexpr float CylinderSize = 100.f;
}

float AOCPedestrian::CapsuleHalfHeight()
{
	return OCPopulation::HalfHeight;
}

AOCPedestrian::AOCPedestrian()
{
	PrimaryActorTick.bCanEverTick = false; // the population subsystem places us

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(OCPopulation::CapsuleRadius, OCPopulation::HalfHeight);
	Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Capsule->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Capsule->SetCanEverAffectNavigation(false);
	RootComponent = Capsule;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Placeholder art: the engine cylinder, scaled to the capsule and centred on it.
	Mesh->SetRelativeScale3D(FVector(2.f * OCPopulation::CapsuleRadius, 2.f * OCPopulation::CapsuleRadius,
		OCPopulation::BodyHeight) / OCPopulation::CylinderSize);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
}

void AOCPedestrian::SetPooled(const FTransform& Where)
{
	bDead = false;
	Health = 1.f;
	DeathTime = 0.0;
	bInWorld = false;
	SimId = INDEX_NONE;
	State = EOCPedState::Idle;
	Mesh->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	SetActorLocationAndRotation(Where.GetLocation(), Where.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AOCPedestrian::SetActive(bool bActive)
{
	if (bActive == bInWorld) return;
	bInWorld = bActive;
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
}

bool AOCPedestrian::ApplyDamage(float Amount, const FVector& Impulse, const AActor* Instigator)
{
	if (bDead || Amount <= 0.f || !bInWorld) return false;
	if (UWorld* W = GetWorld())
	{
		if (UOCPopulationSubsystem* Pop = W->GetSubsystem<UOCPopulationSubsystem>())
		{
			return Pop->DamagePed(this, Amount, Impulse, Instigator);
		}
	}
	return false;
}

void AOCPedestrian::SetHealthFraction(float Fraction)
{
	Health = FMath::Clamp(Fraction, 0.f, 1.f);
}

void AOCPedestrian::Die(double Now)
{
	if (bDead) return;
	bDead = true;
	Health = 0.f;
	DeathTime = Now;
	State = EOCPedState::Dead;
	// Lie the body along the ground, and stop blocking people (bullets and traces still find it).
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, OCPopulation::CapsuleRadius - OCPopulation::HalfHeight),
		FRotator(90.f, 0.f, 0.f));
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}

void AOCPedestrian::SetSimPose(const FVector& FeetLocation, const FVector& Forward)
{
	Feet = FeetLocation;
	if (!bInWorld) return;
	const FVector Centre = FeetLocation + FVector(0.0, 0.0, OCPopulation::HalfHeight);
	if (bDead)
	{
		SetActorLocation(Centre, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	const FRotator Rot = Forward.IsNearlyZero() ? GetActorRotation() : Forward.Rotation();
	SetActorLocationAndRotation(Centre, Rot, false, nullptr, ETeleportType::TeleportPhysics);
}
