#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"

/**
 * The drag-drop operation an FN7 power tile starts when a player drags it. Public interface of the style kit;
 * Benito's side uses it without touching the style-kit files.
 *
 * While a power is dragged, the style kit draws the floating wood token itself (inside the game viewport, gliding
 * gently after the cursor; there is no OS cursor window). The 3D table plugs in through three static hooks:
 *  - SnapToBoard(): bind it to return the SCREEN-space centre of the board hex under ScreenPosition (or unset when
 *    the cursor is not over the board). The token glides onto that hex instead of following the cursor, and the
 *    table should light the power's area of effect around that hex at the same time.
 *  - OnDragOverWorld(): every mouse move during the drag (power slug + screen position), e.g. to move the AoE.
 *  - OnDroppedOnWorld(): the drop, when no Slate widget took it. Cast there (UFNNetClient::CastAt) and clear the AoE.
 *  - OnDragEnded(): the drag is over (dropped anywhere or cancelled): clear any highlight.
 * Slate drop targets can also read it directly: DragDropEvent.GetOperationAs<FFNPowerDragDrop>().
 */
class FINALNIGHT_API FFNPowerDragDrop : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFNPowerDragDrop, FDragDropOperation)

	/** The server's power slug. */
	FString Power;

	/** The latest screen-space cursor position of this drag. */
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	static TSharedRef<FFNPowerDragDrop> New(const FString& InPower, const FVector2D& InScreenPosition);

	/** The drag in progress, if any (the style kit's token layer draws it). */
	static TSharedPtr<FFNPowerDragDrop> GetActive();

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return nullptr; }
	virtual void OnDragged(const FDragDropEvent& DragDropEvent) override;
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	DECLARE_DELEGATE_RetVal_TwoParams(TOptional<FVector2D>, FOnSnapToBoard, const FString& /*Power*/, const FVector2D& /*ScreenPosition*/);
	static FOnSnapToBoard& SnapToBoard();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPowerAtScreen, const FString& /*Power*/, const FVector2D& /*ScreenPosition*/);
	static FOnPowerAtScreen& OnDragOverWorld();
	static FOnPowerAtScreen& OnDroppedOnWorld();

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPowerDragEnded, const FString& /*Power*/);
	static FOnPowerDragEnded& OnDragEnded();

	/** Where the last drop landed (screen space) and when, so the token can settle there. */
	static bool GetLastDrop(FVector2D& OutScreenPosition, double& OutTime, FString& OutPower);
};
