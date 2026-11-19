#include "Combat/OCProjectile.h"

#include "Components/StaticMeshComponent.h"
#include "Damage/OCDamageable.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace OCCombat
{
	constexpr float ProjectileRadius = 4.f;
	constexpr float LifetimeSeconds = 3.f;
	const FVector PoolLocation(0.0, 0.0, -200000.0);
}

AOCProjectile::AOCProjectile()
{
	PrimaryActorTick.bCanEverTick = false; // movement handled by UProjectileMovementComponent

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	// Query-only sweep against everything a bullet can hit; never a physics body itself.
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);
	RootComponent = Mesh;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
	Mesh->SetRelativeScale3D(FVector(OCCombat::ProjectileRadius / 50.f));

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Mesh);
	Movement->InitialSpeed = 0.f;
	Movement->MaxSpeed = 0.f; // no cap: the weapon sets the speed
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->ProjectileGravityScale = 0.f; // straight line, no drop (M6 baseline)
	Movement->bAutoActivate = false;
}

void AOCProjectile::BeginPlay()
{
	Super::BeginPlay();
	Mesh->OnComponentHit.AddDynamic(this, &AOCProjectile::OnHit);
	ReturnToPool(); // start pooled
}

void AOCProjectile::Fire(const FVector& Origin, const FVector& Dir, float SpeedCmS, float DamageIn, AActor* InShooter)
{
	if (bInFlight)
	{
		ReturnToPool();
	}
	Damage = DamageIn;
	Shooter = InShooter;
	bInFlight = true;

	if (InShooter)
	{
		Mesh->IgnoreActorWhenMoving(InShooter, true);
	}
	SetActorLocationAndRotation(Origin, Dir.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	// A stop on the previous hit cleared the updated component; re-arm it every launch.
	Movement->SetUpdatedComponent(Mesh);
	Movement->Velocity = Dir.GetSafeNormal() * SpeedCmS;
	Movement->Activate(true);

	GetWorldTimerManager().SetTimer(LifetimeTimer, this, &AOCProjectile::ReturnToPool,
		OCCombat::LifetimeSeconds, false);
}

void AOCProjectile::ReturnToPool()
{
	bInFlight = false;
	Damage = 0.f;
	if (AActor* S = Shooter.Get())
	{
		Mesh->IgnoreActorWhenMoving(S, false);
	}
	Shooter = nullptr;
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(LifetimeTimer);
	}
	Movement->StopMovementImmediately();
	Movement->Deactivate();
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorLocationAndRotation(OCCombat::PoolLocation, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
}

void AOCProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bInFlight || !OtherActor || OtherActor == this || OtherActor == Shooter.Get()) return;

	if (Damage > 0.f)
	{
		if (IOCDamageable* Damageable = Cast<IOCDamageable>(OtherActor))
		{
			Damageable->ApplyDamage(Damage, Movement->Velocity.GetSafeNormal() * 300.f, Shooter.Get());
		}
	}
	ReturnToPool();
}

void AOCProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(LifetimeTimer);
	}
	Super::EndPlay(EndPlayReason);
}
