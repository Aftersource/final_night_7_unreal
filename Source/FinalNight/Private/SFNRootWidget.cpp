#include "SFNRootWidget.h"
#include "FNNetClient.h"
#include "FNStudio.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Dom/JsonObject.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Images/SImage.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Engine/Texture2D.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogFNUI, Log, All);
#define LOCTEXT_NAMESPACE "FinalNight"

namespace FNUi
{
	static const FLinearColor Panel(0.02f, 0.02f, 0.035f, 0.82f);
	static const FLinearColor Edge(0.55f, 0.40f, 0.12f, 0.9f);
	static const FLinearColor Gold(1.0f, 0.866f, 0.21f);
	static const FLinearColor Orange(0.965f, 0.525f, 0.23f);
	static const FLinearColor Cyan(0.133f, 1.0f, 1.0f);
	static const FLinearColor Red(0.9f, 0.2f, 0.15f);
	static const FLinearColor Pale(0.88f, 0.85f, 0.80f);
	static const FLinearColor Dim(0.55f, 0.52f, 0.48f);
	static FSlateFontInfo Font(int32 Size, const TCHAR* W = TEXT("Regular")) { return FCoreStyle::GetDefaultFontStyle(W, Size); }
	static const FSlateBrush* White() { return FAppStyle::GetBrush("WhiteBrush"); }
	static TSharedRef<SWidget> Framed(TSharedRef<SWidget> Content, FMargin Pad = FMargin(16.f, 12.f))
	{
		return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Edge).Padding(1.5f)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(Pad)[Content]
		];
	}
	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* F) { FString S; if (O.IsValid()) O->TryGetStringField(F, S); return S; }
	static double Numb(const TSharedPtr<FJsonObject>& O, const TCHAR* F, double Def = 0) { double N = Def; if (O.IsValid()) O->TryGetNumberField(F, N); return N; }
	static TSharedPtr<FJsonObject> Sub(const TSharedPtr<FJsonObject>& O, const TCHAR* F) { const TSharedPtr<FJsonObject>* P; return (O.IsValid() && O->TryGetObjectField(F, P)) ? *P : nullptr; }
	static const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* F) { const TArray<TSharedPtr<FJsonValue>>* A = nullptr; if (O.IsValid()) O->TryGetArrayField(F, A); return A; }
	static bool LonLat(const TSharedPtr<FJsonObject>& O, const TCHAR* F, double& Lon, double& Lat)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = Arr(O, F);
		if (!A || A->Num() < 2) return false;
		Lon = (*A)[0]->AsNumber(); Lat = (*A)[1]->AsNumber();
		return true;
	}
}
using namespace FNUi;

// Your progress between games (XP, level, wins), kept in Saved/FinalNightProfile.txt
namespace FNProfile
{
	struct FData { int32 Xp = 0, Wins = 0, Games = 0, BestPlace = 0; };
	static FString PathOf() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FinalNightProfile.txt")); }
	static FData Load()
	{
		FData D; FString Txt;
		if (!FFileHelper::LoadFileToString(Txt, *PathOf())) return D;
		TArray<FString> Lines; Txt.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines)
		{
			FString K, V; if (!L.Split(TEXT("="), &K, &V)) continue;
			const int32 N = FCString::Atoi(*V);
			if (K == TEXT("xp")) D.Xp = N; else if (K == TEXT("wins")) D.Wins = N; else if (K == TEXT("games")) D.Games = N; else if (K == TEXT("best_place")) D.BestPlace = N;
		}
		return D;
	}
	static void Save(const FData& D)
	{
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("xp=%d\nwins=%d\ngames=%d\nbest_place=%d\n"), D.Xp, D.Wins, D.Games, D.BestPlace), *PathOf());
	}
	static int32 Level(int32 Xp) { return 1 + Xp / 1000; }
	static const TCHAR* Title(int32 Lv)
	{
		static const TCHAR* T[] = { TEXT("Peasant"), TEXT("Squire"), TEXT("Adventurer"), TEXT("Hero"), TEXT("Champion"), TEXT("Legend") };
		return Lv - 1 < 6 ? T[FMath::Max(0, Lv - 1)] : TEXT("Mythic");
	}
	static const TCHAR* Quests[] = {
		TEXT("Roll a natural 20"), TEXT("Survive all the days"), TEXT("Find or build 4 things"), TEXT("Win 3 dice rolls"),
		TEXT("Never leave the safe zone"), TEXT("Block a fate"), TEXT("Finish with 7+ health"), TEXT("Win the night") };
}

void SFNRootWidget::Construct(const FArguments& InArgs)
{
	Net = InArgs._Net;
	Studio = InArgs._Studio;
	// A click on a point of the globe: the studio reports [lon, lat]; with an Entropy power armed, it is cast there.
	if (AFNStudio* St = Studio.Get())
	{
		St->OnUserPickedSpot = [this](double Lon, double Lat) { OnGlobePicked(Lon, Lat); };
		St->OnDemigodClicked = [this](const FString& Ref, const FString& Name) { ShowDemigodCard(Ref, Name); };
	}
	if (Net.IsValid())
	{
		MsgHandle = Net->OnServerMessage.AddSP(this, &SFNRootWidget::OnServerMessage);
		StatusHandle = Net->OnStatus.AddSP(this, &SFNRootWidget::OnStatus);
	}

	Intro = TEXT("Pull up a chair. The Dungeon Master is waiting.\n\n")
		TEXT("Pick your hero. Each day the DM tells you what is happening on the real Earth, and you say what you do: your WILL. ")
		TEXT("Then the d20 decides how it goes. Plans that suit your hero get a bonus.\n\n")
		TEXT("Stay inside the safe zone, back or block what fate does to others, and be the last one standing.");

	ChildSlot
	[
		SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		// Title + room / day, top left
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(28.f, 22.f)
		[
			SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("T", "FINAL NIGHT")).Font(Font(26, TEXT("Bold"))).ColorAndOpacity(Gold)]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Dim)
				.Text_Lambda([this]() { return RoomCode.IsEmpty() ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("[ room :: %s ]"), *RoomCode)); })
			]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(12, TEXT("Bold"))).ColorAndOpacity(Orange).Text_Raw(this, &SFNRootWidget::ProfileText)]
		]
		// Tonight's quests, top right
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(28.f, 22.f)
		[
			SNew(SBox).WidthOverride(380.f).Visibility_Lambda([this]() { return Quests.Num() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				Framed(SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Gold).Text_Raw(this, &SFNRootWidget::QuestText), FMargin(14.f, 8.f))
			]
		]
		// Notification panel (intro), left; click to fold it
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(28.f, 118.f)
		[
			SNew(SBox).WidthOverride_Lambda([this]() -> FOptionalSize { return bNotificationOpen ? 400.f : 54.f; })
			[
				SNew(SButton).ButtonColorAndOpacity(FLinearColor::Transparent).ContentPadding(0)
				.OnClicked(this, &SFNRootWidget::OnToggleNotification)
				[
					Framed(SNew(STextBlock).AutoWrapText(true).Font(Font(12)).ColorAndOpacity(Pale).Text_Raw(this, &SFNRootWidget::NotificationText))
				]
			]
		]
		// Your state (health, items, zone), bottom left
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(28.f, 0.f, 0.f, 48.f)
		[
			SNew(SBox).WidthOverride(400.f).Visibility_Lambda([this]() { return Day > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				Framed(SNew(STextBlock).AutoWrapText(true).Font(Font(12)).ColorAndOpacity(Pale).Text_Raw(this, &SFNRootWidget::HudText))
			]
		]
		// Event stream, right
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(28.f, 150.f)
		[
			SNew(SBox).WidthOverride(380.f).Visibility(EVisibility::HitTestInvisible)[SAssignNew(BannerBox, SVerticalBox)]
		]
		// Final Night 7: the demigods' portraits, top centre
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SBox).Visibility_Lambda([this]() { return (Net.IsValid() && Net->bFn7 && Day > 0) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SAssignNew(PortraitStrip, SHorizontalBox)
			]
		]
		// Final Night 7: ENTROPY power bar, bottom centre. Pick a power, then click the globe.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 92.f)
		[
			SNew(SVerticalBox).Visibility_Lambda([this]() { return (Net.IsValid() && Net->bFn7 && Day > 0 && !bEliminated && PowersShown > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
			[
				SNew(STextBlock).Font(Font(12, TEXT("Bold"))).ColorAndOpacity(Orange).Text_Lambda([this]()
				{
					return FText::FromString(ArmedPower.IsEmpty()
						? FString::Printf(TEXT("ENTROPY POWERS  -  %d bolts  -  every bolt you spend gives each rival +2"), Fn7Bolts)
						: FString::Printf(TEXT("%s ARMED  -  click the globe where it should strike  (click the power again to cancel)"), *ArmedPowerName.ToUpper()));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(PowerBar, SHorizontalBox)]
		]
		// Step hint + timer, bottom centre (kept off the tablet so it never covers the story)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 18.f)
		[
			SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Font(16, TEXT("Bold"))).ColorAndOpacity(Cyan).Text_Raw(this, &SFNRootWidget::HintText)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)
			[
				SNew(STextBlock).Font(Font(14, TEXT("Bold"))).ColorAndOpacity(Orange).Text_Raw(this, &SFNRootWidget::TimerText)
			]
		]
		// Hover prompt
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 150.f)
		[
			SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(Gold).Text_Raw(this, &SFNRootWidget::HoverText)
		]
		// WILL entry
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 84.f)
		[
			SNew(SBox).WidthOverride(720.f)
			.Visibility_Lambda([this]() { return (State == EState::Writing || State == EState::DuelWriting) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Framed(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(STextBlock).Font(Font(14, TEXT("Bold"))).ColorAndOpacity(Gold)
						.Text_Lambda([this]()
						{
							if (State == EState::DuelWriting)
								return FText::FromString(FString::Printf(TEXT("DUEL vs %s  -  %s"), *DuelFoe.ToUpper(), DuelRole == TEXT("defender") ? TEXT("DEFEND YOURSELF") : TEXT("YOUR MOVE")));
							return FText::FromString(FString::Printf(TEXT("DAY %d  -  What is your WILL?"), Day));
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(STextBlock).AutoWrapText(true).Font(Font(11)).ColorAndOpacity(Dim)
						.Text_Lambda([this]()
						{
							if (State == EState::DuelWriting) return FText::FromString(DuelPrompt + TEXT("\n") + DuelOdds);
							if (Net.IsValid() && Net->bFn7) return FText::FromString(TEXT("TIME: you are slowing time. Speak to your two hands (lines starting r: go to the right hand). Enter seals your orders and cuts off anyone slower."));
							return FText::FromString(DropCities.Num()
								? FString::Printf(TEXT("Day 1: name a city to drop into: %s"), *FString::Join(DropCities, TEXT(" / ")))
								: FString(TEXT("Ordered intentions. Teleport first and you search at the destination.")));
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
					[
						SNew(SBox).MinDesiredHeight(84.f)
						[
							SAssignNew(WillBox, SMultiLineEditableTextBox).Font(Font(15)).AutoWrapText(true)
							.HintText_Lambda([this]() { if (State == EState::DuelWriting) return DuelRole == TEXT("defender") ? LOCTEXT("DuelHintD", "I hear the floor creak, roll behind the counter and jam a chair under the door...") : LOCTEXT("DuelHintA", "I feint left, duplicate myself, and let the copy take the hit while I..."); return Day <= 1 ? LOCTEXT("WillHint", "I drop into Las Vegas, grab anything shiny, and teleport onto the tallest roof.") : LOCTEXT("WillHint2", "Where do you go, what do you build, how do you survive the night?"); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(Font(11)).ColorAndOpacity_Lambda([this]()
							{
								return FSlateColor(WillBox.IsValid() && WillBox->GetText().ToString().Len() > WillMax ? Red : Dim);
							})
							.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d / %d"), WillBox.IsValid() ? WillBox->GetText().ToString().Len() : 0, WillMax)); })
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton).ButtonColorAndOpacity(Gold).ContentPadding(FMargin(24.f, 9.f))
							.IsEnabled_Lambda([this]()
							{
								if (!WillBox.IsValid()) return false;
								const FString W = WillBox->GetText().ToString();
								return !W.TrimStartAndEnd().IsEmpty() && W.Len() <= WillMax;
							})
							.OnClicked(this, &SFNRootWidget::OnSubmitWill)
							[
								SNew(STextBlock).Text_Lambda([this]() { return State == EState::DuelWriting ? LOCTEXT("LockDuel", "LOCK IN MOVE") : LOCTEXT("Lock", "LOCK IN WILL"); }).Font(Font(14, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.93f, 0.6f))
							]
						]
					]
				, FMargin(18.f, 14.f))
			]
		]
		// Status line
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(28.f, 16.f)
		[
			SNew(STextBlock).Visibility(EVisibility::HitTestInvisible).Font(Font(11)).ColorAndOpacity(Dim)
			.Text_Lambda([this]() { return FText::FromString(Status + (Net.IsValid() && Net->IsDemo() ? TEXT("   [demo Theatre]") : TEXT(""))); })
		]
		// The Dungeon Master's painting of what just happened (click to put it away)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 70.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(440.f)
			.Visibility_Lambda([this]() { return (bPicShown && PicTex.IsValid()) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SButton).ButtonColorAndOpacity(FLinearColor::Transparent).ContentPadding(0).OnClicked(this, &SFNRootWidget::OnHidePicture)
				[
					Framed(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(400.f).HeightOverride(400.f)[SNew(SImage).Image(&PicBrush)]]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).AutoWrapText(true).Font(Font(12, TEXT("Bold"))).ColorAndOpacity(Gold).Text_Lambda([this]() { return FText::FromString(PicCaption); })]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text(LOCTEXT("PicHide", "click to put the picture away"))]
					, FMargin(10.f, 10.f))
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 76.f, 0.f, 0.f)
		[
			SNew(STextBlock).Visibility_Lambda([this]() { return (bPicPending && !bPicShown) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.Font(Font(12, TEXT("Bold"))).ColorAndOpacity(Orange).Text(LOCTEXT("PicPending", "The Dungeon Master is painting the scene..."))
		]
		// Knocked out: watch the rest, or start a fresh game
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 90.f)
		[
			SNew(SBox).WidthOverride(560.f)
			.Visibility_Lambda([this]() { return (State == EState::Spectating && !bSpectatePanelHidden) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Framed(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(Font(24, TEXT("Bold"))).ColorAndOpacity(Red).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("YOU HAVE FALLEN  -  #%d"), FMath::Max(2, FallPlace))); })]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 14)[SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center).Font(Font(13)).ColorAndOpacity(Pale).Text_Lambda([this]() { return FText::FromString(SpectateCause + TEXT("\nYou are out of this game for good. Watch how the night ends, or start a fresh game.")); })]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
						[
							SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.2f, 0.2f, 0.25f)).ContentPadding(FMargin(22.f, 10.f)).OnClicked(this, &SFNRootWidget::OnKeepWatching)
							[SNew(STextBlock).Text(LOCTEXT("Watch", "KEEP WATCHING")).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(Pale)]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
						[
							SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.55f, 0.32f, 0.06f)).ContentPadding(FMargin(22.f, 10.f)).OnClicked(this, &SFNRootWidget::OnPlayAgain)
							[SNew(STextBlock).Text(LOCTEXT("Fresh", "JOIN A FRESH GAME")).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.93f, 0.6f))]
						]
					]
				, FMargin(22.f, 16.f))
			]
		]
		// End of the night: placement, what you earned, and one more game
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(600.f)
			.Visibility_Lambda([this]() { return (State == EState::Ended && EndScreenAt >= 0.0 && Clock >= EndScreenAt) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Framed(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(Font(30, TEXT("Bold"))).ColorAndOpacity(Gold).Text_Lambda([this]() { return FText::FromString(EndTitle); })]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 16)[SNew(STextBlock).AutoWrapText(true).Font(Font(14)).ColorAndOpacity(Pale).Text_Lambda([this]() { return FText::FromString(EndBody); })]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.55f, 0.32f, 0.06f)).ContentPadding(FMargin(40.f, 12.f))
						.OnClicked(this, &SFNRootWidget::OnPlayAgain)
						[
							SNew(STextBlock).Text(LOCTEXT("Again", "PLAY AGAIN")).Font(Font(20, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.93f, 0.6f))
						]
					]
				, FMargin(28.f, 22.f))
			]
		]
	];

	AnimTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNRootWidget::TickAnim));
}

