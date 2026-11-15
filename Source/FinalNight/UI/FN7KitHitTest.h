#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The kit's hit tests for board gestures (Chris's rule: drag = grab-rotate the globe, double-click EMPTY map = one level
 * in, right-click = one level out, a single click never moves the view, and NONE of it inside a demigod's extended
 * click box or over the kit's UI). Viewport pixels, as APlayerController::GetMousePosition gives them.
 */
class AFNStudio;

namespace FN7Kit
{
	/** A near miss on a demigod's diorama box or board piece: within 1.35x its own click zone (that zone is already 1.7x
	 *  the card, at least 22 px), so its clicks win. Not a fixed pixel band: centring puts a demigod at the middle of the
	 *  board, and a flat 90 px band there blocked every zoom at the centre (Benito's log, 2026-11-15). */
	FINALNIGHT_API bool IsOverAnyFigure(UWorld* World, const FVector2D& ViewportPixel);
	/** Over the kit's own UI: a power hex or the pouch, the open duel box (any click while it is up), or a HUD panel. */
	FINALNIGHT_API bool IsOverKitUI(UWorld* World, const FVector2D& ViewportPixel);
	/** The player just moved the view on purpose (a demigod double-click's centre, a zoom): the automatic glides keep
	 *  their hands off for a few seconds (FNEventGlide). */
	/** The table's map is moving this frame (the hand's slide, a zoom, a centring glide, or just re-centred): every
	 *  layer on it (towns, armies, people, earth effects...) redraws EVERY frame while this is true, so nothing trails
	 *  the land (Chris, 2026-11-15: the towns and similar are not moving with the primary tabletop map). */
	FINALNIGHT_API bool BoardMoving(AFNStudio* St);
	FINALNIGHT_API bool BoardMovingAny(UWorld* World);
	FINALNIGHT_API void NoteUserView();
	FINALNIGHT_API double SinceUserView();
	/** Either of the two: no board gesture may start here. */
	FINALNIGHT_API bool BlocksBoardGesture(UWorld* World, const FVector2D& ViewportPixel);
	/** Which rule blocks a board gesture here, for the log ("" = none): "figure dg:3 at 31 px", "duel box up",
	 *  "power hex 2", "pouch", "panel news". */
	FINALNIGHT_API FString WhyBlocked(UWorld* World, const FVector2D& ViewportPixel);
	/** Whose demigod is whose (set by the HUD): only a rival's demigod blocks a board gesture (Chris noted that a double click if
	 *  you're not near an enemy's demigod ... to zoom in"); near your own, a double-click zooms. */
	FINALNIGHT_API void SetRivalTest(TFunction<bool(const FString&)> IsRival);
	FINALNIGHT_API bool IsRival(const FString& Ref);

	/** The one place an empty-board double-click goes. The kit's input layer sees every double-click first: on a figure
	 *  it runs the demigod's action and stops; on empty board (and not over kit UI) it calls this; so the two can never
	 *  both fire. Unset: nothing happens (a double-click never moves the view by itself). */
	FINALNIGHT_API void SetBoardDoubleClick(TFunction<void(const FVector2D& /*ViewportPixel*/)> Handler);
	FINALNIGHT_API bool RunBoardDoubleClick(const FVector2D& ViewportPixel);
}
