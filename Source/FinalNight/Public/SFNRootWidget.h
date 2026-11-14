#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Containers/Ticker.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "FNTypes.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class UFNNetClient;
class AFNStudio;
class SVerticalBox;
class SHorizontalBox;
class FJsonValue;
struct FFNSeatInfo;
class SMultiLineEditableTextBox;
class FJsonObject;

/**
 * Final Night front end on The Theatre: the 2D layer over the tabletop plus the client flow.
 * Tutorial -(easel)-> Connecting -> Connected -(Globe)-> InRoom -(WILL panel = Ready)-> Countdown
 * -> each day: Brief -> Compose (WILL panel, timer) -> Resolving -> Reveal -> Choose (ribbons) -> Commit
 * -> Ended (Final Cut on the tablet).
 */
class FINALNIGHT_API SFNRootWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNRootWidget) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
		SLATE_ARGUMENT(AFNStudio*, Studio)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFNRootWidget() override;

	enum class EState : uint8 { Tutorial, Connecting, Connected, InRoom, Countdown, Waiting, Compose, Writing, Submitted, Resolving, Choose, Chosen, Spectating, Ended, DuelWriting, DuelWaiting };

	void OnWorldClick(FName Id);
	void SetHovered(FName Id) { HoveredId = Id; }
	bool IsTyping() const { return State == EState::Writing || State == EState::DuelWriting; }

private:
	void OnServerMessage(const FString& T, TSharedPtr<FJsonObject> D);
	void OnStatus(const FString& S) { Status = S; }
	void SetState(EState S);
	void ApplySelfView(const TSharedPtr<FJsonObject>& You);
	void ApplyZone(const TSharedPtr<FJsonObject>& Zone);
	/** Final Night 7 payloads (CLIENT_API.md). Returns true when the message was fully handled. */
	bool HandleFn7(const FString& T, TSharedPtr<FJsonObject> D);
	// --- the visible d20 (rules 0.8.0: round.outcome.public.dice[]): a few of the beat's rolls tumble on the table, one after another ---
	void ShowDieRoll(const TSharedPtr<FJsonObject>& D);
	void QueueOutcomeDice(const TSharedPtr<FJsonObject>& Pub);
	bool TickDiceQueue(float Dt);
	TArray<TSharedPtr<FJsonObject>> DiceQueue;
	float DiceWait = 0.f;
	FTSTicker::FDelegateHandle DiceTicker;
	// --- Benito's feature actors (Source/FinalNight/Private/Features/), spawned on first use, fed from the FN7 messages ---
	TWeakObjectPtr<class AFNDayNight> DayNightActor;
	class AFNDayNight* DayNight();
	TWeakObjectPtr<class AFNEncounters> EncountersActor;
	class AFNEncounters* Encounters();
	TWeakObjectPtr<class AFNSettlements> SettlementsActor;
	class AFNSettlements* Settlements();
	TWeakObjectPtr<class AFNPieceStory> PieceStoryActor;
	class AFNPieceStory* PieceStory();
	/** A Final Night 7 SeatView: health, position, alive. */
	void ApplyFn7View(const TSharedPtr<FJsonObject>& View);
	/** The Final Night 7 demigods from a PublicView, as pieces on the globe. */
	void ApplyFn7Public(const TSharedPtr<FJsonObject>& PublicView);
	FString Fn7Demigod;   // my ref, e.g. "dg:0"
	FString Fn7Brief;     // the latest brief text, shown on the easel with the narration
	bool bJoinedOther = false;
	int32 Fn7Bolts = 0;
	FString Fn7Entropy, Fn7Arena;   // for the Final Night 7 status box
	// Final Night 7 superpowers: TIME = the text box (a slow window), ENTROPY = the power bar (instant casts)
	double Fn7WindowDeadline = 0.0;   // epoch ms of the open slow window, 0 = none
	FString Fn7WindowBy;
	TSharedPtr<SHorizontalBox> PowerBar;
	int32 PowersShown = 0;
	FString ArmedPower, ArmedPowerName;
	void RebuildPowerBar(const TArray<TSharedPtr<FJsonValue>>& Powers);
	// Seats around the table: presence per seat (room.updated) and demigod portraits (fetched once per image hash)
	TMap<int32, bool> SeatConnected;
	TMap<FString, TStrongObjectPtr<class UTexture2D>> PortraitTex;   // image hash -> texture
	TSet<FString> PortraitPending;
	TSharedPtr<FJsonObject> LastPublicView;
	void PushSeats();
	TSharedPtr<FJsonObject> LastOutcome;   // round.outcome.public, for demigod cards
	void ShowDemigodCard(const FString& Ref, const FString& Name);
	// Portrait row at the top of the screen: one card per demigod (portrait when ready, a placeholder until then)
	TSharedPtr<SHorizontalBox> PortraitStrip;
	TMap<class UTexture2D*, TSharedPtr<struct FSlateBrush>> PortraitBrush;
	void RebuildPortraitStrip(const TArray<FFNSeatInfo>& Seats);
	void FetchPortrait(const FString& Hash, const FString& Url);
