#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Damage/OCDamageable.h"
#include "OCProjectile.generated.h"

class UStaticMeshComponent;
class UProjectileMovementComponent;

/** A pooled projectile: fires straight, applies damage through IOCDamageable on hit. */
UCLASS()
class OPENCITY_API AOCProjectile : public AActor, public IOCDamageable
{
	GENERATED_BODY()

public:
	AOCProjectile();

	/** Acquire from the pool: fire from Origin along Dir at SpeedCmS, with Damage and Instigator. */
	void Fire(const FVector& Origin, const FVector& Dir, float SpeedCmS, float Damage,
		const AActor* Instigator, TSubclassOf<AOCProjectile> ProjectileClass);

	/** Return to the pool (called by the subsystem after lifetime expires or on hit). */
	void ReturnToPool();

	/** Whether this projectile is currently in flight. */
	bool IsInFlight() const { return bInFlight; }

	/**~ IOCDamageable interface (projectiles are damageable so they can be shot down / deflected). */
	virtual bool ApplyDamage(float Amount, const FVector& Impulse, const AActor* Instigator) override;
	virtual float GetHealthFraction() const override { return 1.f; }
	virtual bool IsDead() const override { return false; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

	float Damage = 0.f;
	TWeakObjectPtr<const AActor> Instigator;
	bool bInFlight = false;

	FTimerHandle ProjectileLifetimeTimer;
};
