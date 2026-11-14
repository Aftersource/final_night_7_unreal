#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Makes a HUD panel movable: a small grip in its top-left corner (shown on hover) drags it, and on release it
 * glides to a snap: the screen edges and centre lines, the edges of the other movable panels, else an 8 px grid.
 * Where it was dropped is remembered per machine (FN7HudPrefs, by Id).
 */
class FINALNIGHT_API SFNPanelMover : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPanelMover) {}
		SLATE_ARGUMENT(FString, Id)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFNPanelMover() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return GripMove(MyGeometry, MouseEvent); }
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return GripUp(MyGeometry, MouseEvent); }

	/** True when an absolute (desktop) point is over any movable panel: power drags skip the board there. */
	static bool IsOverAnyPanel(const FVector2D& AbsolutePoint);
	/** Every movable panel glides back to its home corner (and forgets its saved offset). */
	static void ResetAll();

private:
	FString Id;
	FVector2D Offset = FVector2D::ZeroVector;     // shown offset from the panel's home slot
	FVector2D Target = FVector2D::ZeroVector;     // where it is gliding to
	bool bDragging = false;
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D DragStartOffset = FVector2D::ZeroVector;
	FGeometry LastGeometry;
	float GripAlpha = 0.f;

	FReply GripDown(const FGeometry& G, const FPointerEvent& E);
	FReply GripMove(const FGeometry& G, const FPointerEvent& E);
	FReply GripUp(const FGeometry& G, const FPointerEvent& E);
	FVector2D Snap(const FVector2D& Wanted) const;
	FSlateRect HomeRect() const;     // absolute rect with no offset
};
