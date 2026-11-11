#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCGenerateCommandlet.generated.h"

/**
 * Small source-art generators (scripts/generate.sh):
 *   -what=lut    SourceArt/LUT/T_LUT_Marevia.png, the colour grade (ART_BIBLE.md)
 *   -what=bike   SourceArt/Vehicles/Bike/SKM_BikeChassis.gltf, the motorbike physics chassis
 *   -what=plan   SourceArt/Infra/network_plan.png, the network plan (built + proposed)
 *   -out=<file>  write somewhere else
 */
UCLASS()
class UOCGenerateCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCGenerateCommandlet();
	virtual int32 Main(const FString& Params) override;
};
