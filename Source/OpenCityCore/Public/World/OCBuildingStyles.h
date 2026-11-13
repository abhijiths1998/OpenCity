#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OCBuildingStyles.generated.h"

/** A kit piece family (Tools/kit/KIT_SPEC.md: Window, WindowArch, Hotel_Window...) and its weight. */
USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCPieceWeight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Building")
	FName Piece;

	UPROPERTY(EditAnywhere, Category = "Building", meta = (ClampMin = "0"))
	float Weight = 1.f;
};

/**
 * How one building archetype (BUILDINGS_PLAN.md 3.2) is dressed by the kit assembler. The layout picks the
 * archetype per lot (OCMareviaPlace.cpp) and writes its name; everything about its look is tuned here.
 * Piece lists are drawn once per building, so a building is consistent and its neighbours differ.
 */
USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCBuildingArchetype
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Building")
	FName Name;

	/** Plaster, Brick or Shed: the wall, blank and ground pieces. */
	UPROPERTY(EditAnywhere, Category = "Building")
	FName Material = TEXT("Plaster");

	/** Wall colour palette (index into WallPalettes) and stone trim palette (0 limestone, 1 grey stone,
	 *  2 painted white, 3 brick red: the kit trim material's tint). */
	UPROPERTY(EditAnywhere, Category = "Building")
	int32 Palette = 0;

	UPROPERTY(EditAnywhere, Category = "Building")
	int32 Trim = 0;

	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float PitchedRoof = 0.5f;

	/** Of pitched roofs, the share that are gables (a terrace house between party walls always is). */
	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Gable = 0.3f;

	/** Rise of a pitched roof per metre of half-width (0.47 = 25 degrees; sheds are lower). */
	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0.05"))
	float Pitch = 0.47f;

	UPROPERTY(EditAnywhere, Category = "Facade", meta = (ClampMin = "0", ClampMax = "1"))
	float Shutters = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bQuoins = true;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bPilasters = false;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bStringCourse = false;

	/** Station canopy along the street front's ground floor. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bCanopy = false;

	/** Door used when the plan's door column is 4 m wide (Door, Portone, Hotel_Entrance, Civic_Entrance, Church_Portal). */
	UPROPERTY(EditAnywhere, Category = "Facade")
	FName Door = TEXT("Door");

	/** A symmetric street front asks for a 4 m centre column for that door. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bWideDoor = false;

	UPROPERTY(EditAnywhere, Category = "Facade", meta = (ClampMin = "0", ClampMax = "1"))
	float Shop = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Facade", meta = (ClampMin = "0", ClampMax = "1"))
	float Balcony = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Facade", meta = (ClampMin = "0", ClampMax = "1"))
	float SideWindows = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Facade", meta = (ClampMin = "0", ClampMax = "1"))
	float Symmetric = 0.6f;

	/** Crown under the roof: Cornice, CorniceBracket, Brick_Corbel or None. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> Crowns;

	/** Window family of the middle storeys. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> Upper;

	/** First floor (piano nobile); empty = as Upper. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> UpperFirst;

	/** Top storey of three or more; empty = as Upper. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> Top;

	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> Balconies;

	/** Shopfront variants; low upkeep raises ShopClosed, wealth raises ShopAwning. */
	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> Shops;

	/** Street ground bays that are not shops (GroundWindow, Garage...). */
	UPROPERTY(EditAnywhere, Category = "Facade")
	TArray<FOCPieceWeight> GroundOther;

	// ---- BUILDINGS_PLAN.md section 4: Italian character ----

	/** Eaves under a pitched roof: Coved (plastered cove, town houses), Rafters (exposed rafter ends) or None. */
	UPROPERTY(EditAnywhere, Category = "Roof")
	FName Eaves = TEXT("Coved");

	/** Of pitched roofs, the share in grey slate (Ligurian) instead of clay pan tiles. */
	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Slate = 0.f;

	/** Of flat roofs, the share that are rendered barrel vaults (Campanian coast). */
	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Vault = 0.f;

	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Dormers = 0.f;

	/** Chimneys per pitched roof (0-3), Italian pots with tiled hoods. */
	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0"))
	int32 Chimneys = 1;

	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Altana = 0.f;

	UPROPERTY(EditAnywhere, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Pergola = 0.f;

	/** Painted architecture: surrounds, quoins and bands in cream or a darker shade of the wall, not stone. */
	UPROPERTY(EditAnywhere, Category = "Colour", meta = (ClampMin = "0", ClampMax = "1"))
	float PaintedTrim = 0.f;

	/** Rusticated ground floor (bugnato), pilaster strips between bays (lesene), sill bands (marcadavanzale),
	 *  battered base on slopes (scarpa), ground-floor arcade (portico). */
	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bRustic = false;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bLesene = false;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bSillBand = false;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bScarpa = false;

	UPROPERTY(EditAnywhere, Category = "Facade")
	bool bArcade = false;

	/** Chances per building: an external stair to the first floor, a corner shrine, brace arches across the
	 *  alley to the facing house, a bridge room over the alley to the next house, a walled garden with a gate. */
	UPROPERTY(EditAnywhere, Category = "Features", meta = (ClampMin = "0", ClampMax = "1"))
	float ExtStair = 0.f;

	UPROPERTY(EditAnywhere, Category = "Features", meta = (ClampMin = "0", ClampMax = "1"))
	float Edicola = 0.f;

	UPROPERTY(EditAnywhere, Category = "Features", meta = (ClampMin = "0", ClampMax = "1"))
	float BraceArches = 0.f;

	UPROPERTY(EditAnywhere, Category = "Features", meta = (ClampMin = "0", ClampMax = "1"))
	float Bridges = 0.f;

	UPROPERTY(EditAnywhere, Category = "Features")
	bool bGardenWalls = false;
};

