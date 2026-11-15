#include "SFNHandCursor.h"

#include "FNStudio.h"
#include "FNPowerRing.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

namespace FN7Hand
{
	static const TCHAR* Names[4] = { TEXT("Open"), TEXT("Grab"), TEXT("Point"), TEXT("Fling") };
	// Where the cursor's point is on each sprite (fractions), cut from the concept by tools/hand_cursor/cut_hands.py.
	static const FVector2D Hot[4] = { FVector2D(0.519, 0.465), FVector2D(0.489, 0.381), FVector2D(0.908, 0.066), FVector2D(0.522, 0.589) };
	static constexpr float Height = 48.f;   // Slate units = 48 px at 1080p, DPI-scaled by Slate
	static constexpr double Fade = 0.08;
}

void SFNHandCursor::Construct(const FArguments& InArgs)
{
	using namespace FN7Hand;
	Input = InArgs._Input;
	SetVisibility(EVisibility::HitTestInvisible);
	bReady = true;
	for (int32 k = 0; k < 4; ++k)
	{
		const FString P = FString::Printf(TEXT("/Game/UI/Hand/T_Hand_%s.T_Hand_%s"), Names[k], Names[k]);
		UTexture2D* T = LoadObject<UTexture2D>(nullptr, *P);
		if (!T) { bReady = false; continue; }
		Tex[k].Reset(T);
		Brush[k].SetResourceObject(T);
		Brush[k].DrawAs = ESlateBrushDrawType::Image;
		const float W = T->GetSizeY() > 0 ? Height * (float)T->GetSizeX() / (float)T->GetSizeY() : Height;
		Brush[k].ImageSize = FVector2D(W, Height);
		Hotspot[k] = Hot[k];
	}
	FN7HandCursor::bDrawn = bReady;
	UE_LOG(LogTemp, Display, TEXT("FN7 hand: cursor %s"), bReady ? TEXT("drawn (open / grab / point / fling)") : TEXT("textures missing: the system cursor stays"));
	// Every frame: it follows the mouse.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Continue;
	}));
}

SFNHandCursor::~SFNHandCursor()
{
	FN7HandCursor::bDrawn = false;
}

int32 SFNHandCursor::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace FN7Hand;
	const TSharedPtr<SFNPowerRingInput> In = Input.Pin();
	if (!bReady || !In.IsValid() || !FSlateApplication::IsInitialized()) return LayerId;
	const int32 Now = In->HandState();
	const bool bShow = In->PointerOver() || Now == 1;
	if (!bShow) return LayerId;
	const double T = FPlatformTime::Seconds();
	if (Now != State) { Prev = State; State = Now; ChangedAt = T; }
	const float U = (float)FMath::Clamp((T - ChangedAt) / Fade, 0.0, 1.0);
	const FVector2D Mouse = G.AbsoluteToLocal(In->PointerAbs());
	auto Draw = [&](int32 K, float Alpha, float Scale)
	{
		if (Alpha <= 0.01f) return;
		const FVector2D Size = Brush[K].ImageSize * Scale;
		const FVector2D At = Mouse - Size * Hotspot[K];
		FSlateDrawElement::MakeBox(Out, LayerId + 1, G.ToPaintGeometry(Size, FSlateLayoutTransform(At)), &Brush[K], ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Alpha));
	};
	// Crossfade 80 ms; the fist squashes 1.0 -> 0.9 -> 1.0 as it closes.
	const float Squash = State == 1 ? 1.f - 0.1f * FMath::Sin(U * PI) : 1.f;
	if (U < 1.f) Draw(Prev, 1.f - U, 1.f);
	Draw(State, U, Squash);
	return LayerId + 1;
}
