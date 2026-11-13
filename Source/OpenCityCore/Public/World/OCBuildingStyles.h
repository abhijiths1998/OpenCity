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

	/** Chance a shuttered window is closed or half closed at upkeep 0 (scaled down to a fifth at upkeep 1). */
	UPROPERTY(EditAnywhere, Category = "Detail", meta = (ClampMin = "0", ClampMax = "1"))
	float ClosedShutters = 0.35f;

	/** Wall dressing (AC units, laundry, flower boxes, cables, blade signs, lanterns): 1 = as designed. */
	UPROPERTY(EditAnywhere, Category = "Detail", meta = (ClampMin = "0"))
	float Dressing = 1.f;

	const FOCBuildingArchetype* Find(FName Name) const;

	/** The starting set (BUILDINGS_PLAN.md section 3.2). */
	void FillDefaults();
};