SFNRootWidget::~SFNRootWidget()
{
	if (Net.IsValid())
	{
		Net->OnServerMessage.Remove(MsgHandle);
		Net->OnStatus.Remove(StatusHandle);
	}
}

void SFNRootWidget::SetState(EState S)
{
	State = S;
	AFNStudio* St = Studio.Get();
	if (!St) return;
	St->SetFatesDeciding(S == EState::Resolving);
	if (S == EState::Submitted) St->PlaySfx(EFNSfx::Boom, 0.35f);
	if (S == EState::Choose) St->PlaySfx(EFNSfx::Chime, 0.6f);
	switch (S)
	{
	case EState::InRoom:     St->SetGuardBlitz(false); St->SetWillPanelState(TEXT("WILL\nready"), true); break;
	case EState::Countdown:  St->SetWillPanelState(TEXT("WILL"), false); break;
	case EState::Waiting:    St->SetWillPanelState(TEXT("WILL"), false); St->SetGuardLocked(false); break;
	case EState::Compose:    St->SetWillPanelState(TEXT("WILL"), true); break;
	case EState::Writing:    St->SetWillPanelState(TEXT("WILL"), false); break;
	case EState::Submitted:  St->SetWillPanelState(TEXT("WILL\nlocked in"), false); St->SetGuardLocked(true); break;
	case EState::Choose:     St->SetGuardLocked(false); break;
	case EState::Spectating: St->SetWillPanelState(TEXT("spectating"), false); St->SetGuardLocked(false); break;
	case EState::DuelWriting: St->SetWillPanelState(TEXT("DUEL"), false); break;
	case EState::DuelWaiting: St->SetWillPanelState(TEXT("DUEL\nlocked in"), false); break;
	case EState::Ended:      St->SetWillPanelState(TEXT("the end"), false); St->SetGuardLocked(false); break;
	default: break;
	}
}

// --- text -----------------------------------------------------------------

FText SFNRootWidget::HintText() const
{
	switch (State)
	{
	case EState::Tutorial:   return LOCTEXT("H0", "Click anywhere on the table to begin");
	case EState::Connecting: return LOCTEXT("H1", "Connecting to the Theatre...");
	case EState::Connected:  return (Net.IsValid() && Net->HeroClass.IsEmpty()) ? LOCTEXT("H2a", "Choose your hero: click a card") : LOCTEXT("H2", "Click the Globe to begin");
	case EState::InRoom:     return LOCTEXT("H3", "Click WILL when you're ready");
	case EState::Countdown:  return LOCTEXT("H4", "The match is about to begin...");
	case EState::Waiting:    return FText::FromString(Day > 0 ? FString::Printf(TEXT("Day %d is dawning..."), Day) : FString(TEXT("Waiting...")));
	case EState::Compose:    return LOCTEXT("H5", "Click WILL to write what you'll do today");
	case EState::Writing:    return LOCTEXT("H6", "Write your WILL");
	case EState::Submitted:  return LOCTEXT("H7", "WILL locked in. Waiting for the others...");
	case EState::Resolving:  return FText::FromString(ProgressNote.IsEmpty() ? FString(TEXT("The game master is writing the day...")) : ProgressNote);
	case EState::Choose:     return LOCTEXT("H8", "Fates circle the Globe. Back it, block it, or let fate decide");
	case EState::Chosen:     return LOCTEXT("H9", "Choice made. The day is closing...");
	case EState::Spectating: return FText::FromString(FString::Printf(TEXT("You fell. You placed #%d. Watching the rest of the night..."), FMath::Max(2, FallPlace)));
	case EState::Ended:      return LOCTEXT("H11", "The night is over. Read the Final Cut on the tablet");
	case EState::DuelWriting: return FText::FromString(TEXT("DUEL: write your move. Better plans, better odds"));
	case EState::DuelWaiting: return LOCTEXT("H13", "The Dungeon Master is weighing the odds...");
	}
	return FText::GetEmpty();
}

FText SFNRootWidget::TimerText() const
{
	if (Net.IsValid() && Net->bFn7 && Fn7WindowDeadline > 0.0)
	{
		const int32 Secs = FMath::Max(0, (int32)((Fn7WindowDeadline - Net->NowServerMs()) / 1000.0));
		return FText::FromString(FString::Printf(TEXT("TIME IS SLOWED by %s  -  %d:%02d left"), *Fn7WindowBy, Secs / 60, Secs % 60));
	}
	if (Deadline <= 0.0 || !Net.IsValid() || (State != EState::Compose && State != EState::Writing && State != EState::Choose && State != EState::DuelWriting)) return FText::GetEmpty();
	const int32 Secs = FMath::Max(0, (int32)((Deadline - Net->NowServerMs()) / 1000.0));
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60));
}

FText SFNRootWidget::HoverText() const
{
	if (HoveredId == NAME_None) return FText::GetEmpty();
	if (HoveredId == AFNStudio::PlayerEasel && State == EState::Tutorial) return LOCTEXT("P0", "Begin");
	if (HoveredId == AFNStudio::Globe) return State == EState::Connected ? LOCTEXT("P1", "Globe of Destiny  -  start a game") : LOCTEXT("P1b", "Globe of Destiny");
	if (HoveredId == AFNStudio::WillPanel)
	{
		if (State == EState::InRoom) return LOCTEXT("P2", "WILL  -  ready");
		if (State == EState::Compose) return LOCTEXT("P3", "WILL  -  write today's WILL");
		return LOCTEXT("P4", "WILL");
	}
	if (State == EState::Connected && Net.IsValid() && Net->HeroClass.IsEmpty())
	{
		const int32 H = HoveredId == AFNStudio::RibbonA ? 0 : HoveredId == AFNStudio::RibbonB ? 1 : HoveredId == AFNStudio::RibbonC ? 2 : -1;
		if (HeroChoices.IsValidIndex(H)) return FText::FromString(TEXT("Play as the ") + HeroChoices[H].EpisodeUuid);
	}
	if (State == EState::Choose)
	{
		const int32 I = HoveredId == AFNStudio::RibbonA ? 0 : HoveredId == AFNStudio::RibbonB ? 1 : HoveredId == AFNStudio::RibbonC ? 2 : -1;
		if (Choices.IsValidIndex(I)) return FText::FromString(Choices[I].LocalExperience);
	}
	return FText::GetEmpty();
}

FText SFNRootWidget::NotificationText() const
{
	if (!bNotificationOpen) return FText::FromString(TEXT("≡"));
	if (Net.IsValid() && Net->bFn7)
		return FText::FromString(TEXT("FINAL NIGHT 7. You are a demigod on the real Earth; the last one standing wins.\n\n"
			"TIME: click WILL and write orders for your two hands. Time slows while you write; Enter seals your orders and cuts off anyone slower. Time sends a bill.\n\n"
			"ENTROPY: pick a power below, then click the globe. Every bolt you spend gives each rival +2.\n\n"
			"Stay inside Time's circle."));
	return FText::FromString(Intro.Left(FMath::Clamp((int32)IntroShown, 0, Intro.Len())));
}

FText SFNRootWidget::HudText() const
{
	if (Net.IsValid() && Net->bFn7)
	{
		FString H;
		for (int32 i = 0; i < 10; ++i) H += i < Health ? TEXT("\u25A0") : TEXT("\u25A1");
		if (bEliminated) return FText::FromString(FString::Printf(TEXT("BEAT %d / %d    %d demigods alive\nYOU HAVE FALLEN  -  watching the rest of the night"), Day, Days, Alive));
		return FText::FromString(FString::Printf(TEXT("BEAT %d / %d    %d demigods alive    ENTROPY: %s\nHEALTH  %s  %d/10      LIGHTNING BOLTS: %d\nYou stand in %s\n%s"),
			Day, Days, Alive, *Fn7Entropy, *H, FMath::Max(0, Health), Fn7Bolts, Place.IsEmpty() ? TEXT("...") : *Place, *Fn7Arena));
	}
	FString Hearts;
	for (int32 i = 0; i < 10; ++i) Hearts += i < Health ? TEXT("■") : TEXT("□");
	FString S = FString::Printf(TEXT("DAY %d / %d    %d alive    THE CRISIS: %s\nHEALTH  %s  %d/10\nYou are in %s\n%s"),
		Day, Days, Alive, *Hazard, *Hearts, FMath::Max(0, Health), *Place, *ZoneLine);
	if (bEliminated) S = FString::Printf(TEXT("DAY %d / %d    %d alive\nYOU ARE OUT  (placed #%d)  -  spectating"), Day, Days, Alive, FMath::Max(2, FallPlace));
	S += FString::Printf(TEXT("\nTeleporter (any distance)   Duplicator x%d"), DupCharges);
	if (Bag.Num())
	{
		TArray<FString> Last; for (int32 i = FMath::Max(0, Bag.Num() - 5); i < Bag.Num(); ++i) Last.Add(Bag[i]);
		S += TEXT("\nBag: ") + FString::Join(Last, TEXT(", ")) + (Bag.Num() > 5 ? FString::Printf(TEXT("  (+%d more)"), Bag.Num() - 5) : FString());
	}
	if (!Dawn.IsEmpty()) S += TEXT("\n") + Dawn;
	return FText::FromString(S);
}

FReply SFNRootWidget::OnToggleNotification() { bNotificationOpen = !bNotificationOpen; return FReply::Handled(); }

// --- clicks ---------------------------------------------------------------

void SFNRootWidget::OnWorldClick(FName Id)
{
	AFNStudio* St = Studio.Get();
	if (!Net.IsValid() || !St) return;

	// A click on empty space still moves the story along, so nobody gets stuck hunting for a prop
	if (Id == NAME_None)
	{
		if (State == EState::Tutorial) Id = AFNStudio::PlayerEasel;
		else if (State == EState::Connected) { if (Net->HeroClass.IsEmpty()) return; Id = AFNStudio::Globe; }
		else if (State == EState::InRoom || State == EState::Compose) Id = AFNStudio::WillPanel;
		else return;
	}

	if (Id == AFNStudio::PlayerEasel && State == EState::Tutorial)
	{
		St->PlayerEaselDown();
		SetState(EState::Connecting);
		Net->Start();
	}
	else if (State == EState::Connected && Net->HeroClass.IsEmpty())
	{
		const int32 I = Id == AFNStudio::RibbonA ? 0 : Id == AFNStudio::RibbonB ? 1 : Id == AFNStudio::RibbonC ? 2 : -1;
		if (HeroChoices.IsValidIndex(I))
		{
			Net->HeroClass = HeroChoices[I].EpisodeUuid;
			PickQuests();
			bNotificationOpen = false;   // fold the welcome note; the quests panel takes over
			St->ChooseRibbon(I);
			St->PlaySfx(EFNSfx::Chime, 0.8f);
			St->ShowEaselText(FString::Printf(TEXT("YOU ARE THE %s"), *Net->HeroClass.ToUpper()), TEXT("Good choice.\n\nNow touch the Globe of Destiny to take your seat at the table."));
			AddBanner(TEXT("Your hero: ") + Net->HeroClass, Gold);
		}
	}
	else if (Id == AFNStudio::Globe && State == EState::Connected)
	{
		St->ClearRibbons();
		Net->CreateRoom();   // a private room with short settings; quick play defaults to 100 days
		Status = TEXT("Setting the table...");
	}
	else if (Id == AFNStudio::WillPanel && State == EState::InRoom)
	{
		if (Net->bFn7 && bJoinedOther) { Status = TEXT("Waiting for the host to start the match"); return; }   // only the host starts
		Net->SetReady(true);
		Net->StartMatch();   // we are the host (FN7 host-waits: this second click is the deliberate start)
		Status = TEXT("Starting the match...");
		UE_LOG(LogTemp, Warning, TEXT("FN7 host sent room.start for room %s"), *RoomCode);
	}
	else if (Id == AFNStudio::WillPanel && (State == EState::Compose
		|| (Net->bFn7 && (State == EState::Waiting || State == EState::Resolving || State == EState::Submitted))))
	{
		// Final Night 7: you can write at any time; starting to write slows time (a public window, costs a bolt).
		if (Net->bFn7) { Net->BeginTyping(); AddBanner(TEXT("You slow time to write..."), Dim); }
		SetState(EState::Writing);
		if (WillBox.IsValid()) FSlateApplication::Get().SetKeyboardFocus(WillBox, EFocusCause::SetDirectly);
	}
	else if (State == EState::Choose)
	{
		if (Id == AFNStudio::RibbonA) Choose(0);
		else if (Id == AFNStudio::RibbonB) Choose(1);
		else if (Id == AFNStudio::RibbonC) Choose(2);
	}
}

