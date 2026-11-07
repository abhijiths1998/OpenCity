#include "Game/OCGameMode.h"

#include "Player/OCPlayerController.h"

AOCGameMode::AOCGameMode()
{
	PlayerControllerClass = AOCPlayerController::StaticClass();
}
