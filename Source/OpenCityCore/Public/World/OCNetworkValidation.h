#pragma once

#include "CoreMinimal.h"

class FOCRoadGraph;
class FOCRailNetwork;
class FOCWaterNetwork;

/** One finding of the network validator. */
struct OPENCITYCORE_API FOCNetworkIssue
{
	FName Check;                 // profile, width, lanes, gradient, radius, speed, connectivity, oneway, ztl, tram, junction, rail_*, water_*
	bool bError = false;         // errors break gameplay or data; warnings are design deviations
	int32 Segment = INDEX_NONE;  // road segment, rail line or water body index
	int32 Node = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
	FString Message;
};

struct OPENCITYCORE_API FOCNetworkReport
{
	TArray<FOCNetworkIssue> Issues;

	int32 NumErrors() const;
	int32 Num(FName Check) const;
	/** "check: N errors, M warnings" lines, then up to MaxPerCheck examples per check. */
	FString Summary(int32 MaxPerCheck = 5) const;
};

/**
 * Data-level checks for the road, rail and water networks (INFRA_DESIGN.md, "Validation").
 * Pure logic, unit-tested; run on the shipped assets by Content/Python/oc_validate_networks.py.
 */
class OPENCITYCORE_API FOCNetworkValidator
{
public:
	struct FOptions
	{
		int32 StartNode = 0;            // connectivity is measured from here (gates open)
		float WidthTolerance = 0.12f;   // segment width vs profile carriageway
		float GradeTolerancePct = 1.f;  // the decree allows +1 point where justified
		float MinTurnDeg = 3.f;         // radius is checked only where the road actually bends
	};

	static void ValidateRoads(const FOCRoadGraph& Roads, const FOptions& Options, FOCNetworkReport& Out);
	static void ValidateRail(const FOCRailNetwork& Rail, FOCNetworkReport& Out);
	static void ValidateWater(const FOCWaterNetwork& Water, FOCNetworkReport& Out);

	/** Radius (cm) of the circle through three points in plan view; huge for straight lines. */
	static double PlanRadiusCm(const FVector& A, const FVector& B, const FVector& C);
};
