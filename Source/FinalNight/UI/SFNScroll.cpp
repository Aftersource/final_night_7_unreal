#include "SFNScroll.h"

#include "FNWood.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

namespace FN7ScrollAnim
{
	constexpr float Overhang = 14.f;   // the rods reach past the cloth on each side

	/** Ease out with a little give at the end: the cloth's weight drops it a hair past open, then it settles. */
	float Unroll(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		const float C1 = 1.15f, C3 = C1 + 1.f;
		return 1.f + C3 * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f);
	}
	float RollUp(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
}

void SFNScroll::Construct(const FArguments& InArgs)
{
	using namespace FN7ScrollAnim;
	bUnrolled = InArgs._bUnrolled;
	Duration = FMath::Max(0.2f, InArgs._Duration);
	ContentPadding = InArgs._ContentPadding;
	RodH = FMath::Max(6.f, InArgs._RodHeight);
	bSmooth = InArgs._bSmoothHeight;
	Inner = InArgs._Content.Widget;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Top rod.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(RodH)
			[
				SNew(SImage).Image(FN7Wood::RodBrush())
			]
		]

		// The cloth, as much of it as is unrolled.
		+ SVerticalBox::Slot().AutoHeight().Padding(Overhang, 0.f)
		[
			SNew(SBox)
			.HeightOverride(this, &SFNScroll::GetBodyHeight)
			.Clipping(EWidgetClipping::ClipToBounds)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					// The cloth never sizes the scroll (its texture is 640 px tall): only the inscription does.
					SNew(SImage).Image(FN7Wood::FabricBrush()).DesiredSizeOverride(FVector2D(8.f, 8.f))
				]
				+ SOverlay::Slot().VAlign(VAlign_Top).Padding(ContentPadding)
				[
					Inner.ToSharedRef()
				]
				// Shadow under the top rod.
				+ SOverlay::Slot().VAlign(VAlign_Top)
				[
					SNew(SBox).HeightOverride(16.f).Visibility(EVisibility::HitTestInvisible)[SNew(SImage).Image(FN7Wood::ShadeBrush(false))]   // never eats a click (audit: it covered the header's TREE button)
				]
				// The curl: the rolled cloth throws a shadow up onto the open part, strongest while it moves.
				+ SOverlay::Slot().VAlign(VAlign_Bottom)
				[
					SNew(SBox).HeightOverride(26.f).Visibility(EVisibility::HitTestInvisible)   // shadows never eat clicks (audit: the WILL examples, TREE)
					[
						SNew(SImage)
						.Image(FN7Wood::ShadeBrush(true))
						.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, CurlStrength()); })
					]
				]
			]
		]

		// Bottom rod: it carries the rolled cloth down, sways, and settles.
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(RodH)
			.RenderTransform_Lambda([this]() { return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(Sway, 0.f))); })
			[
				SNew(SImage).Image(FN7Wood::RodBrush())
			]
		]
	];

	bTarget = bUnrolled.Get();
	Progress = (InArgs._bStartOpen && bTarget) ? 1.f : 0.f;
	From = Progress;
	bAnimating = bTarget && Progress < 1.f;
	AnimStart = -1.0;   // starts on the first tick
}

void SFNScroll::Tick(const FGeometry& G, const double Now, const float Dt)
{
	using namespace FN7ScrollAnim;
	SCompoundWidget::Tick(G, Now, Dt);

	const bool bWant = bUnrolled.Get();
	if (bWant != bTarget)
	{
		bTarget = bWant;
		bAnimating = true;
		From = Progress;
		AnimStart = Now;
	}
	if (bAnimating && AnimStart < 0.0) AnimStart = Now;
	if (!bAnimating)
	{
		Sway = FMath::Lerp(Sway, 0.f, 1.f - FMath::Exp(-6.f * Dt));
		if (bSmooth && Progress >= 1.f && Inner.IsValid())
		{
			// The inscription changed: the open cloth eases to its new height (no re-roll).
			const float Target = Inner->GetDesiredSize().Y + ContentPadding.GetTotalSpaceAlong<Orient_Vertical>() + 12.f;
			SmoothH = SmoothH < 0.f ? Target : FMath::FInterpTo(SmoothH, Target, Dt, 9.f);
		}
		return;
	}

	const float T = (float)(Now - AnimStart) / Duration;
	if (bTarget)
	{
		Progress = FMath::Lerp(From, 1.f, Unroll(T));
		Sway = 5.f * FMath::Sin(T * 9.f) * FMath::Exp(-3.5f * T) * (1.f - FMath::Clamp(T, 0.f, 1.f) * 0.3f);
	}
	else
	{
		Progress = FMath::Lerp(From, 0.f, RollUp(T * 1.4f));
		Sway = 0.f;
	}
	if (T >= (bTarget ? 1.f : 1.f / 1.4f))
	{
		bAnimating = false;
		Progress = bTarget ? 1.f : 0.f;
	}
}

FOptionalSize SFNScroll::GetBodyHeight() const
{
	// Open and still: size to the content (no override). A panel rebuilt on every refresh otherwise measured its content
	// at 0 for a frame and collapsed ("the scrolls keep scrolling up").
	if (!bAnimating && Progress >= 1.f) return (bSmooth && SmoothH > 0.f) ? FOptionalSize(SmoothH) : FOptionalSize();
	const float Full = Inner.IsValid() ? Inner->GetDesiredSize().Y + ContentPadding.GetTotalSpaceAlong<Orient_Vertical>() + 12.f : 0.f;
	return FMath::Max(0.f, Full * Progress);
}

float SFNScroll::CurlStrength() const
{
	return bAnimating ? 0.9f : 0.35f;
}
