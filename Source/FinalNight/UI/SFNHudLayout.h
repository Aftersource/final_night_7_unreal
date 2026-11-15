#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "FNStyle.h"
#include "UObject/StrongObjectPtr.h"

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
	int32 Bolts = -1;        // public counts; -1 = not sent
	int32 Sparks = -1;
	UTexture2D* PortraitTexture = nullptr;
	TArray<uint8> PortraitPng;
};

/** One of YOUR two demigods (rules 0.7.0: a Left and a Right hand, each with its own health). */
struct FFNHudDemigod
{
	FString Ref;             // "dg:N": drag the card to summon it
	FText Name;
	FString Side;            // "left" or "right"
	int32 Health = 10;
	int32 MaxHealth = 10;
	bool bAlive = true;
	UTexture2D* PortraitTexture = nullptr;   // the card's main image: the DIORAMA when there is one
	TArray<uint8> PortraitPng;
	UTexture2D* InsetTexture = nullptr;      // the portrait, small, when the diorama leads
	TArray<uint8> InsetPng;
	FString Gear;            // its loadout (look.loadout names), shown under its health
	FString NewItem;         // a just-gained item: "NEW: <name>" on the card for a few seconds
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
	FString Tier;            // "mega" (bolts, map scale), "mini" (sparks, the board) or "" (a server without tiers)
	double RadiusKm = 0.0;
	FString Key() const { return Tier.IsEmpty() ? Slug : Slug + TEXT(":") + Tier; }
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
	int32 Sparks = -1;       // -1 = the server sends no sparks yet
	FText Place;            // "Vienna, Western Europe"
	FText ArenaLine;        // "Time's circle: 8637 km around Kinshasa"
	FText PowersNote;       // "every bolt you spend gives each rival +2"
	FText ComposeHint;      // "Write what you'll do today..."
	FText Timer;            // "0:05"
	FText Status;           // one line under the header: connection, waiting for host, match over
	FText BannerTitle;      // a big moment over the board (a summon's deed), top centre, a few seconds
	FText Banner;
	FLinearColor BannerColor = FLinearColor::White;
	bool bViewer = false;   // watching the house game without a seat
	int32 AliveOf = 0;      // demigods in the match (the "7/8 ALIVE" denominator); 0 = unknown
	bool bReplay = false;   // the house is replaying a recorded match
	TArray<FFNHudDemigod> Mine;   // your two demigods (empty on a one-demigod server: the classic you panel shows)
	FLinearColor YourAura = FLinearColor(0.91f, 0.72f, 0.29f);
	double Debt = -1.0;     // your open debt (summed over your demigods); -1 = not sent
	FText TimeBill;         // the live bill while you hold slowed time, e.g. "1 bolt + 2.4 debt"; empty otherwise
	bool bHoldingTime = false;
	bool bPowerPanel = true;      // the HUD power panel (a fallback until the powers live on the board's edge)
	TArray<FFNHudSeat> Seats;
	TArray<FFNHudNews> News;
	TArray<FFNHudPower> Powers;
};

DECLARE_DELEGATE_OneParam(FOnFNHudSend, const FString& /*Text*/);
DECLARE_DELEGATE(FOnFNHudTyping);
DECLARE_DELEGATE_OneParam(FOnFNHudPower, const FString& /*Slug*/);
DECLARE_DELEGATE(FOnFNHudTakeSeat);
DECLARE_DELEGATE(FOnFNHudLobby);
DECLARE_DELEGATE(FOnFNHudResetView);
/** A drag started on one of your demigod cards: return the drag operation (or null when it can't be summoned). */
DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<class FDragDropOperation>, FOnFNHudCardDrag, const FString& /*Ref*/, const FVector2D& /*Screen*/);
DECLARE_DELEGATE_OneParam(FOnFNHudCardDouble, const FString& /*Ref*/);