FReply SFNRootWidget::OnSubmitWill()
{
	if (!WillBox.IsValid() || !Net.IsValid()) return FReply::Handled();
	const FString Will = WillBox->GetText().ToString().TrimStartAndEnd();
	if (Will.IsEmpty() || Will.Len() > WillMax) return FReply::Handled();
	if (State == EState::DuelWriting)
	{
		Net->SubmitDuel(DuelId, Will);
		WillBox->SetText(FText::GetEmpty());
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
		AddBanner(TEXT("Your move: ") + Will, Pale);
		SetState(EState::DuelWaiting);
		return FReply::Handled();
	}
	if (Net->bFn7)
	{
		// Lines: "b: <target>" = lightning, "c: <power> <target>" = a power, "r: ..." = right hand, else left hand.
		TArray<FString> Lines, Left, Right;
		Will.ParseIntoArrayLines(Lines);
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			const FString Low = L.ToLower();
			if (Low.StartsWith(TEXT("b:"))) Net->CastPower(Day, TEXT("lightning"), L.RightChop(2).TrimStartAndEnd());
			else if (Low.StartsWith(TEXT("c:")))
			{
				FString Power, Target;
				if (L.RightChop(2).TrimStartAndEnd().Split(TEXT(" "), &Power, &Target)) Net->CastPower(Day, Power.ToLower(), Target.TrimStartAndEnd());
			}
			else if (Low.StartsWith(TEXT("r:"))) Right.Add(L.RightChop(2).TrimStartAndEnd());
			else if (Low.StartsWith(TEXT("l:"))) Left.Add(L.RightChop(2).TrimStartAndEnd());
			else if (!L.IsEmpty()) Left.Add(L);
		}
		if (Left.Num() || Right.Num()) Net->SubmitOrders(Day, FString::Join(Left, TEXT("\n")), FString::Join(Right, TEXT("\n")));
		else Net->EndTyping();   // casts only: the next time you write, a new slow window opens
	}
	else Net->SubmitAction(Day, Will);
	WillBox->SetText(FText::GetEmpty());
	FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
	AddBanner(TEXT("Your WILL: ") + Will, Pale);
	SetState(EState::Submitted);
	return FReply::Handled();
}

void SFNRootWidget::Choose(int32 Index)
{
	if (!Choices.IsValidIndex(Index) || !Net.IsValid()) return;
	Net->SubmitChoice(Day, Choices[Index].EpisodeUuid);
	if (Choices[Index].EpisodeUuid.StartsWith(TEXT("down:"))) bBlocked = true;
	if (AFNStudio* St = Studio.Get()) St->ChooseRibbon(Index);
	AddBanner(TEXT("You chose: ") + Choices[Index].LocalExperience, Gold);
	SetState(EState::Chosen);
}

// --- event stream ---------------------------------------------------------

void SFNRootWidget::AddBanner(const FString& Text, const FLinearColor& Color)
{
	Banners.Add({ Day > 0 ? FString::Printf(TEXT("%s %d  -  %s"), (Net.IsValid() && Net->bFn7) ? TEXT("Beat") : TEXT("Day"), Day, *Text) : Text, Color, Clock });
	while (Banners.Num() > ((Net.IsValid() && Net->bFn7) ? 5 : 7)) Banners.RemoveAt(0);
	RebuildBanners();
}

void SFNRootWidget::RebuildBanners()
{
	if (!BannerBox.IsValid()) return;
	BannerBox->ClearChildren();
	for (const FBanner& B : Banners)
		BannerBox->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			Framed(SNew(STextBlock).AutoWrapText(true).Font(Font(12)).ColorAndOpacity(B.Color).Text(FText::FromString(B.Text)), FMargin(12.f, 8.f))
		];
}

EActiveTimerReturnType SFNRootWidget::TickAnim(double Time, float Dt)
{
	Clock += Dt;
	if (IntroShown < Intro.Len()) IntroShown += Dt * 60.f;
	if (DiceBannerAt >= 0.0 && Clock >= DiceBannerAt) { DiceBannerAt = -1.0; AddBanner(DiceBanner, Gold); ProgressNote = TEXT("The Dungeon Master is deciding your fate..."); }
	const int32 Before = Banners.Num();
	const double Life = (Net.IsValid() && Net->bFn7) ? 30.0 : 75.0;
	Banners.RemoveAll([this, Life](const FBanner& B) { return Clock - B.Born > Life; });
	if (Banners.Num() != Before) RebuildBanners();
	if (Net.IsValid() && Net->bAutoTest && !bAutoDone && Clock >= AutoNext)
	{
		AutoNext = Clock + 1.5;
		AutoTestStep();
	}
	return EActiveTimerReturnType::Continue;
}

// --- protocol -> table ----------------------------------------------------

void SFNRootWidget::ApplyZone(const TSharedPtr<FJsonObject>& Zone)
{
	if (!Zone.IsValid()) return;
	double Lon = 0.0, Lat = 0.0;
	if (LonLat(Zone, TEXT("center"), Lon, Lat))
		if (AFNStudio* St = Studio.Get()) St->ShowZoneOnGlobe(Lon, Lat, Numb(Zone, TEXT("radius_km"), 20000));
}

void SFNRootWidget::ApplySelfView(const TSharedPtr<FJsonObject>& You)
{
	if (!You.IsValid()) return;
	Health = (int32)Numb(You, TEXT("health"), Health);
	Kills = (int32)Numb(You, TEXT("kills"), Kills);
	DupCharges = (int32)Numb(You, TEXT("duplicator_charges"), DupCharges);
	const FString P = Str(You, TEXT("place"));
	if (!P.IsEmpty()) Place = P;
	bool bAliveNow = true; You->TryGetBoolField(TEXT("alive"), bAliveNow);
	if (const TSharedPtr<FJsonObject> Z = Sub(You, TEXT("zone")))
	{
		bool bInside = true; Z->TryGetBoolField(TEXT("inside"), bInside);
		if (!bInside && Day > 0 && !Place.Contains(TEXT("battle bus"))) bLeftZone = true;
		ZoneLine = bInside
			? FString::Printf(TEXT("Inside the zone (%d km from centre)"), (int32)Numb(Z, TEXT("distance_to_center_km")))
			: FString::Printf(TEXT("OUTSIDE the zone by %d km  -  head %s"), (int32)Numb(Z, TEXT("outside_by_km")), *Str(Z, TEXT("direction_to_center")));
	}
	Bag.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* B = Arr(You, TEXT("bag")))
		for (const TSharedPtr<FJsonValue>& V : *B)
		{
			const TSharedPtr<FJsonObject> I = V->AsObject();
			const int32 N = (int32)Numb(I, TEXT("count"), 1);
			Bag.Add(N > 1 ? FString::Printf(TEXT("%dx %s"), N, *Str(I, TEXT("name"))) : Str(I, TEXT("name")));
		}
	AFNStudio* St = Studio.Get();
	if (!St) return;
	double Lon = 0.0, Lat = 0.0;
	if (LonLat(You, TEXT("lonlat"), Lon, Lat) && !Place.Contains(TEXT("battle bus"))) St->ShowSelfOnGlobe(Lon, Lat, bAliveNow);
	TArray<FFNPlayer> Near;
	TArray<FFNPiece> Board;
	const bool bPlaced = LonLat(You, TEXT("lonlat"), Lon, Lat) && !Place.Contains(TEXT("battle bus"));
	if (bPlaced)
	{
		FFNPiece Me; Me.Lon = Lon; Me.Lat = Lat; Me.Kind = bAliveNow ? 0 : 3;
		Me.Name = bAliveNow ? FString::Printf(TEXT("YOU  %d/10"), Health) : Str(You, TEXT("name"));
		Board.Add(Me);
		MyLon = Lon; MyLat = Lat;
	}
	TSet<FString> Seen;
	if (const TArray<TSharedPtr<FJsonValue>>* N = Arr(You, TEXT("nearby")))
		for (const TSharedPtr<FJsonValue>& V : *N)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FFNPlayer P2; P2.Name = Str(O, TEXT("name"));
			const double Km = Numb(O, TEXT("distance_km"));
			const FString Dir = Str(O, TEXT("direction"));
			P2.Location = FString::Printf(TEXT("%d km %s"), (int32)Km, *Dir);
			Near.Add(P2);
			// Place them on the board from distance + compass direction
			static const TCHAR* Winds[16] = { TEXT("N"), TEXT("NNE"), TEXT("NE"), TEXT("ENE"), TEXT("E"), TEXT("ESE"), TEXT("SE"), TEXT("SSE"), TEXT("S"), TEXT("SSW"), TEXT("SW"), TEXT("WSW"), TEXT("W"), TEXT("WNW"), TEXT("NW"), TEXT("NNW") };
			int32 W = 0; for (int32 k = 0; k < 16; ++k) if (Dir.Equals(Winds[k], ESearchCase::IgnoreCase)) W = k;
			if (bPlaced)
			{
				const double Th = FMath::DegreesToRadians(W * 22.5), Dl = Km / 6371.0, La1 = FMath::DegreesToRadians(Lat), Lo1 = FMath::DegreesToRadians(Lon);
				const double La2 = FMath::Asin(FMath::Sin(La1) * FMath::Cos(Dl) + FMath::Cos(La1) * FMath::Sin(Dl) * FMath::Cos(Th));
				const double Lo2 = Lo1 + FMath::Atan2(FMath::Sin(Th) * FMath::Sin(Dl) * FMath::Cos(La1), FMath::Cos(Dl) - FMath::Sin(La1) * FMath::Sin(La2));
				FFNPiece Pc; Pc.Name = P2.Name; Pc.Lon = FMath::RadiansToDegrees(Lo2); Pc.Lat = FMath::RadiansToDegrees(La2); Pc.Kind = 1; Pc.Color = 1 + Board.Num() % 3;
				Board.Add(Pc); Seen.Add(Pc.Name);
			}
		}
	// Everyone else the server tells us about (the practice Theatre sends the whole map)
	if (const TArray<TSharedPtr<FJsonValue>>* Wd = Arr(You, TEXT("world")))
		for (const TSharedPtr<FJsonValue>& V : *Wd)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FFNPiece Pc; Pc.Name = Str(O, TEXT("name"));
			if (Seen.Contains(Pc.Name) || Pc.Name == Str(You, TEXT("name"))) continue;
			if (!LonLat(O, TEXT("lonlat"), Pc.Lon, Pc.Lat)) continue;
			bool bA = true; O->TryGetBoolField(TEXT("alive"), bA);
			const bool bNpc = Str(O, TEXT("kind")) == TEXT("npc");
			Pc.Kind = !bA ? 3 : (bNpc ? 2 : 1);
			Pc.Color = 1 + Board.Num() % 3;
			Board.Add(Pc);
		}
	St->ShowWorld(Board);
	St->ShowBuilt(Bag);
	St->ShowNearby(Near);
}

// --- Final Night 7 ----------------------------------------------------------

void SFNRootWidget::ApplyFn7View(const TSharedPtr<FJsonObject>& View)
{
	if (!View.IsValid()) return;
	const FString Me = Str(View, TEXT("demigod"));
	if (!Me.IsEmpty()) Fn7Demigod = Me;
	const TSharedPtr<FJsonObject> You = Sub(View, TEXT("you"));
	if (You.IsValid()) Health = FMath::RoundToInt(Numb(You, TEXT("health"), Health));
	bool bAliveNow = true; View->TryGetBoolField(TEXT("alive"), bAliveNow);
	if (!bAliveNow && !bEliminated)
	{
		bEliminated = true;
		Health = 0;
		FallPlace = FMath::Max(2, Alive + 1);
		AddBanner(TEXT("You have fallen. You keep watching the night as a spectator."), Red);
		SetState(EState::Spectating);
	}
	double Lon = 0.0, Lat = 0.0;
	if (You.IsValid() && LonLat(You, TEXT("pos"), Lon, Lat))
	{
		MyLon = Lon; MyLat = Lat;
		if (AFNStudio* St = Studio.Get()) St->ShowSelfOnGlobe(Lon, Lat, bAliveNow);
	}
}

