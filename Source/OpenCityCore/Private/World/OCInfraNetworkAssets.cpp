#include "World/OCInfraNetworkAssets.h"

#include "OCLog.h"
#include "World/OCNetworkValidation.h"
#include "World/OCRoadGraphAsset.h"

const FOCRailNetwork& UOCRailNetworkAsset::GetNetwork() const
{
	if (!bParsed)
	{
		bParsed = true;
		FString Error;
		if (!Network.LoadFromLayoutJson(LayoutJson, Error))
		{
			Network = FOCRailNetwork();
			UE_LOG(LogOpenCity, Error, TEXT("%s: rail network does not parse: %s"), *GetName(), *Error);
		}
	}
	return Network;
}

const FOCWaterNetwork& UOCWaterNetworkAsset::GetNetwork() const
{
	if (!bParsed)
	{
		bParsed = true;
		FString Error;
		if (!Network.LoadFromLayoutJson(LayoutJson, Error))
		{
			Network = FOCWaterNetwork();
			UE_LOG(LogOpenCity, Error, TEXT("%s: water network does not parse: %s"), *GetName(), *Error);
		}
	}
	return Network;
}

#if WITH_EDITOR
void UOCRailNetworkAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	bParsed = false;
}

void UOCWaterNetworkAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	bParsed = false;
}
#endif

FString UOCNetworkValidationLibrary::ValidateNetworks(const UOCRoadGraphAsset* Roads, const UOCRailNetworkAsset* Rail, const UOCWaterNetworkAsset* Water,
	int32 MaxExamplesPerCheck, int32& OutErrors, int32& OutIssues)
{
	FOCNetworkReport Report;
	if (Roads)
	{
		FOCNetworkValidator::FOptions Options;
		const int32 Start = Roads->GetGraph().FindNearestNode(Roads->StartLocation);
		Options.StartNode = Start == INDEX_NONE ? 0 : Start;
		FOCNetworkValidator::ValidateRoads(Roads->GetGraph(), Options, Report);
	}
	if (Rail)
	{
		FOCNetworkValidator::ValidateRail(Rail->GetNetwork(), Report);
	}
	if (Water)
	{
		FOCNetworkValidator::ValidateWater(Water->GetNetwork(), Report);
	}
	OutErrors = Report.NumErrors();
	OutIssues = Report.Issues.Num();
	return Report.Summary(MaxExamplesPerCheck);
}
