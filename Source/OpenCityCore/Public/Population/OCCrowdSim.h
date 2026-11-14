#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Pedestrian states (M5). Dead peds stop and wait for the game layer to despawn them.
 */
enum class EOCPedState : uint8
{
	Idle,
	Wander,
	Flee,
	Dead,
};

/** One pedestrian in the pure crowd sim: 2D position + behaviour state. Z is resolved by the game layer. */
struct OPENCITYCORE_API FOCPedestrian
{
	int32 Id = INDEX_NONE;
	FVector2D Pos = FVector2D::ZeroVector;   // cm
	FVector2D Dir = FVector2D::UnitX();      // facing (unit)
	double Speed = 0.0;                      // cm/s
	EOCPedState State = EOCPedState::Idle;
	double StateTimer = 0.0;                 // s left in Idle/Wander
	FVector2D WanderTarget = FVector2D::ZeroVector;
	double FleeUntil = -1.0;                 // absolute sim time until which it keeps fleeing
	FVector2D FleeFrom = FVector2D::ZeroVector; // last threat position fled from
	float Health = 1.f;
};

/** Something a pedestrian runs from: a moving vehicle, a gunshot, an explosion. Valid for one Step only. */
struct OPENCITYCORE_API FOCThreat
{
	FVector2D Pos = FVector2D::ZeroVector;   // cm
	FVector2D Vel = FVector2D::ZeroVector;   // cm/s (0 for a stationary shot)
	double RadiusCm = 1200.0;                // noticed within this
	float Weight = 1.f;                      // 0..1, how scary (a gunshot is scarier than a slow car)
};

/**
 * Pure crowd logic (M5): idle / wander / flee on the 2D plane, headless-testable like FOCTrafficSim.
 *
 * - Threats are reported per Step (AddThreat). A ped within a threat's radius sets its FleeUntil
 *   to Now + FleeDurationS and runs away from the nearest threat at FleeSpeed.
 * - Otherwise it wanders between nearby targets at WalkSpeed, idling between them.
 * - Neighbours within SeparationCm push each other apart.
 * - Damage(Id, Amount) reduces Health; at 0 the ped goes Dead and stops.
 *
 * The sim never touches the world: the game layer (UOCPopulationSubsystem) places actors from
 * GetPeds() and resolves Z with a ground trace at AI rate.
 */
class OPENCITYCORE_API FOCCrowdSim
{
public:
	struct FParams
	{
		double WalkSpeedCm = 150.0;        // cm/s
		double FleeSpeedCm = 450.0;        // cm/s
		double TurnRateDegS = 300.0;       // max heading change
		double IdleMinS = 2.0, IdleMaxS = 8.0;
		double WanderMinS = 4.0, WanderMaxS = 12.0;
		double WanderRadiusCm = 2500.0;    // how far a wander target may be
		double ThreatRadiusCm = 1500.0;    // a threat is noticed within this
		double FleeDurationS = 6.0;        // keeps fleeing this long after the last threat
		double SeparationCm = 70.0;        // peds keep at least this apart
		double FleeBrakeCm = 250.0;        // don't stop right at the threat's edge
	};

	int32 Spawn(const FVector2D& Pos, FRandomStream& Rng);
	void Remove(int32 Id);
	void Reset();

	/** Report a threat for this step. Call before Step. */
	void AddThreat(const FOCThreat& T) { Threats.Add(T); }

	/** Advance all peds by Dt seconds at absolute time Now. Call at AI rate (e.g. 4 Hz). */
	void Step(double Dt, double Now, FRandomStream& Rng);

	/** Damage a ped; returns false if it was already dead. */
	bool Damage(int32 Id, float Amount);

	const TArray<FOCPedestrian>& GetPeds() const { return Peds; }
	const FOCPedestrian* FindPed(int32 Id) const;

	void SetParams(const FParams& In) { Params = In; }
	const FParams& GetParams() const { return Params; }

private:
	void PickWanderTarget(FOCPedestrian& P, FRandomStream& Rng) const;
	void SteerToward(FOCPedestrian& P, const FVector2D& To, double Dt) const;
	/** Nearest threat within notice radius; returns its index or INDEX_NONE. */
	int32 NearestThreat(const FOCPedestrian& P) const;

	FParams Params;
	TArray<FOCPedestrian> Peds;
	TArray<FOCThreat> Threats;   // this step only
	int32 NextId = 0;
};