void SFNRootWidget::ApplyFn7Public(const TSharedPtr<FJsonObject>& PV)
{
	if (!PV.IsValid()) return;
	Alive = (int32)Numb(PV, TEXT("alive"), Alive);
	if (const TSharedPtr<FJsonObject> Arena = Sub(PV, TEXT("arena")))
	{
		double Lon = 0.0, Lat = 0.0;
		if (LonLat(Arena, TEXT("center"), Lon, Lat))
			if (AFNStudio* St = Studio.Get()) St->ShowZoneOnGlobe(Lon, Lat, Numb(Arena, TEXT("radius_km"), 20000));
	}
	TArray<FFNPiece> Board;
	if (const TArray<TSharedPtr<FJsonValue>>* Dg = Arr(PV, TEXT("demigods")))
		for (const TSharedPtr<FJsonValue>& V : *Dg)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FFNPiece Pc;
			if (!O.IsValid() || !LonLat(O, TEXT("pos"), Pc.Lon, Pc.Lat)) continue;   // the fallen have no position
			const bool bMine = Str(O, TEXT("ref")) == Fn7Demigod;
			if (bMine) { const FString Pl = Str(O, TEXT("place")), Rg = Str(O, TEXT("region")); Place = Rg.IsEmpty() || Rg == Pl ? Pl : Pl + TEXT(", ") + Rg; }
			const TSharedPtr<FJsonValue> SeatV = O->TryGetField(TEXT("seat"));
			const bool bAi = !SeatV.IsValid() || SeatV->IsNull();   // seat null = the built-in AI's demigod
			Pc.Name = bMine ? FString::Printf(TEXT("YOU  %d/10"), Health) : Str(O, TEXT("name"));
			Pc.Ref = Str(O, TEXT("ref"));
			Pc.Kind = bMine ? 0 : (bAi ? 2 : 1);
			Pc.Color = 1 + Board.Num() % 3;
			Board.Add(Pc);
		}
	if (AFNStudio* St = Studio.Get()) St->ShowWorld(Board);
	LastPublicView = PV;
	PushSeats();
}

void SFNRootWidget::RebuildPowerBar(const TArray<TSharedPtr<FJsonValue>>& Powers)
{
	if (!PowerBar.IsValid()) return;
	PowerBar->ClearChildren();
	PowersShown = Powers.Num();
	for (const TSharedPtr<FJsonValue>& V : Powers)
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const FString Slug = Str(P, TEXT("slug")), Name = Str(P, TEXT("name"));
		if (Slug.IsEmpty() || Slug == TEXT("custom")) continue;   // a custom power needs words: type it as "c: custom ..." in the box
		const int32 Cost = (int32)Numb(P, TEXT("cost"), 1);
		PowerBar->AddSlot().AutoWidth().Padding(3.f, 0.f)
		[
			SNew(SButton).ContentPadding(FMargin(10.f, 6.f))
			.ButtonColorAndOpacity_Lambda([this, Slug]() { return ArmedPower == Slug ? FLinearColor(1.f, 0.55f, 0.2f) : FLinearColor(0.25f, 0.22f, 0.2f); })
			.IsEnabled_Lambda([this, Cost]() { return Fn7Bolts >= Cost; })
			.ToolTipText(FText::FromString(FString::Printf(TEXT("%s: costs %d bolt(s), strikes %d km around the point"), *Name, Cost, (int32)Numb(P, TEXT("radius_km")))))
			.OnClicked_Lambda([this, Slug, Name]()
			{
				ArmedPower = ArmedPower == Slug ? FString() : Slug;
				ArmedPowerName = Name;
				if (AFNStudio* St = Studio.Get()) St->PlaySfx(EFNSfx::Click, 0.6f);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Font(Font(11, TEXT("Bold"))).ColorAndOpacity(Pale).Text(FText::FromString(FString::Printf(TEXT("%s  %d"), *Name, Cost)))
			]
		];
	}
}

void SFNRootWidget::OnGlobePicked(double Lon, double Lat)
{
	if (ArmedPower.IsEmpty() || !Net.IsValid() || !Net->bFn7 || bEliminated) return;   // otherwise a globe click only moves the table view
	Net->CastAt(Day, ArmedPower, Lon, Lat);
	AddBanner(FString::Printf(TEXT("You throw %s at %.1f, %.1f"), *ArmedPowerName, Lat, Lon), Orange);
	ArmedPower.Reset();
}

void SFNRootWidget::FetchPortrait(const FString& Hash, const FString& Url)
{
	if (Hash.IsEmpty() || Url.IsEmpty() || PortraitTex.Contains(Hash) || PortraitPending.Contains(Hash) || !Net.IsValid()) return;
	// Images are immutable by hash: the disk cache (Saved/FN7Media/<hash>.<ext>) is tried first, so each image downloads once ever.
	FString Ext = FPaths::GetExtension(Url.Left(Url.Find(TEXT("?")) == INDEX_NONE ? Url.Len() : Url.Find(TEXT("?"))));
	if (Ext.IsEmpty()) Ext = TEXT("png");
	const FString CachePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FN7Media"), Hash + TEXT(".") + Ext);
	{
		TArray<uint8> Disk;
		if (FFileHelper::LoadFileToArray(Disk, *CachePath))
		{
			UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(TArrayView64<const uint8>(Disk.GetData(), Disk.Num()));
			UE_LOG(LogTemp, Warning, TEXT("FNPortrait: disk cache %s, %d bytes, decode %s"), *Hash.Left(12), Disk.Num(), Tex ? TEXT("OK") : TEXT("FAILED"));
			if (Tex) { PortraitTex.Add(Hash, TStrongObjectPtr<UTexture2D>(Tex)); return; }
		}
	}
	PortraitPending.Add(Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Net->MediaUrl(Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1"));
	TWeakPtr<SFNRootWidget> Weak = SharedThis(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Hash, CachePath](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNRootWidget> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->PortraitPending.Remove(Hash);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
		{
			UE_LOG(LogTemp, Warning, TEXT("FNPortrait: fetch %s failed (HTTP %d); retried next beat"), *Hash.Left(12), Resp.IsValid() ? Resp->GetResponseCode() : 0);
			return;   // a placeholder stays until the next beat retries
		}
		const TArray<uint8>& Bytes = Resp->GetContent();
		UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()));   // PNG/JPG; an SVG placeholder fails and is skipped
		UE_LOG(LogTemp, Warning, TEXT("FNPortrait: fetched %s HTTP 200, %d bytes, decode %s"), *Hash.Left(12), Bytes.Num(), Tex ? TEXT("OK") : TEXT("FAILED"));
		if (!Tex) return;
		FFileHelper::SaveArrayToFile(Bytes, *CachePath);   // next time (and next match) it loads from disk
		Self->PortraitTex.Add(Hash, TStrongObjectPtr<UTexture2D>(Tex));
		Self->PushSeats();
	});
	Req->ProcessRequest();
}

void SFNRootWidget::RebuildPortraitStrip(const TArray<FFNSeatInfo>& Seats)
{
	if (!PortraitStrip.IsValid()) return;
	PortraitStrip->ClearChildren();
	for (const FFNSeatInfo& S : Seats)
	{
		TSharedPtr<FSlateBrush> Brush;
		if (S.Portrait)
		{
			if (TSharedPtr<FSlateBrush>* Found = PortraitBrush.Find(S.Portrait)) Brush = *Found;
			else
			{
				Brush = MakeShared<FSlateBrush>();
				Brush->SetResourceObject(S.Portrait);
				Brush->ImageSize = FVector2D(112.f, 112.f);
				Brush->DrawAs = ESlateBrushDrawType::Image;
				PortraitBrush.Add(S.Portrait, Brush);
				UE_LOG(LogTemp, Warning, TEXT("FNPortrait: %s %s texture %dx%d drawn"), *S.Ref, *S.Name, S.Portrait->GetSizeX(), S.Portrait->GetSizeY());
			}
		}
		const FLinearColor Aura = !S.bAlive ? FLinearColor(0.3f, 0.3f, 0.3f) : (S.bHuman ? FLinearColor(1.f, 0.8f, 0.3f) : FLinearColor(0.2f, 0.9f, 1.f));
		const FString Label = FString::Printf(TEXT("%s%s\n%s"), *S.Name, S.bMine ? TEXT("  (you)") : TEXT(""), !S.bAlive ? TEXT("FALLEN") : (S.bHuman ? (S.bConnected ? TEXT("HUMAN") : TEXT("HUMAN (away)")) : TEXT("AI")));
		TSharedRef<SWidget> Face = Brush.IsValid()
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush.Get()))
			: StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(White()).BorderBackgroundColor(S.Color * 0.35f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Pale).Justification(ETextJustify::Center).Text(FText::FromString(TEXT("portrait\ncoming..."))) ]);
		PortraitStrip->AddSlot().AutoWidth().Padding(6.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Aura).Padding(3.f)
				[ SNew(SBox).WidthOverride(112.f).HeightOverride(112.f)[Face] ]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)
			[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Aura).Justification(ETextJustify::Center).Text(FText::FromString(Label)) ]
		];
	}
}

void SFNRootWidget::ShowDemigodCard(const FString& Ref, const FString& Name)
{
	AFNStudio* St = Studio.Get();
	if (!St || !LastPublicView.IsValid()) return;
	UE_LOG(LogTemp, Warning, TEXT("FNPick: card for %s ref=%s"), *Name, *Ref);
	TSharedPtr<FJsonObject> G;
	if (const TArray<TSharedPtr<FJsonValue>>* Dg = Arr(LastPublicView, TEXT("demigods")))
		for (const TSharedPtr<FJsonValue>& V : *Dg)
			if (V->AsObject().IsValid() && (Str(V->AsObject(), TEXT("ref")) == Ref || (Ref.IsEmpty() && Str(V->AsObject(), TEXT("name")) == Name))) G = V->AsObject();
	if (!G.IsValid()) return;
	const FString R = Str(G, TEXT("ref"));
	bool bAlive = true; G->TryGetBoolField(TEXT("alive"), bAlive);
	const TSharedPtr<FJsonValue> SeatV = G->TryGetField(TEXT("seat"));
	const bool bHuman = SeatV.IsValid() && !SeatV->IsNull();
	FString Body = bAlive
		? FString::Printf(TEXT("%s demigod, alive.  %d lightning bolts.\nStands in %s, %s.\n"), bHuman ? TEXT("Human") : TEXT("AI"), (int32)Numb(G, TEXT("bolts")), *Str(G, TEXT("place")), *Str(G, TEXT("region")))
		: FString::Printf(TEXT("FALLEN on beat %d: %s\n"), (int32)Numb(G, TEXT("eliminated_beat")), *Str(G, TEXT("cause")));
	if (R == Fn7Demigod) Body += FString::Printf(TEXT("Your health: %d/10.\n"), Health);   // rivals' health is never sent (hidden information)
	// Territory: its strongest regions
	TArray<TPair<double, FString>> Inf;
	if (const TArray<TSharedPtr<FJsonValue>>* In = Arr(LastPublicView, TEXT("influence")))
		for (const TSharedPtr<FJsonValue>& V : *In)
			if (Str(V->AsObject(), TEXT("demigod_ref")) == R) Inf.Add({ Numb(V->AsObject(), TEXT("share")), Str(V->AsObject(), TEXT("region")) });
	Inf.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B) { return A.Key > B.Key; });
	if (Inf.Num())
	{
		Body += TEXT("\nTERRITORY: ");
		for (int32 i = 0; i < Inf.Num() && i < 4; ++i) Body += FString::Printf(TEXT("%s%s %d%%"), i ? TEXT(", ") : TEXT(""), *Inf[i].Value, FMath::RoundToInt(Inf[i].Key * 100.0));
		Body += TEXT("\n");
	}
	// Armies and their captains
	int32 NA = 0;
	if (const TArray<TSharedPtr<FJsonValue>>* Ar = Arr(LastPublicView, TEXT("armies")))
		for (const TSharedPtr<FJsonValue>& V : *Ar)
			if (Str(V->AsObject(), TEXT("demigod_ref")) == R && NA++ < 4)
				Body += FString::Printf(TEXT("%sARMY %s: %d strong in %s\n"), NA == 1 ? TEXT("\n") : TEXT(""), *Str(V->AsObject(), TEXT("name")), (int32)Numb(V->AsObject(), TEXT("size")), *Str(V->AsObject(), TEXT("region")));
	// Last things it did (public events naming it)
	int32 NE = 0;
	if (LastOutcome.IsValid())
		if (const TArray<TSharedPtr<FJsonValue>>* Ev = Arr(LastOutcome, TEXT("events")))
			for (const TSharedPtr<FJsonValue>& V : *Ev)
			{
				bool bNamed = false;
				if (const TArray<TSharedPtr<FJsonValue>>* Rf = Arr(V->AsObject(), TEXT("refs"))) for (const TSharedPtr<FJsonValue>& X : *Rf) if (X->AsString() == R) bNamed = true;
				if (bNamed && NE++ < 4) Body += FString::Printf(TEXT("%s- %s\n"), NE == 1 ? TEXT("\nLAST BEAT:\n") : TEXT(""), *Str(V->AsObject(), TEXT("text")));
			}
	St->ShowEaselText(Str(G, TEXT("name")).ToUpper(), Body);
	if (St) St->PlaySfx(EFNSfx::Click, 0.5f);
}

