#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/OCEventSubsystem.h"
#include "Population/OCPopulationSettings.h"
#include "Population/OCCrowdSim.h"
#include "OCPopulationSubsystem.generated.h"

class AOCPedestrian;
class UOCTrafficSubsystem;
class UOCWorldStateSubsystem;
class FOCLaneGraph;

/**
 * Pedestrians around the player (M5): a fixed pool of AOCPedestrian actors allocated at load,
 * driven by the headless crowd sim at ~4 Hz. Peds flee threats (moving vehicles, gunshots)
 * and wander idly otherwise. Spawned only outside view and beyond 150 m, despawned beyond 250 m
 * — no pop-in, no per-frame AI, no play-time spawning (ARCHITECTURE.md section 8).
 *
 * Console: oc.population.Enabled, oc.population.Budget. Dev: oc.dev.population (stats|view|gunshot X Y).
 */
UCLASS()
class OPENCITY_API UOCPopulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bReady; }

	int32 GetBudget() const;
	FString DescribeStats() const;

	/** Report a gunshot at WorldLocation so every nearby ped flees (called by weapons/deaths). */
	void ReportGunshot(const FVector& Location);

private:
	void Populate(const FVector& View, const FVector& ViewDir);
	void PlacePeds(float DeltaTime);
	void GatherThreats(const FVector& View);
	bool InView(const FVector& At, const FVector& View, const FVector& ViewDir) const;
	bool GroundAt(const FVector& At, FVector& OutGround) const;
	void Release(int32 Index);

	FOCCrowdSim Sim;
	const FOCLaneGraph* Lanes = nullptr;   // ground heights for lane walking (optional, traffic lanes)
	FRandomStream Rng;
	bool bReady = false;
	float AiIn = 0.f;
	int32 Spawned = 0, Despawned = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AOCPedestrian>> Pool;

	/** Developer settings (global, not GC-tracked). */
	UOCPopulationSettings* Settings = nullptr;

	/** Subsystem handles (queried lazily). */
	TWeakObjectPtr<UOCTrafficSubsystem> Traffic;
	TWeakObjectPtr<UOCWorldStateSubsystem> WorldState;
	TWeakObjectPtr<UOCEventSubsystem> Events;

	/** React to a crime event: nearby peds panic/flee. */
	void OnCrimeEvent(const FOCEvent& Ev);
};
