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
	// The Will's four hot buttons (Chris, 2026-11-15: an ankh, a notched bow, a meal, and a scene of armies).
	else if (K == TEXT("ankh"))
	{
		TArray<FVector2f> Loop;
		for (int32 i = 0; i <= 20; ++i) { const float A = 2.f * PI * i / 20.f; Loop.Add(V(12.f + 3.6f * FMath::Sin(A), 6.6f - 4.4f * FMath::Cos(A))); }
		S.Add(Loop);
		S.Add(Poly({ V(4.5f, 12.5f), V(19.5f, 12.5f) }));
		S.Add(Poly({ V(12, 11), V(12, 22) }));
	}
	else if (K == TEXT("bow"))
	{
		TArray<FVector2f> Arc;
		for (int32 i = 0; i <= 14; ++i) { const float T = (float)i / 14.f, Y = 2.f + 20.f * T; Arc.Add(V(8.f + 9.f * FMath::Sin(PI * T), Y)); }
		S.Add(Arc);
		S.Add(Poly({ V(8, 2), V(4.5f, 12), V(8, 22) }));                    // the string, drawn back
		S.Add(Poly({ V(4.5f, 12), V(22, 12) }));                            // the arrow, notched
		S.Add(Poly({ V(19, 9.5f), V(22, 12), V(19, 14.5f) }));
		S.Add(Poly({ V(6.5f, 10), V(4.5f, 12), V(6.5f, 14) }));             // fletching
	}
	else if (K == TEXT("meal"))
	{
		// a loaf of bread and a goblet (never anything that reads as a burger)
		S.Add(Join({ Arc(8.f, 19.f, 6.f, 180.f, 0.f, 16), Poly({ V(14, 19), V(2, 19) }) }));          // the loaf
		S.Add(Poly({ V(4.8f, 16.4f), V(6.2f, 14.6f) }));                                               // its scored crust
		S.Add(Poly({ V(7.3f, 15.8f), V(8.7f, 14.f) }));
		S.Add(Poly({ V(9.8f, 16.4f), V(11.2f, 14.6f) }));
		S.Add(Poly({ V(15, 4.5f), V(15.4f, 7.8f), V(16.5f, 9.8f), V(18, 10.6f), V(19.5f, 9.8f), V(20.6f, 7.8f), V(21, 4.5f), V(15, 4.5f) }));   // the cup
		S.Add(Poly({ V(18, 10.6f), V(18, 18.5f) }));                                                   // the stem
		S.Add(Poly({ V(15.5f, 19), V(20.5f, 19) }));                                                   // the foot
	}
	// The engraved HUD (Chris, 2026-11-15 graphics notes): the header, the purse, the items button.
	else if (K == TEXT("door"))
	{
		S.Add(Join({ Poly({ V(6, 22), V(6, 10) }), Arc(12.f, 10.f, 6.f, 180.f, 0.f, 16), Poly({ V(18, 22) }) }));
		S.Add(Poly({ V(3, 22), V(21, 22) }));
		S.Add(Circle(15.f, 16.f, 0.9f, 8));
	}
	else if (K == TEXT("question"))
	{
		S.Add(Join({ Arc(12.f, 8.f, 4.6f, 165.f, -40.f, 16), Poly({ V(12, 12.6f), V(12, 15.5f) }) }));
		S.Add(Circle(12.f, 19.6f, 1.1f, 8));
	}
	else if (K == TEXT("tree"))
	{
		S.Add(Poly({ V(12, 22), V(12, 8) }));
		S.Add(Poly({ V(12, 16), V(6.5f, 11) }));
		S.Add(Poly({ V(12, 14), V(17.5f, 9) }));
		S.Add(Poly({ V(12, 11), V(8.5f, 6) }));
		S.Add(Poly({ V(12, 8), V(14.5f, 3.5f) }));
		S.Add(Circle(6.5f, 11.f, 1.3f, 8)); S.Add(Circle(17.5f, 9.f, 1.3f, 8)); S.Add(Circle(8.5f, 6.f, 1.3f, 8)); S.Add(Circle(14.5f, 3.5f, 1.3f, 8));
		S.Add(Poly({ V(8, 22), V(16, 22) }));
	}
	else if (K == TEXT("key"))
	{
		S.Add(Circle(7.f, 12.f, 3.6f, 14));
		S.Add(Poly({ V(10.6f, 12), V(21, 12) }));
		S.Add(Poly({ V(17, 12), V(17, 15.5f) }));
		S.Add(Poly({ V(20, 12), V(20, 15) }));
	}
	else if (K == TEXT("moon"))
	{
		S.Add(Join({ Arc(12.f, 12.f, 8.f, 60.f, 300.f, 20), Arc(15.5f, 12.f, 6.5f, 245.f, 115.f, 16) }));
	}
	else if (K == TEXT("figure"))
	{
		S.Add(Circle(12.f, 5.8f, 2.6f, 12));
		S.Add(Poly({ V(12, 8.6f), V(12, 15) }));
		S.Add(Poly({ V(6.5f, 11), V(17.5f, 11) }));
		S.Add(Poly({ V(8, 21.5f), V(12, 15), V(16, 21.5f) }));
	}
	else if (K == TEXT("swirl"))
	{
		TArray<FVector2f> Sp;
		for (int32 i = 0; i <= 44; ++i) { const float A = i * 0.42f, R = 0.6f + i * 0.21f; Sp.Add(V(12.f + R * FMath::Cos(A), 12.f + R * FMath::Sin(A))); }
		S.Add(Sp);
	}
	else if (K == TEXT("chest"))
	{
		S.Add(Poly({ V(4, 11), V(20, 11), V(20, 20), V(4, 20), V(4, 11) }));
		S.Add(Poly({ V(4, 11), V(5.5f, 6.5f), V(18.5f, 6.5f), V(20, 11) }));
		S.Add(Poly({ V(10.8f, 11), V(13.2f, 11), V(13.2f, 14.2f), V(10.8f, 14.2f), V(10.8f, 11) }));
		S.Add(Poly({ V(4, 15.5f), V(10.8f, 15.5f) })); S.Add(Poly({ V(13.2f, 15.5f), V(20, 15.5f) }));
	}
	else if (K == TEXT("hourglass"))
	{
		S.Add(Poly({ V(6, 3), V(18, 3) })); S.Add(Poly({ V(6, 21), V(18, 21) }));
		S.Add(Poly({ V(7.5f, 3), V(7.5f, 5.5f), V(12, 12), V(7.5f, 18.5f), V(7.5f, 21) }));
		S.Add(Poly({ V(16.5f, 3), V(16.5f, 5.5f), V(12, 12), V(16.5f, 18.5f), V(16.5f, 21) }));
		S.Add(Poly({ V(9.5f, 19.5f), V(14.5f, 19.5f), V(12, 16), V(9.5f, 19.5f) }));
	}
	else if (K == TEXT("spark"))
	{
		S.Add(Poly({ V(12, 3), V(13.6f, 10.4f), V(21, 12), V(13.6f, 13.6f), V(12, 21), V(10.4f, 13.6f), V(3, 12), V(10.4f, 10.4f), V(12, 3) }));
		S.Add(Poly({ V(17.5f, 6.5f), V(19, 5) })); S.Add(Poly({ V(6.5f, 17.5f), V(5, 19) }));
	}
	else if (K == TEXT("armies"))
	{
		for (int32 k = 0; k < 3; ++k)
		{
			const float X = 4.f + 8.f * k, Top = k == 1 ? 3.f : 6.f;
			S.Add(Poly({ V(X, 22), V(X, Top) }));                           // a spear
			S.Add(Poly({ V(X, Top), V(X + 4.5f, Top + 1.6f), V(X, Top + 3.2f) }));   // its banner
			S.Add(Circle(X, 18.f, 2.2f, 10));                               // a shield
		}
		S.Add(Poly({ V(1, 22), V(23, 22) }));
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

	for (const TArray<FVector2f>& Stroke : Strokes(Slug.Get()))
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
		TArray<FVector2f> Mark;
		if (Kind == 1)        // a carved bolt
		{
			const float X = 1.5f + i * 11.f;
			Mark = { FVector2f(X + 5.5f, 0.5f), FVector2f(X + 1.5f, H * 0.58f), FVector2f(X + 5.f, H * 0.58f), FVector2f(X + 4.f, H - 0.5f), FVector2f(X + 8.5f, H * 0.38f), FVector2f(X + 5.f, H * 0.38f), FVector2f(X + 5.5f, 0.5f) };
		}
		else if (Kind == 2)   // a carved spark: a four-point star
		{
			const FVector2f C(5.5f + i * 11.f, H * 0.5f);
			const float R = H * 0.45f, r = H * 0.14f;
			Mark = { C + FVector2f(0, -R), C + FVector2f(r, -r), C + FVector2f(R, 0), C + FVector2f(r, r), C + FVector2f(0, R), C + FVector2f(-r, r), C + FVector2f(-R, 0), C + FVector2f(-r, -r), C + FVector2f(0, -R) };
		}
		else
		{
			Mark = FN7Icons::Circle(4.5f + i * 9.f, H * 0.5f, 2.6f, 12);
		}
		TArray<FVector2f> LipPts;
		for (const FVector2f& P : Mark) LipPts.Add(P + FVector2f(0.f, 1.f));
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), LipPts, ESlateDrawEffect::None, Lip, true, 1.4f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Mark, ESlateDrawEffect::None, Tint, true, Kind ? 1.6f : 2.2f);
	}
	return Layer + 1;
}

