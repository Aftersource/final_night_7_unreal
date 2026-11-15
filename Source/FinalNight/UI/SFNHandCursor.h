#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class SFNPowerRingInput;
class UTexture2D;

/**
 * The hand of god's cursor (docs/design/hand_of_god §1): a marble-and-gold hand drawn in Slate on top of everything,
 * in four states (OPEN, GRAB, POINT, FLING) read from the board's input layer, 48 px tall at 1080p, each with its own
 * hotspot (the palm; the fingertip for POINT), crossfading in 80 ms with a small squash on GRAB. It shows only while
 * the cursor is over the 3D view (the input layer is hovered or holding a grab); over panels and text boxes the
 * system cursor stays.
 */
class FINALNIGHT_API SFNHandCursor : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNHandCursor) {}
		SLATE_ARGUMENT(TWeakPtr<SFNPowerRingInput>, Input)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFNHandCursor() override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakPtr<SFNPowerRingInput> Input;
	TStrongObjectPtr<UTexture2D> Tex[4];
	FSlateBrush Brush[4];
	FVector2D Hotspot[4];
	mutable int32 State = 0, Prev = 0;
	mutable double ChangedAt = 0.0;
	bool bReady = false;
};
