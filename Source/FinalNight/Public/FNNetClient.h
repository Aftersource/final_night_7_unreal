#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Dom/JsonObject.h"
#include "Engine/TimerHandle.h"
#include "FNNetClient.generated.h"

class IWebSocket;

/**
 * Client for The Theatre (Aftersource/theatre, PROTOCOL.md) running the Final Night plugin
 * (Aftersource/theatre_plugin_finalnight).
 * Envelope both ways: { "t": "family.name", "id": "c-1", "re": "...", "d": { ... } }; d is always an object.
 *
 * bDemoMode runs a local stand-in that speaks the same protocol and sends Final Night's payload
 * shapes (brief / outcome / private / choices / summary), so the client can be shown without a server.
 * With bClaudeMode and Config/ClaudeKey.txt, the stand-in writes each day with Claude.
 */
UCLASS(config = Game)
class FINALNIGHT_API UFNNetClient : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnServerMessage, const FString& /*t*/, TSharedPtr<FJsonObject> /*d*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnStatus, const FString&);
	FOnServerMessage OnServerMessage;
	FOnStatus OnStatus;

	UPROPERTY(config) FString ServerUrl = TEXT("ws://localhost:8080/ws");
	UPROPERTY(config) FString DisplayName = TEXT("Benito");
	UPROPERTY(config) bool bDemoMode = true;
	UPROPERTY(config) bool bAutoTest = false;
	/** Local testing only. Never ship with this on: Final Night 7's AI runs on the server. */
	UPROPERTY(config) bool bClaudeMode = false;
	/** Final Night 7 access key (appended as ?key=). Empty = read Config/FN7Key.txt, then the FN7_KEY env var. */
	UPROPERTY(config) FString AccessKey;
	/** Final Night 7 room settings (room.create): demigods in the match and the flow beat length. */
	UPROPERTY(config) int32 MatchPlayers = 4;
	UPROPERTY(config) int32 MatchFlowMs = 6000;
	UPROPERTY(config) FString ClaudeModel = TEXT("claude-haiku-5-5");
	/** Settings sent with room.create (the plugin's defaults are 100 days / 15 AI contestants). */
	UPROPERTY(config) int32 MatchRounds = 5;
	UPROPERTY(config) int32 MatchNpcs = 11;
	/** Chance each day (from day 2) that a rival comes after you and you must defend (practice Theatre). */
	UPROPERTY(config) float AmbushChance = 0.35f;

	/** The hero you picked at the table (Warrior, Rogue, Mage). The dice favour plans that suit it. */
	FString HeroClass;

	void Start();
	void Stop();
	bool IsDemo() const { return bDemoMode; }
	/** Server clock (epoch ms), corrected by sys.pong. */
	double NowServerMs() const;

	// requests
	void QuickJoin();
	/** Join someone else's room by its 6-letter code (room.join). */
	void JoinRoom(const FString& Code);
	/** ONE GAME: join the server's always-on house room (room.join {house:true}). The server seats us (ready) and never lets clients start it. */
	void JoinHouse();
	/** Viewers (no seat): true when room.snapshot.you.viewer says we only watch. */
	bool bViewer = false;
	/** A viewer asks to become a player (room.take_seat {}); a refusal keeps us a viewer. */
	void TakeSeat();
	/**
	 * A room code to JOIN instead of creating our own match: the first line of Config/FN7JoinCode.txt
	 * (read at click time, so no rebuild or restart). Empty = create and start our own match.
	 */
	FString ResolveJoinCode() const;
	/** Full https URL (with the access key) for a server media path like "/media/<hash>.png". */
	FString MediaUrl(const FString& Path) const;
	void CreateRoom();
	void SetReady(bool bReady);
	void StartMatch();
	void SubmitAction(int32 Round, const FString& Text, const FString& DropSpotId = FString());
	void SubmitChoice(int32 Round, const FString& ChoiceId);
	/** Your move in a duel (attack follow-up, or your defense / strike back). */
	void SubmitDuel(const FString& DuelId, const FString& Text);

	// ---- Final Night 7 (docs/CLIENT_API.md in Aftersource/final_night_7) ----
	/** True once server.hello says game "final_night_7". */
	bool bFn7 = false;
	/** The open slow window (window.opened), or -1. */
	int32 OpenWindowId = -1;
	/** "I started typing": opens a public slow window (costs a bolt + Time's debt). Once per window. */
	void BeginTyping();
	/** Forget the typing state without sending anything (e.g. you only cast, or closed the box). */
	void EndTyping();
	/** Enter: orders to your two hands, into the window you typed in (or as a Compose action). */
	void SubmitOrders(int32 Round, const FString& Left, const FString& Right);
	/** A god power (instant, any phase). Target: a ref ("dg:2"), a name, or "lon,lat". */
	void CastPower(int32 Round, const FString& Power, const FString& Target);
	/** A god power at a point on the globe, [lon, lat]. */
	void CastAt(int32 Round, const FString& Power, double Lon, double Lat);

	void HandleIncomingFrame(const FString& Frame);

