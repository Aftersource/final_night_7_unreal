#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/DragAndDrop.h"
#include "FNPowerRing.generated.h"

class AFNStudio;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;

/**
 * The Entropy powers as game pieces on the table (Chris's design): one carved wood hex per power, lying on the table in
 * an arc that hugs the RIGHT side of the board circle, the power's icon inked on a plaque on top. A small leather pouch
 * at the front end of the arc holds them: click it and the hexes tumble out onto the arc, click again and they slide
 * back in. The pouch shows your bolts as a carved number.
 * There are no MEGA and MINI rows: where a hex lands decides. Dragged over the floating GLOBE it is a MEGA (a bolt,
 * level 1, its area ringed on the globe); slid onto the table BOARD it is a MINI (a spark, that board's level).
 * The hex itself is the piece: it lifts and follows the cursor, and flies back to its place after the drop.
 * Input comes from SFNPowerRingInput (a transparent layer in the HUD); the casting is SFNHudLive's.
 */
UCLASS()
class FINALNIGHT_API AFNPowerRing : public AActor
{
	GENERATED_BODY()

public:
	AFNPowerRing();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The ring for this table (spawned on first use when bCreate). */
	static AFNPowerRing* Get(AFNStudio* Studio, bool bCreate);

	void SetPowers(const TArray<FString>& Slugs, const TArray<FText>& Names);
	void SetPurse(int32 InBolts, int32 InSparks);
	void SetShown(bool bShow);

	/** Hit tests in viewport pixels. */
	int32 HexAtPixel(const FVector2D& Pixel) const;   // -1 = none
	bool PouchAtPixel(const FVector2D& Pixel) const;
	bool HexPixel(int32 Index, FVector2D& OutPixel) const;
	bool PouchPixel(FVector2D& OutPixel) const;

	int32 Num() const { return Hexes.Num(); }
	FString SlugOf(int32 Index) const { return Hexes.IsValidIndex(Index) ? Hexes[Index].Slug : FString(); }
	FText NameOf(int32 Index) const { return Hexes.IsValidIndex(Index) ? Hexes[Index].Name : FText::GetEmpty(); }
	bool IsEnabled() const { return Bolts > 0 || Sparks != 0; }
	bool IsShown() const { return bShown; }
	bool IsOpen() const { return bOpen; }

	void SetHover(int32 Index) { Hover = Index; }
	/** Open: the pouch puffs and the hexes burst out onto their places; closed: they fly back into its mouth. */
	void TogglePouch();
	void SetPouchHover(bool bHover) { bPouchHover = bHover; }
	void BeginLift(int32 Index);
	/** Where the lifted hex should hover (a world point), or follow nothing (stay where it is). */
	/** The lifted hex follows the cursor to WorldPoint, at Scale of its ring size (smaller over the board, so the
	 *  board's highlight under it stays visible). */
	void MoveLifted(const FVector& WorldPoint, float Scale = 1.f);
	void EndLift();
	float LiftScale = 1.f;
	int32 LiftedIndex() const { return Lifted; }

private:
	struct FHex
	{
		FString Slug;
		FText Name;
		USceneComponent* Root = nullptr;
		TArray<UStaticMeshComponent*> Wood;
		UStaticMeshComponent* Plaque = nullptr;
		FVector Pos = FVector::ZeroVector;     // current, world
		FQuat Rot = FQuat::Identity;
		float Out = 0.f;                       // 0 in the pouch .. 1 on its place
		float Wait = 0.f;                      // stagger before it moves after the pouch is clicked
		float Lift = 0.f;
		float Spin = 0.f;
		FVector SpinAxis = FVector::UpVector;
	};
	TArray<FHex> Hexes;
	TWeakObjectPtr<AFNStudio> Studio;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Parts;     // keeps the runtime components alive
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Icons;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> ImageMat;
	USceneComponent* Pouch = nullptr;
	USceneComponent* PouchBody = nullptr;       // squashes and stretches
	UTextRenderComponent* PouchCount = nullptr;
	TArray<UStaticMeshComponent*> PouchParts;
	TArray<UStaticMeshComponent*> Motes;        // twinkling around the pouch
	TArray<UStaticMeshComponent*> Spray;        // the burst's spray
	TArray<FVector> SprayVel;
	class UPointLightComponent* PouchGlow = nullptr;
	float BurstT = -1.f;                        // seconds since the last click (-1 = none)
	float PouchFull = 1.f;                      // 1 = holding the hexes (plump, shiny) .. 0 = empty (sagging, dull)
	float AppliedFull = -1.f;
	TArray<FLinearColor> PouchBase;             // each pouch part's full colour
	bool bPouchHover = false;

