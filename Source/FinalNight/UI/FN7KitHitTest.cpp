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

	static constexpr double NearMissScale = 1.35;

	bool IsOverAnyFigure(UWorld* World, const FVector2D& ViewportPixel)
	{
		return World && !AFNTableGods::FigureAtScreen(World, ViewportPixel, nullptr, NearMissScale).IsEmpty();
	}

	FString WhyBlocked(UWorld* World, const FVector2D& ViewportPixel)
	{
		if (!World) return FString();
		double D = 0.0;
		const FString Fig = AFNTableGods::FigureAtScreen(World, ViewportPixel, nullptr, NearMissScale, &D);
		if (!Fig.IsEmpty()) return FString::Printf(TEXT("figure %s at %.0f px"), *Fig, D);
		if (const AFNDuelBox* Box = AFNDuelBox::Get(World, false))
			if (Box->IsShowing()) return TEXT("duel box up");
		if (AFNStudio* St = StudioOf(World))
			if (const AFNPowerRing* Ring = AFNPowerRing::Get(St, false))
				if (Ring->IsShown())
				{
					const int32 Hex = Ring->HexAtPixel(ViewportPixel);
					if (Hex >= 0) return FString::Printf(TEXT("power hex %d"), Hex);
					if (Ring->PouchAtPixel(ViewportPixel)) return TEXT("pouch");
				}
		FVector2D Abs;
		if (PixelToAbs(ViewportPixel, Abs))
		{
			const FString Panel = SFNPanelMover::PanelAt(Abs);
			if (!Panel.IsEmpty()) return FString::Printf(TEXT("panel %s"), *Panel);
		}
		return FString();
	}

	bool IsOverKitUI(UWorld* World, const FVector2D& ViewportPixel)
	{
		const FString Why = WhyBlocked(World, ViewportPixel);
		return !Why.IsEmpty() && !Why.StartsWith(TEXT("figure "));
	}

	bool BlocksBoardGesture(UWorld* World, const FVector2D& ViewportPixel)
	{
		return !WhyBlocked(World, ViewportPixel).IsEmpty();
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