// --- tile ---------------------------------------------------------------------

void SFNPowerTile::Construct(const FArguments& InArgs)
{
	Slug = InArgs._Slug;
	Tier = InArgs._Tier;
	TileSize = InArgs._TileSize;
	bArmed = InArgs._bArmed;
	bEnabled = InArgs._bEnabled;
	OnClicked = InArgs._OnClicked;
	OnReorder = InArgs._OnReorder;
	const FText Name = InArgs._Name;
	const int32 Cost = InArgs._Cost;

	// Small tiles (the compact panel) engrave a smaller name so it fits.
	const int32 LabelSize = TileSize < 64.f ? 7 : 9;
	FTextBlockStyle Engraved = FN7Style::Type::Label();
	Engraved.SetFont(FN7Style::Type::Font(LabelSize, TEXT("Bold")));
	Engraved.SetShadowOffset(FVector2D(0.f, 1.f));
	Engraved.SetShadowColorAndOpacity(FLinearColor(1.f, 0.96f, 0.86f, 0.5f));
	static TMap<uint32, FTextBlockStyle> StyleCache;   // one stable style object per label size
	FTextBlockStyle& LabelStyle = StyleCache.FindOrAdd((uint32)LabelSize, Engraved);

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
					return FSlateColor(FMath::Lerp(FLinearColor::White, FLinearColor(1.07f, 1.05f, 1.02f), HoverAlpha));
				})
				.RenderTransform_Lambda([this]() { return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(12.f * DropAlpha, -2.f * HoverAlpha))); })
				.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, IsBeingDragged() ? 0.4f : 1.f); })
				.Padding(TileSize < 64.f ? FMargin(3.f, 6.f, 3.f, 5.f) : FMargin(6.f, 9.f, 6.f, 8.f))
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
						SNew(SFNPips).Count(Cost).Kind(Tier == TEXT("mega") ? 1 : (Tier == TEXT("mini") ? 2 : 0)).Color_Lambda([this]() { return InkColor(); })
					]
				]
			]
			// Armed: an orange ring around the block.
			+ SOverlay::Slot().Padding(FMargin(-3.f, -3.f, -1.f, 2.f))
			[
				SNew(SBorder)
				.BorderImage(FN7Style::Brush::AccentRing())
				.Visibility_Lambda([this]() { return ArmedAlpha > 0.01f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, ArmedAlpha)); })
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

