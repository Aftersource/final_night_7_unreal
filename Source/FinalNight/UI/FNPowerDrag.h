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

	/** "mega" (a bolt, map scale), "mini" (a spark, the board), or empty on a server without tiers. */
	FString Tier;

	/** The latest screen-space cursor position of this drag. */
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	static TSharedRef<FFNPowerDragDrop> New(const FString& InPower, const FVector2D& InScreenPosition, const FString& InTier = FString());

	/** The drag in progress, if any (the style kit's token layer draws it). Still set while OnDroppedOnWorld runs, so a
	 *  listener can read its Tier. */
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

	/**
	 * A MEGA power is armed or in the hand (true) / no longer (false): fires on a mega tile's click-to-arm and disarm,
	 * on a mega drag's start, and on its drop or cancel. The globe and the board fade their fault lines in and out on it.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMegaArmed, bool /*bArmed*/, const FString& /*Power*/);
	static FOnMegaArmed& OnMegaArmed();

	/** Where the last drop landed (screen space) and when, so the token can settle there. */
	static bool GetLastDrop(FVector2D& OutScreenPosition, double& OutTime, FString& OutPower);
};
