#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraph.h"
#include "OCRoadGraphAsset.generated.h"

/**
 * The cooked road network: the layout's road graph, imported by the world build (UOCBuildWorldCommandlet)
 * from SourceArt/World/marevia_layout.json (SourceArt is not packaged). Parsed on first use.
 */
UCLASS(BlueprintType)
class OPENCITYCORE_API UOCRoadGraphAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** JSON with a "roads" object (nodes, segments), as written by the layout generator. */
	UPROPERTY(EditAnywhere, Category = "Road Graph", meta = (MultiLine = true))
	FString LayoutJson;

	/** Where a new game starts (cm); push-backs choose roads reachable from here. */
	UPROPERTY(EditAnywhere, Category = "Road Graph")
	FVector StartLocation = FVector::ZeroVector;

	/** The parsed graph (empty if LayoutJson is invalid; the error is logged once). */
	const FOCRoadGraph& GetGraph() const;

	/** Driving lanes derived from the graph (built on first use). */
	const FOCLaneGraph& GetLanes() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	mutable FOCRoadGraph Graph;
	mutable FOCLaneGraph Lanes;
	mutable bool bParsed = false;
	mutable bool bLanesBuilt = false;
};
