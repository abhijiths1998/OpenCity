#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCLayoutCommandlet.generated.h"

/**
 * Generates the Marevia world plan (SourceArt/World: marevia_layout.json, marevia_height.png,
 * marevia_preview.png). Run with scripts/make_layout.sh.
 *
 *   -out=<dir>        output folder (default SourceArt/World)
 *   -compare=<dir>    afterwards, compare the output with the plan files in <dir> (layout fields and
 *                     heightmap pixels) and report every difference
 *   -selftest         check the Python/NumPy-compatible random streams and number formatting first
 */
UCLASS()
class UOCLayoutCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOCLayoutCommandlet();
	virtual int32 Main(const FString& Params) override;
};