void SFNRootWidget::PushSeats()
{
	AFNStudio* St = Studio.Get();
	if (!St || !LastPublicView.IsValid()) return;
	TArray<FFNSeatInfo> Seats;
	// Debug: Config/FN7PortraitTest.txt lists media paths (one per line); seat i shows path i through the same fetch/cache/decode.
	TArray<FString> TestPaths;
	{
		FString Txt;
		if (FFileHelper::LoadFileToString(Txt, *FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7PortraitTest.txt")))) Txt.ParseIntoArrayLines(TestPaths);
		for (FString& L : TestPaths) L.TrimStartAndEndInline();
		TestPaths.RemoveAll([](const FString& L) { return L.IsEmpty() || L.StartsWith(TEXT("#")); });
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Dg = Arr(LastPublicView, TEXT("demigods")))
		for (const TSharedPtr<FJsonValue>& V : *Dg)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FFNSeatInfo S;
			S.Ref = Str(O, TEXT("ref"));
			S.Name = Str(O, TEXT("name"));
			const TSharedPtr<FJsonValue> SeatV = O->TryGetField(TEXT("seat"));
			S.Seat = (SeatV.IsValid() && !SeatV->IsNull()) ? (int32)SeatV->AsNumber() : -1;
			const FString Ctl = Str(O, TEXT("controller"));
			S.bHuman = Ctl.IsEmpty() ? S.Seat >= 0 : Ctl == TEXT("human");
			bool bConn = true;
			if (!O->TryGetBoolField(TEXT("connected"), bConn) && S.Seat >= 0 && SeatConnected.Contains(S.Seat)) bConn = SeatConnected[S.Seat];
			S.bConnected = bConn;
			S.bMine = S.Ref == Fn7Demigod;
			O->TryGetBoolField(TEXT("alive"), S.bAlive);
			const uint32 H = GetTypeHash(S.Ref);   // a stable colour per demigod
			S.Color = FLinearColor::MakeFromHSV8((uint8)(H % 256), 170, 230);
			if (TestPaths.IsValidIndex(Seats.Num()))
			{
				const FString TP = TestPaths[Seats.Num()];
				const FString TH = FPaths::GetBaseFilename(TP.Left(TP.Find(TEXT("?")) == INDEX_NONE ? TP.Len() : TP.Find(TEXT("?"))));
				FetchPortrait(TH, TP);
				if (const TStrongObjectPtr<UTexture2D>* Tx = PortraitTex.Find(TH)) S.Portrait = Tx->Get();
			}
			else
			for (const TCHAR* Kind : { TEXT("portrait_image"), TEXT("diorama_image") })
				if (const TSharedPtr<FJsonObject> Img = Sub(O, Kind))
				{
					const FString Hash = Str(Img, TEXT("hash"));
					if (Str(Img, TEXT("status")) == TEXT("ready")) FetchPortrait(Hash, Str(Img, TEXT("url")));
					if (const TStrongObjectPtr<UTexture2D>* Tx = PortraitTex.Find(Hash))
						(FCString::Strcmp(Kind, TEXT("portrait_image")) == 0 ? S.Portrait : S.Diorama) = Tx->Get();
				}
			Seats.Add(S);
		}
	St->ShowSeats(Seats);
	RebuildPortraitStrip(Seats);
}

bool SFNRootWidget::HandleFn7(const FString& T, TSharedPtr<FJsonObject> D)
{
	AFNStudio* St = Studio.Get();
	if (T == TEXT("room.snapshot") || T == TEXT("room.updated"))
	{
		// Presence per seat: a human who leaves (or comes back) flips their aura live.
		if (const TArray<TSharedPtr<FJsonValue>>* Ss = Arr(D, TEXT("seats")))
			for (const TSharedPtr<FJsonValue>& V : *Ss)
			{
				bool bC = true; V->AsObject()->TryGetBoolField(TEXT("connected"), bC);
				bool bBot = false; V->AsObject()->TryGetBoolField(TEXT("is_bot"), bBot);
				SeatConnected.Add((int32)Numb(V->AsObject(), TEXT("seat")), bC && !bBot);
			}
		PushSeats();   // then fall through to the room handling below
	}
	if (T == TEXT("match.started"))
	{
		Days = (int32)Numb(Sub(D, TEXT("rules")), TEXT("beats_total"), 100);
		SetState(EState::Waiting);
		// (no banner: the intro note and the WILL hint already explain how to play)
		return true;
	}
	// One click to play: connected -> create a room -> ready + start (no hero cards in Final Night 7).
	if (T == TEXT("auth.ok"))
	{
		SetState(EState::Connected);
		Status = TEXT("FINAL NIGHT 7  -  connected to the Theatre as ") + Str(D, TEXT("display_name"));
		UE_LOG(LogTemp, Warning, TEXT("FN7 auth.ok player_id=%s display_name=%s"), *Str(D, TEXT("player_id")), *Str(D, TEXT("display_name")));
		const FString Join = Net.IsValid() ? Net->ResolveJoinCode() : FString();
		bJoinedOther = !Join.IsEmpty();
		if (St) St->ShowEaselText(TEXT("FINAL NIGHT 7"), bJoinedOther
			? FString::Printf(TEXT("Connected to Chris's Theatre server.\n\nJoining room %s. You are READY; the host starts the match."), *Join)
			: FString(TEXT("Connected to Chris's Theatre server.\n\nSetting the table: four demigods on the real Earth. The match starts by itself.")));
		if (Net.IsValid() && RoomCode.IsEmpty()) { if (bJoinedOther) Net->JoinRoom(Join); else Net->CreateRoom(); }
		return true;
	}
	if (T == TEXT("room.snapshot") && Str(D, TEXT("state")) == TEXT("open") && Net.IsValid())
	{
		RoomCode = Str(D, TEXT("code"));
		SetState(EState::InRoom);
		Status = FString::Printf(TEXT("FINAL NIGHT 7  -  room %s%s"), *RoomCode, bJoinedOther ? TEXT("  (joined: waiting for the host to start)") : TEXT(""));
		FString Seats;
		if (const TArray<TSharedPtr<FJsonValue>>* S = Arr(D, TEXT("seats")))
			for (const TSharedPtr<FJsonValue>& V : *S) Seats += FString::Printf(TEXT("[%d %s] "), (int32)Numb(V->AsObject(), TEXT("seat")), *Str(V->AsObject(), TEXT("display_name")));
		UE_LOG(LogTemp, Warning, TEXT("FN7 room.snapshot code=%s seats=%s"), *RoomCode, *Seats);
		Net->SetReady(true);
		const bool bHostWaits = FPaths::FileExists(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7HostOnly.txt")));
		if (!bJoinedOther && bHostWaits)
		{
			UE_LOG(LogTemp, Warning, TEXT("FN7 room created code=%s (host waits: click again to start)"), *RoomCode);
			Status = FString::Printf(TEXT("FINAL NIGHT 7  -  ROOM CODE %s  -  you host: click the table again to START"), *RoomCode);
			if (St) St->ShowEaselText(FString::Printf(TEXT("ROOM %s"), *RoomCode), FString::Printf(TEXT("Your room is open. Give the code %s to the other players.\n\nWhen everyone has joined, click the table to start the match."), *RoomCode));
		}
		else if (!bJoinedOther) Net->StartMatch();   // our own room: we are the host; the server's AI plays the other demigods
		return true;
	}
	if (T == TEXT("phase.changed") && State == EState::Writing)
	{
		// Flow beats keep running while you type (your slow window holds your orders): stay in the box.
		Day = (int32)Numb(D, TEXT("round"), Day);
		Deadline = 0.0;
		return true;
	}
	if (T == TEXT("round.started"))
	{
		const TSharedPtr<FJsonObject> Pub = Sub(D, TEXT("public")), Priv = Sub(D, TEXT("private"));
		Health = FMath::RoundToInt(Numb(Priv, TEXT("health"), Health));
		bool bOut = false; if (Priv.IsValid()) Priv->TryGetBoolField(TEXT("outside_arena"), bOut);
		if (bOut) AddBanner(TEXT("You are OUTSIDE the arena: Time's clock hurts you every beat. Order your hand to move inward."), Red);
		if (const TArray<TSharedPtr<FJsonValue>>* E = Arr(Pub, TEXT("eliminated")))
			for (const TSharedPtr<FJsonValue>& V : *E) AddBanner(V->AsString() + TEXT(" fell at the start of the beat"), Red);
		return true;
	}
	if (T == TEXT("round.brief"))
	{
		const TSharedPtr<FJsonObject> Pub = Sub(D, TEXT("public")), Priv = Sub(D, TEXT("private"));
		Day = (int32)Numb(Pub, TEXT("beat"), Day);
		Days = (int32)Numb(Pub, TEXT("beats_total"), Days);
		Alive = (int32)Numb(Pub, TEXT("alive"), Alive);
		WillMax = 4000;   // two hands x 2000 characters
		ApplyFn7View(Priv);
		const TSharedPtr<FJsonObject> Arena = Sub(Pub, TEXT("arena"));
		double Lon = 0.0, Lat = 0.0;
		if (Arena.IsValid() && LonLat(Arena, TEXT("center"), Lon, Lat) && St) St->ShowZoneOnGlobe(Lon, Lat, Numb(Arena, TEXT("radius_km"), 20000));
		const TSharedPtr<FJsonObject> You = Sub(Priv, TEXT("you"));
		const TSharedPtr<FJsonObject> Ent = Sub(Pub, TEXT("entropy"));
		Fn7Bolts = (int32)Numb(You, TEXT("bolts"), Fn7Bolts);
		if (const TArray<TSharedPtr<FJsonValue>>* Pw = Arr(Pub, TEXT("powers"))) if (Pw->Num() != PowersShown) RebuildPowerBar(*Pw);
		Fn7Entropy = Str(Ent, TEXT("key")).ToUpper();
		Fn7Arena = FString::Printf(TEXT("Time's circle: %d km around %s (stay inside)"), (int32)Numb(Arena, TEXT("radius_km")), *Str(Arena, TEXT("city")));
		Fn7Brief = FString::Printf(TEXT("%d demigods alive. Time's circle: %d km, closing on %s.\nYou: health %d, %d lightning bolts.  Entropy: %s.\n\n"
			"Type orders to your hands (left by default; start a line with r: for the right hand).\nb: <place or lon,lat> throws a lightning bolt.  c: <power> <place> casts a power."),
			Alive, (int32)Numb(Arena, TEXT("radius_km")), *Str(Arena, TEXT("city")), Health, (int32)Numb(You, TEXT("bolts")), *Str(Ent, TEXT("key")));
		if (St) { St->PlaySfx(EFNSfx::Chime, 0.6f); St->SetMusicLevel(Days > 1 ? (float)(Day - 1) / (float)(Days - 1) : 0.f); St->ShowEaselText(Str(Pub, TEXT("title")).ToUpper(), Fn7Brief); }
		return true;
	}
	if (T == TEXT("round.outcome"))
	{
		const TSharedPtr<FJsonObject> Pub = Sub(D, TEXT("public"));
		LastOutcome = Pub;
		Alive = (int32)Numb(Pub, TEXT("alive"), Alive);
		const FString Cap = Str(Pub, TEXT("previous_caption"));
		if (!Cap.IsEmpty()) { AddBanner(Cap, Pale); CutLines.Add(Cap); CutSpots.Add(FVector2D(MyLon, MyLat)); }
		if (const TArray<TSharedPtr<FJsonValue>>* Om = Arr(Pub, TEXT("omens")))
			for (int32 i = 0; i < Om->Num() && i < 2; ++i) AddBanner(Str((*Om)[i]->AsObject(), TEXT("god")) + TEXT(": ") + Str((*Om)[i]->AsObject(), TEXT("speech")), Gold);
		if (const TArray<TSharedPtr<FJsonValue>>* Ev = Arr(Pub, TEXT("events")))
			for (int32 i = 0; i < Ev->Num(); ++i)
			{
				const TSharedPtr<FJsonObject> E = (*Ev)[i]->AsObject();
				if (i < 3) AddBanner(Str(E, TEXT("text")), FLinearColor(0.55f, 0.85f, 1.f));
				double Lo = 0.0, La = 0.0;   // the big world events strike the globe too
				if (St && Numb(E, TEXT("magnitude")) >= 6 && Str(E, TEXT("kind")) != TEXT("cast") && LonLat(E, TEXT("at"), Lo, La)) St->FlashAt(Lo, La);
			}
		if (const TArray<TSharedPtr<FJsonValue>>* E = Arr(Pub, TEXT("eliminated")))
		{
			if (St && E->Num() > 0) St->PlaySfx(EFNSfx::Snuff, 0.8f, 2.5f);
			for (const TSharedPtr<FJsonValue>& V : *E) AddBanner(TEXT("ELIMINATED: ") + V->AsString(), Red);
		}
		return true;
	}
	if (T == TEXT("round.private"))
	{
		const TSharedPtr<FJsonObject> Priv = Sub(D, TEXT("private"));
		const int32 HealthBefore = Health;
		if (const TSharedPtr<FJsonObject> You = Sub(Priv, TEXT("you"))) Health = FMath::RoundToInt(Numb(You, TEXT("health"), Health));
		if (Health < HealthBefore) AddBanner(FString::Printf(TEXT("You lost %d health  (%d/10 left)"), HealthBefore - Health, FMath::Max(0, Health)), Red);
		FString Body;
		if (const TArray<TSharedPtr<FJsonValue>>* Hs = Arr(Priv, TEXT("hands")))
			for (int32 i = 0; i < Hs->Num(); ++i)
			{
				const FString Said = Str((*Hs)[i]->AsObject(), TEXT("said"));
				if (!Said.IsEmpty()) Body += FString::Printf(TEXT("%s hand: %s\n"), i ? TEXT("Right") : TEXT("Left"), *Said);
			}
		if (const TArray<TSharedPtr<FJsonValue>>* Nw = Arr(Priv, TEXT("news")))
			for (int32 i = 0; i < Nw->Num() && i < 6; ++i) Body += TEXT("- ") + Str((*Nw)[i]->AsObject(), TEXT("text")) + TEXT("\n");
		if (St && !Body.IsEmpty()) St->ShowEaselText(FString::Printf(TEXT("[beat %d]  what you learned"), Day), Body + TEXT("\n") + Fn7Brief);
		return true;
	}
	if (T == TEXT("round.committed"))
	{
		ApplyFn7Public(Sub(D, TEXT("public_state")));
		if (!bEliminated && Health > 0) DaysLived = FMath::Max(DaysLived, Day);
		return true;
	}
	if (T == TEXT("round.private_state")) { ApplyFn7View(Sub(D, TEXT("state"))); return true; }
	if (T == TEXT("match.snapshot"))
	{
		Day = (int32)Numb(D, TEXT("round"), Day);
		ApplyFn7Public(Sub(D, TEXT("public_state")));
		ApplyFn7View(Sub(Sub(D, TEXT("you")), TEXT("private_state")));
		return true;
	}
	if (T == TEXT("window.opened")) { Fn7WindowDeadline = Numb(D, TEXT("deadline"), 0.0); return true; }
	if (T == TEXT("window.closed"))
	{
		Fn7WindowDeadline = 0.0;
		const FString Why = Str(D, TEXT("reason"));
		AddBanner(Why == TEXT("submitted") ? TEXT("Time snaps back: the orders are sealed") : TEXT("Time snaps back"), Dim);
		return true;
	}
	if (T == TEXT("phase.event"))
	{
		const TSharedPtr<FJsonObject> P = Sub(D, TEXT("public"));
		const FString Ev = Str(P, TEXT("event"));
		if (Ev == TEXT("slowed")) { Fn7WindowBy = Str(P, TEXT("by")); AddBanner(Str(P, TEXT("text")).ToUpper() + TEXT(": anyone may write now, until they press Enter"), Cyan); if (St) St->PlaySfx(EFNSfx::Whoosh, 0.7f); }
		else if (Ev == TEXT("streak"))
		{
			AddBanner(FString::Printf(TEXT("%s casts %s at %s (%s)"), *Str(P, TEXT("by")), *Str(P, TEXT("power_name")), *Str(P, TEXT("place")), *Str(P, TEXT("region"))), Orange);
			double Lo = 0.0, La = 0.0;
			if (St && LonLat(P, TEXT("at"), Lo, La)) St->FlashAt(Lo, La);
			if (St) St->PlaySfx(EFNSfx::Boom, 0.6f);
		}
		return true;
	}
	if (T == TEXT("action.received"))
	{
		bool bOk = true; D->TryGetBoolField(TEXT("accepted"), bOk);
		const FString Why = Str(D, TEXT("reason"));
		if (!bOk && Why == TEXT("cut")) { AddBanner(TEXT("YOUR ORDERS WERE CUT! Whoever slowed time pressed Enter first."), Red); AddBanner(TEXT("CUT OFF  -  write faster next time"), Red); if (St) St->PlaySfx(EFNSfx::Snuff, 1.f); }
		else if (!bOk) AddBanner(TEXT("Refused: ") + Why, Red);
		else Status = TEXT("Orders received");
		return true;
	}
	if (T == TEXT("seat.revived")) { AddBanner(TEXT("Back in play: ") + Str(D, TEXT("reason")), Gold); return true; }
	return false;   // everything else (auth, room, seat.eliminated, match.ended, errors) is shared with the old client
}

void SFNRootWidget::OnServerMessage(const FString& T, TSharedPtr<FJsonObject> D)
{
	AFNStudio* St = Studio.Get();
	if (Net.IsValid() && Net->bFn7 && HandleFn7(T, D)) return;

	if (T == TEXT("auth.ok"))
	{
		SetState(EState::Connected);
		if (St && Net.IsValid() && Net->HeroClass.IsEmpty())
		{
			HeroChoices.Reset();
			auto Hero = [this](const TCHAR* Id, const TCHAR* Kind, const TCHAR* Label) { FFNEpisode E; E.EpisodeUuid = Id; E.RoundOutcome = Kind; E.LocalExperience = Label; HeroChoices.Add(E); };
			Hero(TEXT("Warrior"), TEXT("down"), TEXT("WARRIOR\nTough as old iron.\n+3 when you fight, guard, build or hold the line."));
			Hero(TEXT("Mage"), TEXT("up"), TEXT("MAGE\nWishes bend to you.\n+3 for spells, wishes, teleports and potions."));
			Hero(TEXT("Rogue"), TEXT("pass"), TEXT("ROGUE\nNobody sees you coming.\n+3 when you sneak, steal, hide or escape."));
			St->ShowRibbons(HeroChoices);
			St->ShowEaselText(TEXT("CHOOSE YOUR HERO"), TEXT("Three heroes wait at the table. Click one of the cards.\n\nThe Dungeon Master rolls a d20 for everything you do. Plans that suit your hero get +3."));
		}
		Status = TEXT("Connected as ") + Str(D, TEXT("display_name"));
	}
	else if (T == TEXT("room.snapshot") || T == TEXT("room.updated"))
	{
		RoomCode = Str(D, TEXT("code"));
		const FString RS = Str(D, TEXT("state"));
		if (RS == TEXT("open") && (State == EState::Connected || State == EState::InRoom))
		{
			SetState(EState::InRoom);
			int32 Humans = 0, Bots = 0;
			if (const TArray<TSharedPtr<FJsonValue>>* S = Arr(D, TEXT("seats")))
				for (const TSharedPtr<FJsonValue>& V : *S) { bool B = false; V->AsObject()->TryGetBoolField(TEXT("is_bot"), B); (B ? Bots : Humans)++; }
			if (St) St->ShowEaselText(TEXT("THE TABLE IS SET"), FString::Printf(TEXT("Room %s.  %d player(s), %d bot(s) at the table. More AI contestants fill the world.\n\nClick WILL when you are ready."), *RoomCode, Humans, Bots));
		}
	}
	else if (T == TEXT("room.countdown"))
	{
		SetState(EState::Countdown);
	}
	else if (T == TEXT("match.started"))
	{
		const TSharedPtr<FJsonObject> Rules = Sub(D, TEXT("rules"));
		Days = (int32)Numb(Rules, TEXT("rounds"), 100);
		SetState(EState::Waiting);
		AddBanner(TEXT("The match has begun"), Gold);
	}
	else if (T == TEXT("phase.changed"))
	{
		Day = (int32)Numb(D, TEXT("round"), Day);
		const FString Ph = Str(D, TEXT("phase"));
		Deadline = Numb(D, TEXT("deadline"), 0);
		if (bEliminated) { SetState(EState::Spectating); return; }
		if (Ph == TEXT("start") || Ph == TEXT("brief")) { SetState(EState::Waiting); if (St) St->ClearRibbons(); Choices.Reset(); }
		else if (Ph == TEXT("compose")) { if (State != EState::Submitted) SetState(EState::Compose); }
		else if (Ph == TEXT("resolve") || Ph == TEXT("reveal")) { ProgressNote.Reset(); SetState(EState::Resolving); }
		else if (Ph == TEXT("choose")) { if (Choices.Num()) SetState(EState::Choose); }
		else if (Ph == TEXT("commit")) { if (St) St->ClearRibbons(); SetState(EState::Waiting); }
	}
	else if (T == TEXT("phase.progress"))
	{
		const FString Note = Str(D, TEXT("note"));
		ProgressNote = FString::Printf(TEXT("The game master is writing the day... %d / %d%s"), (int32)Numb(D, TEXT("done")), (int32)Numb(D, TEXT("total")), Note.IsEmpty() ? TEXT("") : *(TEXT("  (") + Note + TEXT(")")));
	}
	else if (T == TEXT("round.started"))
	{
		const TSharedPtr<FJsonObject> Pub = Sub(D, TEXT("public")), Priv = Sub(D, TEXT("private"));
		Dawn = Str(Priv, TEXT("dawn"));
		Health = (int32)Numb(Priv, TEXT("health"), Health);
		if (!Dawn.IsEmpty()) AddBanner(Dawn, Red);
		if (const TArray<TSharedPtr<FJsonValue>>* F = Arr(Pub, TEXT("fell_at_dawn")))
			for (const TSharedPtr<FJsonValue>& V : *F) AddBanner(V->AsString() + TEXT(" fell at dawn"), Red);
		ApplyZone(Sub(Pub, TEXT("zone")));
	}
	else if (T == TEXT("round.brief"))
	{
		const TSharedPtr<FJsonObject> Pub = Sub(D, TEXT("public")), Priv = Sub(D, TEXT("private"));
		Day = (int32)Numb(Pub, TEXT("round"), Day);
		Days = (int32)Numb(Pub, TEXT("rounds"), Days);
		Alive = (int32)Numb(Pub, TEXT("alive"), Alive);
		WillMax = (int32)Numb(Pub, TEXT("will_max_chars"), 295);
		Hazard = Str(Pub, TEXT("hazard")).ToUpper();
		if (St) { St->PlaySfx(EFNSfx::Chime, 0.8f); St->SetMusicLevel(Days > 1 ? (float)(Day - 1) / (float)(Days - 1) : 0.f); }
		const FString D2 = Str(Priv, TEXT("dawn")); if (!D2.IsEmpty()) Dawn = D2; else if (Day <= 1) Dawn.Reset();
		ApplyZone(Sub(Pub, TEXT("zone")));
		ApplySelfView(Priv);

		DropCities.Reset();
		TArray<FFNPiece> Spots;
		if (const TSharedPtr<FJsonObject> Bus = Sub(Pub, TEXT("battle_bus")))
			if (const TArray<TSharedPtr<FJsonValue>>* S = Arr(Bus, TEXT("spots")))
				for (const TSharedPtr<FJsonValue>& V : *S)
				{
					const TSharedPtr<FJsonObject> O = V->AsObject();
					FString Name = Str(O, TEXT("name"));
					int32 Comma; if (Name.FindChar(',', Comma)) Name = Name.Left(Comma);
					DropCities.Add(Name);
					double Lon = 0.0, Lat = 0.0;
					if (LonLat(O, TEXT("lonlat"), Lon, Lat)) { FFNPiece Pc; Pc.Name = Name; Pc.Lon = Lon; Pc.Lat = Lat; Pc.Kind = 4; Spots.Add(Pc); }
				}
		if (St)
		{
			St->ShowDropSpots(Spots);   // empty after day 1: the banners come down
			const TSharedPtr<FJsonObject> Zone = Sub(Pub, TEXT("zone"));
			FString Body = FString::Printf(TEXT("THE CRISIS today: %s.  %d still alive.\nThe safe zone: %d km around %s.\n\n"),
				*Str(Pub, TEXT("hazard")), Alive, (int32)Numb(Zone, TEXT("radius_km")), *Str(Zone, TEXT("center_place")));
			if (DropCities.Num()) Body += TEXT("The battle bus flies over ") + FString::Join(DropCities, TEXT(", ")) + TEXT(". Name one in your WILL to drop there.\n\n");
			else Body += FString::Printf(TEXT("You are in %s. %s\n\n"), *Place, *ZoneLine);
			Body += Str(Pub, TEXT("rules"));
			St->ShowEaselText(Str(Pub, TEXT("title")).ToUpper(), Body);
		}
	}
	else if (T == TEXT("dice.rolled"))
	{
		const int32 D20 = (int32)Numb(D, TEXT("d20"), 10), Mod = (int32)Numb(D, TEXT("mod")), Tot = (int32)Numb(D, TEXT("total"), D20);
		const FString Verdict = Str(D, TEXT("verdict"));
		if (St) St->RollDice(D20, Mod, Verdict);
		BestRoll = FMath::Max(BestRoll, D20);
		if (D20 == 20) ++Nat20s;
		if (Verdict.StartsWith(TEXT("SUCCESS")) || Verdict.StartsWith(TEXT("LEGEND"))) ++Successes;
		DiceBanner = Mod ? FString::Printf(TEXT("The d20 shows %d, +%d %s = %d.  %s"), D20, Mod, *Str(D, TEXT("skill")), Tot, *Verdict) : FString::Printf(TEXT("The d20 shows %d.  %s"), D20, *Verdict);
		DiceBannerAt = Clock + 1.8;
		ProgressNote = TEXT("The dice are rolling...");
	}
	else if (T == TEXT("action.received"))
	{
		Status = TEXT("WILL received");
	}
	else if (T == TEXT("round.outcome"))
	{
		const TSharedPtr<FJsonObject> Pub = Sub(D, TEXT("public"));
		Alive = (int32)Numb(Pub, TEXT("alive"), Alive);
		if (St) St->PlayHazard(Str(Pub, TEXT("hazard")));
		if (const TArray<TSharedPtr<FJsonValue>>* Hs = Arr(Pub, TEXT("headlines")))
			for (const TSharedPtr<FJsonValue>& V : *Hs)
				if (Str(V->AsObject(), TEXT("kind")) == TEXT("rival"))
					AddBanner(Str(V->AsObject(), TEXT("place")) + TEXT(": ") + Str(V->AsObject(), TEXT("headline")), FLinearColor(0.55f, 0.85f, 1.f));
		if (const TArray<TSharedPtr<FJsonValue>>* E0 = Arr(Pub, TEXT("eliminated")))
			if (St && E0->Num() > 0) St->PlaySfx(EFNSfx::Snuff, 0.8f, 2.5f);
		if (const TArray<TSharedPtr<FJsonValue>>* E = Arr(Pub, TEXT("eliminated")))
			for (const TSharedPtr<FJsonValue>& V : *E)
				AddBanner(FString::Printf(TEXT("ELIMINATED: %s  (%s)"), *Str(V->AsObject(), TEXT("name")), *Str(V->AsObject(), TEXT("cause"))), Red);
	}
	else if (T == TEXT("round.private"))
	{
		const TSharedPtr<FJsonObject> Priv = Sub(D, TEXT("private"));
		const int32 HealthBefore = Health;
		ApplySelfView(Sub(Priv, TEXT("you")));
		if (Health < HealthBefore) AddBanner(FString::Printf(TEXT("You lost %d health today  (%d/10 left)"), HealthBefore - Health, FMath::Max(0, Health)), Red);
		else if (Health > HealthBefore) AddBanner(FString::Printf(TEXT("You healed %d  (%d/10)"), Health - HealthBefore, Health), FLinearColor(0.4f, 1.f, 0.5f));
		const TSharedPtr<FJsonObject> Vic = Sub(Priv, TEXT("vicinity"));
		FString Body = Str(Priv, TEXT("experience"));
		if (Body.StartsWith(TEXT("BECAUSE OF YOUR WILL: ")))
		{
			FString Why = Body.RightChop(22); int32 Nl; if (Why.FindChar('\n', Nl)) Why = Why.Left(Nl);
			AddBanner(TEXT("Your WILL mattered: ") + Why, Gold);
		}
		if (Vic.IsValid() && !Str(Vic, TEXT("outcome")).IsEmpty()) Body += TEXT("\n\n") + Str(Vic, TEXT("outcome"));
		bool bSpect = false; Priv->TryGetBoolField(TEXT("spectating"), bSpect);
		if (St) St->ShowEaselText(FString::Printf(TEXT("[day %d]  %s"), Day, Vic.IsValid() ? *Str(Vic, TEXT("headline")) : TEXT("")), Body);
		{
			FString Scene = Body; int32 Cut = Scene.Find(TEXT("THE DUNGEON MASTER:")); if (Cut != INDEX_NONE) Scene = Scene.Left(Cut);
			Scene.ReplaceInline(TEXT("BECAUSE OF YOUR WILL: "), TEXT(""));
			RequestPicture(FString::Printf(TEXT("Day %d, the crisis is %s, in %s. %s %s"), Day, *Hazard.ToLower(), *Place, Vic.IsValid() ? *Str(Vic, TEXT("headline")) : TEXT(""), *Scene.Left(600)),
				FString::Printf(TEXT("Day %d: %s"), Day, Vic.IsValid() ? *Str(Vic, TEXT("headline")) : TEXT("")));
		}
		{
			FString Line = Vic.IsValid() ? Str(Vic, TEXT("headline")) : FString();
			FString Exp = Str(Priv, TEXT("experience"));
			if (Exp.StartsWith(TEXT("BECAUSE OF YOUR WILL: "))) { int32 Nl; Exp = Exp.FindChar('\n', Nl) ? Exp.RightChop(Nl).TrimStartAndEnd() : FString(); }
			int32 Cut = Exp.Find(TEXT("THE DUNGEON MASTER:")); if (Cut != INDEX_NONE) Exp = Exp.Left(Cut).TrimStartAndEnd();
			CutLines.Add(Line + TEXT("\n\n") + Exp);
			CutSpots.Add(FVector2D(MyLon, MyLat));
		}
		if (const TArray<TSharedPtr<FJsonValue>>* F = Arr(Priv, TEXT("found")))
			for (const TSharedPtr<FJsonValue>& V : *F) { ++ItemsGot; AddBanner(TEXT("Found: ") + Str(V->AsObject(), TEXT("name")), Orange); if (St) St->PlaySfx(EFNSfx::Chime, 0.35f, 1.f); }
		const FString Kept = Str(Priv, TEXT("kept"));
		if (!Kept.IsEmpty()) AddBanner(TEXT("Into your bag: ") + Kept, Gold);
		bool bDead = false; Priv->TryGetBoolField(TEXT("eliminated"), bDead);
		if (bDead && !bEliminated) { bEliminated = true; FallPlace = FMath::Max(2, Alive); AddBanner(FString::Printf(TEXT("YOU PLACED #%d"), FallPlace), Gold); if (St) St->PlaySfx(EFNSfx::Snuff, 1.f, 1.f); AddBanner(TEXT("You have been eliminated: ") + Str(Priv, TEXT("cause")), Red); }

		Choices.Reset();
		if (const TArray<TSharedPtr<FJsonValue>>* C = Arr(D, TEXT("choices")))
			for (const TSharedPtr<FJsonValue>& V : *C)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FFNEpisode E;
				E.EpisodeUuid = Str(O, TEXT("choice_id"));
				E.RoundOutcome = Str(O, TEXT("kind"));
				E.LocalExperience = Str(O, TEXT("label"));
				E.EpisodeDescription = Str(O, TEXT("detail"));
				Choices.Add(E);
			}
		// Only three ribbons fit around the Globe: keep "pass" as the last one
		if (Choices.Num() > 3)
		{
			const int32 PassIdx = Choices.IndexOfByPredicate([](const FFNEpisode& E) { return E.EpisodeUuid == TEXT("pass"); });
			FFNEpisode Pass; if (PassIdx != INDEX_NONE) { Pass = Choices[PassIdx]; Choices.RemoveAt(PassIdx); }
			Choices.SetNum(PassIdx != INDEX_NONE ? 2 : 3);
			if (PassIdx != INDEX_NONE) Choices.Add(Pass);
		}
		if (bEliminated) Choices.Reset();
		if (St) St->ShowRibbons(Choices);
	}
	else if (T == TEXT("choice.received"))
	{
		Status = TEXT("Choice received");
	}
	else if (T == TEXT("round.committed"))
	{
		const TSharedPtr<FJsonObject> PS = Sub(D, TEXT("public_state"));
		Alive = (int32)Numb(PS, TEXT("alive"), Alive);
		if (!bEliminated && Health > 0) DaysLived = FMath::Max(DaysLived, Day);
		else if (FallPlace == 0) { FallPlace = FMath::Max(2, Alive + 1); }
	}
	else if (T == TEXT("round.private_state"))
	{
		ApplySelfView(Sub(D, TEXT("state")));
		Dawn.Reset();
	}
	else if (T == TEXT("seat.eliminated"))
	{
		const FString Who = Str(D, TEXT("display_name"));
		AddBanner(Who.IsEmpty() ? FString::Printf(TEXT("Seat %d is out: %s"), (int32)Numb(D, TEXT("seat")), *Str(D, TEXT("reason"))) : FString::Printf(TEXT("ELIMINATED: %s  (%s)   %d left"), *Who, *Str(D, TEXT("reason")), FMath::Max(1, Alive - 1)), Red);
		if (St) St->PlaySfx(EFNSfx::Snuff, 0.7f);
	}
	else if (T == TEXT("duel.started"))
	{
		DuelId = Str(D, TEXT("duel_id"));
		DuelRole = Str(D, TEXT("role"));
		const FString Atk = Str(D, TEXT("attacker")), Def = Str(D, TEXT("defender"));
		DuelFoe = DuelRole == TEXT("defender") ? Atk : Def;
		AddBanner(FString::Printf(TEXT("DUEL!  %s vs %s"), *Atk, *Def), Red);
		if (St)
		{
			St->PlaySfx(EFNSfx::Impact, 0.9f);
			St->ShowEaselText(FString::Printf(TEXT("DUEL  -  %s vs %s"), *Atk.ToUpper(), *Def.ToUpper()),
				FString::Printf(TEXT("%s: \"%s\"\n\n%s"), *Atk, *Str(D, TEXT("text")), *Str(D, TEXT("note"))));
		}
		if (DuelRole == TEXT("attacker")) SetState(EState::DuelWaiting);
	}
	else if (T == TEXT("duel.cancelled"))
	{
		AddBanner(Str(D, TEXT("note")), Dim);
		SetState(EState::Submitted);
	}
	else if (T == TEXT("duel.turn"))
	{
		if (bEliminated) return;
		DuelId = Str(D, TEXT("duel_id"));
		DuelRole = Str(D, TEXT("role"));
		DuelFoe = Str(D, TEXT("foe"));
		Deadline = Numb(D, TEXT("deadline"), 0);
		const double OneIn = Numb(D, TEXT("one_in"), 4);
		const FString OneInTxt = OneIn < 10.0 ? FString::Printf(TEXT("%.1f"), OneIn) : FText::AsNumber((int64)FMath::RoundToDouble(OneIn)).ToString();
		const FString Who = DuelRole == TEXT("defender") ? DuelFoe + TEXT("'s") : FString(TEXT("Your"));
		DuelOdds = FString::Printf(TEXT("%s attack right now: 1 in %s  (a d%d, needs %d+)."), *Who, *OneInTxt, (int32)Numb(D, TEXT("sides"), 20), (int32)Numb(D, TEXT("need"), 20));
		DuelPrompt = Str(D, TEXT("prompt"));
		FString Body;
		if (const TArray<TSharedPtr<FJsonValue>>* L = Arr(D, TEXT("lines")))
			for (const TSharedPtr<FJsonValue>& V : *L)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				Body += FString::Printf(TEXT("%s: \"%s\"\n\n"), *Str(O, TEXT("name")), *Str(O, TEXT("text")));
			}
		const FString Note = Str(D, TEXT("note"));
		Body += DuelOdds + (Note.IsEmpty() ? FString() : TEXT("\nTHE DUNGEON MASTER: ") + Note);
		if (St) { St->ShowEaselText(FString::Printf(TEXT("DUEL vs %s"), *DuelFoe.ToUpper()), Body); St->PlaySfx(EFNSfx::Chime, 0.7f); }
		AddBanner(DuelOdds, Gold);
		SetState(EState::DuelWriting);
		if (WillBox.IsValid()) FSlateApplication::Get().SetKeyboardFocus(WillBox, EFocusCause::SetDirectly);
	}
	else if (T == TEXT("duel.rolled"))
	{
		const int32 Roll = (int32)Numb(D, TEXT("roll"), 1), Sides = (int32)Numb(D, TEXT("sides"), 20), Need = (int32)Numb(D, TEXT("need"), 20);
		bool bHit = false; D->TryGetBoolField(TEXT("success"), bHit);
		bool bCrit = false; D->TryGetBoolField(TEXT("crit"), bCrit);
		const FString Label = Str(D, TEXT("label")) + TEXT(": ") + Str(D, TEXT("who"));
		if (St) St->RollDuelDice(Roll, Sides, Need, bHit, Label);
		if (Net.IsValid() && Str(D, TEXT("who")) == Net->DisplayName) { BestRoll = FMath::Max(BestRoll, FMath::Min(20, Roll)); if (bHit) ++Successes; }
		AddBanner(FString::Printf(TEXT("%s  -  d%d rolled %d, needed %d+:  %s%s"), *Label, Sides, Roll, Need,
			bHit ? *FString::Printf(TEXT("HIT for %d"), (int32)Numb(D, TEXT("damage"))) : TEXT("MISS"), bCrit ? TEXT("  (PERFECT ROLL)") : TEXT("")), bHit ? Gold : Dim);
	}
	else if (T == TEXT("duel.result"))
	{
		const FString Killed = Str(D, TEXT("killed"));
		AddBanner(Str(D, TEXT("summary")), Pale);
		if (!Killed.IsEmpty()) AddBanner(FString::Printf(TEXT("%s IS DEAD"), *Killed.ToUpper()), Red);
		if (Net.IsValid() && !Killed.IsEmpty() && Killed != Net->DisplayName && Str(D, TEXT("attacker")) == Net->DisplayName) ++Kills;
		if (St) St->ShowEaselText(FString(TEXT("THE DUEL IS DECIDED")), Str(D, TEXT("summary")) + TEXT("\n\n") + Str(D, TEXT("detail")));
		CutLines.Add(TEXT("The duel\n\n") + Str(D, TEXT("summary")));
		RequestPicture(FString::Printf(TEXT("A duel between %s and %s in %s. %s"), *Str(D, TEXT("attacker")), *Str(D, TEXT("defender")), *Place, *Str(D, TEXT("summary"))),
			FString::Printf(TEXT("The duel: %s vs %s"), *Str(D, TEXT("attacker")), *Str(D, TEXT("defender"))));
		CutSpots.Add(FVector2D(MyLon, MyLat));
		if (State == EState::DuelWaiting || State == EState::DuelWriting) SetState(EState::Waiting);
	}
	else if (T == TEXT("you.eliminated"))
	{
		SpectateCause = Str(D, TEXT("cause"));
		const int32 Place2 = (int32)Numb(D, TEXT("place"), FMath::Max(2, Alive));
		if (!bEliminated)
		{
			AddBanner(TEXT("You have been eliminated: ") + SpectateCause, Red);
			if (St) St->PlaySfx(EFNSfx::Snuff, 1.f, 0.5f);
		}
		bEliminated = true;
		FallPlace = FMath::Max(2, Place2);
		Health = 0;
		bSpectatePanelHidden = false;
		Deadline = 0.0;
		if (St) { St->ClearRibbons(); St->ShowEaselText(TEXT("YOU HAVE FALLEN"), SpectateCause + FString::Printf(TEXT("\n\nYou placed #%d. You are out for good: watch the rest of the night, or join a fresh game."), FallPlace)); }
		SetState(EState::Spectating);
	}
	else if (T == TEXT("match.ended"))
	{
		const TSharedPtr<FJsonObject> Sum = Sub(D, TEXT("summary"));
		FString Champs;
		if (const TArray<TSharedPtr<FJsonValue>>* C = Arr(Sum, TEXT("champions")))
			for (const TSharedPtr<FJsonValue>& V : *C) Champs += (Champs.IsEmpty() ? TEXT("") : TEXT(", ")) + Str(V->AsObject(), TEXT("name"));
		if (St)
		{
			const bool bChamp = Net.IsValid() && Champs.Contains(Net->DisplayName);
			const FString Closing = FString::Printf(TEXT("%s\n\nLast one standing: %s\n\n%s the %s, the table remembers you."),
				bChamp ? TEXT("VICTORY. The Dungeon Master bows.") : TEXT("You fell. The Dungeon Master closes the book, gently."),
				*Champs, Net.IsValid() ? *Net->DisplayName : TEXT("Hero"), Net.IsValid() && !Net->HeroClass.IsEmpty() ? *Net->HeroClass : TEXT("Wanderer"));
			St->PlayFinalCut(CutLines, CutSpots, Closing);
			FinishMatch(bChamp);
		}
		if (const TArray<TSharedPtr<FJsonValue>>* Piles = Arr(Sum, TEXT("piles")))
			for (const TSharedPtr<FJsonValue>& V : *Piles)
			{
				const TSharedPtr<FJsonObject> P = V->AsObject();
				if (Str(P, TEXT("name")) != (Net.IsValid() ? Net->DisplayName : FString())) continue;
				TArray<FString> Items;
				if (const TArray<TSharedPtr<FJsonValue>>* I = Arr(P, TEXT("items")))
					for (const TSharedPtr<FJsonValue>& X : *I) Items.Add(Str(X->AsObject(), TEXT("item")));
				AddBanner(TEXT("Your pile: ") + FString::Join(Items, TEXT(", ")), Gold);
			}
		AddBanner(TEXT("Winner: ") + Champs, Gold);
		if (St) { St->PlaySfx(EFNSfx::Boom, 0.7f); St->PlaySfx(EFNSfx::Chime, 1.f, 0.6f); St->PlaySfx(EFNSfx::Chime, 0.8f, 1.6f); }
		SetState(EState::Ended);
	}
	else if (T == TEXT("error"))
	{
		AddBanner(TEXT("Server: ") + Str(D, TEXT("message")), Red);
		if (Str(D, TEXT("code")) == TEXT("too_long") || Str(D, TEXT("code")) == TEXT("bad_request"))
			if (State == EState::Submitted) SetState(EState::Compose);
	}
}

