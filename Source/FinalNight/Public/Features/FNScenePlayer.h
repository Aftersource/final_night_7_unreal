#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNScenePlayer.generated.h"

class UFNNetClient;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;

/**
 * Token play (Benito wants typed actions acted out with tokens): the server's round.outcome public.scenes[] (Chris, schema
 * 18) for YOUR demigods play on the board at people level: the camera drops to the scene's place, game tokens walk the
 * steps' cells, the house's roof lifts when one enters, captions float over whoever talks, the d20 tumbles at its
 * moment (AFNStudio::RollDice) and the outcome is written over the place; then the view goes back exactly where you were.
 * HAND_OF_GOD §6: your own scenes only, never while you hold, pan, zoom or type (it waits), and if you move the view
 * during the scene it stops and leaves the view yours.
 */
UCLASS()
class FINALNIGHT_API AFNScenePlayer : public AActor
{
	GENERATED_BODY()
public:
	AFNScenePlayer();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	/** A scene of yours is playing or waiting to (the event glide stands aside). */
	bool IsActive() const { return bPlaying || Queue.Num() > 0; }

private:
	struct FStep { float T = 0, Dur = 0; FString Actor, Verb, ToCell, Target, Caption, Mood; TArray<FString> Path; };
	struct FActorTok { FString Id, Name, Kind, Cell; UStaticMeshComponent* Body = nullptr; UStaticMeshComponent* Head = nullptr; UTextRenderComponent* Text = nullptr; FVector Pos = FVector::ZeroVector; bool bInside = false, bFallen = false; float Kneel = 0.f; };
	struct FScene
	{
		FString Id, PlaceCell, PlaceName, RoofCell, OutcomeText, Result, Verdict;
		TArray<FActorTok> Actors;
		TArray<FStep> Steps;
		int32 Roll = 0, Mod = 0;
		float DiceAt = -1.f, TotalS = 9.f;
	};
	TArray<FScene> Queue;
	FScene Now;
	bool bPlaying = false, bDiceDone = false;
	float T = 0.f;
	double SavedLat = 0, SavedLon = 0, PlayLat = 0, PlayLon = 0;
	bool bSavedPicked = false;
	int32 SavedLevel = 3;
	TArray<FString> MineKeys;
	TSet<FString> Seen;   // scene ids already played
	int32 MySeat = -1;
	UStaticMeshComponent* Roof = nullptr;
	UTextRenderComponent* Banner = nullptr;
	FString LastWhy;
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Colors;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	bool Busy();
	bool CellPos(const FString& Cell, FVector& Out) const;
	UStaticMeshComponent* Piece(const TCHAR* Mesh, const FColor& C);
	UTextRenderComponent* Label(const FColor& C);
	void Start();
	void Finish(bool bReturn);
	void Clear();
};

UCLASS()
class FINALNIGHT_API UFNScenePlayerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
