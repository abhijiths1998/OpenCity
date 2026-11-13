#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

class FOCLaneGraph;

/** One kinematic traffic car: on a lane, or crossing a junction along a lane link. */
struct OPENCITYCORE_API FOCTrafficCar
{
	int32 Id = INDEX_NONE;
	int32 Lane = INDEX_NONE;        // the lane it is on, or the lane it came from while on a link
	int32 Link = INDEX_NONE;        // crossing a junction on this link (INDEX_NONE on a lane)
	int32 NextLink = INDEX_NONE;    // the link it will take at the end of its lane (chosen on entry)
	double S = 0.0;                 // cm along the lane, or along the link's curve
	double Speed = 0.0;             // cm/s
	double Accel = 0.0;             // cm/s^2, last step
	float Temper = 1.0f;            // driver variation: scales desired speed (0.85 to 1.1)
	bool bHolding = false;          // waiting at a stop line for the junction to clear
	bool bReserved = false;         // NextLink is held for it (it is past the point where it could still stop)
	double Driven = 0.0;            // cm since it was spawned (tests, stall detection)
	double StoppedFor = 0.0;        // s at a standstill
};

/** Where a car is, for the game layer to place its actor. Z follows the lane line (the road graph's node heights). */
struct OPENCITYCORE_API FOCTrafficPose
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
};

/**
 * Kinematic traffic on FOCLaneGraph (M5), pure logic so it can be unit-tested headless.
 *
 * - Following: the Intelligent Driver Model (IDM) against the next car ahead, looking past the end of
 *   the lane onto the chosen link and the lane after it.
 * - Speed: the lane's limit times the driver's temper; on links, also the curve (lateral acceleration).
 * - Junctions: links at a node conflict when their curves pass within a car's width of each other, or
 *   end in the same lane. A car may start across a junction only when no car is on a conflicting link and
 *   its exit lane has room for it (it never stops inside the junction); otherwise it holds at the stop line.
 *   First to ask is first served.
 * - Routes: at each lane end a random link, never into a closed gate; a car with no way on stops at the end.
 */
class OPENCITYCORE_API FOCTrafficSim
{
public:
	struct FParams
	{
		double CarLengthCm = 450.0;
		double MinGapCm = 200.0;           // IDM s0: bumper to bumper at a standstill
		double HeadwayS = 1.4;             // IDM T
		double MaxAccel = 180.0;           // cm/s^2 (IDM a)
		double ComfortDecel = 300.0;       // cm/s^2 (IDM b)
		double MaxDecel = 900.0;           // cm/s^2: hard cap on braking
		double LateralAccel = 250.0;       // cm/s^2 through junction curves
		double ConflictDistanceCm = 220.0; // two links' curves closer than this conflict
	};

	/** Prepare link curves and the junction conflict table. The lane graph must outlive the sim. */
	void Build(const FOCLaneGraph& InLanes, const FParams& InParams);
	void Build(const FOCLaneGraph& InLanes) { Build(InLanes, FParams()); }

	/** Add a car on a lane at S cm (clamped to the lane). Returns its id. */
	int32 Spawn(int32 Lane, double S, double Speed, FRandomStream& Rng);
	void Remove(int32 Id);
	void Reset();

	/** Advance every car by Dt seconds (call with small steps, e.g. 1/30 s). */
	void Step(double Dt, FRandomStream& Rng);

	/** Lanes or links whose gate is closed are never entered. */
	void SetClosedGates(const TSet<FName>& Gates) { ClosedGates = Gates; }

	const TArray<FOCTrafficCar>& GetCars() const { return Cars; }
	const FOCTrafficCar* FindCar(int32 Id) const;
	FOCTrafficPose GetPose(const FOCTrafficCar& Car) const;

	/** Link curve (quadratic Bezier from the lane end to the next lane's start), at T in [0, 1]. */
	FVector LinkPoint(int32 Link, double T) const;
	double LinkLength(int32 Link) const { return LinkLengths.IsValidIndex(Link) ? LinkLengths[Link] : 0.0; }
	bool LinksConflict(int32 A, int32 B) const;

	/** Number of cars currently on links that conflict with each other (0 always, unless the rules break). */
	int32 CountConflictingCrossings() const;

private:
	double LaneLength(int32 Lane) const;
	double SpeedLimit(const FOCTrafficCar& Car) const;
	int32 ChooseLink(int32 Lane, FRandomStream& Rng) const;
	bool CanEnter(int32 Link, int32 CarIndex) const;
	/** Distance to the next car ahead (rear bumper) and its speed, looking up to Horizon cm ahead. */
	void FindLeader(int32 CarIndex, double Horizon, double& OutGap, double& OutLeaderSpeed) const;
	double LinkMaxSpeed(int32 Link) const;

	const FOCLaneGraph* Lanes = nullptr;
	FParams Params;
	TArray<FOCTrafficCar> Cars;
	TArray<double> LinkLengths;
	TArray<double> LinkMinRadius;      // cm, tightest curvature along the curve
	TArray<FVector> LinkControl;       // Bezier control point
	TArray<TArray<int32>> Conflicts;   // link -> conflicting links (same junction)
	TArray<int32> LinkNode;            // link -> junction node (the lane's ToNode)
	TSet<FName> ClosedGates;
	int32 NextId = 0;
	// Per step: cars by lane and by link, sorted by S (indices into Cars).
	TMap<int32, TArray<int32>> OnLane, OnLink;
};
