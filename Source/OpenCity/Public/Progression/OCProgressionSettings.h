#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "OCProgressionSettings.generated.h"

class UOCAreaUnlockData;
class UOCRoadGraphAsset;

/** Project Settings > Game > OpenCity Progression (DefaultGame.ini). */
UCLASS(Config = Game, DefaultConfig, BlueprintType, meta = (DisplayName = "OpenCity Progression"))
class OPENCITY_API UOCProgressionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Progression")
	TSoftObjectPtr<UOCAreaUnlockData> UnlockData;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Progression")
	TSoftObjectPtr<UOCRoadGraphAsset> RoadGraph;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
