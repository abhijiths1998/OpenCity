#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OCProjectile.generated.h"

class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * A pooled projectile (M6), owned by a UOCWeaponComponent: flies straight, and on its first
 * blocking hit applies its damage (0 for a hitscan weapon's tracer) through IOCDamageable and
 * returns to the pool. It ignores the shooter. Subclass in Blueprint for the look only.
 */
UCLASS()
class OPENCITY_API AOCProjectile : public AActor
{
	GENERATED_BODY()

public:
	AOCProjectile();

	/** Launch from Origin along Dir at SpeedCmS, carrying Damage, ignoring Shooter. */
	void Fire(const FVector& Origin, const FVector& Dir, float SpeedCmS, float Damage, AActor* Shooter);

	/** Return to the pool (on hit or after its lifetime). */
	void ReturnToPool();

	/** Whether this projectile is currently in flight. */
	bool IsInFlight() const { return bInFlight; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	float Damage = 0.f;
	TWeakObjectPtr<AActor> Shooter;
	bool bInFlight = false;

	FTimerHandle LifetimeTimer;
};
