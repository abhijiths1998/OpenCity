#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/OCTrafficSim.h"
#include "OCTrafficSubsystem.generated.h"

class AOCTrafficVehicle;
class AOCVehicle;
class UOCVehicleData;
class FOCLaneGraph;

/**
 * Traffic around the player (M5): FOCTrafficSim on the road graph's lanes, shown by a fixed pool of
 * AOCTrafficVehicle actors (allocated at load, so play never spawns actors). Each frame: one sim step,
 * a forward probe per car for things outside the sim (the player's car), and a ground trace per axle.
 * Twice a second: despawn far cars out of view and fill the budget on lanes 60-200 m away, never in view
 * within 150 m. Console: oc.Traffic.Enabled, oc.Traffic.Budget, oc.dev.traffic (stats).
 */
UCLASS()
class OPENCITY_API UOCTrafficSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bReady; }

	const FOCTrafficSim& GetSim() const { return Sim; }
	int32 GetBudget() const;
	FString DescribeStats() const;

	/**
	 * Swap a traffic car for its pooled drivable twin (same model, paint and place; its traffic speed plus
	 * Impulse, or standing still with bStop). The traffic car goes back to the pool. Null if no twin is free.
	 */
	AOCVehicle* Physicalize(AOCTrafficVehicle* Traffic, const FVector& Impulse, bool bStop);

	/** A physics body hit this traffic car: physicalize it on the next tick. */
	void RequestKnock(AOCTrafficVehicle* Traffic, const FVector& Impulse);

	/** The actor showing a sim car (null if none). */
	AOCTrafficVehicle* FindActor(int32 SimId) const;

private:
	void Populate(const FVector& View, const FVector& ViewDir);
	void PlaceActors(float DeltaTime);
	bool InView(const FVector& At, const FVector& View, const FVector& ViewDir) const;
	bool GroundAt(const FVector& At, FVector& OutGround) const;
	void Release(int32 Index);

	FOCTrafficSim Sim;
	const FOCLaneGraph* Lanes = nullptr;
	FRandomStream Rng;
	bool bReady = false;
	float PopulateIn = 0.f;
	int32 Spawned = 0, Despawned = 0, SpawnRejected = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AOCTrafficVehicle>> Pool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UOCVehicleData>> Models;

	/** Drivable twins, parked until a traffic car turns physical. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AOCVehicle>> PhysPool;

	TArray<TPair<TWeakObjectPtr<AOCTrafficVehicle>, FVector>> PendingKnocks;
	int32 Knocked = 0, Taken = 0;
};
