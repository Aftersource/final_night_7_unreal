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
	SLATE_BEGIN_ARGS(SFNPips) : _Count(1), _Kind(0) {}
		SLATE_ARGUMENT(int32, Count)
		/** 0 = round pips, 1 = carved bolts (mega), 2 = carved sparks (mini). */
		SLATE_ARGUMENT(int32, Kind)
		SLATE_ATTRIBUTE(FLinearColor, Color)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Count = FMath::Clamp(InArgs._Count, 0, 6); Kind = InArgs._Kind; Color = InArgs._Color; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(FMath::Max(1, Count) * (Kind ? 11.f : 9.f), Kind ? 12.f : 8.f); }

private:
	int32 Count = 1;
	int32 Kind = 0;
	TAttribute<FLinearColor> Color;
};

/**
 * An Entropy power as a carved wood block: procedural grain, bevelled edge, soft shadow, the power's vector icon,
 * its name and its bolt cost as carved pips. Click to arm it; drag it to cast (an FFNPowerDragDrop carrying the
 * slug, see FNPowerDrag.h). Disabled tiles (not enough bolts) are greyed wood and can't be dragged.
 */
DECLARE_DELEGATE_OneParam(FOnFNTileReorder, const FString& /*DraggedSlug*/);

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
		/** "mega", "mini" or empty: the pips show bolts, sparks or plain rings, and the drag carries it. */
		SLATE_ARGUMENT(FString, Tier)
		SLATE_ATTRIBUTE(bool, bArmed)
		SLATE_ATTRIBUTE(bool, bEnabled)
		SLATE_ARGUMENT(float, TileSize)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		/** Another tile was dropped on this one: move it here. */
		SLATE_EVENT(FOnFNTileReorder, OnReorder)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	FString Slug;
	FString Tier;
	float TileSize = 88.f;
	TAttribute<bool> bArmed;
	TAttribute<bool> bEnabled;
	FSimpleDelegate OnClicked;
	FOnFNTileReorder OnReorder;
	bool bDropHover = false;
	float DropAlpha = 0.f;
	bool bPressed = false;
	float HoverAlpha = 0.f;   // eased, so nothing pops
	float ArmedAlpha = 0.f;

	FLinearColor InkColor() const;
	bool IsBeingDragged() const;
};
