#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * A holographic "shiny card" finish laid over a surface (the WILL scroll): a soft rainbow foil band that drifts with
 * time and leans toward the mouse, a white specular streak that follows the cursor, a few twinkling sparkles, and a
 * pulsing gold edge glow while bHot is true (time is slowed for you). It never takes input and clips to its bounds.
 */
class FINALNIGHT_API SFNFoil : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNFoil) : _bHot(false), _Strength(1.f), _CornerRadius(8.f) {}
		SLATE_ATTRIBUTE(bool, bHot)
		/** 0 = off, 1 = the default sheen. */
		SLATE_ATTRIBUTE(float, Strength)
		SLATE_ARGUMENT(float, CornerRadius)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8.f, 8.f); }

private:
	TAttribute<bool> bHot;
	TAttribute<float> Strength;
	float CornerRadius = 8.f;
	double StartTime = 0.0;
};
