#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNDuelStage.generated.h"

class USkeletalMeshComponent;
class UTextRenderComponent;
class UAnimSequence;
class AFNStudio;

/**
 * The duel stage on the table (docs/DUEL_UNREAL.md: Chris's Archon-style typed duel, ported from the browser lab).
 * Step A: the imported mannequin (Content/Duel/Mannequin), its 35 clips and the five skins (Content/Duel/Skins), shown
 * by FN7.DuelDemo as a parade: one mannequin per skin in a row in front of the globe, each playing every clip in turn.
 */
UCLASS()
class FINALNIGHT_API AFNDuelStage : public AActor
{
	GENERATED_BODY()

public:
	AFNDuelStage();
	virtual void Tick(float DeltaSeconds) override;

	/** This world's stage (spawned on demand). */
	static AFNDuelStage* Get(UWorld* World, bool bCreate);
	/** The server's clip vocabulary (assets /Game/Duel/Mannequin/Anims/A_<clip>). */
	static const TArray<FString>& ClipNames();
	/** The skins, in the order the name hash picks them: wood, obsidian, gold, dragonscale, butterfly. */
	static const TArray<FString>& SkinNames();
	/** The skin a demigod wears until the server sends one: FNV-1a 32 of the lower-cased name's UTF-8 bytes, mod 5. */
	static int32 SkinForName(const FString& DemigodName);

	/** Step A proof: five mannequins on the table, one per skin, each cycling every clip. */
	void StartParade(AFNStudio* Studio);
	void StopParade();
	bool IsParading() const { return Meshes.Num() > 0; }

private:
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Meshes;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Labels;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Clips;
	TArray<int32> ClipOf;
	TArray<float> ClipT;
	TArray<FVector> BaseAt;          // where each figure stands (world)
	FVector RefPelvis = FVector::ZeroVector;   // the pelvis in component space in the reference pose
	bool bHaveRefPelvis = false;

	/** The clips carry their travel in the pelvis: undo its horizontal drift (component X/Y) so the figure stays where
	 *  the stage puts it (as duel_lab/lib/fighter.js does); the height stays, so jumps and kneels still read. */
	void StripPelvisTravel(int32 Figure);

	void PlayClip(int32 Figure, int32 Clip);
};
