#include "SFNPowerTile.h"

#include "FNPowerDrag.h"
#include "FNStyle.h"
#include "FNWood.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace FN7Icons
{
	using FPath = TArray<FVector2f>;

	FPath Poly(std::initializer_list<FVector2f> Points) { return FPath(Points); }

	/** Degrees, counter-clockwise from +X with Y up (converted to screen Y-down). */
	FPath Arc(float Cx, float Cy, float R, float A0, float A1, int32 N = 18)
	{
		FPath P;
		for (int32 i = 0; i <= N; ++i)
		{
			const float A = FMath::DegreesToRadians(FMath::Lerp(A0, A1, (float)i / N));
			P.Add(FVector2f(Cx + R * FMath::Cos(A), Cy - R * FMath::Sin(A)));
		}
		return P;
	}

	FPath Circle(float Cx, float Cy, float R, int32 N = 20) { return Arc(Cx, Cy, R, 0.f, 360.f, N); }

	FPath Wave(float Y0, float Amp, float Cycles, float X0 = 2.f, float X1 = 22.f)
	{
		FPath P;
		const int32 N = 32;
		for (int32 i = 0; i <= N; ++i)
		{
			const float T = (float)i / N;
			P.Add(FVector2f(FMath::Lerp(X0, X1, T), Y0 + Amp * FMath::Sin(T * Cycles * 2.f * PI)));
		}
		return P;
	}

	FPath Join(std::initializer_list<FPath> Parts)
	{
		FPath Out;
		for (const FPath& Part : Parts) Out.Append(Part);
		return Out;
	}
}

// --- icon ---------------------------------------------------------------------

void SFNPowerIcon::Construct(const FArguments& InArgs)
{
	Slug = InArgs._Slug;
	Size = InArgs._Size;
	Color = InArgs._Color;
}

