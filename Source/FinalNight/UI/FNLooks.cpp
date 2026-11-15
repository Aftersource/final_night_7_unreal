#include "FNLooks.h"

#include "FNHudPrefs.h"
#include "FNStyle.h"
#include "FNWood.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FN7Looks"

namespace FN7LooksDetail
{
	static const FSlateBrush* None() { static const FSlateNoResource B; return &B; }
	static const FSlateBrush* Glass() { static const FSlateRoundedBoxBrush B(FLinearColor(0.03f, 0.05f, 0.09f, 0.82f), 14.f, FLinearColor(0.55f, 0.8f, 1.f, 0.55f), 1.5f); return &B; }
	static const FSlateBrush* GoldRim() { static const FSlateRoundedBoxBrush B(FLinearColor(0.42f, 0.3f, 0.08f), 12.f, FLinearColor(1.f, 0.8f, 0.36f), 2.f); return &B; }
	static const FSlateBrush* Night() { static const FSlateRoundedBoxBrush B(FLinearColor(0.012f, 0.02f, 0.06f, 0.96f), 12.f, FLinearColor(0.85f, 0.72f, 0.4f, 0.7f), 1.f); return &B; }
	static const FSlateBrush* Dot() { static const FSlateRoundedBoxBrush B(FLinearColor::White, 2.f); return &B; }
	/** The pill, without its padding: the digit fits a 20 px square (with the pill's padding it collapsed to nothing). */
	static const FButtonStyle& Square()
	{
		static FButtonStyle S = []() { FButtonStyle B = FN7Style::PillButton(); B.SetNormalPadding(FMargin(0.f)); B.SetPressedPadding(FMargin(0.f)); return B; }();
		return S;
	}

	/** The star map's sky: fixed stars (the same every time) and a few gold constellation lines. */
	class SStarMap : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SStarMap) {}
		SLATE_END_ARGS()
		void Construct(const FArguments&) {}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8.f, 8.f); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const FVector2D S = G.GetLocalSize();
			FRandomStream R(1977);
			for (int32 i = 0; i < 140; ++i)
			{
				const FVector2D P(R.FRandRange(10.f, S.X - 10.f), R.FRandRange(10.f, S.Y - 10.f));
				const float Sz = R.FRand() < 0.1f ? 3.f : R.FRand() < 0.4f ? 2.f : 1.2f;
				const float A = R.FRandRange(0.25f, 0.95f);
				FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Sz, Sz), FSlateLayoutTransform(FVector2f(P))), Dot(), ESlateDrawEffect::None,
					FLinearColor(0.85f, 0.9f, 1.f, A * Style.GetColorAndOpacityTint().A));
			}
			// Three constellations in thin gold, kept in the margins (never across the words).
			const TArray<TArray<FVector2D>> Shapes = {
				{ {0.03, 0.955}, {0.09, 0.975}, {0.15, 0.96}, {0.21, 0.985}, {0.27, 0.97} },
				{ {0.975, 0.30}, {0.962, 0.38}, {0.978, 0.46}, {0.965, 0.55} },
				{ {0.72, 0.985}, {0.8, 0.965}, {0.88, 0.982}, {0.95, 0.962} } };
			for (const TArray<FVector2D>& Sh : Shapes)
			{
				TArray<FVector2f> Pts;
				for (const FVector2D& P : Sh) Pts.Add(FVector2f(P.X * S.X, P.Y * S.Y));
				FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, FLinearColor(1.f, 0.8f, 0.4f, 0.35f), true, 1.f);
				for (const FVector2f& P : Pts)
					FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(4.f, 4.f), FSlateLayoutTransform(P - FVector2f(2.f, 2.f))), Dot(), ESlateDrawEffect::None, FLinearColor(1.f, 0.9f, 0.6f, 0.9f));
			}
			return Layer + 1;
		}
	};
}

FText FN7Looks::Name(int32 L)
{
	switch (L)
	{
	case 2: return LOCTEXT("Parchment", "PARCHMENT");
	case 3: return LOCTEXT("Wood", "CARVED WOOD");
	case 4: return LOCTEXT("Glass", "DARK GLASS");
	case 5: return LOCTEXT("Gilded", "GILDED SLATE");
	case 6: return LOCTEXT("Stars", "STAR MAP");
	default: return LOCTEXT("Onyx", "ONYX");
	}
}

