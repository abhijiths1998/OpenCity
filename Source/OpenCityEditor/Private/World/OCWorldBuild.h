#pragma once

// The Marevia world build (UOCBuildWorldCommandlet, scripts/build_world.sh), split by topic.
// Formerly the world build (OCBuildWorld), OCWorldStory.cpp, OCWorldWater.cpp, WriteNetworkAssets (OCWorldBuildCommon.cpp),
// ApplyLook (OCWorldBuildCommon.cpp) and OCDevMaterials.cpp.

#include "CoreMinimal.h"
#include "Layout/OCLayoutCompat.h"

class UDataLayerAsset;
class UMaterialInterface;
class UStaticMesh;
class UWorld;
class AStaticMeshActor;
struct FPostProcessSettings;

namespace OCWorldBuild
{
	using OCLayout::FJsonPtr;

	const TCHAR* const Tag = TEXT("oc_m3");
	const TCHAR* const MapPath = TEXT("/Game/Maps/Marevia/L_Marevia");
	const TCHAR* const Cube = TEXT("/Engine/BasicShapes/Cube");
	const TCHAR* const Cylinder = TEXT("/Engine/BasicShapes/Cylinder");
	const TCHAR* const Plane = TEXT("/Engine/BasicShapes/Plane");

	/** Grey-box M_Dev_* materials (/Game/Dev/Materials). */
	void BuildDevMaterials();

	/** Marevia post-process values (ART_BIBLE.md): exposure range, local exposure, LUT, rain blendable. */
	void ApplyLook(FPostProcessSettings& S);

	/** DA_MareviaRoads, DA_MareviaRail, DA_MareviaWater (/Game/World) from the layout. */
	void WriteNetworkAssets(const FJsonPtr& Layout);

	/** Bridges, landmarks, gate barriers, data layers, locked areas, deep water, DA_AreaUnlocks. */
	void BuildStory(UWorld* World, const FJsonPtr& Layout);

	/** Water surfaces and distant scenery. */
	void BuildWater(const FJsonPtr& Layout);

	// ---- small shared helpers -------------------------------------------------------------------

	/** layout[key] as a number / int / string / array (fatal if missing). */
	double Num(const FJsonPtr& J, const TCHAR* Key);
	int32 Int(const FJsonPtr& J, const TCHAR* Key);
	FString Str(const FJsonPtr& J, const TCHAR* Key);
	const TArray<FJsonPtr>& Arr(const FJsonPtr& J, const TCHAR* Key);
	/** True if J has Key with a value other than null, false, 0 or "". */
	bool Truthy(const FJsonPtr& J, const TCHAR* Key);
	/** Item i of an array value as a number. */
	double At(const FJsonPtr& J, int32 I);

	/** Layout metres (x east, y north) to Unreal cm (Y south). */
	inline FVector Cm(double X, double Y, double Z) { return FVector(X * 100.0, -Y * 100.0, Z * 100.0); }
	/** Unreal yaw (degrees) of a direction in layout axes. */
	inline double UeYaw(double Dx, double Dy) { return FMath::RadiansToDegrees(FMath::Atan2(-Dy, Dx)); }
	/** Fiume Ardo half width at layout y (the layout generator's river_half_width). */
	double RiverHalfWidth(double Y);

	/** A cached asset load; fatal if missing. */
	UObject* Asset(const FString& Path);
	/** Final-art material if built (oc_art_materials), else the grey-box fallback. */
	UMaterialInterface* Art(const FString& Path, const FString& Fallback);
	/** Grey-box material M_Dev_<Name>, or its final-art box instance when that exists. */
	UMaterialInterface* BoxMaterial(const FString& Name);

	/** A static mesh actor centred at Center (layout m), Size in metres (engine shapes are 1 m). */
	AStaticMeshActor* Shape(const FString& Label, const FString& Folder, const FVector& CenterM, const FVector& SizeM,
		UMaterialInterface* Material, const TCHAR* Mesh = Cube, const FRotator& Rot = FRotator::ZeroRotator);
}
