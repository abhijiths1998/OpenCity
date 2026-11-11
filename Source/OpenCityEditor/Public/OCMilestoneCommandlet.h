#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCMilestoneCommandlet.generated.h"

/**
 * Milestone assets and test maps, rebuilt in place (safe to re-run):
 *   -steps=m0          L_PerfBaseline and L_Stress (Maps/Test)
 *   -steps=m1          input actions and contexts, player and game mode Blueprints, L_DevPlayground
 *   -steps=m2          vehicle input, radio stations, bike chassis, wheels, vehicle data and Blueprints, L_DevDriving
 *   -steps=verify-m1,verify-m2   check those assets after a fresh load (exit code 1 on any failure)
 */
UCLASS()
class UOCMilestoneCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCMilestoneCommandlet();
	virtual int32 Main(const FString& Params) override;
};
