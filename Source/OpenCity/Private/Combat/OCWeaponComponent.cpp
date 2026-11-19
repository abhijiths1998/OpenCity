#include "Combat/OCWeaponComponent.h"

#include "Combat/OCProjectile.h"
#include "Damage/OCDamageable.h"
#include "Engine/World.h"
#include "Events/OCEventSubsystem.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "OCLog.h"
#include "TimerManager.h"

namespace OCWeapon
{
	constexpr int32 MaxPooledProjectiles = 16;
	constexpr float DecayDecalSeconds = 10.f;

	/** What a bullet can hit: world, props, people, cars. Pawn capsules ignore ECC_Visibility, so a channel trace would miss every ped. */
	FCollisionObjectQueryParams ShotObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		Objects.AddObjectTypesToQuery(ECC_Vehicle);
		Objects.AddObjectTypesToQuery(ECC_Destructible);
		return Objects;
	}
}

UOCWeaponComponent::UOCWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // fire, reload and automatic fire all run on timers
}

void UOCWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	Equip(DefaultWeapon);
}

void UOCWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(AutoFireTimer);
		W->GetTimerManager().ClearTimer(ReloadTimer);
	}
	for (AOCProjectile* P : ProjectilePool)
	{
		if (IsValid(P)) P->Destroy();
	}
	ProjectilePool.Reset();
	Super::EndPlay(EndPlayReason);
}

void UOCWeaponComponent::Equip(UOCWeaponData* Weapon)
{
	StopFire();
	if (const UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(ReloadTimer);
	}
	EquippedWeapon = Weapon;
	if (Weapon)
	{
		Ammo.Equip(Weapon->MagazineSize, Weapon->AmmoPerShot, FMath::Min(Weapon->StartingReserve, Weapon->MaxReserve));
	}
	else
	{
		Ammo = FOCAmmo();
	}
	bReloading = false;
	NextFireTime = 0.0;
	ShotsFired = 0;

	UE_LOG(LogOpenCity, Log, TEXT("Combat: equipped %s (mag %d / reserve %d)"),
		Weapon ? *Weapon->DisplayName.ToString() : TEXT("unarmed"), Ammo.InMagazine, Ammo.Reserve);
}

float UOCWeaponComponent::ShotInterval() const
{
	return EquippedWeapon ? 1.f / FMath::Max(EquippedWeapon->FireRate, 0.1f) : 0.25f;
}

void UOCWeaponComponent::Fire()
{
	if (!EquippedWeapon) return;
	bTriggerHeld = true;
	TryFire();
	if (EquippedWeapon && EquippedWeapon->bAutomatic)
	{
		GetWorld()->GetTimerManager().SetTimer(AutoFireTimer, this, &UOCWeaponComponent::AutoFireTick,
			ShotInterval(), true);
	}
}

void UOCWeaponComponent::StopFire()
{
	bTriggerHeld = false;
	if (const UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(AutoFireTimer);
	}
}

void UOCWeaponComponent::AutoFireTick()
{
	if (!bTriggerHeld || !EquippedWeapon || !EquippedWeapon->bAutomatic)
	{
		StopFire();
		return;
	}
	TryFire();
}

