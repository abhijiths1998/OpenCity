#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Events/OCEventSubsystem.h"
#include "Population/OCPopulationSettings.h"
#include "Population/OCCrowdSim.h"
#include "OCPopulationSubsystem.generated.h"

class AOCPedestrian;
class UOCTrafficSubsystem;
class FOCLaneGraph;

/**
 * Pedestrians around the player (M5): a fixed pool of AOCPedestrian actors allocated at load,
 * driven by the headless crowd sim at AiRateHz. Between AI steps each ped is extrapolated along
 * its heading so movement is smooth, and its feet follow the ground with one short trace a frame.
 * Peds flee threats (moving traffic, Event.Crime.Gunshot / Murder / Assault) and wander otherwise.
 * Spawned only outside view and beyond 150 m, despawned beyond 250 m — no pop-in, no per-frame
 * AI, no play-time spawning (ARCHITECTURE.md section 8).
 *
 * Damage: AOCPedestrian forwards IOCDamageable hits here; health lives in the sim. The player's
 * vehicle knocks peds down when it reaches them above VehicleKnockdownKmh.
 *
 * Console: oc.population.Enabled, oc.population.Budget. Dev: oc.dev.population (stats | gunshot X Y).
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

	/** Make every ped near Location (world cm) flee, as if a shot was fired there. */
	void ReportGunshot(const FVector& Location);

	/** Damage a ped through the sim (from AOCPedestrian::ApplyDamage). Amount is in hit points. */
	bool DamagePed(AOCPedestrian* Ped, float Amount, const FVector& Impulse, const AActor* Instigator);

	/** The nearest live, active ped to Location within MaxDistCm (dev commands, tests). */
	AOCPedestrian* FindNearestPed(const FVector& Location, double MaxDistCm) const;

	/** Live, active peds (dev commands). */
	TArray<AOCPedestrian*> GetActivePeds() const;

private:
	void Populate(const FVector& View, const FVector& ViewDir);
	void PlacePeds(double SinceStep);
	void GatherThreats();
	void KnockdownByPlayerVehicle();
	void AddThreat(const FVector& Location, double RadiusCm, float Weight);
	bool InView(const FVector& At, const FVector& View, const FVector& ViewDir) const;
	bool GroundAt(const FVector& At, double UpCm, double DownCm, FVector& OutGround) const;
	void Release(int32 Index);
	void OnCrimeEvent(const FOCEvent& Ev);

	FOCCrowdSim Sim;
	const FOCLaneGraph* Lanes = nullptr;   // spawn points (traffic lanes)
	FRandomStream Rng;
	bool bReady = false;
	double AiIn = 0.0;        // seconds until the next AI step
	double SinceStep = 0.0;   // seconds since the last AI step (extrapolation)
	int32 Spawned = 0, Despawned = 0, Killed = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AOCPedestrian>> Pool;

	/** Developer settings (class default object, lives for the whole run). */
	const UOCPopulationSettings* Settings = nullptr;

	TWeakObjectPtr<UOCTrafficSubsystem> Traffic;
	TWeakObjectPtr<UOCEventSubsystem> Events;
};
