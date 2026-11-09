#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OCKitBuildingChunk.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * One World Partition chunk of kit-built buildings: an instanced mesh per kit piece
 * (Tools/blender/KIT_SPEC.md), assembled at edit time by UOCWorldBuilderLibrary::BuildKitBuildings.
 * Visual only: building collision is the hidden one-box-per-building blockout (AOCBlockoutActor).
 *
 * Per-instance custom data (8 floats, read by M_Master_Surface / M_Master_KitGlass):
 *   0-2 wall tint, 3-5 wood (shutter/door) tint, 6 wear, 7 window lit at night.
 */
UCLASS()
class OPENCITY_API AOCKitBuildingChunk : public AActor
{
	GENERATED_BODY()

public:
	static constexpr int32 NumCustomData = 8;

	AOCKitBuildingChunk();

#if WITH_EDITOR
	/** The instanced component for Mesh, created on first use (editor-time assembly only). */
	UInstancedStaticMeshComponent* GetOrAddPiece(UStaticMesh* Mesh);
#endif
};
