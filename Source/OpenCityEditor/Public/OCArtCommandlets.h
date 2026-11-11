#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCArtCommandlets.generated.h"

/**
 * The final-art pipeline (ART_PIPELINE.md), run by scripts/build_art.sh:
 *   -steps=decals,materials,weather,physmat,props,kit   (default: all, in that order)
 *   -steps=stats   MATSTAT lines: master instruction counts against the budgets (needs a GPU renderer)
 * The materials step needs the DatasmithContent plugin mounted once, for the water normals.
 */
UCLASS()
class UOCBuildArtCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCBuildArtCommandlet();
	virtual int32 Main(const FString& Params) override;
};

/**
 * Imports a folder of T_<Asset>_<BC|N|ORM|E|M> textures with the ART_PIPELINE.md settings and makes one
 * MI_<Asset> per set:   -src=<folder> -dest=/Game/... -parent=/Game/Materials/M_Master_Surface
 */
UCLASS()
class UOCImportTexturesCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCImportTexturesCommandlet();
	virtual int32 Main(const FString& Params) override;
};
