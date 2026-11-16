#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UFNNetClient;

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

	/**
	 * Every picture by its FULL media hash, verified (a wrong picture must be impossible: a burger nobody generated was
	 * shown): the server's sidecar /media/<hash>.json gives the PNG's byte length; a cached or downloaded PNG is used only
	 * if it came from /media/<that exact hash>.png, is cached under that full hash, and has exactly that many bytes.
	 * A mismatch deletes the file, refetches it, and logs "FN7 media: hash mismatch ... (N so far)". Nothing ever falls
	 * back to another picture: if nothing valid arrives, OnPng is never called and the frame stays plain.
	 */
	FINALNIGHT_API void Fetch(UFNNetClient* Net, const FString& Hash, TFunction<void(const TArray<uint8>&)> OnPng, TFunction<void()> OnFail = nullptr);
	/** "/media/<hash>.png": the only place a picture is fetched from. */
	FINALNIGHT_API FString PathFor(const FString& Hash);
	/** The client to check sidecars with (LoadCached/SaveCached callers that don't use Fetch get checked through it). */
	FINALNIGHT_API void SetNet(UFNNetClient* Net);
	FINALNIGHT_API int32 Mismatches();
}