private:
	void Send(const FString& T, TSharedPtr<FJsonObject> D = nullptr);
	void Connect();
	void ScheduleReconnect();
	void Ping();
	UWorld* GetTimerWorld() const;

	TSharedPtr<IWebSocket> Socket;
	FString ResumeToken;
	FString ResolveAccessKey() const;
	bool bBegun = false;
	int32 TypingWindowId = -1;
	int32 NextId = 1;
	double ClockOffsetMs = 0.0;
	bool bConnected = false;
	bool bStopping = false;
	FTimerHandle ReconnectTimer, PingTimer;

	// ---- demo stand-in ------------------------------------------------------
	struct FDemoSeat { int32 Seat; FString Name; bool bBot; double Lon, Lat; int32 Health; int32 Kills; bool bAlive; TArray<TPair<FString, FString>> Bag; };
	void DemoRequest(const FString& T, TSharedPtr<FJsonObject> D, const FString& Id);
	void DemoPush(float Delay, const FString& T, TSharedPtr<FJsonObject> D, const FString& Re = FString());
	void DemoStartMatch();
	void DemoBeginDay();
	void DemoResolve();
	void DemoReveal(const FString& Headline, const FString& Outcome, const FString& Experience, const TArray<TPair<FString, FString>>& Found, const FString& EpVictim, int32 EpDamage, const FString& EpDesc);
	void DemoCommit();
	void DemoEndMatch();
	bool DemoClaudeDay();
	TSharedPtr<FJsonObject> DemoRoom(const FString& State) const;
	TSharedPtr<FJsonObject> DemoSelfView() const;
	TSharedPtr<FJsonObject> DemoZone() const;
	FString LoadClaudeKey() const;

	// ---- knockouts ----
	void DemoOpenCompose(float Delay);
	void DemoFallbackDay();
	void DemoEliminatePlayer(const FString& Cause);
	void DemoSpectateDay();
	int32 DemoAliveBots() const;
	void DemoLater(float Delay, TFunction<void()> Fn);
	bool bDemoPlayerOut = false;

	// ---- duels: one player tries to hurt another, both sides write, the DM sets the odds, the dice decide ----
	struct FDemoDuel
	{
		bool bActive = false;
		bool bPlayerAttacks = true;
		int32 Attacker = 0, Defender = 0;
		FString Id;
		TArray<TPair<FString, FString>> Lines;   // who wrote what, in order
		double OneIn = 4.0;                       // attacker's chance = 1 in OneIn (no ceiling)
		FString Note;
		int32 Damage = 4;
		bool bCounter = false;
		double CounterOneIn = 8.0;
		int32 CounterDamage = 3;
		FString IfHit, IfMiss;
		bool bAwaitingPlayer = false;
	};
	FDemoDuel Duel;
	FString DemoDuelSummary;   // today's settled duel, handed to the Dungeon Master
	int32 DemoFindTarget(const FString& Will) const;
	void DemoStartPlayerDuel(int32 TargetSeat);
	void DemoStartAmbush(int32 BotSeat);
	void DemoDuelJudge(bool bWriteReply, TFunction<void()> Then);
	void DemoDuelHeuristic(bool bWriteReply);
	void DemoDuelTurn();
	void DemoDuelReply(const FString& Text);
	void DemoSettleDuel();
	void CallClaude(const FString& Prompt, int32 MaxTokens, TFunction<void(bool bOk, const FString& Text, const FString& Raw)> Done);

	TArray<FDemoSeat> DemoSeats;
	int32 DemoDay = 0;
	bool bDemoDropped = false;
	bool bDemoInRoom = false;
	FString DemoWill;
	FString DemoChoice;
	FString DemoEpisodeId;
	FString DemoEpisodeVictim;
	int32 DemoEpisodeDamage = 0;
	TArray<FString> DemoStory;
	int32 DemoRoll = 10, DemoMod = 0;
	FString DemoVerdict, DemoSkill;
	TArray<FTimerHandle> DemoTimers;
	FTimerHandle DemoDeadline;
};