TArray<TArray<FVector2f>> SFNPowerIcon::Strokes(const FString& InSlug)
{
	using namespace FN7Icons;
	using V = FVector2f;
	TArray<FPath> S;
	const FString K = InSlug.ToLower();

	if (K == TEXT("lightning"))
	{
		S.Add(Poly({ V(13, 2), V(4, 14), V(11.5f, 14), V(10.5f, 22), V(20, 10), V(12.5f, 10), V(13, 2) }));
	}
	else if (K == TEXT("storm_clouds") || K == TEXT("storm"))
	{
		S.Add(Join({ Arc(7.5f, 13.5f, 3.5f, 270, 90), Arc(12.5f, 10.f, 5.f, 165, 15), Arc(17.5f, 13.5f, 3.5f, 100, -90), Poly({ V(17.5f, 17), V(7.5f, 17) }) }));
		S.Add(Poly({ V(9, 19.5f), V(8, 22.5f) }));
		S.Add(Poly({ V(13, 19.5f), V(12, 22.5f) }));
		S.Add(Poly({ V(17, 19.5f), V(16, 22.5f) }));
	}
	else if (K == TEXT("volcano"))
	{
		S.Add(Poly({ V(2, 22), V(8.5f, 11), V(10, 12.5f), V(14, 12.5f), V(15.5f, 11), V(22, 22), V(2, 22) }));
		S.Add(Poly({ V(11.5f, 12.5f), V(11, 16), V(12.5f, 15), V(12, 18) }));
		S.Add(Circle(10.f, 7.5f, 1.6f, 14));
		S.Add(Circle(13.5f, 5.f, 2.f, 14));
		S.Add(Circle(17.5f, 3.2f, 1.3f, 12));
	}
	else if (K == TEXT("earthquake"))
	{
		S.Add(Poly({ V(2, 15), V(9, 15) }));
		S.Add(Poly({ V(15, 15), V(22, 15) }));
		S.Add(Poly({ V(12, 3), V(10, 8), V(14, 11), V(10, 15), V(13, 18.5f), V(11, 22) }));
		S.Add(Arc(12.f, 12.f, 10.f, 150, 210, 8));
		S.Add(Arc(12.f, 12.f, 10.f, -30, 30, 8));
	}
	else if (K == TEXT("flood"))
	{
		S.Add(Wave(7.f, 1.6f, 1.5f));
		S.Add(Wave(12.f, 1.6f, 1.5f));
		S.Add(Wave(17.f, 1.6f, 1.5f));
	}
	else if (K == TEXT("wildfire"))
	{
		S.Add(Poly({ V(12, 2), V(14.5f, 6), V(17.5f, 10), V(18, 14), V(16.5f, 18.5f), V(12, 22), V(7.5f, 18.5f), V(6, 14), V(7, 10.5f), V(9, 12.5f), V(9.5f, 8), V(12, 2) }));
		S.Add(Poly({ V(12, 12.5f), V(14.2f, 16), V(12, 20), V(9.8f, 16.5f), V(12, 12.5f) }));
	}
	else if (K == TEXT("meteor"))
	{
		S.Add(Circle(16.f, 8.f, 4.2f, 22));
		S.Add(Circle(17.f, 7.f, 1.0f, 10));
		S.Add(Poly({ V(13, 11.5f), V(4, 20.5f) }));
		S.Add(Poly({ V(11.8f, 9.f), V(5.5f, 15.f) }));
		S.Add(Poly({ V(15.5f, 13.f), V(9.5f, 19.5f) }));
	}
	else if (K == TEXT("raise_land"))
	{
		S.Add(Poly({ V(2, 21), V(9, 11), V(13, 16), V(15.5f, 13), V(22, 21), V(2, 21) }));
		S.Add(Poly({ V(18.5f, 9.5f), V(18.5f, 2) }));
		S.Add(Poly({ V(16, 4.5f), V(18.5f, 2), V(21, 4.5f) }));
	}
	else if (K == TEXT("sink_land"))
	{
		S.Add(Poly({ V(2, 13), V(8, 13), V(10, 17), V(14, 17), V(16, 13), V(22, 13) }));
		S.Add(Poly({ V(12, 2), V(12, 9.5f) }));
		S.Add(Poly({ V(9.5f, 7), V(12, 9.5f), V(14.5f, 7) }));
		S.Add(Poly({ V(4, 21), V(20, 21) }));
	}
	else if (K == TEXT("grow_forest"))
	{
		S.Add(Poly({ V(8, 3), V(3.5f, 11), V(6, 11), V(2.5f, 17), V(13.5f, 17), V(10, 11), V(12.5f, 11), V(8, 3) }));
		S.Add(Poly({ V(8, 17), V(8, 21.5f) }));
		S.Add(Poly({ V(17, 8), V(13.5f, 14.5f), V(20.5f, 14.5f), V(17, 8) }));
		S.Add(Poly({ V(17, 14.5f), V(17, 21.5f) }));
		S.Add(Poly({ V(2, 21.5f), V(22, 21.5f) }));
	}
	else if (K == TEXT("swarm"))
	{
		S.Add(Poly({ V(3, 20), V(7, 15), V(12, 12.5f), V(17, 9), V(21, 4) }));
		S.Add(Circle(6.f, 9.f, 1.7f, 12));
		S.Add(Circle(11.f, 5.5f, 1.7f, 12));
		S.Add(Circle(15.5f, 15.f, 1.7f, 12));
		S.Add(Circle(20.f, 12.5f, 1.7f, 12));
		S.Add(Circle(9.5f, 19.5f, 1.7f, 12));
	}
	else
	{
		S.Add(Poly({ V(12, 2), V(14, 10), V(22, 12), V(14, 14), V(12, 22), V(10, 14), V(2, 12), V(10, 10), V(12, 2) }));
		S.Add(Poly({ V(19, 2.5f), V(19.7f, 4.3f), V(21.5f, 5), V(19.7f, 5.7f), V(19, 7.5f), V(18.3f, 5.7f), V(16.5f, 5), V(18.3f, 4.3f), V(19, 2.5f) }));
	}
	return S;
}

int32 SFNPowerIcon::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool) const
{
	const FVector2f Local = FVector2f(G.GetLocalSize());
	const float Scale = FMath::Min(Local.X, Local.Y) / 24.f;
	const FVector2f Offset = (Local - FVector2f(24.f * Scale)) * 0.5f;
	const float Thick = FMath::Max(1.5f, 1.85f * Scale);
	const FLinearColor Tint = Color.Get() * Style.GetColorAndOpacityTint();
	const FLinearColor Lip(1.f, 0.96f, 0.86f, 0.45f * Tint.A);   // the carved lip: a light line just below each stroke

	for (const TArray<FVector2f>& Stroke : Strokes(Slug))
	{
		TArray<FVector2f> Pts, LipPts;
		Pts.Reserve(Stroke.Num());
		LipPts.Reserve(Stroke.Num());
		for (const FVector2f& P : Stroke)
		{
			const FVector2f Q = Offset + P * Scale;
			Pts.Add(Q);
			LipPts.Add(Q + FVector2f(0.f, FMath::Max(1.f, 0.6f * Scale)));
		}
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), LipPts, ESlateDrawEffect::None, Lip, true, Thick);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Tint, true, Thick);
	}
	return Layer + 1;
}

// --- pips ---------------------------------------------------------------------