bool UOCWeaponComponent::TryFire()
{
	UWorld* World = GetWorld();
	if (!EquippedWeapon || bReloading || !World) return false;

	const double Now = World->GetTimeSeconds();
	if (Now + KINDA_SMALL_NUMBER < NextFireTime) return false;

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		StopFire(); // unpossessed (got into a car, died): release the trigger
		return false;
	}

	if (!Ammo.CanFire())
	{
		StopFire();
		Reload(); // empty: reload if there is a reserve, otherwise just click
		return false;
	}
	Ammo.Consume();
	++ShotsFired;
	NextFireTime = Now + ShotInterval();

	// 1) Where the shooter is looking (the camera for the player): what is under the crosshair.
	FVector ViewLoc;
	FRotator ViewRot;
	Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Dir = ViewRot.Vector();
	const FVector Muzzle = Pawn->GetPawnViewLocation();
	// Start the view trace level with the shooter so nothing between the camera and the player is hit.
	const FVector ViewStart = ViewLoc + Dir * FMath::Max(0.0, FVector::DotProduct(Muzzle - ViewLoc, Dir));
	const FVector ViewEnd = ViewStart + Dir * EquippedWeapon->Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OCWeaponShot), /*bTraceComplex*/ false, GetOwner());
	for (const AOCProjectile* P : ProjectilePool)
	{
		Params.AddIgnoredActor(P);
	}
	const FCollisionObjectQueryParams Objects = OCWeapon::ShotObjects();

	FHitResult AimHit;
	const FVector AimPoint = World->LineTraceSingleByObjectType(AimHit, ViewStart, ViewEnd, Objects, Params)
		? AimHit.ImpactPoint : ViewEnd;

	// 2) The bullet's own path from the shooter to that point (a wall right in front of the gun wins).
	const FVector ShotDir = (AimPoint - Muzzle).GetSafeNormal();
	const FVector ShotEnd = Muzzle + ShotDir * EquippedWeapon->Range;
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByObjectType(Hit, Muzzle, ShotEnd, Objects, Params);

	if (EquippedWeapon->bHitscan && bHit)
	{
		if (IOCDamageable* Damageable = Cast<IOCDamageable>(Hit.GetActor()))
		{
			Damageable->ApplyDamage(EquippedWeapon->Damage, ShotDir * EquippedWeapon->HitImpulse, GetOwner());
		}
		if (EquippedWeapon->ImpactDecal && !Cast<APawn>(Hit.GetActor()))
		{
			UGameplayStatics::SpawnDecalAtLocation(World, EquippedWeapon->ImpactDecal, FVector(8.0),
				Hit.ImpactPoint, (-Hit.ImpactNormal).Rotation(), OCWeapon::DecayDecalSeconds);
		}
	}
	SpawnTracer(Muzzle, bHit ? Hit.ImpactPoint : ShotEnd);

	if (EquippedWeapon->FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, EquippedWeapon->FireSound, Muzzle);
	}

	// Every shot is heard: peds panic (population) and the player may become wanted.
	if (UOCEventSubsystem* Events = World->GetSubsystem<UOCEventSubsystem>())
	{
		Events->Publish(OCEventTags::Gunshot(), Muzzle, GetOwner(), EquippedWeapon->GunshotSeverity);
	}

	UE_LOG(LogOpenCity, Log, TEXT("Combat: fire %s -> %s (mag %d / reserve %d)"),
		*EquippedWeapon->DisplayName.ToString(), bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing"),
		Ammo.InMagazine, Ammo.Reserve);
	return true;
}

void UOCWeaponComponent::SpawnTracer(const FVector& From, const FVector& To)
{
	if (!EquippedWeapon || !EquippedWeapon->ProjectileClass) return;

	AOCProjectile* Proj = nullptr;
	for (AOCProjectile* P : ProjectilePool)
	{
		if (IsValid(P) && !P->IsInFlight() && P->GetClass() == EquippedWeapon->ProjectileClass)
		{
			Proj = P;
			break;
		}
	}
	if (!Proj && ProjectilePool.Num() < OCWeapon::MaxPooledProjectiles)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.Owner = GetOwner();
		Proj = GetWorld()->SpawnActor<AOCProjectile>(EquippedWeapon->ProjectileClass, FVector(0.0, 0.0, -200000.0),
			FRotator::ZeroRotator, SpawnParams);
		if (Proj) ProjectilePool.Add(Proj);
	}
	if (Proj)
	{
		// Hitscan weapons already applied damage; their projectile is only a tracer.
		const float ProjDamage = EquippedWeapon->bHitscan ? 0.f : EquippedWeapon->Damage;
		Proj->Fire(From, (To - From).GetSafeNormal(), EquippedWeapon->ProjectileSpeed, ProjDamage, GetOwner());
	}
}

bool UOCWeaponComponent::Reload()
{
	if (!EquippedWeapon || bReloading || !Ammo.CanReload())
	{
		return false;
	}
	StopFire();
	bReloading = true;
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &UOCWeaponComponent::FinishReload,
		FMath::Max(EquippedWeapon->ReloadSeconds, 0.01f), false);
	UE_LOG(LogOpenCity, Log, TEXT("Combat: reloading %s"), *EquippedWeapon->DisplayName.ToString());
	return true;
}

void UOCWeaponComponent::FinishReload()
{
	bReloading = false;
	if (!EquippedWeapon) return;
	Ammo.Reload();
	NextFireTime = GetWorld()->GetTimeSeconds() + 0.1;
	UE_LOG(LogOpenCity, Log, TEXT("Combat: reloaded (mag %d / reserve %d)"), Ammo.InMagazine, Ammo.Reserve);
}

void UOCWeaponComponent::AddAmmo(int32 Rounds)
{
	if (EquippedWeapon)
	{
		Ammo.AddReserve(Rounds, EquippedWeapon->MaxReserve);
	}
}

void UOCWeaponComponent::SetAiming(bool bAimingIn)
{
	bAiming = bAimingIn;
}
