#pragma once

#include "CoreMinimal.h"

/**
 * Marevia's road network: nodes (junctions/bends) and segments between them, loaded from
 * SourceArt/World/marevia_layout.json (Tools/make_marevia_layout.py). Pure logic, unit-tested.
 *
 * Positions are Unreal world space (cm). The layout file is in metres with x east, y north;
 * Unreal maps that to X = east, Y = south (Y = -north) so the top-down view keeps north up.
 */
struct OPENCITYCORE_API FOCRoadNode
{
	FVector Location = FVector::ZeroVector;
};

struct OPENCITYCORE_API FOCRoadSegment
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	FName Kind;              // avenue, street, alley, coast, pass, pier
	int32 LanesPerDirection = 1;
	float WidthCm = 900.f;
	float SpeedKmh = 50.f;
	FName Bridge;            // NAME_None unless a named bridge
	FName Gate;              // NAME_None unless closed by a story gate (e.g. Gate.PonteMobile)

	float LengthCm(const TArray<FOCRoadNode>& Nodes) const;
};

class OPENCITYCORE_API FOCRoadGraph
{
public:
	/** Parse the layout JSON ("roads" section). Returns false and fills OutError on failure. */
	bool LoadFromLayoutJson(const FString& JsonText, FString& OutError);

	/** Metres (x east, y north, z up) to Unreal cm (X east, Y south, Z up). */
	static FVector LayoutToWorld(double X, double Y, double Z);

	int32 NumNodes() const { return Nodes.Num(); }
	int32 NumSegments() const { return Segments.Num(); }
	const TArray<FOCRoadNode>& GetNodes() const { return Nodes; }
	const TArray<FOCRoadSegment>& GetSegments() const { return Segments; }

	void AddNode(const FVector& Location);
	void AddSegment(const FOCRoadSegment& Segment);

	/** Closest node to a world position (INDEX_NONE if the graph is empty). */
	int32 FindNearestNode(const FVector& Location) const;

	/**
	 * Shortest path by travel time (A*), skipping segments whose gate is in ClosedGates.
	 * Returns node indices from Start to Goal, or empty if unreachable.
	 */
	TArray<int32> FindPath(int32 Start, int32 Goal, const TSet<FName>& ClosedGates = TSet<FName>()) const;

	/** Travel time in seconds along a path at each segment's speed limit. */
	double PathTimeSeconds(const TArray<int32>& Path) const;

	/** Number of nodes reachable from Start with ClosedGates closed. */
	int32 CountReachable(int32 Start, const TSet<FName>& ClosedGates = TSet<FName>()) const;

private:
	void RebuildAdjacency();
	double SegmentTime(const FOCRoadSegment& S) const;

	TArray<FOCRoadNode> Nodes;
	TArray<FOCRoadSegment> Segments;
	TArray<TArray<int32>> Adjacency; // node -> segment indices
};
