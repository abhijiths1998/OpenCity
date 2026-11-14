#include "Combat/OCProjectile.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace OCCombat
{
	constexpr float ProjectileRadius = 4.f;
	constexpr float LifetimeSeconds = 5.f;
	constexpr float DefaultSpeedCmS = 1800.f; // ~65 km/h
}

AOCProjectile::AOCProjectile()
{
	PrimaryActorTick.bCanEverTick = false; // movement handled by UProjectileMovementComponent

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(TEXT("Projectile"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetGenerateOverlapEvents(true);
	RootComponent = Mesh;

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Mesh;
	Movement->InitialSpeed = OCCombat::DefaultSpeedCmS;
	Movement->MaxSpeed = OCCombat::DefaultSpeedCmS;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
	Movement->ProjectileGravityScale = 0.f; // straight line, no drop (M6 baseline)
	Movement->HomingAccelerationMagnitude = 0.f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
	Mesh->SetRelativeScale3D(FVector(OCCombat::ProjectileRadius / 50.f));
}

void AOCProjectile::Fire(const FVector& Origin, const FVector& Dir, float SpeedCmS, float DamageIn,
	const AActor* InstigatorActor, TSubclassOf<AOCProjectile> InProjectileClass)
{
	Damage = DamageIn;
	Instigator = InstigatorActor;
	bInFlight = true;

	SetActorLocationAndRotation(Origin, Dir.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(false);

	Movement->Velocity = Dir.GetSafeNormal() * SpeedCmS;
	Movement->ProjectileGravityScale = 0.f;

	Mesh->OnComponentHit.AddDynamic(this, &AOCProjectile::OnHit);

	// Auto-return after lifetime even if nothing was hit.
	GetWorldTimerManager().SetTimer(ProjectileLifetimeTimer, this, &AOCProjectile::ReturnToPool,
		OCCombat::LifetimeSeconds, false);
}

void AOCProjectile::ReturnToPool()
{
	bInFlight = false;
	Damage = 0.f;
	Instigator = nullptr;
	Mesh->OnComponentHit.RemoveAll(this);
	GetWorldTimerManager().ClearTimer(ProjectileLifetimeTimer);

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
	SetActorLocationAndRotation(FVector(0.f, 0.f, -200000.f), FRotator::ZeroRotator,
		false, nullptr, ETeleportType::TeleportPhysics);
}

void AOCProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bInFlight || !OtherActor || OtherActor == this) return;

	if (IOCDamageable* Damageable = Cast<IOCDamageable>(OtherActor))
	{
		const FVector Impulse = NormalImpulse * 300.f;
		Damageable->ApplyDamage(Damage, Impulse, Instigator.Get());
	}

	ReturnToPool();
}

void AOCProjectile::BeginPlay()
{
	Super::BeginPlay();
	ReturnToPool(); // start pooled
}

void AOCProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReturnToPool();
	Super::EndPlay(EndPlayReason);
}

bool AOCProjectile::ApplyDamage(float Amount, const FVector& Impulse, const AActor* InstigatorActor)
{
	// Projectiles are not meaningfully damageable; ignore.
	return false;
}
