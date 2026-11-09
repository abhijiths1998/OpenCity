#pragma once

#include "CoreMinimal.h"

class FOCRoadGraph;

/** How a lane link turns, seen from the driver (right-hand traffic). */
enum class EOCLaneTurn : uint8
{
	Straight,
	Right,
	Left,
	UTurn,
};

/**
 * One directed driving lane along one road segment, as a straight line between the two
 * junctions it connects (pulled back from each junction so turns have room).
 * Lane 0 is next to the centre line; higher indices are further right.
 */
struct OPENCITYCORE_API FOCLane
{
	int32 Segment = INDEX_NONE;
	int32 FromNode = INDEX_NONE;   // travel direction: FromNode -> ToNode
	int32 ToNode = INDEX_NONE;
	int32 Index = 0;               // 0 = innermost
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	float WidthCm = 350.f;
	float SpeedKmh = 50.f;
	FName Gate;                    // the segment's story gate (NAME_None if ungated)

	FVector Direction() const { return (End - Start).GetSafeNormal(); }
	double LengthCm() const { return FVector::Dist(Start, End); }
};

/** A legal move from the end of one lane to the start of another, across a junction. */
struct OPENCITYCORE_API FOCLaneLink
{
	int32 FromLane = INDEX_NONE;
	int32 ToLane = INDEX_NONE;
	EOCLaneTurn Turn = EOCLaneTurn::Straight;
};

/**
 * Driving lanes derived from the road graph, so traffic, police routing and the minimap
 * read the same network that the road meshes were built from. Pure logic, unit-tested.
 *
 * Rules: right-hand traffic. Straight on keeps the lane (extra lanes merge or split at the
 * edge); right turns use the outermost lanes, left turns the innermost; U-turns only at
 * dead ends. Every lane that ends at a junction with any way on has at least one link.
 */
class OPENCITYCORE_API FOCLaneGraph
{
public:
	void Build(const FOCRoadGraph& Roads);

	int32 NumLanes() const { return Lanes.Num(); }
	int32 NumLinks() const { return Links.Num(); }
	const TArray<FOCLane>& GetLanes() const { return Lanes; }
	const TArray<FOCLaneLink>& GetLinks() const { return Links; }

	/** Links leaving the end of a lane (indices into GetLinks()). */
	const TArray<int32>& GetOutLinks(int32 Lane) const;

	/** Lanes of one segment in one direction, innermost first. */
	TArray<int32> GetSegmentLanes(int32 Segment, int32 FromNode) const;

	/**
	 * The lane closest to Location (distance to its line), preferring lanes heading along
	 * Forward when Forward is non-zero (lanes facing more than 90 degrees away are skipped).
	 * Lanes whose gate is in ClosedGates are skipped. INDEX_NONE if none qualifies.
	 */
	int32 FindNearestLane(const FVector& Location, const FVector& Forward = FVector::ZeroVector,
		const TSet<FName>& ClosedGates = TSet<FName>()) const;

	/** Lateral offset of a lane's centre from the road centre line (cm, positive = right). */
	static double LaneOffsetCm(float RoadWidthCm, int32 LanesPerDirection, int32 Index);

	/** Turn type for driving along InDir and leaving along OutDir (both horizontal). */
	static EOCLaneTurn ClassifyTurn(const FVector& InDir, const FVector& OutDir);

private:
	TArray<FOCLane> Lanes;
	TArray<FOCLaneLink> Links;
	TArray<TArray<int32>> OutLinks; // lane -> link indices
	TMap<TPair<int32, int32>, TArray<int32>> BySegmentDir; // (segment, from node) -> lanes
};