// --- self-test --------------------------------------------------------------

// --- the one-more-game loop ------------------------------------------------

void SFNRootWidget::PickQuests()
{
	Quests.Reset();
	TArray<int32> Pool; for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(FNProfile::Quests); ++i) Pool.Add(i);
	for (int32 k = 0; k < 3 && Pool.Num(); ++k) { const int32 j = FMath::RandRange(0, Pool.Num() - 1); Quests.Add(FString::FromInt(Pool[j])); Pool.RemoveAt(j); }
}

bool SFNRootWidget::QuestDone(int32 Index) const
{
	if (!Quests.IsValidIndex(Index)) return false;
	const bool bOver = State == EState::Ended;
	switch (FCString::Atoi(*Quests[Index]))
	{
	case 0: return Nat20s > 0;
	case 1: return bOver && !bEliminated && Health > 0;
	case 2: return ItemsGot >= 4;
	case 3: return Successes >= 3;
	case 4: return bOver && !bLeftZone && DaysLived > 0;
	case 5: return bBlocked;
	case 6: return bOver && !bEliminated && Health >= 7;
	case 7: return bWon;
	}
	return false;
}

FText SFNRootWidget::QuestText() const
{
	FString S = TEXT("TONIGHT'S QUESTS  (+150 XP each)");
	for (int32 i = 0; i < Quests.Num(); ++i)
		S += FString::Printf(TEXT("\n%s  %s"), QuestDone(i) ? TEXT("[X]") : TEXT("[  ]"), FNProfile::Quests[FMath::Clamp(FCString::Atoi(*Quests[i]), 0, (int32)UE_ARRAY_COUNT(FNProfile::Quests) - 1)]);
	return FText::FromString(S);
}

