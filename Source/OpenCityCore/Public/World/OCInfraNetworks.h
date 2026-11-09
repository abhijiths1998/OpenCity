#pragma once

#include "CoreMinimal.h"

/**
 * Rail and water networks (INFRA_DESIGN.md, "Data model"). Like the road graph they are data from
 * Tools/make_marevia_layout.py ("rail" and "water_network" sections of marevia_layout.json); meshes,
 * props, lanes and validation are generated from them. Positions are Unreal world space (cm).
 */

/** A station or stop on a rail line. */
struct OPENCITYCORE_API FOCRailStation
{
	FName Name;
	int32 PointIndex = INDEX_NONE;  // the line point the platform is centred on
	float PlatformCm = 0.f;
};

/** One rail line: regional train, tram or funicular. */
struct OPENCITYCORE_API FOCRailLine
{
	FName Id;
	FName Kind;                  // regional, tram, funicular
	int32 GaugeMm = 1435;
	FName Electrification;       // 3kV_DC, 750V_DC, 600V_DC, none
	float MaxGradePct = 0.f;     // design limit for this line (0: take the kind's default)
	TArray<FVector> Points;
	TArray<FOCRailStation> Stations;

	float LengthCm() const;
	/** The kind's default gradient limit: regional 2.5%, tram 8%, funicular unlimited. */
	float GradeLimitPct() const;
};

/** One stretch of a water body's edge and how it is treated (quay, embankment, beach, ...). */
struct OPENCITYCORE_API FOCWaterEdge
{
	FName Treatment;             // quay, embankment_wall, walkway_low, natural_bank, beach_sand, beach_pebble, rock_platform, cliff, breakwater, dam, gravel_bed
	FName Side;                  // left, right, both, all
	float FromM = 0.f;           // along the body's centreline or shoreline (m)
	float ToM = 0.f;
};

/** A sea, port basin, river, torrent, reservoir or canal. */
struct OPENCITYCORE_API FOCWaterBody
{
	FName Id;
	FName Type;                  // sea, port, river, torrent, reservoir, estuary, canal
	float DepthM = 0.f;
	float FlowMps = 0.f;         // downstream speed; 0 for still water
	float LevelM = 0.f;          // surface height above sea level (rivers: at the mouth)
	bool bSeasonal = false;      // mostly dry (torrents)
	bool bRisesWithWeather = false;
	TArray<FVector> Centerline;  // rivers and torrents; empty for areas
	TArray<FOCWaterEdge> Edges;
};

class OPENCITYCORE_API FOCRailNetwork
{
public:
	/** Parse the "rail" section ({"lines": [...]}); an absent section is an empty network. */
	bool LoadFromLayoutJson(const FString& JsonText, FString& OutError);
	const TArray<FOCRailLine>& GetLines() const { return Lines; }
	void AddLine(const FOCRailLine& Line) { Lines.Add(Line); }

private:
	TArray<FOCRailLine> Lines;
};

class OPENCITYCORE_API FOCWaterNetwork
{
public:
	/** Parse the "water_network" section ({"bodies": [...]}); an absent section is an empty network. */
	bool LoadFromLayoutJson(const FString& JsonText, FString& OutError);
	const TArray<FOCWaterBody>& GetBodies() const { return Bodies; }
	const FOCWaterBody* FindBody(FName Id) const;
	void AddBody(const FOCWaterBody& Body) { Bodies.Add(Body); }

private:
	TArray<FOCWaterBody> Bodies;
};
