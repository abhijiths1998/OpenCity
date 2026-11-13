#pragma once

#include "CoreMinimal.h"

/**
 * Facade plans for the building kit (Tools/kit/KIT_SPEC.md): which piece goes in which bay of which storey,
 * decided once per facade before anything is placed. Pure logic, no engine objects; the assembler
 * (UOCWorldBuilderLibrary::BuildKitBuildings) turns a plan into kit instances.
 *
 * A facade is a row of columns (2 m or 4 m bays) and every column keeps its type all the way up: windows
 * stack over windows, a balcony column runs over a run of storeys, a blank column stays blank. Street
 * facades are mirror-symmetric about their middle by default, with the door on the axis when the bay count
 * allows it, and balconies follow a pattern (centre, ends, alternate, all) instead of a per-cell coin flip.
 */

enum class EOCBay : uint8
{
	Blank,          // plain wall
	Window,         // an upper-floor window (or two in a 4 m bay)
	Balcony,        // French door and balcony (4 m only)
	Shop,           // ground floor shopfront (4 m only)
	Door,           // ground floor street door (Door_200; in a 4 m column it fills one half)
	GroundWindow,   // ground floor barred windows (4 m only)
};

/** What a district style asks of its facades (chances 0-1). */
struct OPENCITYCORE_API FOCFacadeStyle
{
	float Shop = 0.5f;          // a street-side 4 m ground column is a shop
	float Balcony = 0.25f;      // a street facade carries a balcony scheme (scaled: 0.5 -> always)
	float SideWindows = 0.5f;   // a side or back column has windows
	float Symmetric = 0.6f;     // a street facade is mirror-symmetric about a central axis
};

struct OPENCITYCORE_API FOCFacadeIn
{
	double LengthCm = 800.0;    // a whole multiple of 200, at least 400
	int32 Storeys = 3;
	bool bStreet = true;        // faces a street: door, shops, balconies, all columns glazed
	bool bLandmark = false;     // church, town hall...: symmetric, central door, no shops or balconies
	bool bDoor = true;          // a street facade gets the street door (a corner building has one front)
	bool bWideDoor = false;     // symmetric with an even bay count: a 4 m centre column for a portone or entrance
};

struct OPENCITYCORE_API FOCBayColumn
{
	double OffsetCm = 0.0;
	double WidthCm = 400.0;
	EOCBay Ground = EOCBay::Blank;
	EOCBay Upper = EOCBay::Window;      // storeys 1 and up, unless a balcony run covers them
	int32 BalconyFrom = 1;              // storeys [BalconyFrom, BalconyTo] are Balcony (none if To < From)
	int32 BalconyTo = 0;
	bool bDoorAtEnd = false;            // a door in a 4 m column sits in its far half (else its near half)

	bool IsFour() const { return WidthCm > 300.0; }
	/** The bay type at a storey (0 = ground). */
	EOCBay At(int32 Storey) const;
};

struct OPENCITYCORE_API FOCFacadePlan
{
	TArray<FOCBayColumn> Columns;
	int32 DoorColumn = INDEX_NONE;
	bool bSymmetric = false;
	bool bPlainTop = false;     // the top storey (of 3 or more) has plain windows: an attic floor
};

/** A plan for one facade. The same inputs and seed give the same plan. */
OPENCITYCORE_API FOCFacadePlan OCPlanFacade(const FOCFacadeIn& In, const FOCFacadeStyle& Style, uint32 Seed);

/** Bay widths (cm) for a facade: 4 m bays and at most two 2 m bays, laid out as OCPlanFacade does. */
OPENCITYCORE_API TArray<double> OCFacadeBays(double LengthCm, bool bSymmetric, int32 TwoAt, bool bCentreFour = false);

/**
 * A seed for one purpose of one part of a building (bays, colours, roof, props, lit windows...): hashed,
 * never drawn in sequence, so adding a feature or a purpose leaves every other choice where it was.
 */
OPENCITYCORE_API uint32 OCSubSeed(uint32 Seed, uint32 Part, uint32 Purpose);

namespace OCSeedPurpose
{
	enum : uint32 { Bays = 1, Colours = 2, Roof = 3, Props = 4, LitWindows = 5, Building = 6 };
}
