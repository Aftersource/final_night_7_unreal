#pragma once

#include "CoreMinimal.h"

class USceneComponent;
class UStaticMeshComponent;

/**
 * A small 3D stand-in for an item that has no generated picture yet (Benito wants to see all their image generated items
 * collected"). The server's item `icon` (sword, ranged, armor, banner_army, vehicle, beast, structure, relic, artifact,
 * phenomenon, thing) becomes a little model made of engine basic shapes, tinted, standing on its base at the Parent's
 * origin and about SizeCm tall. No collision, no shadows. Used by AFNInventoryShelf; any feature may call it.
 */
namespace FNItemIcon
{
	/** Builds the model under Parent (attached + registered); returns the parts so the caller can hide/fade them. */
	FINALNIGHT_API TArray<UStaticMeshComponent*> Build(USceneComponent* Parent, const FString& Icon, const FLinearColor& Tint, float SizeCm);
	/** Greys a built model (state destroyed) or restores its tint. */
	FINALNIGHT_API void SetTint(const TArray<UStaticMeshComponent*>& Parts, const FLinearColor& Tint);
}
