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

/**
 * Cross-section and design limits of one road class (INFRA_DESIGN.md; source of truth:
 * SourceArt/Infra/road_profiles.json, embedded in the layout by Tools/make_marevia_layout.py).
 * Widths in cm. DMType is the Italian D.M. 5/11/2001 type (A, B, C1, C2, D, E, F, F1, F2) or
 * NAME_None for streets the decree doesn't classify (centro storico lanes, piers, gravel tracks).
 */
struct OPENCITYCORE_API FOCRoadProfile
{
	FName Id;
	FName DMType;
	bool bUrban = true;
	float LaneCm = 350.f;
	int32 LanesPerDirection = 1;
	float ShoulderLeftCm = 0.f;      // banchina beside the median / centre line
	float ShoulderRightCm = 0.f;     // banchina on the outside
	float EmergencyLaneCm = 0.f;     // corsia di emergenza (A, B)
	float MedianCm = 0.f;            // spartitraffico
	float FootpathCm = 0.f;          // per side
	float ParkingCm = 0.f;           // total parking strip between the kerbs
	float MaxGradePct = 8.f;
	float MinRadiusM = 0.f;          // 0: no limit (piazzas, piers)
	float DesignSpeedMaxKmh = 50.f;
	FName Paving;

	/** Paved width between the kerbs: lanes, shoulders, emergency lanes, median and parking. */
	float CarriagewayCm() const;
};

struct OPENCITYCORE_API FOCRoadSegment
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	FName Kind;              // avenue, street, alley, coast, pass, pier (geometry family)
	int32 LanesPerDirection = 1;
	float WidthCm = 900.f;
	float SpeedKmh = 50.f;
	FName Bridge;            // NAME_None unless a named bridge
	FName Gate;              // NAME_None unless closed by a story gate (e.g. Gate.PonteMobile)
	// Infrastructure metadata (INFRA_DESIGN.md); defaults keep older layouts loading.
	FName Profile;           // FOCRoadProfile::Id
	bool bOneWay = false;    // traffic only from A to B
	FName Paving;            // asphalt, sampietrini, porfido, lastricato, brick, concrete, gravel_white
	FName Era;               // roman, medieval, c19, postwar, modern
	float Wear = 0.5f;       // 0 new .. 1 worn out
	bool bZTL = false;       // inside the limited-traffic zone
	FName Parking;           // none, blue (paid), white (free), yellow (reserved)
	bool bTram = false;      // tram tracks in the carriageway

	float LengthCm(const TArray<FOCRoadNode>& Nodes) const;
	/** True if traffic may drive this segment from node From. */
	bool AllowsFrom(int32 From) const { return !bOneWay || From == A; }
};

/** A junction node and how traffic is controlled there (roundabout, signals, piazza, ...). */
struct OPENCITYCORE_API FOCRoadJunction
{
	int32 Node = INDEX_NONE;
	FName Type;              // priority, blind_t, signalized, roundabout, piazza, gate_fork, ztl_gate, level_crossing, tram_crossing, bridge_head
	bool bPlanned = false;   // the type is designed but the geometry doesn't show it yet

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
	const TArray<FOCRoadJunction>& GetJunctions() const { return Junctions; }
	/** The profile a segment uses (nullptr if unknown). */
	const FOCRoadProfile* FindProfile(FName Id) const { return Profiles.Find(Id); }
	const TMap<FName, FOCRoadProfile>& GetProfiles() const { return Profiles; }
	void AddProfile(const FOCRoadProfile& Profile) { Profiles.Add(Profile.Id, Profile); }
	void AddJunction(const FOCRoadJunction& Junction) { Junctions.Add(Junction); }
	/** Segment indices meeting at a node. */
	const TArray<int32>& SegmentsAt(int32 Node) const;

	void AddNode(const FVector& Location);
	void AddSegment(const FOCRoadSegment& Segment);

	/** Closest node to a world position (INDEX_NONE if the graph is empty). */
	int32 FindNearestNode(const FVector& Location) const;

	/**
	 * Shortest path by travel time (A*), skipping segments whose gate is in ClosedGates and
	 * driving one-way segments only from A to B.
	 * Returns node indices from Start to Goal, or empty if unreachable.
	 */
	TArray<int32> FindPath(int32 Start, int32 Goal, const TSet<FName>& ClosedGates = TSet<FName>()) const;

	/** Travel time in seconds along a path at each segment's speed limit. */
	double PathTimeSeconds(const TArray<int32>& Path) const;

	/** Number of nodes reachable from Start with ClosedGates closed. */
	int32 CountReachable(int32 Start, const TSet<FName>& ClosedGates = TSet<FName>()) const;

	/** Per node: reachable from Start with ClosedGates closed (all false if Start is invalid). */
	TBitArray<> ReachableFrom(int32 Start, const TSet<FName>& ClosedGates = TSet<FName>()) const;

	/** Closest node among those with Allowed[i] set (INDEX_NONE if none). */
	int32 FindNearestNode(const FVector& Location, const TBitArray<>& Allowed) const;

private:
	void RebuildAdjacency();
	double SegmentTime(const FOCRoadSegment& S) const;

	TArray<FOCRoadNode> Nodes;
	TArray<FOCRoadSegment> Segments;
	TArray<TArray<int32>> Adjacency; // node -> segment indices
	TArray<FOCRoadJunction> Junctions;
	TMap<FName, FOCRoadProfile> Profiles;
};
