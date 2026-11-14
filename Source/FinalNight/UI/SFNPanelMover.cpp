#include "SFNPanelMover.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "FNHudPrefs.h"
#include "FNStyle.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"

namespace FN7Mover
{
	TArray<TWeakPtr<SFNPanelMover>> All;
	constexpr float SnapPx = 18.f;
	constexpr float Grid = 8.f;
	constexpr float Margin = 24.f;

	/** Six dots: the grip. */
	class SGrip : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGrip) {}
			SLATE_ATTRIBUTE(float, Alpha)
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs) { Alpha = InArgs._Alpha; }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& S, bool) const override
		{
			const float A = Alpha.Get();
			if (A <= 0.01f) return Layer;
			const FSlateBrush* Dot = FN7Style::Brush::Swatch(FLinearColor::White);
			for (int32 i = 0; i < 6; ++i)
			{
				const FVector2f P(6.f + (i % 2) * 6.f, 5.f + (i / 2) * 6.f);
				FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(3.5f, 3.5f), FSlateLayoutTransform(P)), Dot,
					ESlateDrawEffect::None, FN7Style::Color::Accent() * FLinearColor(1.f, 1.f, 1.f, A));
			}
			return Layer;
		}
		virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override { return FCursorReply::Cursor(EMouseCursor::GrabHand); }
	protected:
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(20.f, 22.f); }
	private:
		TAttribute<float> Alpha;
	};
}

void SFNPanelMover::Construct(const FArguments& InArgs)
{
	Id = InArgs._Id;
	Offset = Target = FN7HudPrefs::GetPanelOffset(Id);
	FN7Mover::All.Add(SharedThis(this));

	ChildSlot
	[
		SNew(SBox)
		.RenderTransform_Lambda([this]() { return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(Offset))); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[InArgs._Content.Widget]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(-4.f, -4.f, 0.f, 0.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
				.Padding(0.f)
				.OnMouseButtonDown(this, &SFNPanelMover::GripDown)
				.Cursor(EMouseCursor::GrabHand)
				[
					SNew(FN7Mover::SGrip).Alpha_Lambda([this]() { return GripAlpha; })
				]
			]
		]
	];
}

SFNPanelMover::~SFNPanelMover()
{
	FN7Mover::All.RemoveAll([this](const TWeakPtr<SFNPanelMover>& W) { return !W.IsValid() || W.Pin().Get() == this; });
}

void SFNPanelMover::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	LastGeometry = G;
	GripAlpha = FMath::Lerp(GripAlpha, (IsHovered() || bDragging) ? 1.f : 0.f, 1.f - FMath::Exp(-10.f * Dt));
	if (!bDragging) Offset = FMath::Lerp(Offset, Target, 1.f - FMath::Exp(-14.f * Dt));   // glide into the snap
}

