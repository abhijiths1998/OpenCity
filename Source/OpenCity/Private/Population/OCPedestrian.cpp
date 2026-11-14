#include "Population/OCPedestrian.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Events/OCEventSubsystem.h"
#include "GameplayTags.h"
#include "UObject/ConstructorHelpers.h"

namespace OCPopulation
{
	constexpr float CapsuleRadius = 34.f;   // person-sized, blocks cars like a traffic vehicle
	constexpr float CapsuleHalfHeight = 88.f;
}

AOCPedestrian::AOCPedestrian()
{
	PrimaryActorTick.bCanEverTick = false; // the population subsystem places us at AI rate

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(OCPopulation::CapsuleRadius, OCPopulation::CapsuleHalfHeight);
	Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Capsule->SetCanEverAffectNavigation(false);
	RootComponent = Capsule;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -OCPopulation::CapsuleHalfHeight), FRotator::ZeroRotator);

	// Placeholder art until M6/M9 has a pedestrian mesh (the engine's cylinder keeps it cheap).
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
	bInWorld = false;
	SimId = INDEX_NONE;
	State = EOCPedState::Idle;
	SetActorLocationAndRotation(Where.GetLocation(), Where.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}

void AOCPedestrian::SetActive(bool bActive)
{
	if (bActive == bInWorld) return;
	bInWorld = bActive;
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
	SetActorTickEnabled(bActive);
}

bool AOCPedestrian::ApplyDamage(float Amount, const FVector& Impulse, const AActor* Instigator)
{
	if (bDead || Amount <= 0.f) return false;
	Health = FMath::Max(0.f, Health - Amount);
	if (Health <= 0.f)
	{
		bDead = true;
		// Death notifications go through UOCEventSubsystem (M7 wanted/panic wiring).
		if (const UWorld* W = GetWorld())
		{
			if (UOCEventSubsystem* Events = W->GetSubsystem<UOCEventSubsystem>())
			{
				Events->Publish(FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Crime.Murder"))),
					GetActorLocation(), this, 1.f);
			}
		}
	}
	return true;
}

void AOCPedestrian::SetSimPose(const FVector& Location, const FVector& Forward, double SpeedCmS)
{
	SimLoc = Location;
	if (!bInWorld) return;
	const FRotator Rot = Forward.IsNearlyZero() ? FRotator::ZeroRotator : Forward.Rotation();
	SetActorLocationAndRotation(Location, Rot, false, nullptr, ETeleportType::TeleportPhysics);
}
