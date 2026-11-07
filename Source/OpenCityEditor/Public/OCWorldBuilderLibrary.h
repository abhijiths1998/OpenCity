#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OCWorldBuilderLibrary.generated.h"

class ALandscape;
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
};
