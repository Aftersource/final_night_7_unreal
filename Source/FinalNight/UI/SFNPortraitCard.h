#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "FNStyle.h"

class UTexture2D;

/** A small hourglass drawn with lines, so it reads at any size without a font glyph. */
class FINALNIGHT_API SFNHourglass : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNHourglass)
		: _Color(FLinearColor::White)
		, _Size(FVector2D(10.f, 14.f))
	{}
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ARGUMENT(FVector2D, Size)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TAttribute<FLinearColor> Color;
	FVector2D Size;
};

/**
 * A seat's portrait card: the image (rounded), the name, a HUMAN / AI / FALLEN pill, an aura border in the role's
 * colour, an optional hourglass badge (the seat holds Time's window) and an optional "YOU" marker.
 * The image comes from SetImageFromPng (the bytes of /media/<hash>.png), SetTexture, or the Image brush argument;
 * with none of them it shows the name's initial.
 */
class FINALNIGHT_API SFNPortraitCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPortraitCard)
		: _Image(nullptr)
		, _Role(EFNSeatRole::AI)
		, _bIsYou(false)
		, _bShowHourglass(false)
		, _bPulse(true)
		, _CardWidth(140.f)
	{}
		SLATE_ATTRIBUTE(const FSlateBrush*, Image)
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ATTRIBUTE(EFNSeatRole, Role)
		SLATE_ATTRIBUTE(bool, bIsYou)
		SLATE_ATTRIBUTE(bool, bShowHourglass)
		SLATE_ATTRIBUTE(FText, HourglassText)
		SLATE_ARGUMENT(bool, bPulse)
		SLATE_ARGUMENT(float, CardWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Use this texture as the portrait (kept alive by the card). */
	void SetTexture(UTexture2D* InTexture);

	/** Decode PNG/JPEG bytes into the portrait. Returns false when the bytes don't decode. */
	bool SetImageFromPng(const TArray<uint8>& Bytes);

	void ClearImage();

private:
	TAttribute<const FSlateBrush*> FallbackImage;
	TAttribute<FText> Name;
	TAttribute<EFNSeatRole> Role;
	TAttribute<bool> bIsYou;
	TAttribute<bool> bShowHourglass;
	TAttribute<FText> HourglassText;
	bool bPulse = true;
	float ImageSide = 120.f;

	TStrongObjectPtr<UTexture2D> Texture;
	FSlateBrush RoundedImage;

	const FSlateBrush* GetImage() const;
	EVisibility GetPlaceholderVisibility() const;
	FSlateColor GetImageTint() const;
	FSlateColor GetGlowTint() const;
	FText GetInitial() const;
	EActiveTimerReturnType TickPulse(double InCurrentTime, float InDeltaTime);
};
