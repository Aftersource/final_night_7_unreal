#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FNSeatPawn.generated.h"

class UCameraComponent;

/** You, seated at the table. Just a camera; the controller aims it. */
UCLASS()
class FINALNIGHT_API AFNSeatPawn : public APawn
{
	GENERATED_BODY()
public:
	AFNSeatPawn();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
};
