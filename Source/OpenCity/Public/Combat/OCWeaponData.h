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

	/** Hit points per hit, through IOCDamageable::ApplyDamage (a pedestrian has 100). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage", meta = (ClampMin = "0"))
	float Damage = 25.f;

	/** Impulse applied to the target on hit (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage", meta = (ClampMin = "0", Units = "cm/s"))
	float HitImpulse = 300.f;

	/** Maximum shots per second (also caps how fast a semi-automatic can be tapped). */
	UPROPERTY(EditDefaultsOnly, Category = "Fire", meta = (ClampMin = "0.1"))
	float FireRate = 4.f;

	/** Keep firing at FireRate while the trigger is held; false = one shot per press. */
	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	bool bAutomatic = false;

	/** Damage is applied by an instant trace. ProjectileClass (if set) is then only a visual tracer;
	 *  with bHitscan off the projectile carries the damage instead. */
	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	bool bHitscan = true;

	/** Projectile / tracer speed (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Projectile", meta = (ClampMin = "100", Units = "cm/s"))
	float ProjectileSpeed = 30000.f;

	/** How loud a shot is as a crime (Event.Crime.Gunshot severity: wanted heat and how far peds panic). */
	UPROPERTY(EditDefaultsOnly, Category = "Crime", meta = (ClampMin = "0", ClampMax = "1"))
	float GunshotSeverity = 0.5f;

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

	/** Rounds per magazine. */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MagazineSize = 12;

	/** Rounds carried in reserve when the weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 StartingReserve = 36;

	/** Most reserve rounds the player can carry for this weapon. */
	UPROPERTY(EditDefaultsOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MaxReserve = 120;

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
