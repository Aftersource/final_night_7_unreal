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
	// Zoom gestures (Chris's rule, 2026-11-14 20:10): double-click on empty map = one level in at the cursor, right-click
	// = one level out, a single click never moves the view, and nothing fires within a demigod's extended click box.
	double LastLeftClickAt = -1.0;
	FVector2D LastLeftClickPos = FVector2D::ZeroVector;
	/** The demigod whose extended click box (the kit's ~160 px reach) is under the cursor, else empty. */
	FString DemigodUnderCursor() const;
	FRotator LeanVel = FRotator::ZeroRotator;   // the head turn's velocity (a critically damped follow, see PlayerTick)
};
