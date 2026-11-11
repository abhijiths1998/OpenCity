#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCBuildWorldCommandlet.generated.h"

/**
 * Builds the Marevia World Partition map from SourceArt/World (scripts/build_world.sh, which deletes
 * the old map first): grey-box materials, streaming and HLOD setup, landscape, lighting and look,
 * roads, buildings, street dressing, the story layer (bridges, landmarks, gates, volumes, unlock
 * data), water, scenery and the perf camera loop. Needs -AllowCommandletRendering (landscapes).
 *
 *   -materialsonly   rebuild the M_Dev_* materials and stop
 *   -skipmaterials   don't rebuild the M_Dev_* materials
 */
UCLASS()
class UOCBuildWorldCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOCBuildWorldCommandlet();
	virtual int32 Main(const FString& Params) override;
};