USTRUCT(BlueprintType)
struct OPENCITYCORE_API FOCColourList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Building")
	TArray<FLinearColor> Colours;
};

/**
 * DA_BuildingStyles (/Game/Environment/Buildings): the look of Marevia's buildings. Created with the
 * starting values below by the first world build that needs it; from then on the asset is the tuning source.
 */
UCLASS(BlueprintType)
class OPENCITYCORE_API UOCBuildingStyleSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Building")
	TArray<FOCBuildingArchetype> Archetypes;

	/** Wall tints (multipliers on the plaster texture), per palette: 0 warm Mediterranean, 1 pastel seafront,
	 *  2 industrial, 3 pale stone (ART_BIBLE.md). */
	UPROPERTY(EditAnywhere, Category = "Colour")
	TArray<FOCColourList> WallPalettes;

	/** Shutter, door and awning colours. */
	UPROPERTY(EditAnywhere, Category = "Colour")
	FOCColourList WoodColours;

	/** Weights of WoodColours (dark green dominates on the Ligurian coast). */
	UPROPERTY(EditAnywhere, Category = "Colour")
	TArray<float> WoodWeights;

	/** Chance a shuttered window is closed or half closed at upkeep 0 (scaled down to a fifth at upkeep 1). */
	UPROPERTY(EditAnywhere, Category = "Detail", meta = (ClampMin = "0", ClampMax = "1"))
	float ClosedShutters = 0.35f;

	/** Wall dressing (AC units, laundry, flower boxes, cables, blade signs, lanterns): 1 = as designed. */
	UPROPERTY(EditAnywhere, Category = "Detail", meta = (ClampMin = "0"))
	float Dressing = 1.f;

	/** Defaults generation this asset was filled with; an older one is refilled by the world build. */
	UPROPERTY(VisibleAnywhere, Category = "Building")
	int32 Version = 0;

	static constexpr int32 CurrentVersion = 2;

	const FOCBuildingArchetype* Find(FName Name) const;

	/** The starting set (BUILDINGS_PLAN.md section 3.2). */
	void FillDefaults();
};