FReply SFNPanelMover::GripDown(const FGeometry&, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bDragging = true;
	DragStartMouse = E.GetScreenSpacePosition();
	DragStartOffset = Offset;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFNPanelMover::GripMove(const FGeometry&, const FPointerEvent& E)
{
	if (!bDragging) return FReply::Unhandled();
	const float Scale = LastGeometry.Scale > 0.f ? LastGeometry.Scale : 1.f;
	Offset = Target = DragStartOffset + (E.GetScreenSpacePosition() - DragStartMouse) / Scale;
	return FReply::Handled();
}

FReply SFNPanelMover::GripUp(const FGeometry&, const FPointerEvent& E)
{
	if (!bDragging || E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bDragging = false;
	Target = Snap(Offset);
	FN7HudPrefs::SetPanelOffset(Id, Target);
	return FReply::Handled().ReleaseMouseCapture();
}

FSlateRect SFNPanelMover::HomeRect() const
{
	const FVector2D Pos = FVector2D(LastGeometry.GetAbsolutePosition());
	const FVector2D Size = FVector2D(LastGeometry.GetAbsoluteSize());
	return FSlateRect(Pos, Pos + Size);
}

FVector2D SFNPanelMover::Snap(const FVector2D& Wanted) const
{
	using namespace FN7Mover;
	const float S = LastGeometry.Scale > 0.f ? LastGeometry.Scale : 1.f;
	const FSlateRect Home = HomeRect();
	FSlateRect Me(Home.Left + Wanted.X * S, Home.Top + Wanted.Y * S, Home.Right + Wanted.X * S, Home.Bottom + Wanted.Y * S);

	// What it can snap to, in absolute pixels: the viewport (inset by the margin, plus its centre lines) and the others.
	TArray<float> Xs, Ys;
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	if (TSharedPtr<SViewport> VP = VC ? VC->GetGameViewportWidget() : nullptr)
	{
		const FGeometry& VG = VP->GetCachedGeometry();
		const FVector2D A = FVector2D(VG.GetAbsolutePosition()), B = A + FVector2D(VG.GetAbsoluteSize());
		Xs.Append({ (float)A.X + Margin * S, (float)B.X - Margin * S, (float)(A.X + B.X) * 0.5f });
		Ys.Append({ (float)A.Y + Margin * S, (float)B.Y - Margin * S, (float)(A.Y + B.Y) * 0.5f });
	}
	for (const TWeakPtr<SFNPanelMover>& W : All)
	{
		const TSharedPtr<SFNPanelMover> P = W.Pin();
		if (!P.IsValid() || P.Get() == this) continue;
		const FSlateRect H = P->HomeRect();
		const FSlateRect O(H.Left + P->Target.X * S, H.Top + P->Target.Y * S, H.Right + P->Target.X * S, H.Bottom + P->Target.Y * S);
		Xs.Append({ (float)O.Left - Margin * S * 0.5f, (float)O.Right + Margin * S * 0.5f, (float)O.Left, (float)O.Right });
		Ys.Append({ (float)O.Top - Margin * S * 0.5f, (float)O.Bottom + Margin * S * 0.5f, (float)O.Top, (float)O.Bottom });
	}

	auto Best = [S](float A, float B, float Mid, const TArray<float>& Lines, float& OutShift) -> bool
	{
		float BestD = SnapPx * S;
		bool bFound = false;
		for (float L : Lines)
			for (float EdgeV : { A, B, Mid })
			{
				const float D = L - EdgeV;
				if (FMath::Abs(D) < BestD) { BestD = FMath::Abs(D); OutShift = D; bFound = true; }
			}
		return bFound;
	};
	FVector2D Out = Wanted;
	float Dx = 0.f, Dy = 0.f;
	if (Best(Me.Left, Me.Right, (Me.Left + Me.Right) * 0.5f, Xs, Dx)) Out.X += Dx / S;
	else Out.X = FMath::RoundToFloat(Out.X / Grid) * Grid;
	if (Best(Me.Top, Me.Bottom, (Me.Top + Me.Bottom) * 0.5f, Ys, Dy)) Out.Y += Dy / S;
	else Out.Y = FMath::RoundToFloat(Out.Y / Grid) * Grid;
	return Out;
}

bool SFNPanelMover::IsOverAnyPanel(const FVector2D& P)
{
	for (const TWeakPtr<SFNPanelMover>& W : FN7Mover::All)
	{
		const TSharedPtr<SFNPanelMover> M = W.Pin();
		if (!M.IsValid() || M->GetVisibility() == EVisibility::Collapsed) continue;
		const float S = M->LastGeometry.Scale > 0.f ? M->LastGeometry.Scale : 1.f;
		const FSlateRect H = M->HomeRect();
		const FSlateRect R(H.Left + M->Offset.X * S, H.Top + M->Offset.Y * S, H.Right + M->Offset.X * S, H.Bottom + M->Offset.Y * S);
		if (R.GetSize().X > 1.f && R.ContainsPoint(P)) return true;
	}
	return false;
}
