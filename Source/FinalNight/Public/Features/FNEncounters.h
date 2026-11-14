#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNEncounters.generated.h"

class AFNStudio;
class FJsonObject;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;

/**
 * Benito's features 4 + 5 on the table map (rules 0.8.0):
 *  - MAP ENCOUNTERS: each marked hex (game.encounters; encounter.marked) is a violet disc with a floating "?".
 *    When a demigod steps on it (encounter.revealed) it flares in its type's colour and shows the type and the
 *    outcome for a while, then sinks away; encounter.faded dissolves it unfound.
 *  - LOOT LEFT BEHIND: each pile (loot.dropped; game.region/node loot[]) is a small spinning gold token with its
 *    items' icons written under it; loot.taken lifts it up and away.
 * Plain engine shapes, no lights, no shadows. Only what falls on the board's current view is shown.
 */
UCLASS()
class FINALNIGHT_API AFNEncounters : public AActor
{
	GENERATED_BODY()
public:
	AFNEncounters();
	void Bind(AFNStudio* InStudio);
	/** A game.encounters reply (the full list: unknown ids are dropped), or a game.region / game.node reply (encounters[], loot[]). */
	void Apply(const TSharedPtr<FJsonObject>& Payload);
	/** A dm_events[] entry: encounter.marked / revealed / faded, loot.dropped / taken. */
	void ApplyEvent(const TSharedPtr<FJsonObject>& Event);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FMark
	{
		double Lon = 0.0, Lat = 0.0;
		bool bLoot = false, bRevealed = false;
		FString Type, Label;
		float Age = 0.f, Leave = -1.f;    // Leave >= 0: going away, seconds into it
		float ShowFor = 0.f;              // a revealed one lingers this long, then leaves
		TObjectPtr<UStaticMeshComponent> Base = nullptr;
		TObjectPtr<UTextRenderComponent> Text = nullptr;
	};
	TMap<FString, FMark> Marks;          // "e:<id>" for encounters, "l:<pile>" for loot
	TSet<FString> Finished;              // shown and gone (revealed ones stay in the server list forever): never re-added
	TWeakObjectPtr<AFNStudio> Studio;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;

	FMark& Upsert(const FString& Key, bool bLoot, const FString& Cell, const TSharedPtr<FJsonObject>& Src);
	void Leave(const FString& Key);
	void Reveal(FMark& M, const FString& Type, const FString& Outcome);
	void Paint(FMark& M, const FLinearColor& Color);
};
