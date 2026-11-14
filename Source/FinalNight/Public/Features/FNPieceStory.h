#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNPieceStory.generated.h"

class AFNStudio;
class FJsonObject;

/**
 * Benito's feature 2: your piece carries its story (rules 0.8.0 `demigods[].story` {followers, items[], wardrobe{stage,
 * name, condition}, scars[], deeds}). It doesn't draw a second figure: it dresses the style kit's own AFNDemigodFigure
 * through IFNStoryDecorable::ApplyStory (AFNTableGods::FindFigure(ref)), translating the server's story into what the
 * figure shows: follower figures on a log scale (10 -> 1 .. 100,000+ -> 10), held items, scars, and the wardrobe stage
 * (0 plain .. 4 mythic) as `wardrobe_stage`. Only changed stories are re-applied; figures that aren't up yet get it later.
 */
UCLASS()
class FINALNIGHT_API AFNPieceStory : public AActor
{
	GENERATED_BODY()
public:
	AFNPieceStory();
	void Bind(AFNStudio* InStudio);
	/** Anything carrying demigods[] with story: game.globe, game.region, public_state, round.outcome.public. */
	void Apply(const TSharedPtr<FJsonObject>& Payload);
	virtual void Tick(float DeltaSeconds) override;

private:
	TWeakObjectPtr<AFNStudio> Studio;
	TMap<FString, TSharedPtr<FJsonObject>> Pending;   // ref -> the translated story, until its figure takes it
	TMap<FString, FString> Applied;                   // ref -> the raw story JSON last applied
	float SinceTry = 0.f;
	void TryApply();
};
