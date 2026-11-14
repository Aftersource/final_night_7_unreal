#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"

/**
 * The drag-drop operation an FN7 power tile starts when a player drags it. Public interface of the style kit:
 * Benito's side accepts it without touching the style-kit files, in either of two ways:
 *  - a Slate drop target: in OnDrop / OnDragOver, DragDropEvent.GetOperationAs<FFNPowerDragDrop>() gives the
 *    operation; Power is the server's power slug ("volcano", "lightning", ...).
 *  - the 3D table and globe (not Slate widgets): subscribe to FFNPowerDragDrop::OnDroppedOnWorld(), which fires
 *    when no widget handled the drop, with the slug and the screen-space drop position. Deproject that point to
 *    the globe and cast there (UFNNetClient::CastAt or CastPower).
 */
class FINALNIGHT_API FFNPowerDragDrop : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFNPowerDragDrop, FDragDropOperation)

	/** The server's power slug. */
	FString Power;

	static TSharedRef<FFNPowerDragDrop> New(const FString& InPower, TSharedPtr<SWidget> InDecorator);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return Decorator; }
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	/** Fires when a power tile is dropped where no Slate widget took it (over the 3D table or globe). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPowerDroppedOnWorld, const FString& /*Power*/, const FVector2D& /*ScreenPosition*/);
	static FOnPowerDroppedOnWorld& OnDroppedOnWorld();

private:
	TSharedPtr<SWidget> Decorator;
};