FText SFNRootWidget::ProfileText() const
{
	if (Net.IsValid() && Net->bFn7) return FText::GetEmpty();   // the old game's hero level / XP mean nothing in Final Night 7
	// read the file at most once a second (this text is drawn every frame)
	static FNProfile::FData P; static double LastRead = -10.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRead > 1.0) { P = FNProfile::Load(); LastRead = Now; }
	const int32 Lv = FNProfile::Level(P.Xp);
	return FText::FromString(FString::Printf(TEXT("LEVEL %d %s   %d/1000 XP   %d win%s"), Lv, *FString(FNProfile::Title(Lv)).ToUpper(), P.Xp % 1000, P.Wins, P.Wins == 1 ? TEXT("") : TEXT("s")));
}

void SFNRootWidget::FinishMatch(bool bWinner)
{
	if (bProfileSaved) return;
	bProfileSaved = true;
	bWon = bWinner;
	if (!bEliminated && Health > 0) DaysLived = FMath::Max(DaysLived, Days);
	const int32 MyPlace = bWinner ? 1 : (FallPlace > 0 ? FallPlace : 2);
	int32 QuestsDone = 0; for (int32 i = 0; i < Quests.Num(); ++i) if (QuestDone(i)) ++QuestsDone;
	XpGained = DaysLived * 100 + Successes * 50 + ItemsGot * 25 + Nat20s * 200 + QuestsDone * 150 + (bWinner ? 1000 : 0) + (MyPlace <= 3 ? 200 : 0);

	FNProfile::FData P = FNProfile::Load();
	const int32 OldLv = FNProfile::Level(P.Xp);
	P.Xp += XpGained; P.Games += 1; P.Wins += bWinner ? 1 : 0;
	P.BestPlace = P.BestPlace == 0 ? MyPlace : FMath::Min(P.BestPlace, MyPlace);
	FNProfile::Save(P);
	const int32 NewLv = FNProfile::Level(P.Xp);

	EndTitle = bWinner ? TEXT("VICTORY!  LAST ONE STANDING") : FString::Printf(TEXT("YOU PLACED  #%d"), MyPlace);
	EndBody = FString::Printf(TEXT("%s the %s\n\nDays survived: %d      Best roll: %d%s\nDice won: %d      Things found or built: %d\nQuests done: %d of %d\n\n+%d XP"),
		Net.IsValid() ? *Net->DisplayName : TEXT("Hero"), Net.IsValid() && !Net->HeroClass.IsEmpty() ? *Net->HeroClass : TEXT("Wanderer"),
		DaysLived, BestRoll, Nat20s ? TEXT("  (NATURAL 20!)") : TEXT(""), Successes, ItemsGot, QuestsDone, Quests.Num(), XpGained);
	if (NewLv > OldLv) EndBody += FString::Printf(TEXT("\n\nLEVEL UP!  You are now a %s  (level %d)"), FNProfile::Title(NewLv), NewLv);
	else EndBody += FString::Printf(TEXT("\nLevel %d %s:  %d / 1000 XP to the next level"), NewLv, FNProfile::Title(NewLv), P.Xp % 1000);
	EndBody += FString::Printf(TEXT("\n\nGames: %d    Wins: %d    Best place: #%d"), P.Games, P.Wins, P.BestPlace);
	EndScreenAt = Clock + CutLines.Num() * 5.5 + 9.0;
	if (AFNStudio* St = Studio.Get()) { St->PlaySfx(EFNSfx::Chime, 0.9f, CutLines.Num() * 5.5f + 9.f); if (NewLv > OldLv) St->PlaySfx(EFNSfx::Chime, 1.f, CutLines.Num() * 5.5f + 9.6f); }
}

