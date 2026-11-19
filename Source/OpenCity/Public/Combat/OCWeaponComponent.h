#pragma once

#include "CoreMinimal.h"
#include "Combat/OCAmmo.h"
#include "Components/ActorComponent.h"
#include "Combat/OCWeaponData.h"
#include "OCWeaponComponent.generated.h"

class AOCProjectile;

/**
 * Weapon handling on a pawn (M6): fire, aim, reload, ammo. Tuning lives in UOCWeaponData.
 * Never ticks: automatic fire runs on a timer while the trigger is held. A shot traces from the
 * owner's view point (the camera for the player) so it lands under the crosshair, then checks the
 * line from the shooter to that point; damage goes through IOCDamageable, and every shot publishes
 * Event.Crime.Gunshot (peds panic, wanted heat).
 */
UCLASS(ClassGroup = (OpenCity), meta = (BlueprintSpawnableComponent))
class OPENCITY_API UOCWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOCWeaponComponent();

	/** Equip a weapon data asset (or nullptr to unequip). */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void Equip(UOCWeaponData* Weapon);

	/** Current equipped weapon data (nullptr if unarmed). */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	UOCWeaponData* GetEquippedWeapon() const { return EquippedWeapon; }

	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	int32 GetAmmoInMagazine() const { return Ammo.InMagazine; }

	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	int32 GetAmmoReserve() const { return Ammo.Reserve; }

	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	bool IsReloading() const { return bReloading; }

	/** Trigger pressed: fire one shot now if allowed, and keep firing while held if automatic. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void Fire();

	/** Trigger released. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void StopFire();

	/** Reload now. Returns true if a reload started. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	bool Reload();

	/** Add reserve rounds for the equipped weapon (pickups), capped at its MaxReserve. */
	void AddAmmo(int32 Rounds);

	/** Aim down sights / weapon aim mode on/off. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void SetAiming(bool bAiming);

	bool IsAiming() const { return bAiming; }

	/** Shots fired since equip (dev logging). */
	int32 GetShotsFired() const { return ShotsFired; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** One shot if the weapon is ready; returns whether it fired. */
	bool TryFire();
	void AutoFireTick();
	void FinishReload();
	void SpawnTracer(const FVector& From, const FVector& To);
	float ShotInterval() const;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UOCWeaponData> DefaultWeapon;

	UPROPERTY(Transient)
	TObjectPtr<UOCWeaponData> EquippedWeapon;

	FOCAmmo Ammo;
	double NextFireTime = 0.0;
	bool bReloading = false;
	bool bAiming = false;
	bool bTriggerHeld = false;
	int32 ShotsFired = 0;

	FTimerHandle ReloadTimer;
	FTimerHandle AutoFireTimer;

	/** Pooled projectiles / tracers for the equipped weapon. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AOCProjectile>> ProjectilePool;
};
