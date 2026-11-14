#include "SFNDragToken.h"

#include "FNPowerDrag.h"
#include "Framework/Application/SlateApplication.h"
#include "FNStyle.h"
#include "FNWood.h"
#include "SFNPowerTile.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCanvas.h"

namespace FN7Token
{
	constexpr float Size = 72.f;
	float Smooth(float Rate, float Dt) { return 1.f - FMath::Exp(-Rate * Dt); }
	float EaseOutCubic(float T) { T = FMath::Clamp(T, 0.f, 1.f); return 1.f - FMath::Pow(1.f - T, 3.f); }
}

void SFNDragToken::Construct(const FArguments&)
{
	using namespace FN7Token;
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	[
		SNew(SCanvas)
		// Shadow on the table.
		+ SCanvas::Slot().Position(FVector2D::ZeroVector).Size(FVector2D(Size, Size))
		[
			SNew(SBorder)
			.BorderImage(FN7Style::Brush::Swatch(FLinearColor(0.f, 0.f, 0.f, 1.f)))
			.RenderTransform_Lambda([this]() { return TOptional<FSlateRenderTransform>(ShadowTransform()); })
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, Opacity * (0.18f + 0.22f * (1.f - Lift))); })
			.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Opacity * (0.18f + 0.22f * (1.f - Lift)))); })
		]
		// The token.
		+ SCanvas::Slot().Position(FVector2D::ZeroVector).Size(FVector2D(Size, Size))
		[
			SNew(SBorder)
			.BorderImage(FN7Wood::TileBrush())
			.Padding(14.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.RenderTransform_Lambda([this]() { return TOptional<FSlateRenderTransform>(TokenTransform()); })
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, Opacity); })
			.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Opacity)); })
			[
				SAssignNew(IconHolder, SBox).WidthOverride(40.f).HeightOverride(40.f)
			]
		]
	];
}

void SFNDragToken::Tick(const FGeometry& G, const double Now, const float Dt)
{
	using namespace FN7Token;
	SCompoundWidget::Tick(G, Now, Dt);

	TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();
	if (Op.IsValid() && Op->bFromRing) Op.Reset();   // a hex from the table's power ring is its own 3D piece: no 2D token
	if (Op.IsValid())
	{
		if (!bDragging || ShownPower != Op->Power)
		{
			bDragging = true;
			bSettling = false;
			AppearTime = Now;
			ShownPower = Op->Power;
			Pos = FVector2D(G.AbsoluteToLocal(Op->ScreenPosition));
			Vel = FVector2D::ZeroVector;
			Scale = 0.72f;
			Opacity = 0.f;
			Lift = 1.f;
			IconHolder->SetContent(SNew(SFNPowerIcon).Slug(ShownPower).Size(40.f).Color(FN7Wood::Ink()));
		}

		// Where it wants to be: the snapped hex if the table reports one, else just ahead of the cursor.
		FVector2D TargetAbs = Op->ScreenPosition;
		bool bSnapped = false;
		if (FFNPowerDragDrop::SnapToBoard().IsBound())
			if (const TOptional<FVector2D> S = FFNPowerDragDrop::SnapToBoard().Execute(Op->Power, Op->ScreenPosition)) { TargetAbs = S.GetValue(); bSnapped = true; }
		const FVector2D Target = FVector2D(G.AbsoluteToLocal(TargetAbs)) + (bSnapped ? FVector2D::ZeroVector : FVector2D(0.f, -10.f));

		// A soft spring: gentle, a little behind, never overshooting much.
		const FVector2D Prev = Pos;
		Pos = FMath::Lerp(Pos, Target, Smooth(bSnapped ? 7.f : 9.f, Dt));
		Vel = FMath::Lerp(Vel, (Pos - Prev) / FMath::Max(Dt, 0.001f), Smooth(8.f, Dt));
		Tilt = FMath::Lerp(Tilt, FMath::Clamp((float)Vel.X * 0.012f, -10.f, 10.f), Smooth(6.f, Dt));

		const float Appear = EaseOutCubic((float)(Now - AppearTime) / 0.28f);
		Opacity = Appear;
		Lift = FMath::Lerp(Lift, bSnapped ? 0.35f : 1.f, Smooth(5.f, Dt));
		Scale = FMath::Lerp(0.72f, 1.f, Appear) * FMath::Lerp(0.88f, 1.f, Lift);
		return;
	}

	if (bDragging)
	{
		// The drag just ended: sink onto where it landed, then fade.
		bDragging = false;
		FVector2D DropAbs;
		double DropTime = 0.0;
		FString DropPower;
		if (FFNPowerDragDrop::GetLastDrop(DropAbs, DropTime, DropPower) && DropTime != SettleSeenDrop)
		{
			SettleSeenDrop = DropTime;
			bSettling = true;
			SettleStart = Now;
			Pos = FMath::Lerp(Pos, FVector2D(G.AbsoluteToLocal(DropAbs)), 0.5f);
		}
		else
		{
			Opacity = 0.f;
		}
	}

	if (bSettling)
	{
		const float T = (float)(Now - SettleStart) / 0.45f;
		Lift = FMath::Lerp(Lift, 0.f, Smooth(9.f, Dt));
		Scale = FMath::Lerp(Scale, 0.78f, Smooth(6.f, Dt));
		Tilt = FMath::Lerp(Tilt, 0.f, Smooth(8.f, Dt));
		Opacity = 1.f - EaseOutCubic(FMath::Max(0.f, T - 0.35f) / 0.65f);
		if (T >= 1.f) { bSettling = false; Opacity = 0.f; }
	}
}

FSlateRenderTransform SFNDragToken::TokenTransform() const
{
	using namespace FN7Token;
	const double Now = FSlateApplication::Get().GetCurrentTime();
	const float Bob = FMath::Sin((float)Now * 2.4f) * 2.5f * Lift;
	const FVector2D TopLeft = Pos - FVector2D(Size * 0.5f, Size * 0.5f) + FVector2D(0.f, -14.f * Lift + Bob);
	const float R = FMath::DegreesToRadians(Tilt);
	const float C = FMath::Cos(R) * Scale, S = FMath::Sin(R) * Scale;
	return FSlateRenderTransform(FMatrix2x2(C, S, -S, C), FVector2f(TopLeft));
}

FSlateRenderTransform SFNDragToken::ShadowTransform() const
{
	using namespace FN7Token;
	const FVector2D TopLeft = Pos - FVector2D(Size * 0.5f, Size * 0.5f) + FVector2D(6.f + 6.f * Lift, 10.f + 4.f * Lift);
	return FSlateRenderTransform(FMatrix2x2(Scale * (0.92f + 0.12f * Lift), 0.f, 0.f, Scale * (0.55f + 0.1f * Lift)), FVector2f(TopLeft));
}
