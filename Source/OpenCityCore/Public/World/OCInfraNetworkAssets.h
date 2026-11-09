#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "World/OCInfraNetworks.h"
#include "OCInfraNetworkAssets.generated.h"

class UOCRoadGraphAsset;

/** The cooked rail network ({"rail": {...}} from marevia_layout.json, written by oc_m3_story.py). */
UCLASS(BlueprintType)
class OPENCITYCORE_API UOCRailNetworkAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Rail", meta = (MultiLine = true))
	FString LayoutJson;

	const FOCRailNetwork& GetNetwork() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	mutable FOCRailNetwork Network;
	mutable bool bParsed = false;
};

/** The cooked water network ({"water_network": {...}} from marevia_layout.json). */
UCLASS(BlueprintType)
class OPENCITYCORE_API UOCWaterNetworkAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Water", meta = (MultiLine = true))
	FString LayoutJson;

	const FOCWaterNetwork& GetNetwork() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	mutable FOCWaterNetwork Network;
	mutable bool bParsed = false;
};

/** Network validation for editor scripts (Content/Python/oc_validate_networks.py). */
UCLASS()
class OPENCITYCORE_API UOCNetworkValidationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Runs every road, rail and water check; returns the report and the number of errors. Null assets are skipped. */
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Networks")
	static FString ValidateNetworks(const UOCRoadGraphAsset* Roads, const UOCRailNetworkAsset* Rail, const UOCWaterNetworkAsset* Water,
		int32 MaxExamplesPerCheck, int32& OutErrors, int32& OutIssues);
};
