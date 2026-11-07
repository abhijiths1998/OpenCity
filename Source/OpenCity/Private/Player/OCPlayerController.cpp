#include "Player/OCPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"

void AOCPlayerController::OnPossess(APawn* InPawn)
{
	// Start each pawn from a clean slate; the pawn adds its contexts in PawnClientRestart.
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->ClearAllMappings();
		}
	}
	Super::OnPossess(InPawn);
}
