#include "SFNFoil.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace FN7FoilGen
{
	/** A pastel hue for the foil: H in [0,1). */
	FLinearColor Hue(float H, float A)
	{
		FLinearColor C = FLinearColor::MakeFromHSV8((uint8)(FMath::Frac(H) * 255.f), 190, 255);
		C.A = A;
		return C;
	}

	const FSlateBrush* GoldEdge(float Radius)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
		const int32 Key = FMath::RoundToInt(Radius);
		TSharedPtr<FSlateRoundedBoxBrush>& B = Cache.FindOrAdd(Key);
		if (!B.IsValid()) B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Transparent, Radius, FLinearColor(1.f, 0.78f, 0.28f, 1.f), 2.5f);
		return B.Get();
	}
}

void SFNFoil::Construct(const FArguments& InArgs)
{
	bHot = InArgs._bHot;
	Strength = InArgs._Strength;
	CornerRadius = InArgs._CornerRadius;
	StartTime = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
	SetCanTick(false);
	SetClipping(EWidgetClipping::ClipToBounds);
	// Repaint every frame while visible: the sheen moves with time and the mouse.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Continue;
	}));
}

int32 SFNFoil::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool) const
{
	const float K = FMath::Clamp(Strength.Get(), 0.f, 2.f) * Style.GetColorAndOpacityTint().A;
	const FVector2f Size = FVector2f(G.GetLocalSize());
	if (Size.X < 4.f || Size.Y < 4.f) return Layer;
	const float T = (float)((FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0) - StartTime);
	if (K <= 0.f)
	{
		// No sheen; the gold edge still breathes while time is slowed for you.
		if (bHot.Get())
		{
			const float Pulse = 0.55f + 0.45f * FMath::Sin(T * 5.f);
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), FN7FoilGen::GoldEdge(CornerRadius), ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Pulse));
		}
		return Layer;
	}

	// The mouse, in local space, as 0..1 (the sheen leans toward it; outside the widget it just drifts).
	FVector2f Mouse(0.5f, 0.5f);
	if (FSlateApplication::IsInitialized())
	{
		const FVector2f Local = FVector2f(G.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()));
		Mouse = FVector2f(FMath::Clamp(Local.X / Size.X, -0.5f, 1.5f), FMath::Clamp(Local.Y / Size.Y, -0.5f, 1.5f));
	}

	// 1) The rainbow foil: a wide band of hues, rotated 24 degrees, sliding with time and the mouse.
	const float Diag = FMath::Sqrt(Size.X * Size.X + Size.Y * Size.Y);
	const float BandW = Diag * 2.2f;
	const float Shift = FMath::Frac(T * 0.045f + Mouse.X * 0.35f + Mouse.Y * 0.12f);
	TArray<FSlateGradientStop> Stops;
	const int32 N = 14;
	for (int32 i = 0; i <= N; ++i)
	{
		const float U = (float)i / N;
		const float A = (0.20f + 0.09f * FMath::Sin(U * 19.f + T * 1.3f)) * K;
		Stops.Add(FSlateGradientStop(FVector2D(U * BandW, 0.f), FN7FoilGen::Hue(U * 2.f + Shift, A)));
	}
	{
		const FVector2f BandSize(BandW, Diag * 1.6f);
		const FVector2f Centre = Size * 0.5f;
		const float Slide = (Shift - 0.5f) * Diag * 0.6f;
		const FSlateRenderTransform Rot(FQuat2D(FMath::DegreesToRadians(-24.f)));
		const FSlateLayoutTransform Place(FVector2f(Centre.X - BandSize.X * 0.5f + Slide, Centre.Y - BandSize.Y * 0.5f));
		const FPaintGeometry PG = G.ToPaintGeometry(BandSize, Place, Rot, FVector2f(0.5f, 0.5f));
		FSlateDrawElement::MakeGradient(Out, Layer, PG, Stops, Orient_Vertical, ESlateDrawEffect::None);
	}

	// 2) The specular streak: a narrow white band through the mouse, leaning the same way.
	{
		const float W = FMath::Max(40.f, Size.X * 0.10f);
		const FVector2f BandSize(W, Diag * 1.6f);
		const float X = FMath::Lerp(-W, Size.X + W, FMath::Clamp(Mouse.X, 0.f, 1.f)) + FMath::Sin(T * 0.7f) * 12.f;
		const FSlateRenderTransform Rot(FQuat2D(FMath::DegreesToRadians(-24.f)));
		const FSlateLayoutTransform Place(FVector2f(X - W * 0.5f, Size.Y * 0.5f - BandSize.Y * 0.5f));
		TArray<FSlateGradientStop> Spec;
		Spec.Add(FSlateGradientStop(FVector2D(0.f, 0.f), FLinearColor(1.f, 1.f, 1.f, 0.f)));
		Spec.Add(FSlateGradientStop(FVector2D(W * 0.5f, 0.f), FLinearColor(1.f, 1.f, 0.96f, 0.32f * K)));
		Spec.Add(FSlateGradientStop(FVector2D(W, 0.f), FLinearColor(1.f, 1.f, 1.f, 0.f)));
		FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(BandSize, Place, Rot, FVector2f(0.5f, 0.5f)), Spec, Orient_Vertical, ESlateDrawEffect::None);
	}

	// 3) Sparkles: small four-point stars that twinkle in and out at fixed spots.
	for (int32 i = 0; i < 14; ++i)
	{
		const float Px = FMath::Frac(FMath::Sin(i * 12.9898f) * 43758.5453f);
		const float Py = FMath::Frac(FMath::Sin(i * 78.233f) * 12543.137f);
		const float Phase = FMath::Frac(T * (0.35f + 0.07f * i) + Px);
		const float Tw = FMath::Max(0.f, FMath::Sin(Phase * PI));
		if (Tw < 0.05f) continue;
		const FVector2f C(8.f + Px * (Size.X - 16.f), 6.f + Py * (Size.Y - 12.f));
		const float R = (2.f + 4.f * Tw) * (i % 3 == 0 ? 1.4f : 1.f);
		const FLinearColor Col = FN7FoilGen::Hue(Px + T * 0.1f, 0.9f * Tw * K);
		const FLinearColor White(1.f, 1.f, 1.f, 0.85f * Tw * K);
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { C + FVector2f(-R, 0), C + FVector2f(R, 0) }, ESlateDrawEffect::None, Col, true, 1.5f);
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { C + FVector2f(0, -R), C + FVector2f(0, R) }, ESlateDrawEffect::None, Col, true, 1.5f);
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { C + FVector2f(-R * 0.35f, -R * 0.35f), C + FVector2f(R * 0.35f, R * 0.35f) }, ESlateDrawEffect::None, White, true, 1.f);
		FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { C + FVector2f(-R * 0.35f, R * 0.35f), C + FVector2f(R * 0.35f, -R * 0.35f) }, ESlateDrawEffect::None, White, true, 1.f);
	}

	// 4) Time slowed for you: a gold edge that breathes.
	if (bHot.Get())
	{
		const float Pulse = 0.55f + 0.45f * FMath::Sin(T * 5.f);
		FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(), FN7FoilGen::GoldEdge(CornerRadius), ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Pulse));
		const FVector2f Inset(3.f, 3.f);
		FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(Size - Inset * 2.f, FSlateLayoutTransform(Inset)), FN7FoilGen::GoldEdge(FMath::Max(2.f, CornerRadius - 3.f)),
			ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Pulse * 0.45f));
	}
	return Layer + 3;
}
