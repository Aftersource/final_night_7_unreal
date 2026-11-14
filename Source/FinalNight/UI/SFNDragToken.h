#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SBox;

/**
 * Draws the power being dragged as a carved wood token floating over the game viewport. It eases in (no pop),
 * trails the cursor on a soft spring, tilts with the motion, bobs gently with a shadow that grows as it lifts,
 * glides onto the board hex FFNPowerDragDrop::SnapToBoard() reports, and on drop sinks onto that spot and fades.
 * Purely visual: it never takes input.
 */
class FINALNIGHT_API SFNDragToken : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNDragToken) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedPtr<SBox> IconHolder;
	FString ShownPower;

	bool bDragging = false;
	double AppearTime = 0.0;
	FVector2D Pos = FVector2D::ZeroVector;        // smoothed, viewport-local
	FVector2D Vel = FVector2D::ZeroVector;
	float Tilt = 0.f;
	float Lift = 1.f;                              // 1 = held up, 0 = resting on the board
	float Scale = 0.7f;
	float Opacity = 0.f;
	bool bSettling = false;
	double SettleStart = 0.0;
	double SettleSeenDrop = -1.0;

	FSlateRenderTransform TokenTransform() const;
	FSlateRenderTransform ShadowTransform() const;
};
