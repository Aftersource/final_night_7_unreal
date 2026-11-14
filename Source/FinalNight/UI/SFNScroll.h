#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SBox;

/**
 * An old cloth scroll: woven linen (FN7Wood::FabricBrush) with crisp torn edges between two turned wooden rods.
 * It unrolls from the top rod when bUnrolled turns true (the cloth drops with a little give, the bottom rod sways
 * and settles, a curl shadow rides the rolled edge) and rolls back up when it turns false. Content is inscribed
 * on the cloth and revealed as it unrolls.
 */
class FINALNIGHT_API SFNScroll : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNScroll)
		: _bUnrolled(true)
		, _Duration(1.4f)
		, _ContentPadding(FMargin(34.f, 22.f))
		, _RodHeight(30.f)
		, _bStartOpen(false)
	{}
		SLATE_ATTRIBUTE(bool, bUnrolled)
		SLATE_ARGUMENT(float, Duration)
		SLATE_ARGUMENT(FMargin, ContentPadding)
		/** The rods' thickness (the header's scroll uses slim rods). */
		SLATE_ARGUMENT(float, RodHeight)
		/** Already unrolled when built (a panel rebuilt on every refresh must not replay the unroll). */
		SLATE_ARGUMENT(bool, bStartOpen)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TAttribute<bool> bUnrolled;
	float Duration = 1.4f;
	FMargin ContentPadding;
	float RodH = 30.f;
	TSharedPtr<SWidget> Inner;

	bool bTarget = false;
	bool bAnimating = false;
	double AnimStart = 0.0;
	float From = 0.f;
	float Progress = 0.f;   // 0 rolled up .. 1 open (briefly a little past 1 as the cloth gives)
	float Sway = 0.f;

	FOptionalSize GetBodyHeight() const;
	float CurlStrength() const;
};