/**
 * The FN7 HUD, screen map v2: four corners around a completely clear centre (the hex board and the globe).
 * Top left: the header strip and LOBBY. Top right: WORLD news, collapsible. Lower left: YOUR two demigods (portraits,
 * their own health, bolts, sparks, debt, Time's live bill). Lower right: the WILL, a slim shiny scroll that unrolls
 * when clicked and rolls up after SEND, with the compact power panel above it while it is the fallback.
 * The rivals are not in the HUD: they are the gods on pedestals around the table.
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
		SLATE_EVENT(FOnFNHudTakeSeat, OnTakeSeat)
		/** The LOBBY pill in the header. */
		SLATE_EVENT(FOnFNHudLobby, OnLobby)
		/** RESET VIEW in the header (R / Home do the same). */
		SLATE_EVENT(FOnFNHudResetView, OnResetView)
		/** Dragging one of your two demigod cards summons it. */
		SLATE_EVENT(FOnFNHudCardDrag, OnCardDrag)
		/** Double-clicking one of your demigod cards zooms the board to it. */
		SLATE_EVENT(FOnFNHudCardDouble, OnCardDoubleClick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetModel(const FFNHudModel& InModel);
	/** Time's live bill, updated every tick without rebuilding the sections. */
	void SetTimeBill(const FText& Bill, bool bHolding) { Model.TimeBill = Bill; Model.bHoldingTime = bHolding; }
	/** Unroll the WILL scroll and put the cursor in it (a click on the strip, or Enter). */
	void FocusWill() { OpenWill(); }
	bool IsWillOpen() const { return bWillOpen; }
	/** Roll the WILL back up without sending (Esc while writing); the text stays. */
	void RollUpWill();
	const FFNHudModel& GetModel() const { return Model; }

	/** A model filled with a believable mid-match state, for previews and screenshots. */
	static FFNHudModel SampleModel();

private:
	FFNHudModel Model;
	TAttribute<FText> TimerAttr;
	FOnFNHudSend OnSend;
	FOnFNHudTyping OnTypingStarted;
	FOnFNHudPower OnPower;
	FOnFNHudTakeSeat OnTakeSeat;
	FOnFNHudLobby OnLobby;
	FOnFNHudResetView OnResetView;
	FOnFNHudCardDrag OnCardDrag;
	FOnFNHudCardDouble OnCardDoubleClick;
	bool bTypingSignalled = false;

	TSharedPtr<SBox> HeaderHolder, NewsHolder, YouHolder, PowersHolder;
	FString YouSignature, PowersSignature;   // rebuild those sections only when their content changes
	FString MakeYouSignature() const;
	bool bNewsCollapsed = false;
	// The header and world scrolls are built once; their inscription is swapped only when it changes (make it
	// unroll and roll up properly when and only when it needs to change").
	TSharedPtr<SBox> HeaderBody, NewsBody;
	FString HeaderSignature, NewsSignature;
	bool bNewsOpen = false, bNewsBuilt = false, bNewsSwapPending = false;
	EActiveTimerReturnType SwapNews(double, float);
	EActiveTimerReturnType StartNewsRoll(double, float);
	double NextNewsRollAt = 0.0;   // at most one roll cycle every 8 s (news comes in bursts)
	bool bWillOpen = false;       // the WILL scroll: a slim rolled strip until clicked (or Enter), rolled up after SEND
	TSharedPtr<FSlateBrush> AuraFrameBrush;
	FLinearColor AuraFrameColor = FLinearColor::Transparent;
	const FSlateBrush* AuraFrame();
	TMap<const UTexture2D*, TSharedPtr<FSlateBrush>> InsetBrushes;
	TArray<TStrongObjectPtr<UTexture2D>> InsetKeep;   // textures made here from PNG bytes (previews)
	const FSlateBrush* InsetBrush(const FFNHudDemigod& D);
	TSharedRef<SWidget> MakeMine();
	void OpenWill();
	FString MakePowersSignature() const;
	TArray<FFNHudPower> OrderedPowers() const;     // the server's powers in this machine's saved order
	void ReorderPower(const FString& Dragged, const FString& Before);
	TSharedPtr<SFNTextBox> Compose;

	void RefreshSections();
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeNews();
	TSharedRef<SWidget> MakeYou();
	TSharedRef<SWidget> MakePowers();
	TSharedRef<SWidget> MakeComposePanel();
	void HandleTextChanged(const FText& Text);
	void HandleTextCommitted(const FText& Text, ETextCommit::Type Commit);
	FReply HandleSend();
};
