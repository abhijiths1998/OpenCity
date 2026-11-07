#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OCGameMode.generated.h"

/** Default game mode. The pawn class is set in the Blueprint subclass BP_OCGameMode. */
UCLASS()
class OPENCITY_API AOCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOCGameMode();
};
