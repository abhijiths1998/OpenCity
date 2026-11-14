#include "Combat/OCWeaponComponent.h"

#include "Combat/OCProjectile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "OCLog.h"
#include "TimerManager.h"
#include "Damage/OCDamageable.h"

UOCWeaponComponent::UOCWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // re-evaluated on fire/reload/timer only
}

void UOCWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	Equip(DefaultWeapon);
}

void UOCWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Automatic fire while the trigger is held.
	if (EquippedWeapon && EquippedWeapon->FireRate > 0.f && !bReloading)
	{
		const float Interval = 1.f / EquippedWeapon->FireRate;
		if (GetWorld()->GetTimeSeconds() >= NextFireTime)
		{
			TryFire();
		}
	}
}

void UOCWeaponComponent::Equip(UOCWeaponData* Weapon)
{
	EquippedWeapon = Weapon;
	AmmoInMagazine = Weapon ? Weapon->MagazineSize : 0;
	AmmoReserve = 0; // reserve comes from the ammo pickup system; 0 until M6 pickups exist
	bReloading = false;
	NextFireTime = 0.f;

	UE_LOG(LogOpenCity, Log, TEXT("Combat: equipped %s (ammo %d)"),
		Weapon ? *Weapon->DisplayName.ToString() : TEXT("unarmed"), AmmoInMagazine);
}

void UOCWeaponComponent::Fire()
{
	if (!EquippedWeapon || bReloading) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextFireTime) return;

	TryFire();
}

void UOCWeaponComponent::StopFire()
{
	// Automatic fire is driven by TickComponent; nothing to stop here.
}

void UOCWeaponComponent::TryFire()
{
	if (!EquippedWeapon || bReloading) return;
	if (AmmoInMagazine < EquippedWeapon->AmmoPerShot)
	{
		Reload();
		return;
	}

	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char || !Char->GetController()) return;

	const FVector EyeLoc = Char->GetActorLocation() + FVector(0.f, 0.f, 80.f); // approx eye height
	const FRotator EyeRot = Char->GetController()->GetControlRotation();
	const FVector Dir = EyeRot.Vector();
	const FVector End = EyeLoc + Dir * EquippedWeapon->Range;

	// Hitscan: apply damage to the first IOCDamageable in line.
	FHitResult Hit;
	FCollisionQueryParams TraceParams;
	TraceParams.AddIgnoredActor(GetOwner());
	if (GetWorld()->LineTraceSingleByChannel(Hit, EyeLoc, End, ECC_Visibility, TraceParams))
	{
		if (IOCDamageable* Damageable = Cast<IOCDamageable>(Hit.GetActor()))
		{
			const FVector Impulse = Hit.ImpactNormal * EquippedWeapon->HitImpulse;
			Damageable->ApplyDamage(EquippedWeapon->Damage, Impulse, GetOwner());
		}
	}

	// Projectile variant (visual tracer): spawn a pooled projectile if a class is set.
	if (EquippedWeapon->ProjectileClass)
	{
		AOCProjectile* Proj = nullptr;
		for (AOCProjectile* P : ProjectilePool)
		{
			if (P && !P->IsInFlight())
			{
				Proj = P;
				break;
			}
		}
		if (!Proj)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			Proj = GetWorld()->SpawnActor<AOCProjectile>(FVector(0.f, 0.f, -200000.f), FRotator::ZeroRotator, Params);
			if (Proj) ProjectilePool.Add(Proj);
		}
		if (Proj)
		{
			Proj->Fire(EyeLoc, Dir, 1800.f, EquippedWeapon->Damage, GetOwner(), EquippedWeapon->ProjectileClass);
		}
	}

	AmmoInMagazine -= EquippedWeapon->AmmoPerShot;
	NextFireTime = GetWorld()->GetTimeSeconds() + (EquippedWeapon->FireRate > 0.f
		? 1.f / EquippedWeapon->FireRate : 0.25f);

	UE_LOG(LogOpenCity, Log, TEXT("Combat: fire %s (mag %d / reserve %d)"),
		*EquippedWeapon->DisplayName.ToString(), AmmoInMagazine, AmmoReserve);
}

bool UOCWeaponComponent::Reload()
{
	if (bReloading || AmmoInMagazine >= EquippedWeapon->MagazineSize || AmmoReserve <= 0)
	{
		return false;
	}
	bReloading = true;
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &UOCWeaponComponent::FinishReload,
		EquippedWeapon->ReloadSeconds, false);
	return true;
}

void UOCWeaponComponent::FinishReload()
{
	bReloading = false;
	const int32 Needed = EquippedWeapon->MagazineSize - AmmoInMagazine;
	const int32 ToAdd = FMath::Min(Needed, AmmoReserve);
	AmmoInMagazine += ToAdd;
	AmmoReserve -= ToAdd;
	NextFireTime = GetWorld()->GetTimeSeconds() + 0.25f;

	UE_LOG(LogOpenCity, Log, TEXT("Combat: reloaded (mag %d / reserve %d)"),
		AmmoInMagazine, AmmoReserve);
}

void UOCWeaponComponent::SetAiming(bool bAimingIn)
{
	bAiming = bAimingIn;
}