FReply SFNRootWidget::OnPlayAgain()
{
	if (AFNStudio* St = Studio.Get()) UGameplayStatics::OpenLevel(St, FName(TEXT("/Engine/Maps/Entry")));
	return FReply::Handled();
}

void SFNRootWidget::Shot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("FNAutoTest_%02d_%s"), ++AutoShots, Name), true, false);
	UE_LOG(LogFNUI, Display, TEXT("FNAutoTest: SHOT %s"), Name);
}

void SFNRootWidget::AutoTestStep()
{
	FString Step;
	switch (State)
	{
	case EState::Tutorial:  Step = TEXT("click-easel"); Shot(TEXT("tutorial")); OnWorldClick(AFNStudio::PlayerEasel); break;
	case EState::Connected: Step = TEXT("click-globe"); Shot(TEXT("connected")); OnWorldClick(Net->HeroClass.IsEmpty() ? AFNStudio::RibbonB : AFNStudio::Globe); break;
	case EState::InRoom:    Step = TEXT("click-will-ready"); Shot(TEXT("room")); OnWorldClick(AFNStudio::WillPanel); break;
	case EState::Compose:   Step = FString::Printf(TEXT("click-will-day%d"), Day); Shot(*FString::Printf(TEXT("brief_day%d"), Day)); OnWorldClick(AFNStudio::WillPanel); break;
	case EState::Writing:
	{
		static const TCHAR* Wills[] = {
			TEXT("I drop into Las Vegas, grab the nearest slot machine lever and climb onto a casino roof to watch."),
			TEXT("I teleport toward the zone centre, search a gas station for supplies, and hide."),
			TEXT("I duplicate my best item and set a trap for anyone who comes close."),
			TEXT("I stay inside the zone and wish for a very large umbrella."),
			TEXT("I make a heroic speech and then run."),
		};
		Step = FString::Printf(TEXT("submit-will-day%d"), Day);
		WillBox->SetText(FText::FromString(Wills[FMath::Clamp(Day - 1, 0, 4)]));
		Shot(*FString::Printf(TEXT("will_day%d"), Day));
		OnSubmitWill();
		break;
	}
	case EState::Choose:
		Step = FString::Printf(TEXT("choose-day%d"), Day);
		Shot(*FString::Printf(TEXT("choices_day%d"), Day));
		OnWorldClick(Day % 2 == 1 ? AFNStudio::RibbonA : (Choices.Num() > 1 ? AFNStudio::RibbonB : AFNStudio::RibbonA));
		break;
	case EState::DuelWriting:
		Step = FString::Printf(TEXT("duel-day%d"), Day);
		Shot(*FString::Printf(TEXT("duel_day%d"), Day));
		WillBox->SetText(FText::FromString(TEXT("I keep my back to the wall, raise my shield and wait for the opening, then shove them into the street.")));
		OnSubmitWill();
		break;
	case EState::Ended:
	case EState::Spectating:
		if (State == EState::Ended)
		{
			Shot(TEXT("final_cut"));
			bAutoDone = true;
			UE_LOG(LogFNUI, Display, TEXT("FNAutoTest: PASS - full match: lobby, %d days, WILLs, choices, Final Cut"), Day);
			return;
		}
		break;
	default: break;
	}
	if (!Step.IsEmpty())
	{
		UE_LOG(LogFNUI, Display, TEXT("FNAutoTest: STEP %s"), *Step);
		AutoLastStep = Step;
		AutoStuckSince = Clock;
	}
	else if (Clock - AutoStuckSince > 90.0)
	{
		bAutoDone = true;
		Shot(TEXT("stuck"));
		UE_LOG(LogFNUI, Error, TEXT("FNAutoTest: FAIL - stuck after '%s' in state %d, status '%s'"), *AutoLastStep, (int32)State, *Status);
	}
}

// --- pictures --------------------------------------------------------------

FString SFNRootWidget::LoadOpenAIKey() const
{
	// Config/OpenAIKey.txt (just the key), or OPENAI_API_KEY=... in Config/.env or the project's .env
	const FString Dir = FPaths::ProjectDir();
	const FString Files[] = { FPaths::Combine(Dir, TEXT("Config/OpenAIKey.txt")), FPaths::Combine(Dir, TEXT("Config/.env")), FPaths::Combine(Dir, TEXT(".env")) };
	for (const FString& F : Files)
	{
		FString Txt;
		if (!FFileHelper::LoadFileToString(Txt, *F)) continue;
		TArray<FString> Lines; Txt.ParseIntoArrayLines(Lines);
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			FString K, V;
			if (L.Split(TEXT("="), &K, &V)) { if (!K.TrimStartAndEnd().Contains(TEXT("OPENAI"))) continue; L = V.TrimStartAndEnd(); }
			L.ReplaceInline(TEXT("\""), TEXT("")); L.ReplaceInline(TEXT("'"), TEXT(""));
			if (L.StartsWith(TEXT("sk-"))) return L;
		}
	}
	return FString();
}

void SFNRootWidget::RequestPicture(const FString& Scene, const FString& Caption)
{
	const FString Key = LoadOpenAIKey();
	if (Key.IsEmpty())
	{
		if (!bPicWarned) { bPicWarned = true; UE_LOG(LogFNUI, Warning, TEXT("No OpenAI key (Config/OpenAIKey.txt or .env): no pictures this game")); AddBanner(TEXT("No picture key yet: put the OpenAI key in Config/OpenAIKey.txt to see each day painted"), Dim); }
		return;
	}
	FString Model = TEXT("gpt-image-2");
	{
		FString M; if (FFileHelper::LoadFileToString(M, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Config/OpenAIModel.txt")))) { M.TrimStartAndEndInline(); if (!M.IsEmpty()) Model = M; }
	}
	const FString Prompt = TEXT("A painted dark-fantasy illustration, like a page from a Dungeons and Dragons book, candlelit and mythic, set on the real Earth. ")
		TEXT("No words or letters in the picture, no gore, no real people. Scene: ") + Scene.Left(1500);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Model);
	Body->SetStringField(TEXT("prompt"), Prompt);
	Body->SetStringField(TEXT("size"), TEXT("1024x1024"));
	Body->SetStringField(TEXT("quality"), TEXT("low"));
	Body->SetNumberField(TEXT("n"), 1);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Body, W);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TEXT("https://api.openai.com/v1/images/generations"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Key);
	Req->SetContentAsString(Out);
	Req->SetTimeout(120.f);
	const int32 Serial = ++PicSerial;
	bPicPending = true;
	UE_LOG(LogFNUI, Display, TEXT("Painting (%s): %s"), *Model, *Caption);
	TWeakPtr<SFNRootWidget> Weak = StaticCastSharedRef<SFNRootWidget>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak, Serial, Caption](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNRootWidget> Self = Weak.Pin();
		if (!Self.IsValid() || Serial != Self->PicSerial) return;   // a newer picture is on its way
		Self->bPicPending = false;
		const FString Raw = Resp.IsValid() ? Resp->GetContentAsString() : FString();
		TSharedPtr<FJsonObject> J;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		FString B64;
		if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200 && FJsonSerializer::Deserialize(R, J) && J.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Data;
			if (J->TryGetArrayField(TEXT("data"), Data) && Data->Num() > 0)
				if (const TSharedPtr<FJsonObject> First = (*Data)[0]->AsObject()) First->TryGetStringField(TEXT("b64_json"), B64);
		}
		TArray<uint8> Bytes;
		UTexture2D* Tex = nullptr;
		if (!B64.IsEmpty() && FBase64::Decode(B64, Bytes)) Tex = FImageUtils::ImportBufferAsTexture2D(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()));
		if (!Tex)
		{
			UE_LOG(LogFNUI, Warning, TEXT("Picture failed (HTTP %d): %s"), Resp.IsValid() ? Resp->GetResponseCode() : 0, *Raw.Left(800));
			Self->AddBanner(FString::Printf(TEXT("The picture didn't come through (error %d). Details are in the log."), Resp.IsValid() ? Resp->GetResponseCode() : 0), Dim);
			return;
		}
		Self->PicTex.Reset(Tex);
		Self->PicBrush.SetResourceObject(Tex);
		Self->PicBrush.ImageSize = FVector2D(400.f, 400.f);
		Self->PicBrush.DrawAs = ESlateBrushDrawType::Image;
		Self->PicCaption = Caption;
		Self->bPicShown = true;
		UE_LOG(LogFNUI, Display, TEXT("Painted: %s"), *Caption);
	});
	Req->ProcessRequest();
}

#undef LOCTEXT_NAMESPACE
