#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/OCWeaponData.h"
#include "OCWeaponComponent.generated.h"

class AOCProjectile;
class UOCAmmoData;

/** Weapon handling on a pawn: fire, aim, reload, ammo reserve. Kept small; art/tuning in data assets. */
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

	/** Ammo currently in the magazine. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }

	/** Ammo in reserve for the equipped weapon's ammo type. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	int32 GetAmmoReserve() const { return AmmoReserve; }

	/** Whether the weapon is currently reloading. */
	UFUNCTION(BlueprintPure, Category = "OpenCity|Combat")
	bool IsReloading() const { return bReloading; }

	/** Fire one shot (or start automatic fire while held). Called from input. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void Fire();

	/** Stop automatic fire. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void StopFire();

	/** Reload now. Returns true if a reload started. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	bool Reload();

	/** Aim down sights / weapon aim mode on/off. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Combat")
	void SetAiming(bool bAiming);

	bool IsAiming() const { return bAiming; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void TryFire();
	void StartReload();
	void FinishReload();

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UOCWeaponData> DefaultWeapon;

	UPROPERTY(Transient)
	TObjectPtr<UOCWeaponData> EquippedWeapon;

	UPROPERTY(Transient)
	int32 AmmoInMagazine = 0;

	UPROPERTY(Transient)
	int32 AmmoReserve = 0;

	UPROPERTY(Transient)
	float NextFireTime = 0.f;

	UPROPERTY(Transient)
	bool bReloading = false;

	UPROPERTY(Transient)
	bool bAiming = false;

	FTimerHandle ReloadTimer;

	/** Pooled projectiles for the equipped weapon. */
	TArray<TObjectPtr<AOCProjectile>> ProjectilePool;
};