int32 SFNPips::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool) const
{
	const FLinearColor Tint = Color.Get() * Style.GetColorAndOpacityTint();
	const FLinearColor Lip(1.f, 0.96f, 0.86f, 0.5f * Tint.A);
	const float H = G.GetLocalSize().Y;
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector2f C(4.5f + i * 9.f, H * 0.5f);
		TArray<FVector2f> Ring = FN7Icons::Circle(C.X, C.Y, 2.6f, 12);
		TArray<FVector2f> RingLip = FN7Icons::Circle(C.X, C.Y + 1.f, 2.6f, 12);
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), RingLip, ESlateDrawEffect::None, Lip, true, 1.6f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Tint, true, 2.2f);
	}
	return Layer + 1;
}

// --- tile ---------------------------------------------------------------------

void SFNPowerTile::Construct(const FArguments& InArgs)
{
	Slug = InArgs._Slug;
	TileSize = InArgs._TileSize;
	bArmed = InArgs._bArmed;
	bEnabled = InArgs._bEnabled;
	OnClicked = InArgs._OnClicked;
	const FText Name = InArgs._Name;
	const int32 Cost = InArgs._Cost;

	FTextBlockStyle Engraved = FN7Style::Type::Label();
	Engraved.SetFont(FN7Style::Type::Font(9, TEXT("Bold")));
	Engraved.SetShadowOffset(FVector2D(0.f, 1.f));
	Engraved.SetShadowColorAndOpacity(FLinearColor(1.f, 0.96f, 0.86f, 0.5f));
	static TMap<uint32, FTextBlockStyle> StyleCache;   // one stable style object for every tile
	FTextBlockStyle& LabelStyle = StyleCache.FindOrAdd(0, Engraved);

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(TileSize)
		.HeightOverride(TileSize)
		[
			SNew(SOverlay)
			// Soft shadow under the block.
			+ SOverlay::Slot().Padding(FMargin(2.f, 5.f, 0.f, 0.f))
			[
				SNew(SBorder).BorderImage(FN7Style::Brush::Swatch(FLinearColor(0.f, 0.f, 0.f, 0.42f)))
			]
			// The block.
			+ SOverlay::Slot().Padding(FMargin(0.f, 0.f, 2.f, 5.f))
			[
				SNew(SBorder)
				.BorderImage(FN7Wood::TileBrush())
				.BorderBackgroundColor_Lambda([this]()
				{
					if (!bEnabled.Get()) return FSlateColor(FLinearColor(0.42f, 0.42f, 0.44f));
					return FSlateColor(IsHovered() ? FLinearColor(1.08f, 1.06f, 1.02f) : FLinearColor::White);
				})
				.RenderTransform_Lambda([this]() { return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(0.f, IsHovered() && bEnabled.Get() ? -2.f : 0.f))); })
				.Padding(FMargin(6.f, 9.f, 6.f, 8.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SFNPowerIcon).Slug(Slug).Size(TileSize * 0.40f).Color_Lambda([this]() { return InkColor(); })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 3.f)
					[
						SNew(STextBlock).TextStyle(&LabelStyle).Text(Name).Justification(ETextJustify::Center).AutoWrapText(true).LineHeightPercentage(0.9f)
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(InkColor()); })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SFNPips).Count(Cost).Color_Lambda([this]() { return InkColor(); })
					]
				]
			]
			// Armed: an orange ring around the block.
			+ SOverlay::Slot().Padding(FMargin(-3.f, -3.f, -1.f, 2.f))
			[
				SNew(SBorder)
				.BorderImage(FN7Style::Brush::AccentRing())
				.Visibility_Lambda([this]() { return bArmed.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.ColorAndOpacity(FLinearColor::White)
			]
		]
	];
}

FLinearColor SFNPowerTile::InkColor() const
{
	return bEnabled.Get() ? FN7Wood::Ink() : FLinearColor(0.16f, 0.16f, 0.17f, 0.85f);
}

FReply SFNPowerTile::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bPressed = true;
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SFNPowerTile::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (bPressed) OnClicked.ExecuteIfBound();
	bPressed = false;
	return FReply::Handled();
}

FReply SFNPowerTile::OnDragDetected(const FGeometry&, const FPointerEvent&)
{
	bPressed = false;
	if (!bEnabled.Get()) return FReply::Handled();
	return FReply::Handled().BeginDragDrop(FFNPowerDragDrop::New(Slug, MakeDecorator()));
}

FCursorReply SFNPowerTile::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(bEnabled.Get() ? EMouseCursor::GrabHand : EMouseCursor::SlashedCircle);
}

TSharedRef<SWidget> SFNPowerTile::MakeDecorator() const
{
	return SNew(SBox)
		.WidthOverride(64.f)
		.HeightOverride(64.f)
		[
			SNew(SBorder)
			.BorderImage(FN7Wood::TileBrush())
			.Padding(12.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SFNPowerIcon).Slug(Slug).Size(36.f).Color(FN7Wood::Ink())
			]
		];
}
