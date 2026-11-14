#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "FNStyle.h"

class SFNPortraitCard;

/** One demigod seat as the HUD shows it. PortraitPng = the bytes of /media/<hash>.png (empty: the initial). */
struct FFNHudSeat
{
	FText Name;
	EFNSeatRole Role = EFNSeatRole::AI;
	bool bIsYou = false;
	bool bHoldsTime = false;
	TArray<uint8> PortraitPng;
};

/** One line of world news: a short tag (e.g. "DAY 7") and the text. */
struct FFNHudNews
{
	FText Tag;
	FText Text;
};

/** One power on the bar. */
struct FFNHudPower
{
	FText Name;
	int32 Cost = 1;
	bool bArmed = false;
};

/** Everything the HUD layout draws; the bridge fills it from the payloads. */
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
	TArray<FFNHudSeat> Seats;
	TArray<FFNHudNews> News;
	TArray<FFNHudPower> Powers;
};

/**
 * The full FN7 HUD, laid out on a 4-corner grid around a clear centre (the globe):
 * top-left header strip, right WORLD news column, lower-left you + seat cards, bottom-centre powers + compose.
 */
class FINALNIGHT_API SFNHudLayout : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNHudLayout) {}
		SLATE_ARGUMENT(FFNHudModel, Model)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetModel(const FFNHudModel& InModel);

	/** A model filled with a believable mid-match state, for previews and screenshots. */
	static FFNHudModel SampleModel();

private:
	FFNHudModel Model;
	TArray<TSharedPtr<SFNPortraitCard>> Cards;
	void Rebuild();

	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeNews();
	TSharedRef<SWidget> MakeYou();
	TSharedRef<SWidget> MakeSeats();
	TSharedRef<SWidget> MakeCompose();
};
