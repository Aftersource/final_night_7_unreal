#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FNPlayerController.generated.h"

class UFNNetClient;
class SFNRootWidget;
class AFNStudio;

/** Seated at the tabletop: the view leans toward the mouse; hover and click the table's objects. */
UCLASS()
class FINALNIGHT_API AFNPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaTime) override;

	UPROPERTY() TObjectPtr<UFNNetClient> Net;
	UPROPERTY() TObjectPtr<AFNStudio> Studio;

private:
	FName TraceUnderCursor() const;
	TSharedPtr<SFNRootWidget> Root;
	TSharedPtr<class SWidget> RootContainer;
	FRotator BaseRot;
	FRotator Lean = FRotator::ZeroRotator;
	FRotator LeanVel = FRotator::ZeroRotator;   // the head turn's velocity (a critically damped follow, see PlayerTick)
};
