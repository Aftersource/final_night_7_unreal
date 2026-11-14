#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/** A power's vector icon, drawn with lines on a 24-unit grid: crisp at any size, no fonts or images. */
class FINALNIGHT_API SFNPowerIcon : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPowerIcon)
		: _Size(32.f)
		, _Color(FLinearColor::Black)
	{}
		SLATE_ARGUMENT(FString, Slug)
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** The polylines (24-unit grid) that make up a slug's icon; unknown slugs draw the custom-power star. */
	static TArray<TArray<FVector2f>> Strokes(const FString& Slug);

protected:
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }

private:
	FString Slug;
	float Size = 32.f;
	TAttribute<FLinearColor> Color;
};

/** Carved pips for a bolt cost. */
class FINALNIGHT_API SFNPips : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPips) : _Count(1) {}
		SLATE_ARGUMENT(int32, Count)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Count = FMath::Clamp(InArgs._Count, 0, 6); Color = InArgs._Color; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(FMath::Max(1, Count) * 9.f, 8.f); }

private:
	int32 Count = 1;
	TAttribute<FLinearColor> Color;
};

/**
 * An Entropy power as a carved wood block: procedural grain, bevelled edge, soft shadow, the power's vector icon,
 * its name and its bolt cost as carved pips. Click to arm it; drag it to cast (an FFNPowerDragDrop carrying the
 * slug, see FNPowerDrag.h). Disabled tiles (not enough bolts) are greyed wood and can't be dragged.
 */
class FINALNIGHT_API SFNPowerTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPowerTile)
		: _Cost(1)
		, _bArmed(false)
		, _bEnabled(true)
		, _TileSize(88.f)
	{}
		SLATE_ARGUMENT(FString, Slug)
		SLATE_ARGUMENT(FText, Name)
		SLATE_ARGUMENT(int32, Cost)
		SLATE_ATTRIBUTE(bool, bArmed)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ARGUMENT(float, TileSize)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	FString Slug;
	float TileSize = 88.f;
	TAttribute<bool> bArmed;
	TAttribute<bool> bEnabled;
	FSimpleDelegate OnClicked;
	bool bPressed = false;

	FLinearColor InkColor() const;
	TSharedRef<SWidget> MakeDecorator() const;
};
