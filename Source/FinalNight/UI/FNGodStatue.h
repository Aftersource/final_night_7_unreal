#pragma once

#include "CoreMinimal.h"

class AFNGodPedestal;
class UStaticMesh;

/**
 * A player's own god statue on their seat's pedestal (ACCOUNTS.md §14; CLIENT_API "The god at a seat"): asks
 * GET /api/assets/figures?name=<player>&subject_kind=god; an ACCOUNT god shows its accepted picture standing on the
 * pedestal at once, then, when its statue GLB is ready and light enough (<= 80k triangles; Meshy now remeshes to ~30k),
 * the statue itself, built at runtime from the GLB (positions, normals, indices) in the chosen stone's colour.
 * Heavier statues keep the picture (the backend's rule: never thin a mesh, it leaves holes). GLBs and pictures are
 * cached by sha256 under Saved/FN7Statues/. House gods (source "house") are the packaged statues already on the table.
 */
namespace FN7GodStatue
{
	FINALNIGHT_API void Ask(AFNGodPedestal* Pedestal, const FString& Player);
	/** A GLB's first mesh as a runtime static mesh (glTF Y-up metres -> Unreal Z-up), or nullptr with the reason. */
	FINALNIGHT_API UStaticMesh* MeshFromGlb(const TArray<uint8>& Glb, UObject* Outer, FString& OutWhy, int32& OutTris);
	FINALNIGHT_API FLinearColor StoneColor(const FString& Stone);
}
