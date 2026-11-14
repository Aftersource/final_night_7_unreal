#include "FNGameMode.h"
#include "FNPlayerController.h"
#include "FNSeatPawn.h"

AFNGameMode::AFNGameMode()
{
	PlayerControllerClass = AFNPlayerController::StaticClass();
	DefaultPawnClass = AFNSeatPawn::StaticClass();
}