public:
	/** Called by the studio when the player clicks a point on the globe: casts the armed power there. */
	void OnGlobePicked(double Lon, double Lat);
private:   // joined someone else's room (Config/FN7JoinCode.txt): never send room.start

	FReply OnSubmitWill();
	FReply OnToggleNotification();
	void AddBanner(const FString& Text, const FLinearColor& Color);
	void RebuildBanners();
	void Choose(int32 Index);

	FText HintText() const;
	FText HoverText() const;
	FText NotificationText() const;
	FText HudText() const;
	FText TimerText() const;
	EActiveTimerReturnType TickAnim(double Time, float Dt);

	void AutoTestStep();
	void Shot(const TCHAR* Name);

	TWeakObjectPtr<UFNNetClient> Net;
	TWeakObjectPtr<AFNStudio> Studio;
	TSharedPtr<SMultiLineEditableTextBox> WillBox;
	TSharedPtr<SVerticalBox> BannerBox;

	EState State = EState::Tutorial;
	FString Status = TEXT("Not connected");
	FName HoveredId;
	FString RoomCode;
	int32 Day = 0, Days = 0, Alive = 0, WillMax = 295;
	int32 Health = 10, Kills = 0, DupCharges = 3;
	FString Hazard, Place, ZoneLine, Dawn, ProgressNote;
	TArray<FString> Bag;
	TArray<FString> DropCities;
	double Deadline = 0.0;
	TArray<FFNEpisode> Choices;   // EpisodeUuid = choice_id, RoundOutcome = kind, LocalExperience = label
	bool bEliminated = false;
	TArray<FFNEpisode> HeroChoices;      // the three hero cards at the start
	TArray<FString> CutLines;            // one line per day, for the Final Cut replay
	TArray<FVector2D> CutSpots;          // where you were that day
	double MyLon = 0.0, MyLat = 0.0;
	FString DiceBanner;
	// --- the "one more game" loop: stats, quests, XP, end screen ---
	int32 BestRoll = 0, Nat20s = 0, Successes = 0, ItemsGot = 0, FallPlace = 0, XpGained = 0, DaysLived = 0;
	bool bLeftZone = false, bBlocked = false, bWon = false, bProfileSaved = false;
	double EndScreenAt = -1.0;
	TArray<FString> Quests;
	FString EndTitle, EndBody;
	void PickQuests();
	bool QuestDone(int32 Index) const;
	void FinishMatch(bool bWinner);
	FText ProfileText() const;
	FText QuestText() const;
	FReply OnPlayAgain();
	// --- pictures of the story (OpenAI image model; key in Config/OpenAIKey.txt or a .env) ---
	void RequestPicture(const FString& Scene, const FString& Caption);
	FString LoadOpenAIKey() const;
	TStrongObjectPtr<class UTexture2D> PicTex;
	FSlateBrush PicBrush;
	bool bPicShown = false, bPicPending = false, bPicWarned = false;
	FString PicCaption;
	int32 PicSerial = 0;
	FReply OnHidePicture() { bPicShown = false; return FReply::Handled(); }
	FReply OnKeepWatching() { bSpectatePanelHidden = true; return FReply::Handled(); }
	// --- duels ---
	FString DuelId, DuelFoe, DuelRole, DuelPrompt, DuelOdds;
	bool bSpectatePanelHidden = false;
	FString SpectateCause;
	double DiceBannerAt = -1.0;

	FString Intro;
	float IntroShown = 0.f;
	bool bNotificationOpen = true;
	struct FBanner { FString Text; FLinearColor Color; double Born; };
	TArray<FBanner> Banners;

	double Clock = 0.0;
	TSharedPtr<FActiveTimerHandle> AnimTimer;
	FDelegateHandle MsgHandle, StatusHandle;

	double AutoNext = 2.0, AutoStuckSince = 0.0;
	FString AutoLastStep;
	bool bAutoDone = false;
	int32 AutoShots = 0;
};
