#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OCWorldBuilderLibrary.generated.h"

class AActor;
class ALandscape;
class UHLODLayer;
class UDataLayerAsset;
class UStaticMesh;
class UMaterialInterface;
class UWorld;

/**
 * Editor-only world building helpers for scripts (Content/Python/oc_m3_world.py).
 * Wraps engine steps that Python can't do on its own, following what the editor's
 * New Landscape tool does.
 */
UCLASS()
class OPENCITYEDITOR_API UOCWorldBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a landscape from a 16-bit grayscale PNG (one pixel per vertex, row 0 at the
	 * landscape's min Y). Size must be (components * sections * quads + 1) on each axis.
	 * In a World Partition world the landscape is split into streaming proxies of
	 * GridSizeInComponents x GridSizeInComponents components. Returns null on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static ALandscape* CreateLandscapeFromHeightmap(UWorld* World, const FString& HeightmapPng, FVector Location,
		FVector Scale, int32 SectionsPerComponent, int32 QuadsPerSection, int32 GridSizeInComponents,
		UMaterialInterface* Material);

	/**
	 * Set cell size and loading range (cm) on every grid partition of the World Partition
	 * runtime hash (5.8's runtime hash set). Returns how many partitions were changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 ConfigureStreaming(UWorld* World, int32 CellSizeCm, int32 LoadingRangeCm);

	/**
	 * Use RootLayer (and its parent chain) as the world's HLOD layers: sets the World Partition's
	 * default HLOD layer and replaces every runtime partition's HLOD setups with one per layer in
	 * the chain, HLOD level i using CellSizesCm[i] and LoadingRangesCm[i]. Run before HLODs are
	 * built (scripts/build_hlod.sh). Returns the number of HLOD levels per partition (0 on failure).
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 ConfigureHLOD(UWorld* World, UHLODLayer* RootLayer, const TArray<int32>& CellSizesCm, const TArray<int32>& LoadingRangesCm);

	/**
	 * Build road surface meshes from the layout's road graph: road strips, junction discs,
	 * sidewalks and curbs (for SidewalkKinds), in square chunks of ChunkSizeCm. Every vertex is
	 * draped on HeightmapPng (the same heights the landscape imported), every 4 m along a road.
	 * Road UV0 = (lateral m, along m) and UV1 = (half width m, marked lanes per direction) drive
	 * the road material's markings. Each chunk becomes a Nanite static mesh asset in AssetFolder
	 * and an actor in the world (folder "Roads"). Bridges are skipped (built separately). Road surfaces
	 * get one material slot per paving (segment "paving" in the layout: PavingMaterials, falling back to
	 * RoadMaterial); markings follow the Codice della Strada style of each road's D.M. type.
	 * Returns the number of chunks.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildRoadMeshes(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng, const FString& AssetFolder,
		UMaterialInterface* RoadMaterial, UMaterialInterface* SidewalkMaterial, const TArray<FName>& SidewalkKinds,
		float SidewalkWidthCm, float ChunkSizeCm, const TMap<FName, UMaterialInterface*>& PavingMaterials);

	/**
	 * Grey-box buildings from the layout's "buildings" list: one AOCBlockoutActor per square
	 * chunk of ChunkSizeCm (folder "Buildings"), one instance of Mesh (a 100 cm cube) per
	 * building, with custom data [style index, random]. Returns the number of buildings.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildBuildings(UWorld* World, const FString& LayoutJsonPath, UStaticMesh* Mesh, UMaterialInterface* Material, float ChunkSizeCm);

	/**
	 * Buildings from the modular kit (Tools/blender/KIT_SPEC.md, meshes SM_Kit_* in KitFolder) for every
	 * building in the layout: plinth, street-facing ground floor (shops, a door, barred windows),
	 * shuttered upper floors and balconies, corner quoins, cornice, hip or flat roof with clutter.
	 * One AOCKitBuildingChunk per ChunkSizeCm square (folder "Buildings/Kit"), visual only.
	 * Variation is seeded per building from Seed. Returns the number of piece instances.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildKitBuildings(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
		const FString& KitFolder, float ChunkSizeCm, int32 Seed);

	/**
	 * Street dressing from the road graph (props imported by oc_import_props.py): street lights every
	 * 28 m at the kerb, trees every 10 m on avenues and the coast road, bins near junctions, hydrants.
	 * Junctions, bridges and alleys stay clear. One AOCKitBuildingChunk per ChunkSizeCm square
	 * (folder "Props"); lamps, bins and hydrants collide. Returns the number of props.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildStreetProps(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
		float SidewalkWidthCm, float ChunkSizeCm, int32 Seed);

	/**
	 * Retaining walls from the layout's "retaining_walls" runs (the layout generator (scripts/make_layout.sh, Source/OpenCityEditor/Private/Layout/)): kit
	 * panels (SM_Kit_RetainingWall_Stone_400 / _Concrete_400, 4 x 2 m) stacked from the top down to
	 * below the bottom height, capped with SM_Kit_WallCoping_400. Cut walls face the road; fill walls
	 * face away from it and stand 0.8 m above it as a parapet. Walls collide. Returns the panel count.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildRetainingWalls(UWorld* World, const FString& LayoutJsonPath, float ChunkSizeCm);

	/**
	 * Street fixtures from the road data (INFRA_DESIGN.md Phase 5), kit pieces from
	 * Tools/blender/make_kit.py: wall lanterns along centro storico streets, galvanised LED poles on
	 * post-war streets and the coast road (BuildStreetProps keeps the cast-iron lamps for the rest),
	 * Codice della Strada signs at ZTL gates (ZTL + 30), urban entries (50), blind T-junctions (STOP +
	 * convex mirror opposite) and minor roads meeting a viale (give way), recycling bins on post-war
	 * pavements, drinking fountains at the piazza and some centro junctions, iron bollards at centro
	 * corners. Returns the number of fixtures.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildStreetFixtures(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
		float SidewalkWidthCm, float ChunkSizeCm, int32 Seed);

	/**
	 * Road decals (oc_decals.py instances): manholes about every 70 m in a lane, tyre marks on 40% of
	 * junction approaches, broken asphalt about every 150 m, chewing gum on pavements. Bridges and
	 * the mountain roads' junction stubs are skipped. One AOCKitBuildingChunk per ChunkSizeCm square
	 * (folder "Decals"). Returns the number of decals.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static int32 BuildRoadDecals(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
		float SidewalkWidthCm, float ChunkSizeCm, int32 Seed);

	/**
	 * A runtime Data Layer asset (created at PackagePath/Name, or reused) with an instance in the
	 * world, initially Activated or Unloaded. Returns the asset, or null on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static UDataLayerAsset* CreateRuntimeDataLayer(UWorld* World, const FString& PackagePath, const FString& Name, bool bInitiallyActivated);

	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static bool AddActorToDataLayer(AActor* Actor, UDataLayerAsset* DataLayer);

	/** A registered gameplay tag by name (invalid tag and an error if it isn't registered). */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|World Builder")
	static FGameplayTag MakeTag(FName TagName);
};
