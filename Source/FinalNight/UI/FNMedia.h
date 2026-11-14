#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Server media (portraits, dioramas) as textures at full fidelity: the PNG decoded at its own resolution with a full
 * mip chain (trilinear, never downscaled, so a 1024 px diorama stays sharp up close and calm far away), plus a
 * desaturated variant for the fallen. Textures are transient and owned by the caller (hold them in a
 * TStrongObjectPtr or a UPROPERTY). The disk cache keeps /media files in Saved/FN7Media/<hash>.png.
 */
namespace FN7Media
{
	FINALNIGHT_API UTexture2D* TextureFromPng(const TArray<uint8>& Png, bool bGrey = false);
	FINALNIGHT_API bool LoadCached(const FString& Hash, TArray<uint8>& OutPng);
	FINALNIGHT_API void SaveCached(const FString& Hash, const TArray<uint8>& Png);
}
