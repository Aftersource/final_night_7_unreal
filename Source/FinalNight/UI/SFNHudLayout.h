#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "FNStyle.h"

class SBox;
class SFNPortraitCard;
class SFNTextBox;
class UTexture2D;

/** One demigod seat as the HUD shows it. Portrait: a cached texture, else PNG bytes, else the initial. */
struct FFNHudSeat
{
	FText Name;
	EFNSeatRole Role = EFNSeatRole::AI;
	bool bIsYou = false;
	bool bHoldsTime = false;
	UTexture2D* PortraitTexture = nullptr;
	TArray<uint8> PortraitPng;
};

/** One line of news: a short tag (e.g. "BEAT 7", "YOU") and the text. */
struct FFNHudNews
{
	FText Tag;
	FText Text;
};

/** One power on the bar; Slug is what the server casts. */
struct FFNHudPower
{
	FString Slug;
	FText Name;
	int32 Cost = 1;
	bool bArmed = false;
};

/** Everything the HUD layout draws; SFNHudLive fills it from the payloads. */
struct FFNHudModel
{
	FText Room;
	int32 Beat = 0;
	int32 BeatsTotal = 0;
	int32 Alive = 0;
	FText Entropy;          // e.g. "CALM 8"
	FText YouName;
	int32 Health = 10;
	int32 MaxHealth = 10;
	int32 Bolts = 0;
	FText Place;            // "Vienna, Western Europe"
	FText ArenaLine;        // "Time's circle: 8637 km around Kinshasa"
	FText PowersNote;       // "every bolt you spend gives each rival +2"
	FText ComposeHint;      // "Write what you'll do today..."
	FText Timer;            // "0:05"
	FText Status;           // one line under the header: connection, waiting for host, match over
	TArray<FFNHudSeat> Seats;
	TArray<FFNHudNews> News;
	TArray<FFNHudPower> Powers;
};

DECLARE_DELEGATE_OneParam(FOnFNHudSend, const FString& /*Text*/);
DECLARE_DELEGATE(FOnFNHudTyping);
DECLARE_DELEGATE_OneParam(FOnFNHudPower, const FString& /*Slug*/);

/**
 * The full FN7 HUD on a corner grid around a clear centre (the globe): header strip top left, WORLD news top right,
 * seat cards and your status lower left, powers and the WILL composer bottom centre (one bottom row, no overlap).
 * SetModel refreshes every section in place; the composer keeps its text and focus across updates.
 */
class FINALNIGHT_API SFNHudLayout : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNHudLayout) {}
		SLATE_ARGUMENT(FFNHudModel, Model)
		/** Overrides Model.Timer when bound (a live countdown). */
		SLATE_ATTRIBUTE(FText, Timer)
		SLATE_EVENT(FOnFNHudSend, OnSend)
		SLATE_EVENT(FOnFNHudTyping, OnTypingStarted)
		SLATE_EVENT(FOnFNHudPower, OnPower)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetModel(const FFNHudModel& InModel);
	const FFNHudModel& GetModel() const { return Model; }

	/** A model filled with a believable mid-match state, for previews and screenshots. */
	static FFNHudModel SampleModel();

private:
	FFNHudModel Model;
	TAttribute<FText> TimerAttr;
	FOnFNHudSend OnSend;
	FOnFNHudTyping OnTypingStarted;
	FOnFNHudPower OnPower;
	bool bTypingSignalled = false;

	TSharedPtr<SBox> HeaderHolder, NewsHolder, SeatsHolder, YouHolder, PowersHolder;
	TSharedPtr<SFNTextBox> Compose;
	TArray<TSharedPtr<SFNPortraitCard>> Cards;

	void RefreshSections();
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeNews();
	TSharedRef<SWidget> MakeYou();
	TSharedRef<SWidget> MakeSeats();
	TSharedRef<SWidget> MakePowers();
	TSharedRef<SWidget> MakeComposePanel();
	void HandleTextChanged(const FText& Text);
	void HandleTextCommitted(const FText& Text, ETextCommit::Type Commit);
	FReply HandleSend();
};