int32 FN7Looks::Get(const FString& Screen, int32 Default)
{
	return FMath::Clamp(FN7HudPrefs::GetInt(Screen + TEXT("_variant"), Default), 1, Count);
}

void FN7Looks::Set(const FString& Screen, int32 L)
{
	L = FMath::Clamp(L, 1, Count);
	FN7HudPrefs::SetInt(Screen + TEXT("_variant"), L);
	UE_LOG(LogTemp, Display, TEXT("FN7 %s: variant %d (%s)"), *Screen, L, *Name(L).ToString());
}

FLinearColor FN7Looks::Ink(int32 L)
{
	switch (L)
	{
	case 2: return FN7Wood::Ink();
	case 3: return FLinearColor(0.12f, 0.06f, 0.02f);
	case 5: return FLinearColor(1.f, 0.95f, 0.84f);
	case 6: return FLinearColor(0.9f, 0.93f, 1.f);
	default: return FN7Style::Color::Text();
	}
}

FLinearColor FN7Looks::Dim(int32 L)
{
	FLinearColor C = Ink(L);
	if (L == 1 || L == 4) return FN7Style::Color::TextDim();
	C.A = L == 2 || L == 3 ? 0.78f : 0.66f;
	return C;
}

FLinearColor FN7Looks::Accent(int32 L)
{
	switch (L)
	{
	case 2: return FN7Wood::RedInk();
	case 3: return FLinearColor(0.42f, 0.08f, 0.04f);
	case 4: return FLinearColor(0.45f, 0.85f, 1.f);
	case 5: case 6: return FLinearColor(1.f, 0.8f, 0.36f);
	default: return FN7Style::Color::Accent();
	}
}

TSharedRef<SWidget> FN7Looks::Frame(TAttribute<int32> Look, TSharedRef<SWidget> Inner, const FMargin& Pad)
{
	using namespace FN7LooksDetail;
	return SNew(SBorder)   // the gilded rim (gilded slate only)
		.BorderImage_Lambda([Look]() { return Look.Get() == 5 ? GoldRim() : None(); })
		.Padding_Lambda([Look]() { return FMargin(Look.Get() == 5 ? 5.f : 0.f); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image_Lambda([Look]()
				{
					switch (Look.Get())
					{
					case 2: return FN7Wood::WillBoxBrush();
					case 3: return FN7Wood::TileBrush();
					case 4: return Glass();
					case 5: return FN7Wood::StoneBrush();
					case 6: return Night();
					default: return FN7Wood::MarbleBrush();
					}
				})
			]
			+ SOverlay::Slot()
			[
				SNew(SStarMap).Visibility_Lambda([Look]() { return Look.Get() == 6 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			]
			+ SOverlay::Slot().Padding(Pad)
			[
				Inner
			]
		];
}

TSharedRef<SWidget> FN7Looks::Switch(TAttribute<int32> Look, TFunction<void(int32)> OnPick)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 V = 1; V <= Count; ++V)
	{
		Row->AddSlot().AutoWidth().Padding(1.5f, 0.f)
		[
			SNew(SBox).WidthOverride(20.f).HeightOverride(18.f)
			[
				SNew(SButton)
				.ButtonStyle(&FN7LooksDetail::Square())
				.ContentPadding(FMargin(0.f))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ToolTipText(Name(V))
				.ButtonColorAndOpacity_Lambda([Look, V]() { return Look.Get() == V ? FN7Style::Color::Accent() : FLinearColor(1.f, 1.f, 1.f, 0.55f); })
				.OnClicked_Lambda([OnPick, V]() { OnPick(V); return FReply::Handled(); })
				[
					SNew(STextBlock).Font(FN7Style::Type::Font(9, TEXT("Bold")))
					.ColorAndOpacity_Lambda([Look, V]() { return FSlateColor(Look.Get() == V ? FLinearColor(0.05f, 0.03f, 0.02f) : FN7Style::Color::Text()); })
					.Text(FText::AsNumber(V))
				]
			]
		];
	}
	return Row;
}

#undef LOCTEXT_NAMESPACE
