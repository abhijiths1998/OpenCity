#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OCWeaponData.generated.h"

class UMaterialInterface;
class USoundBase;
class AOCProjectile;

/** Damage type tag used to route damage through IOCDamageable and to compute heat/crime. */
UENUM(BlueprintType)
enum class EOCWeaponDamageType : uint8
{
	Bullet,
	Melee,
	Explosion,
};

/** Tuning for one weapon model: pistol, SMG, melee. Balance lives here, never in C++. */
UCLASS(BlueprintType)
class OPENCITY_API UOCWeaponData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Info")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, Category = "Info")
	EOCWeaponDamageType DamageType = EOCWeaponDamageType::Bullet;

	/** Damage applied to the target through IOCDamageable::ApplyDamage. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage", meta = (ClampMin = "0"))
	float Damage = 25.f;

	/** Impulse applied to the target on hit (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage", meta = (ClampMin = "0", Units = "cm/s"))
	float HitImpulse = 300.f;

	/** Shots per second. 0 = single-shot (one per trigger press). */
	UPROPERTY(EditDefaultsOnly, Category = "Fire", meta = (ClampMin = "0"))
	float FireRate = 6.f;

	/** Ammo consumed per shot. */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 AmmoPerShot = 1;

	/** Maximum range of a shot (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Fire", meta = (ClampMin = "0", Units = "cm"))
	float Range = 6000.f;

	/** Projectile class to fire (set for hitscan-less weapons). May be NULL for pure hitscan. */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<class AOCProjectile> ProjectileClass;

	/** Decal spawned at impact. May be NULL. */
	UPROPERTY(EditDefaultsOnly, Category = "Impact")
	TObjectPtr<UMaterialInterface> ImpactDecal;

	/** Sound played when firing. May be NULL until M10 MetaSounds. */
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundBase> FireSound;

	/** Ammo reserve consumed per magazine (used by OCWeaponComponent reload). */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MagazineSize = 12;

	/** Seconds to reload a magazine. */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0", Units = "s"))
	float ReloadSeconds = 2.0f;
};

/** Ammo reserve data: how much of an ammo type the player can carry. */
UCLASS(BlueprintType)
class OPENCITY_API UOCAmmoData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Info")
	FText DisplayName;

	/** Max rounds of this ammo type the player can carry in reserve. */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MaxReserve = 120;
};