FReply SFNPowerTile::OnDragDetected(const FGeometry&, const FPointerEvent& MouseEvent)
{
	bPressed = false;
	if (!bEnabled.Get()) return FReply::Handled();
	return FReply::Handled().BeginDragDrop(FFNPowerDragDrop::New(Slug, MouseEvent.GetScreenSpacePosition(), Tier));
}

FCursorReply SFNPowerTile::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(bEnabled.Get() ? EMouseCursor::GrabHand : EMouseCursor::SlashedCircle);
}

void SFNPowerTile::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	const float K = 1.f - FMath::Exp(-12.f * Dt);
	HoverAlpha = FMath::Lerp(HoverAlpha, (IsHovered() && bEnabled.Get() && !IsBeingDragged()) ? 1.f : 0.f, K);
	ArmedAlpha = FMath::Lerp(ArmedAlpha, bArmed.Get() ? 1.f : 0.f, 1.f - FMath::Exp(-9.f * Dt));
	if (!FFNPowerDragDrop::GetActive().IsValid()) bDropHover = false;
	DropAlpha = FMath::Lerp(DropAlpha, bDropHover ? 1.f : 0.f, 1.f - FMath::Exp(-12.f * Dt));
}

bool SFNPowerTile::IsBeingDragged() const
{
	const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();
	return Op.IsValid() && Op->Power == Slug && Op->Tier == Tier;
}

void SFNPowerTile::OnDragEnter(const FGeometry&, const FDragDropEvent& E)
{
	const TSharedPtr<FFNPowerDragDrop> Op = E.GetOperationAs<FFNPowerDragDrop>();
	bDropHover = Op.IsValid() && Op->Power != Slug;
}

void SFNPowerTile::OnDragLeave(const FDragDropEvent&)
{
	bDropHover = false;
}

FReply SFNPowerTile::OnDragOver(const FGeometry&, const FDragDropEvent& E)
{
	const TSharedPtr<FFNPowerDragDrop> Op = E.GetOperationAs<FFNPowerDragDrop>();
	return Op.IsValid() ? FReply::Handled() : FReply::Unhandled();
}

FReply SFNPowerTile::OnDrop(const FGeometry&, const FDragDropEvent& E)
{
	bDropHover = false;
	const TSharedPtr<FFNPowerDragDrop> Op = E.GetOperationAs<FFNPowerDragDrop>();
	if (!Op.IsValid()) return FReply::Unhandled();
	if (Op->Power != Slug) OnReorder.ExecuteIfBound(Op->Power);
	return FReply::Handled();   // a drop on the panel never casts
}
