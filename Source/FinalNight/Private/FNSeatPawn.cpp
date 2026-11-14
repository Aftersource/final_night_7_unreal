#include "FNSeatPawn.h"
#include "Camera/CameraComponent.h"

AFNSeatPawn::AFNSeatPawn()
{
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->SetFieldOfView(72.f);
	Camera->bUsePawnControlRotation = true;
}
