#pragma once

#include "CoreMinimal.h"

/**
 * Per-machine HUD preferences, kept in Saved/FN7Hud.json (never committed): where each movable panel was dropped
 * (an offset from its home corner) and the order of the power tiles.
 */
namespace FN7HudPrefs
{
	FINALNIGHT_API FVector2D GetPanelOffset(const FString& Id);
	FINALNIGHT_API void SetPanelOffset(const FString& Id, const FVector2D& Offset);

	FINALNIGHT_API const TArray<FString>& GetPowerOrder();
	FINALNIGHT_API void SetPowerOrder(const TArray<FString>& Order);
	/** A small named setting (e.g. "chat_variant"). */
	FINALNIGHT_API int32 GetInt(const FString& Key, int32 Default);
	FINALNIGHT_API void SetInt(const FString& Key, int32 Value);
}
