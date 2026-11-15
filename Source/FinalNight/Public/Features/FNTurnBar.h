#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNTurnBar.generated.h"

class UFNNetClient;
class SWidget;

/**
 * The turn, made visible (Benito said he does not understand the turn system, including when the player is supposed to spend will). One quiet strip
 * at the top centre of the screen:
 *   BEAT 53 / 60      YOUR MOVE  0:09   + a bar that runs out with the beat
 *   the one thing that matters to you this beat (worked out here from the state: fallen, outside Time's circle, weak,
 *   a rival close by, else safe), in gold
 *   the goal: last one standing wins, how many of yours stand, how many demigods are left
 * Reads the server messages itself; no other file changes. The server's own per-seat hint replaces line 2 later.
 */
UCLASS()
class FINALNIGHT_API AFNTurnBar : public AActor
{
	GENERATED_BODY()
public:
	AFNTurnBar();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	FText LineBeat() const;
	FText LineHint() const;
	FText LineGoal() const;
	float BeatLeft() const;   // 1 at the beat's start, 0 at its end
	bool IsShown() const { return Beat > 0; }

private:
	struct FDg { FString Ref, Name, Side; int32 Health = 10, MaxHealth = 10, Seat = -1; bool bAlive = true, bMine = false, bHasPos = false; double Lon = 0, Lat = 0; };
	TMap<FString, FDg> Dgs;
	int32 Beat = 0, BeatsTotal = 0, AliveAll = 0;
	double ArenaLon = 0, ArenaLat = 0, ArenaKm = 0;
	double DeadlineMs = 0, BeatStartMs = 0, BeatLenMs = 15000;   // the beat clock (server ms)
	double LastBeatAtMs = 0;
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	TSharedPtr<SWidget> Widget;
	FString LastHint;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	void ReadPublic(const TSharedPtr<class FJsonObject>& P);
	void ReadMine(const TSharedPtr<class FJsonObject>& View);
	double NowMs() const;
};

UCLASS()
class FINALNIGHT_API UFNTurnBarSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
