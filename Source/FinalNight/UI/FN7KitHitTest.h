#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The kit's hit tests for board gestures (Chris's rule: drag = grab-rotate the globe, double-click EMPTY map = one level
 * in, right-click = one level out, a single click never moves the view, and NONE of it inside a demigod's extended
 * click box or over the kit's UI). Viewport pixels, as APlayerController::GetMousePosition gives them.
 */
namespace FN7Kit
{
	/** Within ReachPx of any demigod's diorama box (by its god's pedestal) or board piece: that demigod's own clicks win. */
	FINALNIGHT_API bool IsOverAnyFigure(UWorld* World, const FVector2D& ViewportPixel, double ReachPx = 90.0);
	/** Over the kit's own UI: a power hex or the pouch, the open duel box (any click while it is up), or a HUD panel. */
	FINALNIGHT_API bool IsOverKitUI(UWorld* World, const FVector2D& ViewportPixel);
	/** Either of the two: no board gesture may start here. */
	FINALNIGHT_API bool BlocksBoardGesture(UWorld* World, const FVector2D& ViewportPixel);

	/** The one place an empty-board double-click goes. The kit's input layer sees every double-click first: on a figure
	 *  it runs the demigod's action and stops; on empty board (and not over kit UI) it calls this; so the two can never
	 *  both fire. Unset: nothing happens (a double-click never moves the view by itself). */
	FINALNIGHT_API void SetBoardDoubleClick(TFunction<void(const FVector2D& /*ViewportPixel*/)> Handler);
	FINALNIGHT_API bool RunBoardDoubleClick(const FVector2D& ViewportPixel);
}