	bool bShown = true;
	bool bOpen = false;                         // the hexes start in the pouch
	int32 Hover = -1;
	int32 Lifted = -1;
	FVector LiftTarget = FVector::ZeroVector;
	bool bLiftHasTarget = false;
	int32 Bolts = 0;
	int32 Sparks = -1;
	FString PowersSig;
	float Time = 0.f;
	bool bMegaInHand = false;     // FFNPowerDragDrop::OnMegaArmed: the fault lines show only then
	float FaultAlpha = -1.f;      // -1 = not applied yet (the studio draws them visible)
	FDelegateHandle MegaHandle;

	void BuildPouch();
	void ClearHexes();
	bool Rim(FVector& Center, double& Radius, FRotator& TableRot) const;
	FVector SlotWorld(int32 Index, int32 Count, FQuat& OutRot) const;
	FVector PouchWorld() const;
	float WorldHexR() const;
	UStaticMeshComponent* Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color);
	UTexture2D* IconTexture(const FString& Slug);
	static void SetColor(UStaticMeshComponent* C, const FLinearColor& Color);
};

DECLARE_DELEGATE_OneParam(FOnFNRingHexClicked, const FString& /*Slug*/);

/** Dragging one of YOUR demigods (its diorama box or its board piece) to summon it somewhere. */
class FINALNIGHT_API FFNSummonDragDrop : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFNSummonDragDrop, FDragDropOperation)
	FString Ref;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	static TSharedRef<FFNSummonDragDrop> New(const FString& InRef, const FVector2D& InScreen);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return nullptr; }
	virtual void OnDragged(const FDragDropEvent& DragDropEvent) override;
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSummonAt, const FString& /*Ref*/, const FVector2D& /*ScreenPosition*/);
	static FOnSummonAt& OnOver();
	static FOnSummonAt& OnDropped();
};

/**
 * A transparent, full-viewport input layer under the HUD panels: a press on a ring hex starts the power drag (the hex
 * lifts), a click arms it, a click on the pouch opens or closes it; anything else falls through to the game. Over a
 * hex it shows the power's name and how to cast it.
 */
class FINALNIGHT_API SFNPowerRingInput : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPowerRingInput) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AFNStudio>, Studio)
		SLATE_EVENT(FOnFNRingHexClicked, OnHexClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetStudio(AFNStudio* InStudio) { Studio = InStudio; }
	/** Which demigod refs are yours (only those can be picked up and summoned). */
	void SetIsMine(TFunction<bool(const FString&)> InIsMine) { IsMine = MoveTemp(InIsMine); }
	/** Why a demigod can't be summoned ("" = it can): a press on it then shows the reason instead of dragging. */
	void SetSummonCheck(TFunction<FString(const FString&)> InWhyNot, TFunction<void(const FString&)> InToast) { WhyNot = MoveTemp(InWhyNot); Toast = MoveTemp(InToast); }
	/** A double-click on any demigod's box or board piece (yours: zoom to it; a rival's: zoom, later challenge). */
	void SetOnFigureDoubleClick(TFunction<void(const FString&)> InDouble) { OnFigureDouble = MoveTemp(InDouble); }
	/** The wheel over/near a demigod: zoom centred on it (return false to let the studio's own wheel zoom happen). */
	void SetOnFigureWheel(TFunction<bool(const FString&, float)> InWheel) { OnFigureWheel = MoveTemp(InWheel); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<AFNStudio> Studio;
	FOnFNRingHexClicked OnHexClicked;
	int32 Pressed = -1;
	int32 HoverHex = -1;
	bool bHoverPouch = false;
	FString PressedRef;          // a demigod pressed (dragging it summons it when it's yours)
	FString PressedWhy;          // why it can't be summoned ("" = it can)
	bool bHoverMine = false;
	bool bHoverFigure = false;
	TFunction<void(const FString&)> OnFigureDouble;
	TFunction<bool(const FString&, float)> OnFigureWheel;
	TFunction<bool(const FString&)> IsMine;
	TFunction<FString(const FString&)> WhyNot;
	TFunction<void(const FString&)> Toast;
	FReply PressFigure(const FVector2D& Px);
	AFNPowerRing* Ring() const;
	bool ToPixel(const FGeometry& G, const FVector2D& Screen, FVector2D& OutPixel) const;
};
