#include "FN7KitHitTest.h"

#include "FNDuelBox.h"
#include "FNPowerRing.h"
#include "FNStudio.h"
#include "FNTableGods.h"
#include "SFNPanelMover.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

namespace FN7Kit
{
	static TFunction<void(const FVector2D&)> GBoardDoubleClick;

	static AFNStudio* StudioOf(UWorld* World)
	{
		if (!World) return nullptr;
		TActorIterator<AFNStudio> It(World);
		return It ? *It : nullptr;
	}

	static bool PixelToAbs(const FVector2D& Pixel, FVector2D& OutAbs)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> W = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!W.IsValid() || !VC->Viewport) return false;
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Size = FVector2D(G.GetLocalSize());
		const FIntPoint Px = VC->Viewport->GetSizeXY();
		if (Px.X <= 0 || Px.Y <= 0) return false;
		OutAbs = FVector2D(G.LocalToAbsolute(Pixel * FVector2D(Size.X / Px.X, Size.Y / Px.Y)));
		return true;
	}

	bool IsOverAnyFigure(UWorld* World, const FVector2D& ViewportPixel, double ReachPx)
	{
		if (!World) return false;
		return !AFNTableGods::FigureAtScreen(World, ViewportPixel).IsEmpty() || !AFNTableGods::FigureNear(World, ViewportPixel, ReachPx).IsEmpty();
	}

	bool IsOverKitUI(UWorld* World, const FVector2D& ViewportPixel)
	{
		if (!World) return false;
		if (const AFNDuelBox* Box = AFNDuelBox::Get(World, false))
			if (Box->IsShowing()) return true;
		if (AFNStudio* St = StudioOf(World))
			if (const AFNPowerRing* Ring = AFNPowerRing::Get(St, false))
				if (Ring->IsShown() && (Ring->HexAtPixel(ViewportPixel) >= 0 || Ring->PouchAtPixel(ViewportPixel))) return true;
		FVector2D Abs;
		return PixelToAbs(ViewportPixel, Abs) && SFNPanelMover::IsOverAnyPanel(Abs);
	}

	bool BlocksBoardGesture(UWorld* World, const FVector2D& ViewportPixel)
	{
		return IsOverAnyFigure(World, ViewportPixel) || IsOverKitUI(World, ViewportPixel);
	}

	void SetBoardDoubleClick(TFunction<void(const FVector2D&)> Handler)
	{
		GBoardDoubleClick = MoveTemp(Handler);
	}

	bool RunBoardDoubleClick(const FVector2D& ViewportPixel)
	{
		if (!GBoardDoubleClick) return false;
		GBoardDoubleClick(ViewportPixel);
		return true;
	}
}
