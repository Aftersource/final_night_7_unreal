#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

/**
 * Six looks for a menu screen, for the team to pick a favourite from (six options to choose between).
 * the-favorite of the login screen settings character selection creating ur god statue etc."), the way the chat has six.
 * Each screen keeps its own choice in Saved/FN7Hud.json ("<screen>_variant") and logs "FN7 <screen>: variant N".
 * The menus are the tablet side of Benito's two styles (the room realistic, menus as objects):
 *   1 ONYX (the black marble tablet)   2 PARCHMENT (on carved wood)   3 CARVED WOOD
 *   4 Dark glass                       5 Gilded slate                 6 Star map
 * The frame follows the look live (no rebuild); text inside takes Ink / Dim / Accent of the look.
 */
namespace FN7Looks
{
	constexpr int32 Count = 6;
	FINALNIGHT_API FText Name(int32 Look);
	/** Read / write a screen's look ("lobby", "login", "settings", "demigods", "god"); Set logs it. */
	FINALNIGHT_API int32 Get(const FString& Screen, int32 Default = 1);
	FINALNIGHT_API void Set(const FString& Screen, int32 Look);
	/** The look's tablet around Inner (the frame changes with Look; Inner is never rebuilt). */
	FINALNIGHT_API TSharedRef<SWidget> Frame(TAttribute<int32> Look, TSharedRef<SWidget> Inner, const FMargin& Pad);
	/** The 1-6 switch (small square buttons, the current one lit; a tooltip names each look). */
	FINALNIGHT_API TSharedRef<SWidget> Switch(TAttribute<int32> Look, TFunction<void(int32)> OnPick);
	/** Text on the look's surface: dark ink on parchment and wood, light ink on the dark ones. */
	FINALNIGHT_API FLinearColor Ink(int32 Look);
	FINALNIGHT_API FLinearColor Dim(int32 Look);
	FINALNIGHT_API FLinearColor Accent(int32 Look);
}
