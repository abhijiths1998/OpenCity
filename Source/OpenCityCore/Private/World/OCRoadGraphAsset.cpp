#include "World/OCRoadGraphAsset.h"

#include "OCLog.h"

const FOCRoadGraph& UOCRoadGraphAsset::GetGraph() const
{
	if (!bParsed)
	{
		bParsed = true;
		FString Error;
		if (!Graph.LoadFromLayoutJson(LayoutJson, Error))
		{
			Graph = FOCRoadGraph();
			UE_LOG(LogOpenCity, Error, TEXT("%s: road graph does not parse: %s"), *GetName(), *Error);
		}
	}
	return Graph;
}

#if WITH_EDITOR
void UOCRoadGraphAsset::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	bParsed = false;
}
#endif
