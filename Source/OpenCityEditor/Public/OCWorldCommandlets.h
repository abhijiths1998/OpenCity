#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCWorldCommandlets.generated.h"

/**
 * Checks the saved Marevia map against the M3 acceptance list (ROADMAP.md) from World Partition actor
 * descriptors, so it sees every actor whatever is streamed in. Exit code 1 if any check fails.
 *   -requirehlod   also require built HLOD actors (or OC_VERIFY_HLOD=1)
 */
UCLASS()
class UOCVerifyMareviaCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCVerifyMareviaCommandlet();
	virtual int32 Main(const FString& Params) override;
};

/**
 * Validates the road, rail and water networks (FOCNetworkValidator, INFRA_DESIGN.md) and writes
 * Saved/Validation/networks.md. Errors give exit code 1.
 *   -import   first rewrite DA_MareviaRoads/Rail/Water from SourceArt/World/marevia_layout.json
 */
UCLASS()
class UOCValidateNetworksCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCValidateNetworksCommandlet();
	virtual int32 Main(const FString& Params) override;
};

/** Applies the Marevia post-process look to L_Marevia's unbound post-process volume without a rebuild. */
UCLASS()
class UOCApplyLookCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCApplyLookCommandlet();
	virtual int32 Main(const FString& Params) override;
};
