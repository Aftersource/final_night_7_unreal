#include "SFNHudLive.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "FNNetClient.h"
#include "FNPlayerController.h"
#include "FNStudio.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "ImageUtils.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Containers/Ticker.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/SOverlay.h"
#include "SFNStartPanel.h"
#include "FNPowerDrag.h"
#include "SFNDragToken.h"
#include "SFNPanelMover.h"
#include "FNPowerRing.h"
#include "FNTableGods.h"
#include "FNDuelStage.h"
#include "FNDuelBox.h"
#include "FNStarBackdrop.h"
#include "Styling/CoreStyle.h"
#include "Components/TextRenderComponent.h"
#include "FNWood.h"
#include "FNMedia.h"
#include "FNStyle.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNHudLive"

namespace FN7HudJson
{
	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return (O.IsValid() && O->TryGetObjectField(K, Out) && Out) ? *Out : nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return (O.IsValid() && O->TryGetArrayField(K, Out)) ? Out : nullptr;
	}
	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		FString S;
		if (O.IsValid()) O->TryGetStringField(K, S);
		return S;
	}
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Default = 0.0)
	{
		double V = Default;
		if (O.IsValid()) O->TryGetNumberField(K, V);
		return V;
	}
	bool Has(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		return O.IsValid() && O->HasField(K) && !O->GetField<EJson::None>(K)->IsNull();
	}
	/** A JSON boolean (true / false); Num() never reads those. */
	bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* K, bool Default = false)
	{
		bool V = Default;
		if (O.IsValid()) O->TryGetBoolField(K, V);
		return V;
	}
}

namespace FN7Looks
{
	/** The image to draw for a demigod now (schema 11): look.<Key> when the look carries one (it is the gear image once
	 *  ready, the previous look's while the next renders), else the identity image. */
	static TSharedPtr<FJsonObject> Image(const TSharedPtr<FJsonObject>& G, const TCHAR* Key)
	{
		if (const TSharedPtr<FJsonObject> Look = FN7HudJson::Obj(G, TEXT("look")))
			if (const TSharedPtr<FJsonObject> Img = FN7HudJson::Obj(Look, Key))
				if (!FN7HudJson::Str(Img, TEXT("hash")).IsEmpty()) return Img;
		return FN7HudJson::Obj(G, Key);
	}
	/** Its loadout names ("Butterfly Wing Blade, Moonsteel Plate"), at most three. */
	static FString Gear(const TSharedPtr<FJsonObject>& G)
	{
		TArray<FString> Names;
		if (const TArray<TSharedPtr<FJsonValue>>* L = FN7HudJson::Arr(FN7HudJson::Obj(G, TEXT("look")), TEXT("loadout")))
			for (const TSharedPtr<FJsonValue>& V : *L)
				if (const TSharedPtr<FJsonObject> It = V.IsValid() ? V->AsObject() : nullptr)
				{
					const FString N = FN7HudJson::Str(It, TEXT("name"));
					if (!N.IsEmpty() && Names.Num() < 3) Names.Add(N);
				}
		return FString::Join(Names, TEXT(", "));
	}
}

/** True when keyboard focus is in the game viewport or the HUD over it (not in an editor panel). */
static bool FN7LonLat(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& OutLat, double& OutLon);

static bool FN7GameHasFocus()
{
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	TSharedPtr<SViewport> VP = VC ? VC->GetGameViewportWidget() : nullptr;
	return VP.IsValid() && (VP->HasKeyboardFocus() || VP->HasFocusedDescendants());
}

/** Esc (no modifiers) opens and closes the lobby, Enter opens the WILL, while we are in a room and the game has focus. */
/** True when a text field has the keyboard (the WILL, the lobby's name box): letter keys belong to it then. */
static bool FN7TypingInText()
{
	if (!FSlateApplication::IsInitialized()) return false;
	const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
	return F.IsValid() && F->GetTypeAsString().Contains(TEXT("EditableText"));
}

class FFN7LobbyKeys : public IInputProcessor
{
public:
	TFunction<bool()> OnEscape;
	TFunction<bool()> OnEnter;
	TFunction<bool()> OnReset;
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
	{
		if (E.IsShiftDown() || E.IsControlDown() || E.IsAltDown() || E.IsCommandDown() || E.IsRepeat()) return false;
		if (!FN7GameHasFocus()) return false;
		if (E.GetKey() == EKeys::Escape) return OnEscape ? OnEscape() : false;
		if (E.GetKey() == EKeys::Enter) return OnEnter ? OnEnter() : false;
		if ((E.GetKey() == EKeys::R || E.GetKey() == EKeys::Home) && !FN7TypingInText()) return OnReset ? OnReset() : false;
		return false;
	}
};

void SFNHudLive::Construct(const FArguments& InArgs)
{
	// Tooltips wait a moment before they appear (Chris: hovers that pop at once got in the way).
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.TooltipSummonDelay"))) V->Set(0.45f, ECVF_SetByCode);
	Net = InArgs._Net;
	Model.YouName = FText::GetEmpty();
	Model.ComposeHint = LOCTEXT("Hint", "Write what you'll do. Time slows while you write; SEND to act. Lines starting r: go to your right hand.");
	Model.PowersNote = LOCTEXT("PowersNote", "Arm a power, write its target (a place, a name or lon,lat), SEND");

	ChildSlot
	[
		SNew(SOverlay)
		// Under everything: the power hexes on the table take a press (lift and drag a hex, click the pouch); any other
		// click falls through to the game.
		+ SOverlay::Slot()
		[
			SAssignNew(RingInput, SFNPowerRingInput)
			.OnHexClicked_Lambda([this](const FString& Slug) { OnPower(Slug); })
			.Visibility_Lambda([this]() { return (bInRoom && !bLobbyOpen && !Model.bViewer) ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Layout, SFNHudLayout)
			.Model(Model)
			.Visibility_Lambda([this]() { return bInRoom ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			.Timer(this, &SFNHudLive::GetTimer)
			.OnSend(this, &SFNHudLive::OnSend)
			.OnTypingStarted(this, &SFNHudLive::OnTyping)
			.OnPower(this, &SFNHudLive::OnPower)
			.OnTakeSeat_Lambda([this]() { if (UFNNetClient* N = Net.Get()) { N->bWatchOnly = false; N->bSeatAsked = true; N->TakeSeat(); } })
			.OnLobby_Lambda([this]() { SetLobbyOpen(true); })
			.OnResetView_Lambda([this]() { ResetView(); })
			.OnHowToPlay_Lambda([this]() { bHowTo = true; })
			.OnCardDoubleClick_Lambda([this](const FString& Ref) { OnDemigodDoubleClick(Ref); })
			.OnCardDrag_Lambda([this](const FString& Ref, const FVector2D& Screen) -> TSharedPtr<FDragDropOperation>
			{
				const FString Why = WhyNotSummon(Ref);
				UE_LOG(LogTemp, Display, TEXT("FN7 summon card drag: %s %s"), *Ref, Why.IsEmpty() ? TEXT("-> drag") : *Why);
				if (!Why.IsEmpty()) { Toast(Why); return nullptr; }
				if (AFNTableGods* T = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr)) T->SetFigureHeld(Ref, true);
				return FFNSummonDragDrop::New(Ref, Screen);
			})
		]
		// Out of the match: a small card under the header, never over the board's centre.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 92.f, 0.f, 0.f))
		[
			MakeOutCard()
		]
		// The board's debug lines, under the title bar, in a typewriter font (Chris).
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(34.f, 68.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return BoardInfo.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
			.ColorAndOpacity(FLinearColor(0.88f, 0.85f, 0.76f, 0.9f))
			.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
			.Text_Lambda([this]() { return FText::FromString(BoardInfo); })
		]
		// "Updating in N s": shown before the Mac session restarts this editor on a new build.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 120.f, 0.f, 0.f))
		[
			SNew(SBorder).BorderImage(FN7Wood::StoneBrush()).Padding(FMargin(26.f, 12.f))
			.Visibility_Lambda([this]()
			{
				const double Now = (double)FDateTime::UtcNow().ToUnixTimestamp();
				return (UpdateAt > 0.0 && Now < UpdateAt + 5.0) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock).Font(FN7Style::Type::Font(17, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.86f, 0.52f))
				.Text_Lambda([this]()
				{
					const int32 Left = FMath::Max(0, FMath::CeilToInt(UpdateAt - (double)FDateTime::UtcNow().ToUnixTimestamp()));
					return FText::FromString(Left > 0 ? FString::Printf(TEXT("%s in %d s"), *UpdateText, Left) : UpdateText + TEXT(" now..."));
				})
			]
		]
		// HOW TO PLAY: a stone tablet, shown once on a player's first join and from the "?" button.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(780.f)
			.Visibility_Lambda([this]() { return bHowTo ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBorder).BorderImage(FN7Wood::StoneBrush()).Padding(FMargin(34.f, 24.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(FN7Style::Type::Font(26, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.84f, 0.5f))
						.Text(LOCTEXT("HowTitle", "HOW TO PLAY"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(FN7Style::Type::Font(15, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.95f, 0.92f, 0.85f))
						.WrapTextAt(700.f).Text_Lambda([this]() { return FText::FromString(HowToText()); })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 18.f, 0.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
						.OnClicked_Lambda([this]()
						{
							bHowTo = false;
							FFileHelper::SaveStringToFile(TEXT("seen"), *(FPaths::ProjectSavedDir() / TEXT("FN7SeenHowToPlay.txt")));
							return FReply::Handled();
						})
						[
							SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(1.f, 0.8f, 0.42f)).Padding(FMargin(24.f, 7.f))
							[
								SNew(STextBlock).Font(FN7Style::Type::Font(15, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.12f, 0.07f, 0.03f))
								.Text(LOCTEXT("HowGotIt", "GOT IT"))
							]
						]
					]
				]
			]
		]
		// Can't reach the game: a centred onyx card over the table with the net client's plain words.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(640.f)
			.Visibility_Lambda([this]() { return NetProblem.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			[
				SNew(SBorder).BorderImage(FN7Wood::StoneBrush()).Padding(FMargin(30.f, 22.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(FN7Style::Type::Font(22, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.84f, 0.5f))
						.Text(LOCTEXT("NetCardTitle", "CAN'T REACH THE GAME"))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(FN7Style::Type::Font(15, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.93f, 0.9f, 0.84f))
						.WrapTextAt(580.f).Justification(ETextJustify::Center)
						.Text_Lambda([this]() { return FText::FromString(NetProblem); })
					]
				]
			]
		]
		// The duel tray: bottom centre while a duel runs (yours to type in, or one you watch).
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SAssignNew(Tray, SFNDuelTray)
			.Visibility_Lambda([this]() { return Duel.IsValid() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			.Session_Lambda([this]() { return Duel; })
			.Caption_Lambda([this]()
			{
				AFNStudio* St = FindStudio();
				const AFNDuelBox* B = St ? AFNDuelBox::Get(St->GetWorld(), false) : nullptr;
				return (B && Duel.IsValid() && B->DuelId() == Duel->DuelId) ? B->CurrentCaption() : FString();
			})
			.ServerNowMs_Lambda([this]() { UFNNetClient* N = Net.Get(); return N ? N->NowServerMs() : 0.0; })
			.OnSend_Lambda([this](const FString& Line) { DuelSendLine(Line); })
			.OnTyping_Lambda([this](const FString& Text) { DuelTyping(Text); })
		]
		// The victory screen: a veil, then an onyx tablet with the champion (never takes input: the next match is coming).
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FN7Style::Brush::Segment(FLinearColor::Black))
			.BorderBackgroundColor_Lambda([this]() { return FLinearColor(0.f, 0.f, 0.02f, 0.6f * VictoryAlpha()); })
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, VictoryAlpha()); })
			.Visibility_Lambda([this]() { return bVictory ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(780.f)
				[
					SNew(SBorder)
					.BorderImage(FN7Wood::MarbleBrush())
					.Padding(FMargin(40.f, 30.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock).Font(FN7Style::Type::Font(46, TEXT("Bold"))).Justification(ETextJustify::Center)
							.ColorAndOpacity_Lambda([this]() { return bVictoryMine ? FLinearColor(1.f, 0.82f, 0.3f) : FLinearColor(1.f, 0.55f, 0.3f); })
							.Text_Lambda([this]() { return VictoryTitle; })
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 14.f)
						[
							SNew(STextBlock).Font(FN7Style::Type::Font(17, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.86f, 0.82f, 0.74f))
							.WrapTextAt(680.f).Justification(ETextJustify::Center)
							.Text_Lambda([this]() { return VictorySub; })
							.Visibility_Lambda([this]() { return VictorySub.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 14.f)
						[
							SNew(SBox).WidthOverride(220.f).HeightOverride(220.f)
							.Visibility_Lambda([this]() { return bVictoryPortrait ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
							[
								SNew(SImage).Image(&VictoryPortrait)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 12.f)
						[
							SNew(STextBlock).TextStyle(&FN7Style::Type::Body()).ColorAndOpacity(FLinearColor(0.92f, 0.9f, 0.86f))
							.WrapTextAt(680.f).Justification(ETextJustify::Center)
							.Text_Lambda([this]() { return VictoryCaption; })
							.Visibility_Lambda([this]() { return VictoryCaption.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 12.f)
						[
							SNew(STextBlock).Font(FN7Style::Type::Font(14, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.78f, 0.76f, 0.72f))
							.Justification(ETextJustify::Center)
							.Text_Lambda([this]() { return VictoryStandings; })
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock).Font(FN7Style::Type::Font(15, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.95f, 0.78f, 0.4f))
							.Text_Lambda([this]()
							{
								if (VictoryFadeAt > 0.0) return LOCTEXT("VicGo", "THE NEXT MATCH BEGINS");
								if (NextMatchAt <= 0.0) return LOCTEXT("VicSoon", "NEXT MATCH SOON");
								const int32 Left = FMath::Max(0, FMath::CeilToInt(NextMatchAt - FPlatformTime::Seconds()));
								return FText::Format(LOCTEXT("VicIn", "NEXT MATCH IN {0} s"), FText::AsNumber(Left));
							})
						]
					]
				]
			]
		]
		// A challenge's triple flash (0.75 s), over the board and HUD, never taking input.
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FN7Style::Brush::Segment(FLinearColor::White))
			.Visibility(EVisibility::HitTestInvisible)
			.BorderBackgroundColor_Lambda([this]()
			{
				const double T = FlashStart < 0.0 ? 1.0 : FPlatformTime::Seconds() - FlashStart;
				if (T < 0.0 || T >= 0.75) return FLinearColor(1.f, 1.f, 1.f, 0.f);
				const double Local = FMath::Fmod(T, 0.25) / 0.25;
				return FLinearColor(1.f, 0.96f, 0.9f, (float)(0.55 * (1.0 - Local)));
			})
		]
		// The challenge confirm box: a dark veil and a stone tablet with the choice.
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FN7Style::Brush::Segment(FLinearColor(0.f, 0.f, 0.02f, 0.55f)))
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.Visibility_Lambda([this]() { return bConfirmOpen ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBox).WidthOverride(560.f)
				[
					SNew(SBorder)
					.BorderImage(FN7Wood::StoneBrush())
					.Padding(FMargin(34.f, 26.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 10.f)
						[
							SNew(STextBlock).TextStyle(&FN7Style::Type::Title()).Font(FN7Style::Type::Font(20, TEXT("Bold")))
							.ColorAndOpacity(FLinearColor(1.f, 0.42f, 0.25f)).Text_Lambda([this]() { return ConfirmTitle; })
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 18.f)
						[
							SNew(STextBlock).TextStyle(&FN7Style::Type::Body()).WrapTextAt(480.f).Justification(ETextJustify::Center)
							.Text_Lambda([this]() { return ConfirmBody; })
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 14.f, 0.f)
							[
								SNew(SBox).WidthOverride(220.f).HeightOverride(46.f)
								[
									SNew(SButton).ButtonStyle(&FN7Style::AccentButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
									.OnClicked_Lambda([this]() { ConfirmChallenge(); return FReply::Handled(); })
									[
										SNew(STextBlock).TextStyle(&FN7Style::Type::Label()).Font(FN7Style::Type::Font(14, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.05f, 0.03f, 0.02f))
										.Text_Lambda([this]() { return bConfirmTeleport ? LOCTEXT("ChalGoTp", "TELEPORT + CHALLENGE") : LOCTEXT("ChalGo", "CHALLENGE"); })
									]
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBox).WidthOverride(140.f).HeightOverride(46.f)
								[
									SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
									.OnClicked_Lambda([this]() { bConfirmOpen = false; return FReply::Handled(); })
									[
										SNew(STextBlock).TextStyle(&FN7Style::Type::Label()).Font(FN7Style::Type::Font(13, TEXT("Bold"))).Text(LOCTEXT("ChalNo", "CANCEL"))
									]
								]
							]
						]
					]
				]
			]
		]
		// The lobby: covers everything (and swallows clicks) until we are in a room, and over the game when opened.
		+ SOverlay::Slot()
		[
			SAssignNew(StartPanel, SFNStartPanel)
			.Net(Net.Get())
			.bInGame_Lambda([this]() { return bInRoom; })
			.OnBack_Lambda([this]() { SetLobbyOpen(false); })
			.Visibility_Lambda([this]() { return (!bInRoom || bLobbyOpen) ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		// The power being dragged, floating over everything (never takes input).
		+ SOverlay::Slot()
		[
			SNew(SFNDragToken)
		]
	];

	if (UFNNetClient* N = Net.Get())
	{
		MessageHandle = N->OnServerMessage.AddSP(this, &SFNHudLive::HandleMessage);
		StatusHandle = N->OnStatus.AddSP(this, &SFNHudLive::HandleStatus);
	}
	DropHandle = FFNPowerDragDrop::OnDroppedOnWorld().AddSP(this, &SFNHudLive::OnPowerDropped);
	DragOverHandle = FFNPowerDragDrop::OnDragOverWorld().AddSP(this, &SFNHudLive::OnPowerDragOver);
	DragEndHandle = FFNPowerDragDrop::OnDragEnded().AddSP(this, &SFNHudLive::OnPowerDragEnded);
	FFNPowerDragDrop::SnapToBoard().BindSP(this, &SFNHudLive::SnapPowerToBoard);
	SummonOverHandle = FFNSummonDragDrop::OnOver().AddSP(this, &SFNHudLive::OnSummonOver);
	SummonDropHandle = FFNSummonDragDrop::OnDropped().AddSP(this, &SFNHudLive::OnSummonDropped);
	if (RingInput.IsValid())
	{
		TWeakPtr<SFNHudLive> WeakSelf = StaticCastSharedRef<SFNHudLive>(AsShared());
		RingInput->SetIsMine([WeakSelf](const FString& Ref) { const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin(); return Self.IsValid() && Self->WhyNotSummon(Ref).IsEmpty(); });
		RingInput->SetOnFigureWheel([WeakSelf](const FString& Ref, float Delta) { const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin(); return Self.IsValid() && Self->OnDemigodWheel(Ref, Delta); });
		RingInput->SetOnFigureDoubleClick([WeakSelf](const FString& Ref) { if (const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin()) Self->OnDemigodDoubleClick(Ref); });
		RingInput->SetSummonCheck(
			[WeakSelf](const FString& Ref) { const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin(); return Self.IsValid() ? Self->WhyNotSummon(Ref) : FString(); },
			[WeakSelf](const FString& Text) { if (const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin()) Self->Toast(Text); });
	}
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNHudLive::TickTimer));

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<FFN7LobbyKeys> Keys = MakeShared<FFN7LobbyKeys>();
		TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
		Keys->OnEscape = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			if (!Self.IsValid() || !Self->bInRoom) return false;
			if (Self->bConfirmOpen) { Self->bConfirmOpen = false; return true; }   // Esc cancels a challenge confirm
			// Writing: Esc rolls the WILL back up (the text stays). Otherwise it opens or closes the lobby.
			if (!Self->bLobbyOpen && Self->Layout.IsValid() && Self->Layout->IsWillOpen()) { Self->Layout->RollUpWill(); return true; }
			Self->SetLobbyOpen(!Self->bLobbyOpen);
			return true;
		};
		Keys->OnEnter = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			// In a duel of yours, Enter goes to your line, not the WILL.
			if (Self.IsValid() && Self->Duel.IsValid() && !Self->Duel->MySide.IsEmpty() && !Self->Duel->bOver && Self->Tray.IsValid())
			{
				// Already typing in the tray: let Enter reach it (it strikes). Chris's first duel on the tray lost every line
				// here: this handler took the Enter to re-focus the box, so the box never committed.
				if (Self->Tray->HasInputFocus() || FN7TypingInText()) return false;
				Self->Tray->FocusInput();
				return true;
			}
			if (!Self.IsValid() || !Self->bInRoom || Self->bLobbyOpen || !Self->Layout.IsValid() || Self->Layout->IsWillOpen() || Self->Model.bViewer) return false;
			Self->Layout->FocusWill();
			return true;
		};
		Keys->OnReset = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			if (!Self.IsValid() || !Self->bInRoom || Self->bLobbyOpen) return false;
			Self->ResetView();
			return true;
		};
		LobbyKeys = Keys;
		FSlateApplication::Get().RegisterInputPreProcessor(LobbyKeys);
	}
}

void SFNHudLive::DebugPreview(bool bLobby, bool bFell, bool bWill)
{
	Model = SFNHudLayout::SampleModel();
	bInRoom = true;
	if (bWill)
	{
		// You hold slowed time and are writing: the scroll open, the gold edge, the live bill.
		MySeat = 0;
		WindowSeat = 0;
		bSlowBill = true;
		UFNNetClient* N = Net.Get();
		SlowOpenedMs = (N ? N->NowServerMs() : 0.0) - 8000.0;
		DebtBase = 1.0;
		DebtPerS = 0.15;
		DebtCap = 6.0;
		BoltPaid = 1;
	}
	if (bFell)
	{
		bOut = true;
		bOutDismissed = false;
		OutBeat = Model.Beat;
		OutReason = TEXT("(preview) Both of your demigods have fallen: the Left Hand to Mara's army at Tashkent, the Right Hand to the flood.");
		OutNote.Reset();
	}
	Model.bPowerPanel = FindStudio() == nullptr;
	if (Layout.IsValid()) Layout->SetModel(Model);   // the sample seats as they are (Publish would rebuild them from the live list)
	UpdateRing();
	if (bWill && Layout.IsValid()) Layout->FocusWill();
	PreviewTable();
	SetLobbyOpen(bLobby);
}

void SFNHudLive::ResetView()
{
	GlobeHoldUntil = 0.0;
	AFNStudio* St = FindStudio();
	if (St)
	{
		St->ResetView();
		if (UWorld* W = St->GetWorld())
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (APawn* Me = PC->GetPawn()) Me->SetActorLocation(St->GetSeatLocation());
				PC->SetControlRotation(St->GetSeatRotation());
			}
	}
	SFNPanelMover::ResetAll();
	AddNews(LOCTEXT("ViewTag", "VIEW"), TEXT("Back to the start: the board on your demigod, the camera at your seat, the panels home."));
	Publish();
}

void SFNHudLive::SetLobbyOpen(bool bOpen)
{
	bLobbyOpen = bOpen;
	if (bOpen && StartPanel.IsValid()) StartPanel->Opened();
}

void SFNHudLive::ResetForNewRoom()
{
	bInRoom = false;
	bLobbyOpen = false;
	bOut = false;
	bTakeAsked = false;
	Seats.Reset();
	MySeat = -1;
	Model.Seats.Reset();
	Model.Powers.Reset();
	Model.News.Reset();
	Model.BeatsTotal = 0;
	Model.Beat = 0;
	Model.Room = FText::GetEmpty();
}

TSharedRef<SWidget> SFNHudLive::MakeOutCard()
{
	using namespace FN7Style;
	const FLinearColor OutInk(0.1f, 0.06f, 0.02f);
	return SNew(SBox)
		.WidthOverride(520.f)
		.Visibility_Lambda([this]() { return ((bOut || bWiped) && !bOutDismissed && !bLobbyOpen && bInRoom) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder)
			.BorderImage(Brush::Card())
			.Padding(FMargin(Space::XL, Space::L))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).TextStyle(&Type::Title()).Text_Lambda([this]() { return bOut ? FText::Format(LOCTEXT("YouFell", "You fell at beat {0}."), OutBeat) : LOCTEXT("BothFell", "Both of your demigods have fallen."); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true).Text_Lambda([this]() { return bOut ? FText::FromString(OutReason) : LOCTEXT("PleaHow", "Your WILL still works: plead to Death (\"my demigods come back to life\"). Death names a price in sparks, or a quest."); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Color::Accent()).AutoWrapText(true)
					.Visibility_Lambda([this]() { return OutNote.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					.Text_Lambda([this]() { return FText::FromString(OutNote); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(SBox).HeightOverride(40.f)
						.Visibility_Lambda([this]() { return (bWiped && !bOut) ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.VAlign(VAlign_Center)
							.OnClicked_Lambda([this]()
							{
								bOutDismissed = true;
								if (Layout.IsValid()) Layout->FocusWill();   // the plea goes in the WILL
								return FReply::Handled();
							})
							[
								SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(OutInk).Text(LOCTEXT("Plead", "PLEAD TO DEATH"))
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(SBox).HeightOverride(40.f)
						.Visibility_Lambda([this]() { return bOut ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.VAlign(VAlign_Center)
							.IsEnabled_Lambda([this]() { return !bTakeAsked; })
							.OnClicked_Lambda([this]()
							{
								if (UFNNetClient* N = Net.Get()) N->TakeSeat();
								bTakeAsked = true;
								TakeAskedAt = FPlatformTime::Seconds();
								OutNote = TEXT("Asking for a free demigod...");
								return FReply::Handled();
							})
							[
								SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(OutInk).Text(LOCTEXT("TakeAnother", "TAKE ANOTHER DEMIGOD"))
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(&PillButton())
						.OnClicked_Lambda([this]() { bOutDismissed = true; return FReply::Handled(); })
						[
							SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("OutWatch", "WATCH"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(&PillButton())
						.OnClicked_Lambda([this]() { SetLobbyOpen(true); return FReply::Handled(); })
						[
							SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("OutLobby", "RETURN TO LOBBY"))
						]
					]
				]
			]
		];
}

SFNHudLive::~SFNHudLive()
{
	if (UFNNetClient* N = Net.Get())
	{
		N->OnServerMessage.Remove(MessageHandle);
		N->OnStatus.Remove(StatusHandle);
	}
	FFNPowerDragDrop::OnDroppedOnWorld().Remove(DropHandle);
	FFNPowerDragDrop::OnDragOverWorld().Remove(DragOverHandle);
	FFNPowerDragDrop::OnDragEnded().Remove(DragEndHandle);
	FFNPowerDragDrop::SnapToBoard().Unbind();
	FFNSummonDragDrop::OnOver().Remove(SummonOverHandle);
	FFNSummonDragDrop::OnDropped().Remove(SummonDropHandle);
	if (LobbyKeys.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(LobbyKeys);
}

void SFNHudLive::HandleStatus(const FString& Status)
{
	Model.Status = FText::FromString(Status);
	// The net client's plain words when it can't reach the game ("Can't reach the game server: ... Retrying...").
	const bool bProblem = Status.StartsWith(TEXT("Can't")) || Status.Contains(TEXT("Retrying")) || Status.Contains(TEXT("Disconnected"))
		|| Status.Contains(TEXT("access key")) || Status.Contains(TEXT("internet")) || Status.Contains(TEXT("reconnecting"));
	NetProblem = bProblem ? Status : FString();
	Publish();
}

void SFNHudLive::HandleMessage(const FString& T, TSharedPtr<FJsonObject> D)
{
	NetProblem.Reset();   // the server answered
	// JOIN = TAKE A SEAT: joined as a viewer (the house is replaying, or between seats), ask for a seat once. The net's
	// bWatchOnly (WATCH) and bSeatAsked (the shared once-only guard; the root resets it at match.ended) decide.
	if (T == TEXT("room.snapshot") || T == TEXT("match.snapshot") || T == TEXT("room.updated"))
		if (UFNNetClient* N = Net.Get())
			if (N->bViewer && !N->bWatchOnly && !N->bSeatAsked)
			{
				N->bSeatAsked = true;
				N->TakeSeat();
				Model.Status = LOCTEXT("TakingSeat", "Taking a seat...");
			}
	// TAKE ANOTHER DEMIGOD answered: the house moved us to the lowest active bot seat (from the next round).
	if (T == TEXT("seat.retaken"))
	{
		const bool bMine = FN7HudJson::Str(D, TEXT("display_name")) == Model.YouName.ToString() || (int32)FN7HudJson::Num(D, TEXT("from_seat"), -2) == MySeat;
		if (bMine)
		{
			MySeat = (int32)FN7HudJson::Num(D, TEXT("to_seat"), MySeat);
			bOut = false;
			bTakeAsked = false;
			OutNote.Reset();
			Model.Status = LOCTEXT("Retaken", "You take over a new demigod from the next beat.");
			Publish();
			return;
		}
	}
	if (T == TEXT("seat.joined"))
	{
		Model.Status = FText::GetEmpty();
		if (FN7HudJson::Str(D, TEXT("display_name")) == Model.YouName.ToString() || (int32)FN7HudJson::Num(D, TEXT("seat"), -2) == MySeat)
		{
			bOut = false;   // took another demigod
			bTakeAsked = false;
		}
	}

	if (T == TEXT("auth.ok"))
	{
		Model.YouName = FText::FromString(FN7HudJson::Str(D, TEXT("display_name")));
	}
	else if (T == TEXT("room.snapshot") || T == TEXT("room.updated"))
	{
		bInRoom = true;
		const FString Code = FN7HudJson::Str(D, TEXT("code"));
		if (!Code.IsEmpty()) Model.Room = FText::FromString(Code);
		if (FN7HudJson::Str(D, TEXT("state")) == TEXT("open"))
			Model.Status = FText::Format(LOCTEXT("Lobby", "Room {0} is open. Give the code to the other players; the host starts the match."), Model.Room);
	}
	else if (T == TEXT("match.snapshot"))
	{
		bInRoom = true;
		if (FN7HudJson::Has(D, TEXT("match_id"))) NewMatch(FN7HudJson::Str(D, TEXT("match_id")));
		Round = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
		const TSharedPtr<FJsonObject> PV = FN7HudJson::Obj(D, TEXT("public_state"));
		Model.Beat = (int32)FN7HudJson::Num(PV, TEXT("beat"), Model.Beat);
		Model.BeatsTotal = (int32)FN7HudJson::Num(PV, TEXT("beats_total"), Model.BeatsTotal);
		Model.Alive = (int32)FN7HudJson::Num(PV, TEXT("alive"), Model.Alive);
		ReadEntropy(FN7HudJson::Obj(PV, TEXT("entropy")));
		ReadSeatView(FN7HudJson::Obj(FN7HudJson::Obj(D, TEXT("you")), TEXT("private_state")));
		ReadDemigods(PV);
		RestoreDuels(FN7HudJson::Obj(PV, TEXT("present")));   // an open duel, at the exact present (schema 8+)
		// The power hexes at once on joining (not a beat later): the snapshot's brief, else ask the powers query.
		if (const TArray<TSharedPtr<FJsonValue>>* P = FN7HudJson::Arr(FN7HudJson::Obj(D, TEXT("brief")), TEXT("powers"))) ReadPowers(*P);
		if (Model.Powers.Num() == 0) FetchPowers();
		Model.Status = FText::GetEmpty();
	}
	else if (T == TEXT("match.started"))
	{
		if (bVictory && VictoryFadeAt <= 0.0) VictoryFadeAt = FPlatformTime::Seconds();   // the next one: fade it out
		bInRoom = true;
		NewMatch(FN7HudJson::Has(D, TEXT("match_id")) ? FN7HudJson::Str(D, TEXT("match_id")) : FString::Printf(TEXT("m%.0f"), FPlatformTime::Seconds()));
		bOut = false;
		bTakeAsked = false;
		Model.Status = LOCTEXT("Started", "The match has begun.");
		Model.News.Reset();
	}
	else if (T == TEXT("phase.changed"))
	{
		Round = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
		if (FN7HudJson::Str(D, TEXT("phase")) == TEXT("compose") && WindowSeat < 0) DeadlineMs = FN7HudJson::Num(D, TEXT("deadline"), 0.0);
	}
	else if (T == TEXT("round.brief"))
	{
		bInRoom = true;
		const TSharedPtr<FJsonObject> Pub = FN7HudJson::Obj(D, TEXT("public"));
		Round = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
		Model.Beat = (int32)FN7HudJson::Num(Pub, TEXT("beat"), Round);
		HandleLooks(Pub);
		Model.BeatsTotal = (int32)FN7HudJson::Num(Pub, TEXT("beats_total"), Model.BeatsTotal);
		Model.Alive = (int32)FN7HudJson::Num(Pub, TEXT("alive"), Model.Alive);
		ReadEntropy(FN7HudJson::Obj(Pub, TEXT("entropy")));
		if (const TArray<TSharedPtr<FJsonValue>>* P = FN7HudJson::Arr(Pub, TEXT("powers"))) ReadPowers(*P);
		if (const TSharedPtr<FJsonObject> Arena = FN7HudJson::Obj(Pub, TEXT("arena")))
			Model.ArenaLine = FText::Format(LOCTEXT("Arena", "Time's circle: {0} km around {1}. Stay inside."),
				FText::AsNumber((int32)FN7HudJson::Num(Arena, TEXT("radius_km"))), FText::FromString(FN7HudJson::Str(Arena, TEXT("city"))));
		ReadSeatView(FN7HudJson::Obj(D, TEXT("private")));
		if (Seats.Num() == 0) FetchGlobe();   // the PublicView only comes at the first commit: ask the globe query now
		Model.Status = FText::GetEmpty();
	}
	else if (T == TEXT("round.private_state"))
	{
		ReadSeatView(FN7HudJson::Obj(D, TEXT("state")));
	}
	else if (T == TEXT("round.committed"))
	{
		const TSharedPtr<FJsonObject> PV = FN7HudJson::Obj(D, TEXT("public_state"));
		Model.Alive = (int32)FN7HudJson::Num(PV, TEXT("alive"), Model.Alive);
		ReadEntropy(FN7HudJson::Obj(PV, TEXT("entropy")));
		ReadDemigods(PV);
	}
	else if (T == TEXT("round.started"))
	{
		HandleLooks(FN7HudJson::Obj(D, TEXT("public")));   // a look's "ready" twin can ride here
		// The built-in AI challenges too (0.8.1): its locks come at the beat's start, same shape as challenge.lock.
		if (const TArray<TSharedPtr<FJsonValue>>* Ch = FN7HudJson::Arr(FN7HudJson::Obj(D, TEXT("public")), TEXT("challenges")))
			for (const TSharedPtr<FJsonValue>& V : *Ch)
				if (const TSharedPtr<FJsonObject> Lk = V.IsValid() ? V->AsObject() : nullptr) HandleChallengeLock(Lk);
	}
	else if (T == TEXT("round.outcome"))
	{
		const TSharedPtr<FJsonObject> Pub = FN7HudJson::Obj(D, TEXT("public"));
		ReadEntropy(FN7HudJson::Obj(Pub, TEXT("entropy")));
		HandleDuelsOutcome(Pub);   // duels_open[] (the live score) and duels[] (challenge.end)
		HandleLooks(Pub);          // gear looks: look.changed (and its "ready" twin)
		const FText BeatTag = FText::Format(LOCTEXT("BeatTag", "BEAT {0}"), (int32)FN7HudJson::Num(Pub, TEXT("beat"), Round));
		if (const TArray<TSharedPtr<FJsonValue>>* Events = FN7HudJson::Arr(Pub, TEXT("events")))
		{
			int32 Added = 0;
			for (int32 i = Events->Num() - 1; i >= 0 && Added < 3; --i)
			{
				const TSharedPtr<FJsonObject> E = (*Events)[i]->AsObject();
				const FString Text = FN7HudJson::Str(E, TEXT("text"));
				if (FN7HudJson::Num(E, TEXT("magnitude"), 0.0) < 4.0 || Text.IsEmpty()) continue;
				AddNews(BeatTag, Text);
				++Added;
			}
		}
		const FString Caption = FN7HudJson::Str(Pub, TEXT("previous_caption"));
		if (!Caption.IsEmpty()) AddNews(LOCTEXT("Story", "STORY"), Caption);
		// The DM's decisions and the d20 as WORLD lines (Benito's dice/banners draw in his hidden root HUD): at most two
		// DM lines a beat, and only the rolls worth a glance (natural 1s and 20s, and your own demigods').
		if (const TArray<TSharedPtr<FJsonValue>>* Dm = FN7HudJson::Arr(Pub, TEXT("dm_events")))
		{
			int32 Shown = 0;
			for (const TSharedPtr<FJsonValue>& V : *Dm)
			{
				const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
				const FString Text = FN7HudJson::Str(E, TEXT("text"));
				const FString Ev = FN7HudJson::Str(E, TEXT("event"));
				if (Text.IsEmpty() || Ev == TEXT("summoned")) continue;   // summons have their own banner
				AddNews(LOCTEXT("DmTag", "DM"), Text);
				if (++Shown >= 2) break;
			}
		}
		if (const TArray<TSharedPtr<FJsonValue>>* Dice = FN7HudJson::Arr(Pub, TEXT("dice")))
		{
			int32 Shown = 0;
			for (const TSharedPtr<FJsonValue>& V : *Dice)
			{
				const TSharedPtr<FJsonObject> R = V.IsValid() ? V->AsObject() : nullptr;
				if (!R.IsValid()) continue;
				const FString Who = FN7HudJson::Str(R, TEXT("who"));
				const FString Verdict = FN7HudJson::Str(R, TEXT("verdict"));
				const bool bCrit = Verdict.StartsWith(TEXT("crit"));
				if (!bCrit && !MineRefs.Contains(Who)) continue;
				const int32 Roll = (int32)FN7HudJson::Num(R, TEXT("roll"), 0.0), Mod = (int32)FN7HudJson::Num(R, TEXT("modifier"), 0.0);
				const int32 Total = (int32)FN7HudJson::Num(R, TEXT("total"), (double)(Roll + Mod)), Dc = (int32)FN7HudJson::Num(R, TEXT("target_dc"), 0.0);
				const FString Why = FN7HudJson::Str(R, TEXT("reason"));
				AddNews(LOCTEXT("D20Tag", "D20"), FString::Printf(TEXT("%s rolls %d%s = %d vs %d: %s%s"), *NameOf(Who), Roll,
					Mod ? *FString::Printf(TEXT(" %+d"), Mod) : TEXT(""), Total, Dc, *Verdict.Replace(TEXT("_"), TEXT(" ")),
					Why.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Why)));
				if (++Shown >= 2) break;
			}
		}
		// Summons that landed this beat (rules 0.8.0): the same event comes in summons[], events[] and dm_events[].
		{
			const int32 Beat = (int32)FN7HudJson::Num(Pub, TEXT("beat"), Round);
			const TArray<TSharedPtr<FJsonValue>>* Dice = FN7HudJson::Arr(Pub, TEXT("dice"));
			for (const TCHAR* Key : { TEXT("summons"), TEXT("events"), TEXT("dm_events") })
				if (const TArray<TSharedPtr<FJsonValue>>* L = FN7HudJson::Arr(Pub, Key))
					for (const TSharedPtr<FJsonValue>& V : *L)
					{
						const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
						if (!E.IsValid()) continue;
						const bool bSummoned = FN7HudJson::Str(E, TEXT("kind")) == TEXT("summoned") || FN7HudJson::Str(E, TEXT("event")) == TEXT("summoned")
							|| (FCString::Strcmp(Key, TEXT("summons")) == 0 && (FN7HudJson::Has(E, TEXT("demigod")) || FN7HudJson::Has(E, TEXT("demigod_ref"))));
						if (bSummoned) HandleSummoned(E, Dice, Beat);
					}
		}
	}
	else if (T == TEXT("round.private"))
	{
		const TSharedPtr<FJsonObject> Priv = FN7HudJson::Obj(D, TEXT("private"));
		if (const TArray<TSharedPtr<FJsonValue>>* Hands = FN7HudJson::Arr(Priv, TEXT("hands")))
			for (const TSharedPtr<FJsonValue>& H : *Hands)
			{
				const FString Said = FN7HudJson::Str(H->AsObject(), TEXT("said"));
				if (!Said.IsEmpty()) AddNews(LOCTEXT("Hands", "HANDS"), Said);
			}
		// Rules 0.9.0: every will's answer, part by part, where the player looks (healing and arming currently do nothing):
		// the result always; why it fell short and what would work, when the part says.
		if (const TArray<TSharedPtr<FJsonValue>>* Wills = FN7HudJson::Arr(Priv, TEXT("wills")))
		{
			const FText WillTag = LOCTEXT("WillTag", "WILL");
			for (const TSharedPtr<FJsonValue>& Wv : *Wills)
			{
				const TSharedPtr<FJsonObject> Wo = Wv.IsValid() ? Wv->AsObject() : nullptr;
				if (!Wo.IsValid()) continue;
				const TArray<TSharedPtr<FJsonValue>>* Parts = FN7HudJson::Arr(Wo, TEXT("parts"));
				int32 Shown = 0;
				if (Parts)
					for (const TSharedPtr<FJsonValue>& Pv : *Parts)
					{
						const TSharedPtr<FJsonObject> Part = Pv.IsValid() ? Pv->AsObject() : nullptr;
						const FString Result = FN7HudJson::Str(Part, TEXT("result"));
						if (Result.IsEmpty()) continue;
						FString Line = Result;
						const FString Why = FN7HudJson::Str(Part, TEXT("why")), Next = FN7HudJson::Str(Part, TEXT("next"));
						if (!Why.IsEmpty()) Line += TEXT(". ") + Why;
						if (!Next.IsEmpty()) Line += TEXT(". Next: ") + Next;
						AddNews(WillTag, Line);
						if (++Shown >= 4) break;
					}
				if (Shown == 0)
				{
					const FString Reason = FN7HudJson::Str(Wo, TEXT("reason"));
					if (!Reason.IsEmpty()) AddNews(WillTag, Reason);
				}
				UE_LOG(LogTemp, Display, TEXT("FN7 will: \"%s\" -> %s (%d parts)"), *FN7HudJson::Str(Wo, TEXT("text")).Left(60),
					*FN7HudJson::Str(Wo, TEXT("verdict")), Parts ? Parts->Num() : 0);
			}
		}
	}
	else if (T == TEXT("window.opened"))
	{
		WindowSeat = (int32)FN7HudJson::Num(D, TEXT("by_seat"), -1);
		DeadlineMs = FN7HudJson::Num(D, TEXT("deadline"), 0.0);
	}
	else if (T == TEXT("phase.event"))
	{
		const TSharedPtr<FJsonObject> Pub = FN7HudJson::Obj(D, TEXT("public"));
		// A summon accepted (rules 0.8.0): the piece lifts and dissolves at its origin, for everyone.
		if (FN7HudJson::Str(Pub, TEXT("event")) == TEXT("summoning")) { HandleSummoning(Pub); return; }
		if (FN7HudJson::Str(Pub, TEXT("event")) == TEXT("challenge.lock")) { HandleChallengeLock(Pub); return; }
		{
			const FString Ev = FN7HudJson::Str(Pub, TEXT("event"));
			if (Ev == TEXT("duel.scene") || Ev == TEXT("duel.panel") || Ev == TEXT("duel.typing") || Ev == TEXT("duel.blow")
				|| Ev == TEXT("duel.typing_script") || Ev == TEXT("challenge.end")) { HandleDuelEvent(Pub); return; }
		}
		// Time slowed: when it is you, the Slowed event carries the bill's terms (rules 0.7.0).
		if (FN7HudJson::Str(Pub, TEXT("event")) == TEXT("slowed") && (int32)FN7HudJson::Num(Pub, TEXT("by_seat"), -2) == MySeat && MySeat >= 0)
		{
			bSlowBill = FN7HudJson::Has(Pub, TEXT("debt_per_second"));
			UFNNetClient* N = Net.Get();
			SlowOpenedMs = FN7HudJson::Num(Pub, TEXT("opened_at_ms"), N ? N->NowServerMs() : 0.0);
			DebtBase = FN7HudJson::Num(Pub, TEXT("debt_base"), 0.0);
			DebtPerS = FN7HudJson::Num(Pub, TEXT("debt_per_second"), 0.0);
			DebtCap = FN7HudJson::Num(Pub, TEXT("debt_cap"), 1.0e9);
			BoltPaid = FN7HudJson::Has(Pub, TEXT("bolt_paid")) ? (int32)FN7HudJson::Num(Pub, TEXT("bolt_paid"), 1.0) : 1;
		}
		return;
	}
	else if (T == TEXT("window.closed"))
	{
		bSlowBill = false;
		WindowSeat = -1;
		DeadlineMs = 0.0;
		if (UFNNetClient* N = Net.Get()) N->EndTyping();
	}
	else if (T == TEXT("action.received"))
	{
		bool bAccepted = true;
		if (D.IsValid()) D->TryGetBoolField(TEXT("accepted"), bAccepted);
		if (!bAccepted)
		{
			const FString Reason = FN7HudJson::Str(D, TEXT("reason"));
			static const TMap<FString, FString> SummonWhy = {
				{ TEXT("not_yours"), TEXT("That's not your demigod.") },
				{ TEXT("fallen"), TEXT("That demigod has fallen.") },
				{ TEXT("cooldown"), TEXT("Once a beat: that demigod was just summoned.") },
				{ TEXT("bad_target"), TEXT("It can't be summoned there.") },
				{ TEXT("summon_disabled"), TEXT("Summoning is off in this room.") } };
			static const TMap<FString, FString> ChallengeWhy = {
				{ TEXT("too_far"), TEXT("Too far: get one of yours into its hex to challenge.") },
				{ TEXT("busy"), TEXT("Already in a duel.") },
				{ TEXT("cooldown"), TEXT("Not yet: you challenged a moment ago.") },
				{ TEXT("fallen"), TEXT("That demigod has fallen.") },
				{ TEXT("not_yours"), TEXT("That's not your demigod.") } };
			if (!PendingChallengeRef.IsEmpty() && FPlatformTime::Seconds() - PendingChallengeAt < 8.0)
			{
				int32 Colon = INDEX_NONE;
				const FString Key = Reason.FindChar(TEXT(':'), Colon) ? Reason.Left(Colon) : Reason;   // "too_far:<km>:<allowed>"
				const FString* Why = Reason == TEXT("busy:player") ? nullptr : ChallengeWhy.Find(Key);
				if (Reason == TEXT("busy:player")) { static const FString OneAtATime(TEXT("One duel at a time: another of your demigods is already fighting.")); Why = &OneAtATime; }
				UE_LOG(LogTemp, Display, TEXT("FN7 challenge refused: %s %s"), *PendingChallengeRef, *Reason);
				PendingChallengeRef.Reset();
				Toast(Why ? *Why : FString::Printf(TEXT("Challenge refused: %s"), *Reason));
				Publish();
				return;
			}
			const bool bPending = !PendingSummonRef.IsEmpty() && FPlatformTime::Seconds() - PendingSummonAt < 8.0;
			if (bPending && (SummonWhy.Contains(Reason) || (D.IsValid() && D->HasField(TEXT("instant")))))
			{
				// Your summon was refused: the piece comes back with a red shake, and why.
				const FString* Why = SummonWhy.Find(Reason);
				UE_LOG(LogTemp, Display, TEXT("FN7 summon refused: %s %s"), *PendingSummonRef, *Reason);
				if (AFNTableGods* Tg = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr)) Tg->CancelSummon(PendingSummonRef);
				PendingSummonRef.Reset();
				Toast(Why ? *Why : FString::Printf(TEXT("Summon refused: %s"), *Reason));
				AddNews(LOCTEXT("Refused", "REFUSED"), Why ? *Why : Reason);
			}
			else AddNews(LOCTEXT("Refused", "REFUSED"), Reason == TEXT("cut") ? TEXT("Too slow: the window closed before your orders arrived.") : Reason);
		}
	}
	else if (T == TEXT("error"))
	{
		if (!PendingChallengeRef.IsEmpty() && FPlatformTime::Seconds() - PendingChallengeAt < 8.0)
		{
			const FString Msg = FN7HudJson::Str(D, TEXT("message"));
			UE_LOG(LogTemp, Display, TEXT("FN7 challenge error: %s %s"), *PendingChallengeRef, *Msg);
			PendingChallengeRef.Reset();
			Toast(Msg.Contains(TEXT("challenge")) || FN7HudJson::Str(D, TEXT("code")) == TEXT("bad_request") ? FString(TEXT("Challenges aren't live on the server yet.")) : Msg);
			Publish();
			return;
		}
		// A summon refused with an error frame (e.g. wrong_phase "you join the match at the next round"): it comes back.
		if (!PendingSummonRef.IsEmpty() && FPlatformTime::Seconds() - PendingSummonAt < 8.0)
		{
			const FString Msg = FN7HudJson::Str(D, TEXT("message"));
			UE_LOG(LogTemp, Display, TEXT("FN7 summon refused: %s %s"), *PendingSummonRef, *Msg);
			if (AFNTableGods* Tg = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr)) Tg->CancelSummon(PendingSummonRef);
			PendingSummonRef.Reset();
			Toast(Msg.IsEmpty() ? FString(TEXT("Summon refused.")) : Msg);
			Publish();
			return;
		}
		if (bOut && bTakeAsked)
		{
			bTakeAsked = false;
			OutNote = Model.BeatsTotal > Model.Beat
				? FString::Printf(TEXT("No demigod is free right now. The next match starts in about %d beats."), Model.BeatsTotal - Model.Beat)
				: FString(TEXT("No demigod is free right now. You'll get one next match."));
			return;
		}
		if (FN7HudJson::Str(D, TEXT("code")) == TEXT("room_full"))
		{
			Model.Status = LOCTEXT("TableFull", "Table full: watching. You'll get a seat next match.");
			Publish();
			return;
		}
		AddNews(LOCTEXT("Error", "ERROR"), FN7HudJson::Str(D, TEXT("message")));
	}
	else if (T == TEXT("seat.eliminated"))
	{
		if ((int32)FN7HudJson::Num(D, TEXT("seat"), -2) == MySeat && MySeat >= 0)
		{
			// Rules 0.7.0: this comes once, when both of your demigods have fallen.
			OutReason = FN7HudJson::Str(D, TEXT("reason"));
			OutBeat = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
			OutNote.Reset();
			bOut = true;
			bOutDismissed = false;
			bTakeAsked = false;
			AddNews(LOCTEXT("Fallen", "FALLEN"), FString::Printf(TEXT("You fell: %s"), *OutReason));
		}
	}
	else if (T == TEXT("seat.revived"))
	{
		if ((int32)FN7HudJson::Num(D, TEXT("seat"), -2) == MySeat) bOut = false;
	}
	else if (T == TEXT("room.left"))
	{
		ResetForNewRoom();
	}
	else if (T == TEXT("match.ended"))
	{
		if (UFNNetClient* N = Net.Get()) N->bSeatAsked = false;   // still a viewer next match: JOIN asks again
		const TSharedPtr<FJsonObject> Summary = FN7HudJson::Obj(D, TEXT("summary"));
		FString Champ;
		if (const TArray<TSharedPtr<FJsonValue>>* C = FN7HudJson::Arr(Summary, TEXT("champions")))
			if (C->Num() > 0) Champ = FN7HudJson::Str((*C)[0]->AsObject(), TEXT("name"));
		Model.Status = FText::Format(LOCTEXT("Over", "MATCH OVER  -  champion: {0}"), FText::FromString(Champ));
		DeadlineMs = 0.0;
		WindowSeat = -1;
		ShowVictory(Summary);
	}
	else
	{
		return;
	}
	Publish();
}

void SFNHudLive::ReadSeatView(const TSharedPtr<FJsonObject>& View)
{
	if (!View.IsValid()) return;
	MySeat = (int32)FN7HudJson::Num(View, TEXT("seat"), MySeat);
	const FString Name = FN7HudJson::Str(View, TEXT("name"));
	if (!Name.IsEmpty()) Model.YouName = FText::FromString(Name);
	Model.MaxHealth = FMath::RoundToInt(FN7HudJson::Num(View, TEXT("max_health"), Model.MaxHealth));
	if (const TSharedPtr<FJsonObject> You = FN7HudJson::Obj(View, TEXT("you")))
	{
		Model.Health = FMath::Clamp(FMath::RoundToInt(FN7HudJson::Num(You, TEXT("health"), Model.Health)), 0, Model.MaxHealth);
		Model.Bolts = (int32)FN7HudJson::Num(You, TEXT("bolts"), Model.Bolts);
		if (FN7HudJson::Has(You, TEXT("sparks"))) Model.Sparks = (int32)FN7HudJson::Num(You, TEXT("sparks"), 0.0);
	}
	// Rules 0.7.0: your two demigods in full (each with its own health), your aura, the purse, the debt.
	if (const TArray<TSharedPtr<FJsonValue>>* Hands = FN7HudJson::Arr(View, TEXT("demigods")))
	{
		Model.Mine.Reset();
		MinePortraitHash.Reset();
		MineDioramaHash.Reset();
		double DebtSum = 0.0;
		bool bDebt = false;
		for (const TSharedPtr<FJsonValue>& V : *Hands)
		{
			const TSharedPtr<FJsonObject> G = V.IsValid() ? V->AsObject() : nullptr;
			if (!G.IsValid()) continue;
			FFNHudDemigod Dg;
			Dg.Ref = FN7HudJson::Str(G, TEXT("ref"));
			Dg.Name = FText::FromString(FN7HudJson::Str(G, TEXT("name")));
			Dg.Side = FN7HudJson::Str(G, TEXT("side"));
			Dg.MaxHealth = FMath::Max(1, FMath::RoundToInt(FN7HudJson::Num(G, TEXT("max_health"), 10.0)));
			Dg.Health = FMath::Clamp(FMath::RoundToInt(FN7HudJson::Num(G, TEXT("health"), Dg.MaxHealth)), 0, Dg.MaxHealth);
			bool bAliveHand = true;
			G->TryGetBoolField(TEXT("alive"), bAliveHand);
			Dg.bAlive = bAliveHand;
			if (FN7HudJson::Has(G, TEXT("debt_open"))) { DebtSum += FN7HudJson::Num(G, TEXT("debt_open"), 0.0); bDebt = true; }
			FString Hash, DioHash;
			Dg.Gear = FN7Looks::Gear(G);
			if (const FNewItem* NI = NewItems.Find(Dg.Ref)) if (FPlatformTime::Seconds() - NI->At < 6.0) Dg.NewItem = NI->Name;
			if (const TSharedPtr<FJsonObject> Img = FN7Looks::Image(G, TEXT("portrait_image")))
			{
				Hash = FN7HudJson::Str(Img, TEXT("hash"));
				const FString Url = FN7HudJson::Str(Img, TEXT("status")) == TEXT("ready") ? FN7HudJson::Str(Img, TEXT("url")) : FString();
				if (!Hash.IsEmpty() && !Url.IsEmpty()) RequestPortrait(Hash, Url);
			}
			if (const TSharedPtr<FJsonObject> Dio = FN7Looks::Image(G, TEXT("diorama_image")))
			{
				DioHash = FN7HudJson::Str(Dio, TEXT("hash"));
				const FString Url = FN7HudJson::Str(Dio, TEXT("status")) == TEXT("ready") ? FN7HudJson::Str(Dio, TEXT("url")) : FString();
				if (!DioHash.IsEmpty() && !Url.IsEmpty()) RequestPortrait(DioHash, Url);
			}
			Model.Mine.Add(Dg);
			MinePortraitHash.Add(Hash);
			MineDioramaHash.Add(DioHash);
		}
		// The seat view's demigods ARE yours: the truth for summoning (never names: a taken-over seat keeps the bot's).
		MineRefs.Reset();
		MineAlive.Reset();
		for (const FFNHudDemigod& Dg : Model.Mine) if (!Dg.Ref.IsEmpty()) { MineRefs.Add(Dg.Ref); MineAlive.Add(Dg.Ref, Dg.bAlive); }
		// Rules 0.9.0: a wipe sends no seat.eliminated; your demigods say it, and the WILL stays open for a plea.
		{
			bool bAnyUp = false;
			for (const TPair<FString, bool>& P : MineAlive) bAnyUp |= P.Value;
			const bool bNowWiped = MineAlive.Num() > 0 && !bAnyUp;
			if (bNowWiped && !bWiped)
			{
				bWiped = true;
				bOutDismissed = false;
				OutBeat = Model.Beat;
				AddNews(LOCTEXT("WipedTag", "FALLEN"), TEXT("Both of your demigods have fallen. Plead to Death in your WILL to bring them back."));
				UE_LOG(LogTemp, Display, TEXT("FN7 wiped: both demigods down at beat %d (the WILL stays open)"), Model.Beat);
			}
			else if (!bNowWiped && bWiped)
			{
				bWiped = false;
				AddNews(LOCTEXT("BackTag", "BACK"), TEXT("A demigod of yours stands again."));
				UE_LOG(LogTemp, Display, TEXT("FN7 wiped: a demigod stands again"));
			}
		}
		if (!FN7HudJson::Has(View, TEXT("seat")))
			for (const TSharedPtr<FJsonValue>& V : *Hands)
				if (const TSharedPtr<FJsonObject> G = V.IsValid() ? V->AsObject() : nullptr)
					if (FN7HudJson::Has(G, TEXT("seat"))) { MySeat = (int32)FN7HudJson::Num(G, TEXT("seat"), MySeat); break; }
		{
			FString Log = FString::Printf(TEXT("seat=%d"), MySeat);
			for (const FFNHudDemigod& Dg : Model.Mine) Log += FString::Printf(TEXT(" %s(%s,%s)"), *Dg.Ref, *Dg.Side, Dg.bAlive ? TEXT("alive") : TEXT("fallen"));
			if (Log != LastMineLog) { LastMineLog = Log; UE_LOG(LogTemp, Display, TEXT("FN7 mine: %s"), *Log); }
		}
		if (Model.Mine.Num() == 2 && Model.Mine[0].Side == TEXT("right"))   // Left first
		{
			Model.Mine.Swap(0, 1);
			MinePortraitHash.Swap(0, 1);
			MineDioramaHash.Swap(0, 1);
		}
		if (bDebt) Model.Debt = DebtSum;
	}
	else if (const TSharedPtr<FJsonObject> You = FN7HudJson::Obj(View, TEXT("you")))
	{
		if (FN7HudJson::Has(You, TEXT("debt_open"))) Model.Debt = FN7HudJson::Num(You, TEXT("debt_open"), 0.0);
	}
	const FString Aura = FN7HudJson::Str(View, TEXT("aura"));
	if (Aura.StartsWith(TEXT("#")) && Aura.Len() >= 7) Model.YourAura = FLinearColor::FromSRGBColor(FColor::FromHex(Aura));
	if (const TSharedPtr<FJsonObject> Charges = FN7HudJson::Obj(View, TEXT("charges")))
	{
		Model.Bolts = (int32)FN7HudJson::Num(Charges, TEXT("bolts"), Model.Bolts);
		if (FN7HudJson::Has(Charges, TEXT("sparks"))) Model.Sparks = (int32)FN7HudJson::Num(Charges, TEXT("sparks"), 0.0);
	}
	const FString Player = FN7HudJson::Str(View, TEXT("player"));
	if (!Player.IsEmpty() && Model.Mine.Num() > 0) Model.YouName = FText::FromString(Player);
	Model.PowersNote = FText::Format(LOCTEXT("PowersNoteBolts", "{0} bolts  -  arm a power, write its target (place, name or lon,lat), SEND. Every bolt you spend gives each rival +2."), Model.Bolts);
}

void SFNHudLive::ReadDemigods(const TSharedPtr<FJsonObject>& PV)
{
	const TArray<TSharedPtr<FJsonValue>>* List = FN7HudJson::Arr(PV, TEXT("demigods"));
	if (!List) return;
	Seats.Reset();
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> G = V->AsObject();
		FSeatInfo S;
		S.Ref = FN7HudJson::Str(G, TEXT("ref"));
		S.Name = FN7HudJson::Str(G, TEXT("name"));
		S.Seat = FN7HudJson::Has(G, TEXT("seat")) ? (int32)FN7HudJson::Num(G, TEXT("seat"), -1) : -1;
		bool bAlive = true;
		if (G.IsValid()) G->TryGetBoolField(TEXT("alive"), bAlive);
		S.bAlive = bAlive;
		if (FN7HudJson::Has(G, TEXT("bolts"))) S.Bolts = (int32)FN7HudJson::Num(G, TEXT("bolts"), 0.0);
		if (FN7HudJson::Has(G, TEXT("sparks"))) S.Sparks = (int32)FN7HudJson::Num(G, TEXT("sparks"), 0.0);
		if (const TSharedPtr<FJsonObject> Img = FN7Looks::Image(G, TEXT("portrait_image")))
		{
			S.PortraitHash = FN7HudJson::Str(Img, TEXT("hash"));
			if (FN7HudJson::Str(Img, TEXT("status")) == TEXT("ready")) S.PortraitUrl = FN7HudJson::Str(Img, TEXT("url"));
			if (!S.PortraitHash.IsEmpty() && !S.PortraitUrl.IsEmpty()) RequestPortrait(S.PortraitHash, S.PortraitUrl);
		}
		S.Side = FN7HudJson::Str(G, TEXT("side"));
		S.Player = FN7HudJson::Str(G, TEXT("player"));
		const FString AuraHex = FN7HudJson::Str(G, TEXT("aura"));
		if (AuraHex.StartsWith(TEXT("#")) && AuraHex.Len() >= 7) { S.Aura = FLinearColor::FromSRGBColor(FColor::FromHex(AuraHex)); S.bHasAura = true; }
		S.Controller = FN7HudJson::Str(G, TEXT("controller"));
		if (G.IsValid()) G->TryGetBoolField(TEXT("connected"), S.bConnected);
		if (const TArray<TSharedPtr<FJsonValue>>* Pos = FN7HudJson::Arr(G, TEXT("pos")))
			if (Pos->Num() >= 2) { S.Lon = (*Pos)[0]->AsNumber(); S.Lat = (*Pos)[1]->AsNumber(); S.bHasPos = true; }
		if (const TSharedPtr<FJsonObject> Dio = FN7Looks::Image(G, TEXT("diorama_image")))
		{
			S.DioramaHash = FN7HudJson::Str(Dio, TEXT("hash"));
			const FString Status = FN7HudJson::Str(Dio, TEXT("status"));
			const FString Url = Status == TEXT("ready") ? FN7HudJson::Str(Dio, TEXT("url")) : FString();
			if (!S.DioramaHash.IsEmpty() && !Url.IsEmpty()) RequestPortrait(S.DioramaHash, Url);
			FString& Last = LastDioHash.FindOrAdd(S.Ref);
			if (!S.DioramaHash.IsEmpty() && Last != S.DioramaHash)
			{
				UE_LOG(LogTemp, Display, TEXT("FN7 media: %s \"%s\" diorama %s -> %s (%s)"), *S.Ref, *S.Name, Last.IsEmpty() ? TEXT("none") : *Last.Left(10), *S.DioramaHash.Left(10), *Status);
				Last = S.DioramaHash;
			}
		}
		if (S.Seat >= 0 && S.Seat == MySeat)
		{
			const FString Place = FN7HudJson::Str(G, TEXT("place"));
			const FString Region = FN7HudJson::Str(G, TEXT("region"));
			Model.Place = FText::FromString(Place == Region || Region.IsEmpty() ? Place : Place + TEXT(", ") + Region);
		}
		Seats.Add(S);
	}
	// The players (gods): public_state.players[] when the server sends it.
	if (const TArray<TSharedPtr<FJsonValue>>* PL = FN7HudJson::Arr(PV, TEXT("players")))
	{
		Players.Reset();
		for (const TSharedPtr<FJsonValue>& V : *PL)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			FPlayerInfo P;
			P.Seat = FN7HudJson::Has(O, TEXT("seat")) ? (int32)FN7HudJson::Num(O, TEXT("seat"), -1) : -1;
			P.Player = FN7HudJson::Str(O, TEXT("player"));
			const FString A = FN7HudJson::Str(O, TEXT("aura"));
			if (A.StartsWith(TEXT("#")) && A.Len() >= 7) { P.Aura = FLinearColor::FromSRGBColor(FColor::FromHex(A)); P.bHasAura = true; }
			O->TryGetBoolField(TEXT("alive"), P.bAlive);
			P.Controller = FN7HudJson::Str(O, TEXT("controller"));
			O->TryGetBoolField(TEXT("connected"), P.bConnected);
			Players.Add(P);
		}
	}
}

void SFNHudLive::ReadEntropy(const TSharedPtr<FJsonObject>& E)
{
	if (!E.IsValid()) return;
	Model.Entropy = FText::FromString(FString::Printf(TEXT("%s %d"), *FN7HudJson::Str(E, TEXT("key")).ToUpper(), FMath::RoundToInt(FN7HudJson::Num(E, TEXT("value")))));
}

void SFNHudLive::ReadPowers(const TArray<TSharedPtr<FJsonValue>>& Powers)
{
	// The powers contract: every power has a mega tier (a bolt, map scale) and a mini tier (a spark, the board),
	// each { radius_km, magnitude, cost }. A server without tiers sends cost and radius_km on the power itself.
	Model.Powers.Reset();
	for (const TSharedPtr<FJsonValue>& V : Powers)
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const FString Slug = FN7HudJson::Str(P, TEXT("slug"));
		if (Slug.IsEmpty() || Slug == TEXT("custom")) continue;
		const FText Name = FText::FromString(FN7HudJson::Str(P, TEXT("name")));
		const TSharedPtr<FJsonObject> Mega = FN7HudJson::Obj(P, TEXT("mega"));
		const TSharedPtr<FJsonObject> Mini = FN7HudJson::Obj(P, TEXT("mini"));
		auto Add = [&](const FString& Tier, const TSharedPtr<FJsonObject>& From)
		{
			FFNHudPower Out;
			Out.Slug = Slug;
			Out.Name = Name;
			Out.Tier = Tier;
			Out.Cost = (int32)FN7HudJson::Num(From, TEXT("cost"), 1);
			Out.RadiusKm = FN7HudJson::Num(From, TEXT("radius_km"), 0.0);
			PowerRadiusKm.Add(Out.Key(), Out.RadiusKm);
			Model.Powers.Add(Out);
		};
		if (Mega.IsValid() || Mini.IsValid())
		{
			if (Mega.IsValid()) Add(TEXT("mega"), Mega);
			if (Mini.IsValid()) Add(TEXT("mini"), Mini);
		}
		else
		{
			Add(FString(), P);
		}
	}
}

void SFNHudLive::AddNews(const FText& NewsTag, const FString& Text)
{
	if (Text.IsEmpty()) return;
	FFNHudNews N;
	N.Tag = NewsTag;
	N.Text = FText::FromString(Text.Len() > 220 ? Text.Left(217) + TEXT("...") : Text);
	Model.News.Insert(N, 0);
	if (Model.News.Num() > 6) Model.News.SetNum(6);
}

void SFNHudLive::FetchPowers()
{
	UFNNetClient* N = Net.Get();
	const FString Room = Model.Room.ToString();
	if (!N || Room.IsEmpty() || bPowersPending || FPlatformTime::Seconds() - PowersAskedAt < 10.0) return;   // at most every 10 s
	bPowersPending = true;
	PowersAskedAt = FPlatformTime::Seconds();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(FString::Printf(TEXT("/api/rooms/%s/query/powers"), *Room)));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(15.f);
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bPowersPending = false;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200 || Self->Model.Powers.Num() > 0) return;
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, O) || !O.IsValid()) return;
		if (const TArray<TSharedPtr<FJsonValue>>* P = FN7HudJson::Arr(O, TEXT("powers"))) Self->ReadPowers(*P);
		UE_LOG(LogTemp, Display, TEXT("FN7 ring: powers from the query: %d"), Self->Model.Powers.Num());
		Self->Publish();
	});
	Req->ProcessRequest();
}

void SFNHudLive::FetchGlobe()
{
	UFNNetClient* N = Net.Get();
	const FString Room = Model.Room.ToString();
	if (!N || Room.IsEmpty() || bGlobePending) return;
	bGlobePending = true;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(FString::Printf(TEXT("/api/rooms/%s/query/globe?limit=1"), *Room)));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(15.f);
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bGlobePending = false;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		TSharedPtr<FJsonObject> Globe;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Globe) || !Globe.IsValid()) return;
		if (Self->Seats.Num() > 0) return;   // a commit got there first
		Self->ReadDemigods(Globe);
		Self->Publish();
	});
	Req->ProcessRequest();
}

void SFNHudLive::RequestPortrait(const FString& Hash, const FString& Url)
{
	UFNNetClient* N = Net.Get();
	if (!N || Portraits.Contains(Hash) || PortraitRequests.Contains(Hash)) return;
	// The disk cache first (Saved/FN7Media): full fidelity, a full mip chain.
	{
		TArray<uint8> Cached;
		if (FN7Media::LoadCached(Hash, Cached))
			if (UTexture2D* Tex = FN7Media::TextureFromPng(Cached))
			{
				Portraits.Add(Hash, TStrongObjectPtr<UTexture2D>(Tex));
				MediaPng.Add(Hash, MoveTemp(Cached));
				return;
			}
	}
	PortraitRequests.Add(Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak, Hash](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->PortraitRequests.Remove(Hash);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		const TArray<uint8> Png = Resp->GetContent();
		UTexture2D* Tex = FN7Media::TextureFromPng(Png);
		if (!Tex) Tex = FImageUtils::ImportBufferAsTexture2D(Png);
		if (!Tex) return;
		FN7Media::SaveCached(Hash, Png);
		Self->MediaPng.Add(Hash, Png);
		Self->Portraits.Add(Hash, TStrongObjectPtr<UTexture2D>(Tex));
		Self->Publish();
	});
	Req->ProcessRequest();
}

void SFNHudLive::Publish()
{
	Model.Seats.Reset();
	for (const FSeatInfo& S : Seats)
	{
		FFNHudSeat Out;
		Out.Name = FText::FromString(S.Name);
		Out.Role = !S.bAlive ? EFNSeatRole::Fallen : (S.Seat >= 0 ? EFNSeatRole::Human : EFNSeatRole::AI);
		Out.bIsYou = S.Seat >= 0 && S.Seat == MySeat;
		Out.bHoldsTime = S.Seat >= 0 && S.Seat == WindowSeat;
		Out.Bolts = S.Bolts;
		Out.Sparks = S.Sparks;
		if (const TStrongObjectPtr<UTexture2D>* Tex = Portraits.Find(S.PortraitHash)) Out.PortraitTexture = Tex->Get();
		Model.Seats.Add(Out);
	}
	// Your cards lead with the diorama (the dioramas should lead), the portrait as a small inset.
	for (int32 i = 0; i < Model.Mine.Num(); ++i)
	{
		FString PH = MinePortraitHash.IsValidIndex(i) ? MinePortraitHash[i] : FString();
		FString DH = MineDioramaHash.IsValidIndex(i) ? MineDioramaHash[i] : FString();
		{
			const FString LD = LoadedArt(Model.Mine[i].Ref, true), LP = LoadedArt(Model.Mine[i].Ref, false);
			if (!LD.IsEmpty() && Portraits.Contains(LD)) DH = LD;
			if (!LP.IsEmpty() && Portraits.Contains(LP)) PH = LP;
		}
		UTexture2D* Portrait = MediaTexture(PH);
		UTexture2D* Dio = MediaTexture(DH, !Model.Mine[i].bAlive);
		Model.Mine[i].PortraitTexture = Dio ? Dio : Portrait;
		Model.Mine[i].InsetTexture = Dio ? Portrait : nullptr;
	}
	if (Seats.Num() > 0) Model.AliveOf = Seats.Num();
	for (FFNHudPower& P : Model.Powers) P.bArmed = P.Key() == ArmedPower;
	if (UFNNetClient* N = Net.Get()) Model.bViewer = N->bViewer;
	Model.bPowerPanel = FindStudio() == nullptr;   // the powers live on the table; the HUD rows only without one
	if (Layout.IsValid()) Layout->SetModel(Model);
	UpdateRing();
	UpdateTable();
}

void SFNHudLive::PreviewTable()
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	auto Load = [this](const TCHAR* Rel) -> FString
	{
		const FString Key = FString(TEXT("preview:")) + Rel;
		if (!Portraits.Contains(Key))
		{
			TArray<uint8> Png;
			if (FFileHelper::LoadFileToArray(Png, *FPaths::Combine(FPaths::ProjectDir(), Rel)))
				if (UTexture2D* T = FN7Media::TextureFromPng(Png))
				{
					Portraits.Add(Key, TStrongObjectPtr<UTexture2D>(T));
					MediaPng.Add(Key, Png);
				}
		}
		return Key;
	};
	const FString DioA = Load(TEXT("docs/style-refs/approved/diorama_maxzoom.png"));
	const FString DioB = Load(TEXT("docs/style-refs/approved/event_maxzoom.png"));
	struct FSample { const TCHAR* Name; int32 Seat; uint32 Aura; bool bHuman; bool bConnected; bool bRightFallen; double La, Lo; };
	const FSample Samples[] = {
		{ TEXT("Chris"), 0, 0xE69F00, true, true, false, 46.9, 8.2 },
		{ TEXT("Benito"), 1, 0x009E73, true, true, false, 45.8, 10.6 },
		{ TEXT("Vex"), 2, 0xF0E442, false, true, false, 47.6, 10.1 },
		{ TEXT("Mara"), 3, 0xF28CC0, false, false, true, 46.2, 7.4 },
	};
	TArray<FFNTableGod> Gods;
	int32 Ref = 0;
	for (const FSample& Sm : Samples)
	{
		FFNTableGod G;
		G.Seat = Sm.Seat;
		G.Player = Sm.Name;
		G.Aura = FLinearColor::FromSRGBColor(FColor((Sm.Aura >> 16) & 255, (Sm.Aura >> 8) & 255, Sm.Aura & 255));
		G.bHuman = Sm.bHuman;
		G.bConnected = Sm.bConnected;
		G.bMine = Sm.Seat == 0;
		for (int32 h = 0; h < 2; ++h)
		{
			FFNTableDemigod D;
			D.Ref = FString::Printf(TEXT("dg:%d"), Ref++);
			D.Name = FString::Printf(TEXT("%s's %s Hand"), Sm.Name, h == 0 ? TEXT("Left") : TEXT("Right"));
			D.Side = h == 0 ? TEXT("left") : TEXT("right");
			D.bAlive = !(h == 1 && Sm.bRightFallen);
			D.bHasPos = true;
			D.Lat = Sm.La + h * 0.5;
			D.Lon = Sm.Lo + h * 0.6;
			D.Diorama = MediaTexture(h == 0 ? DioA : DioB);
			D.DioramaGrey = D.bAlive ? nullptr : MediaTexture(h == 0 ? DioA : DioB, true);
			G.Demigods.Add(D);
		}
		Gods.Add(G);
	}
	if (AFNTableGods* T = AFNTableGods::Get(St, true))
	{
		T->SetGods(Gods, 0);
		T->SetShown(true);
	}
	PreviewMine = { TEXT("dg:0"), TEXT("dg:1") };
}

UTexture2D* SFNHudLive::MediaTexture(const FString& Hash, bool bGrey)
{
	if (Hash.IsEmpty()) return nullptr;
	const TStrongObjectPtr<UTexture2D>* Tex = Portraits.Find(Hash);
	if (!Tex) return nullptr;
	if (!bGrey) return Tex->Get();
	if (const TStrongObjectPtr<UTexture2D>* G = GreyMedia.Find(Hash)) return G->Get();
	if (const TArray<uint8>* Png = MediaPng.Find(Hash))
		if (UTexture2D* G = FN7Media::TextureFromPng(*Png, true))
		{
			GreyMedia.Add(Hash, TStrongObjectPtr<UTexture2D>(G));
			return G;
		}
	return Tex->Get();
}

void SFNHudLive::UpdateTable()
{
	AFNStudio* St = FindStudio();
	if (!St || Seats.Num() == 0) return;
	// Gods: the players list, else the demigods grouped by their player.
	TArray<FFNTableGod> Gods;
	auto GodFor = [&Gods](const FString& Player, int32 Seat) -> FFNTableGod&
	{
		for (FFNTableGod& G : Gods) if ((!Player.IsEmpty() && G.Player == Player) || (Player.IsEmpty() && Seat >= 0 && G.Seat == Seat)) return G;
		FFNTableGod& G = Gods.AddDefaulted_GetRef();
		G.Player = Player;
		G.Seat = Seat;
		return G;
	};
	for (const FPlayerInfo& P : Players)
	{
		FFNTableGod& G = GodFor(P.Player, P.Seat);
		G.Seat = P.Seat;
		if (P.bHasAura) G.Aura = P.Aura;
		G.bAlive = P.bAlive;
		G.bConnected = P.bConnected;
		G.bHuman = P.Controller == TEXT("human");
	}
	for (const FSeatInfo& S : Seats)
	{
		FFNTableGod& G = GodFor(S.Player.IsEmpty() ? S.Name : S.Player, S.Seat);
		if (G.Seat < 0) G.Seat = S.Seat;
		if (S.bHasAura && Players.Num() == 0) G.Aura = S.Aura;
		if (Players.Num() == 0)
		{
			G.bHuman = S.Controller.IsEmpty() ? S.Seat >= 0 : S.Controller == TEXT("human");
			G.bConnected = S.bConnected;
			G.bAlive = G.Demigods.Num() == 0 ? S.bAlive : (G.bAlive || S.bAlive);
		}
		FFNTableDemigod D;
		D.Ref = S.Ref;
		D.Name = S.Name;
		D.Side = S.Side;
		D.bAlive = S.bAlive;
		D.bHasPos = S.bHasPos;
		D.Lat = S.Lat;
		D.Lon = S.Lon;
		const FString DioArt = LoadedArt(S.Ref, true);
		D.Diorama = MediaTexture(DioArt);
		D.DioramaGrey = S.bAlive ? nullptr : MediaTexture(DioArt, true);
		if (D.Side == TEXT("right") && G.Demigods.Num() == 1 && G.Demigods[0].Side != TEXT("right")) G.Demigods.Add(D);
		else if (D.Side != TEXT("right")) G.Demigods.Insert(D, 0);
		else G.Demigods.Add(D);
	}
	for (FFNTableGod& G : Gods)
	{
		G.bMine = MySeat >= 0 && G.Seat == MySeat;
		for (const FFNTableDemigod& D : G.Demigods) G.bMine |= MineRefs.Contains(D.Ref);
	}
	if (AFNTableGods* T = AFNTableGods::Get(St, true))
	{
		T->SetGods(Gods, MySeat);
		T->SetShown(bInRoom);
	}
}

double SFNHudLive::RadiusFor(const FString& Slug, const FString& Tier) const
{
	if (const double* R = PowerRadiusKm.Find(Slug + TEXT(":") + Tier)) return *R;
	if (const double* R = PowerRadiusKm.Find(Slug)) return *R;
	return 0.0;
}

void SFNHudLive::UpdateRing()
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	if (RingInput.IsValid()) RingInput->SetStudio(St);
	if (bInRoom && Model.Powers.Num() == 0) FetchPowers();
	if (!bInRoom)
	{
		// The intro: the bag alone on the table (no hexes yet); clicking it joins (OnMouseButtonDown).
		if (AFNPowerRing* Lobby = AFNPowerRing::Get(St, true))
		{
			Lobby->SetPowers(TArray<FString>(), TArray<FText>());
			Lobby->SetPurse(1, 0);
			Lobby->SetShown(true);
		}
		return;
	}
	const bool bShow = bInRoom && Model.Powers.Num() > 0 && !Model.bViewer;
	{
		// Why the ring is up or not, once per change (live diagnosis instead of guessing).
		static FString LastRing;
		const FString R = FString::Printf(TEXT("shown=%d in_room=%d powers=%d viewer=%d"), bShow ? 1 : 0, bInRoom ? 1 : 0, Model.Powers.Num(), Model.bViewer ? 1 : 0);
		if (R != LastRing) { LastRing = R; UE_LOG(LogTemp, Display, TEXT("FN7 ring: %s"), *R); }
	}
	AFNPowerRing* Ring = AFNPowerRing::Get(St, bShow);
	if (!Ring) return;
	// One hex per power (the tier is where it lands), in this machine's saved order.
	TArray<FString> Slugs;
	TArray<FText> Names;
	for (const FFNHudPower& P : Model.Powers)
		if (!Slugs.Contains(P.Slug)) { Slugs.Add(P.Slug); Names.Add(P.Name); }
	Ring->SetPowers(Slugs, Names);
	Ring->SetPurse(Model.Bolts, Model.Sparks);
	Ring->SetShown(bShow);
	// Joined by clicking the bag: it pours its hexes out once they're here.
	if (bOpenPouchOnJoin && bShow && Ring->Num() > 0)
	{
		bOpenPouchOnJoin = false;
		if (!Ring->IsOpen()) Ring->TogglePouch();
	}
}

FReply SFNHudLive::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	// Clicks the lobby didn't use bubble here: on the intro, a click on the bag joins the game.
	if (!bInRoom && E.GetEffectingButton() == EKeys::LeftMouseButton && StartPanel.IsValid())
		if (AFNStudio* St = FindStudio())
			if (AFNPowerRing* R = AFNPowerRing::Get(St, false))
			{
				UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
				const FVector2D Size = G.GetLocalSize();
				if (VC && VC->Viewport && Size.X > 1.0 && Size.Y > 1.0)
				{
					const FVector2D Px = G.AbsoluteToLocal(E.GetScreenSpacePosition()) / Size * FVector2D(VC->Viewport->GetSizeXY());
					if (R->PouchAtPixel(Px))
					{
						UE_LOG(LogTemp, Display, TEXT("FN7 bag clicked on the intro: joining"));
						bOpenPouchOnJoin = true;
						StartPanel->JoinGame();
						return FReply::Handled();
					}
				}
			}
	return SCompoundWidget::OnMouseButtonDown(G, E);
}


void SFNHudLive::OnTyping()
{
	if (UFNNetClient* N = Net.Get())
		if (N->bFn7 && ArmedPower.IsEmpty()) N->BeginTyping();
}

void SFNHudLive::OnSend(const FString& Text)
{
	UFNNetClient* N = Net.Get();
	if (!N) return;
	if (!ArmedPower.IsEmpty())
	{
		// ArmedPower is "slug" or "slug:tier" (mega spends a bolt, mini a spark; a server without tiers takes mini).
		FString ArmedSlug = ArmedPower, ArmedTier;
		ArmedPower.Split(TEXT(":"), &ArmedSlug, &ArmedTier);
		if (ArmedSlug.IsEmpty()) ArmedSlug = ArmedPower;
		if (!N->CastPower(Round, ArmedSlug, Text, ArmedTier.IsEmpty() ? FString(TEXT("mini")) : ArmedTier)) { Toast(TEXT("The gods need a breath: try again in a moment.")); return; }   // still armed
		AddNews(LOCTEXT("Cast", "CAST"), FString::Printf(TEXT("%s at %s"), *ArmedPower, *Text));
		SetArmed(FString());
		Publish();
		return;
	}
	FString Left, Right;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		const FString Line = L.TrimStartAndEnd();
		if (Line.StartsWith(TEXT("r:"), ESearchCase::IgnoreCase)) Right += (Right.IsEmpty() ? TEXT("") : TEXT("\n")) + Line.RightChop(2).TrimStart();
		else if (Line.StartsWith(TEXT("l:"), ESearchCase::IgnoreCase)) Left += (Left.IsEmpty() ? TEXT("") : TEXT("\n")) + Line.RightChop(2).TrimStart();
		else Left += (Left.IsEmpty() ? TEXT("") : TEXT("\n")) + Line;
	}
	N->SubmitOrders(Round, Left, Right);
	N->EndTyping();
	AddNews(LOCTEXT("You", "YOU"), Text);
	Publish();
}

namespace FN7ViewportPx
{
	/** Slate absolute (desktop) position <-> game viewport pixel, through the viewport widget's geometry. */
	bool AbsToPixel(const FVector2D& Abs, FVector2D& OutPixel)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> W = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!W.IsValid() || !VC->Viewport) return false;
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Local = FVector2D(G.AbsoluteToLocal(Abs));
		const FVector2D Size = FVector2D(G.GetLocalSize());
		const FIntPoint Px = VC->Viewport->GetSizeXY();
		if (Size.X <= 0.0 || Size.Y <= 0.0) return false;
		OutPixel = Local * FVector2D(Px.X / Size.X, Px.Y / Size.Y);
		return Local.X >= 0.0 && Local.Y >= 0.0 && Local.X <= Size.X && Local.Y <= Size.Y;
	}

	bool PixelToAbs(const FVector2D& Pixel, FVector2D& OutAbs)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> W = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!W.IsValid() || !VC->Viewport) return false;
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Size = FVector2D(G.GetLocalSize());
		const FIntPoint Px = VC->Viewport->GetSizeXY();
		if (Px.X <= 0 || Px.Y <= 0) return false;
		OutAbs = FVector2D(G.LocalToAbsolute(Pixel * FVector2D(Size.X / Px.X, Size.Y / Px.Y)));
		return true;
	}
}

AFNStudio* SFNHudLive::FindStudio() const
{
	UFNNetClient* N = Net.Get();
	AFNPlayerController* PC = N ? Cast<AFNPlayerController>(N->GetOuter()) : nullptr;
	return PC ? PC->Studio.Get() : nullptr;
}

TOptional<FVector2D> SFNHudLive::SnapPowerToBoard(const FString&, const FVector2D& ScreenPosition)
{
	if (SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return {};   // over the HUD: the token follows the cursor
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	APlayerController* PC = N ? Cast<APlayerController>(N->GetOuter()) : nullptr;
	FVector2D Pixel, Abs, HexPixel;
	double La, Lo, HexM;
	FVector World;
	if (!St || !PC || !FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) || !St->HexAtScreen(Pixel, La, Lo, World, HexM)) return {};
	if (!PC->ProjectWorldLocationToScreen(World, HexPixel) || !FN7ViewportPx::PixelToAbs(HexPixel, Abs)) return {};
	return Abs;
}

bool SFNHudLive::IsMyDemigod(const FString& Ref) const
{
	if (MineRefs.Contains(Ref) || PreviewMine.Contains(Ref)) return true;
	for (const FSeatInfo& S : Seats) if (S.Ref == Ref) return S.Seat >= 0 && S.Seat == MySeat;
	return false;
}

FString SFNHudLive::WhyNotSummon(const FString& Ref) const
{
	if (Model.bViewer) return TEXT("You're watching: take a seat to summon.");
	if (!IsMyDemigod(Ref)) return TEXT("That's a rival's demigod.");
	if (const bool* bA = MineAlive.Find(Ref)) { if (!*bA) return TEXT("That demigod has fallen."); }
	else for (const FSeatInfo& S : Seats) if (S.Ref == Ref && !S.bAlive) return TEXT("That demigod has fallen.");
	return FString();
}

void SFNHudLive::Toast(const FString& Text)
{
	Model.Status = FText::FromString(Text);
	ToastUntil = FPlatformTime::Seconds() + 3.0;
	Publish();
}

void SFNHudLive::OnSummonOver(const FString& Ref, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	double La = 0.0, Lo = 0.0;
	FVector World;
	FVector2D Pixel;
	const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
	St->ShowAreaOfEffect(Where == 1, Pixel, 0.0);           // the gold target hex on the board
	St->ShowMegaOnGlobe(Where == 2, La, Lo, 300.0);          // a small ring on the globe
}

void SFNHudLive::OnSummonDropped(const FString& Ref, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	if (!St) return;
	double La = 0.0, Lo = 0.0;
	FVector World;
	FVector2D Pixel;
	const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
	St->ShowAreaOfEffect(false, FVector2D::ZeroVector, 0.0);
	St->ShowMegaOnGlobe(false, 0.0, 0.0, 0.0);
	AFNTableGods* T = AFNTableGods::GetAny(St->GetWorld());
	if (T) T->SetFigureHeld(Ref, false);
	if (Where == 0) return;   // let go anywhere else: it goes back
	if (N && !N->Summon(Round, Ref, Lo, La)) { Toast(TEXT("The gods need a breath: try again in a moment.")); return; }   // the piece goes back
	// The piece lifts and dissolves at its origin at once; it lands when the server resolves the summon (`summoned`).
	if (T)
	{
		double FromLa = 0.0, FromLo = 0.0;
		bool bFrom = T->GetBoardPos(Ref, FromLa, FromLo);
		for (const FSeatInfo& S : Seats) if (!bFrom && S.Ref == Ref && S.bHasPos) { FromLa = S.Lat; FromLo = S.Lon; bFrom = true; }
		if (bFrom) T->PlaySummoning(Ref, FromLa, FromLo, AuraOf(Ref));
	}
	PendingSummonRef = Ref;
	PendingSummonAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("FN7 summon sent: %s -> %.4f, %.4f (%s)"), *Ref, La, Lo, Where == 2 ? TEXT("globe") : TEXT("board"));
	FString Name = Ref;
	for (const FSeatInfo& S : Seats) if (S.Ref == Ref && !S.Name.IsEmpty()) Name = S.Name;
	for (const FFNHudDemigod& D : Model.Mine) if (Name == Ref && !D.Name.IsEmpty() && ((Ref.EndsWith(TEXT("0")) && D.Side != TEXT("right")) || (Ref.EndsWith(TEXT("1")) && D.Side == TEXT("right")))) Name = D.Name.ToString();
	AddNews(LOCTEXT("Summon", "SUMMON"), FString::Printf(TEXT("%s summoned to %.2f%s %.2f%s (sent)"), *Name, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
	Publish();
}

FLinearColor SFNHudLive::AuraOf(const FString& Ref) const
{
	for (const FSeatInfo& S : Seats) if (S.Ref == Ref && S.bHasAura) return S.Aura;
	return MineRefs.Contains(Ref) || PreviewMine.Contains(Ref) ? Model.YourAura : FLinearColor(1.f, 0.85f, 0.4f);
}

FString SFNHudLive::NameOf(const FString& Ref) const
{
	for (const FSeatInfo& S : Seats) if (S.Ref == Ref && !S.Name.IsEmpty()) return S.Name;
	for (const FFNHudDemigod& D : Model.Mine) if (D.Ref == Ref && !D.Name.IsEmpty()) return D.Name.ToString();
	return Ref;
}

/** [lon, lat] from an array, or from an object's `pos` (the summoned event's from/to). */
static bool FN7LonLat(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& OutLat, double& OutLon)
{
	if (!O.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (!O->TryGetArrayField(Key, A))
	{
		const TSharedPtr<FJsonObject>* Sub = nullptr;
		if (O->TryGetObjectField(Key, Sub) && Sub && Sub->IsValid()) (*Sub)->TryGetArrayField(TEXT("pos"), A);
	}
	if (!A || A->Num() < 2) return false;
	OutLon = (*A)[0]->AsNumber();
	OutLat = (*A)[1]->AsNumber();
	return true;
}

void SFNHudLive::HandleChallengeLock(const TSharedPtr<FJsonObject>& Lock, bool bRestore)
{
	const FString DuelId = FN7HudJson::Str(Lock, TEXT("duel_id"));
	const TSharedPtr<FJsonObject> A = FN7HudJson::Obj(Lock, TEXT("a")), B = FN7HudJson::Obj(Lock, TEXT("b"));
	FDuelInfo Info;
	Info.A = FN7HudJson::Str(A, TEXT("demigod"));
	Info.B = FN7HudJson::Str(B, TEXT("demigod"));
	Info.AName = FN7HudJson::Str(A, TEXT("name"));
	Info.BName = FN7HudJson::Str(B, TEXT("name"));
	if (Info.AName.IsEmpty()) Info.AName = NameOf(Info.A);
	if (Info.BName.IsEmpty()) Info.BName = NameOf(Info.B);
	Info.HpA = (float)FN7HudJson::Num(A, TEXT("health"), 0.0);
	Info.HpB = (float)FN7HudJson::Num(B, TEXT("health"), 0.0);
	Info.bMine = MineRefs.Contains(Info.A) || MineRefs.Contains(Info.B);
	const bool bAi = FN7HudJson::Flag(Lock, TEXT("ai"));
	UE_LOG(LogTemp, Display, TEXT("FN7 challenge.lock: %s %s vs %s%s%s"), *DuelId, *Info.AName, *Info.BName, bAi ? TEXT(" (AI)") : TEXT(""), Info.bMine ? TEXT(" (yours)") : TEXT(""));
	if (!DuelId.IsEmpty() && Duels.Contains(DuelId)) return;   // already shown
	if (!DuelId.IsEmpty()) Duels.Add(DuelId, Info);
	// The tray: your duel always takes it; another's when you aren't in one.
	StartDuelSession(Lock, Info.bMine, bRestore);
	if (const TArray<TSharedPtr<FJsonValue>>* Also = FN7HudJson::Arr(Lock, TEXT("also")))   // duel.scene, duel.panel 0
		for (const TSharedPtr<FJsonValue>& V : *Also)
			if (const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr) HandleDuelEvent(E);
	OpenDuelBox();          // after also[]: the scene is in
	if (bRestore) return;   // a reconnect redraws the duel, it doesn't re-announce it
	if (!PendingChallengeRef.IsEmpty() && Info.B == PendingChallengeRef) PendingChallengeRef.Reset();   // ours: no refusal is coming
	// The AI teleported beside its rival first: the red pulse and the warp-in, as for any summon.
	if (const TSharedPtr<FJsonObject> Leap = FN7HudJson::Obj(Lock, TEXT("leap"))) HandleSummoning(Leap);
	AddNews(LOCTEXT("ClashTag", "CLASH"), FString::Printf(TEXT("%s challenges %s"), *Info.AName, *Info.BName));
	// Every duel, everyone's (flashes ... everyone centers on the fight ... the clash banner ... the globe
	// has spun there as well"): the flash and the banner at once, the glide as soon as you aren't typing or dragging.
	FlashStart = FPlatformTime::Seconds();
	ShowBanner(LOCTEXT("Clash", "CLASH!"), FText::FromString(FString::Printf(TEXT("%s  vs  %s"), *Info.AName, *Info.BName)), FLinearColor(1.f, 0.42f, 0.25f), 0.0, 5.0);
	double La = 0.0, Lo = 0.0;
	if (FN7LonLat(Lock, TEXT("at"), La, Lo))
	{
		bGlidePending = true;
		GlideLat = La;
		GlideLon = Lo;
		GlideDuel = DuelId;
		TickDuelView();
	}
	Publish();
}

void SFNHudLive::StartDuelSession(const TSharedPtr<FJsonObject>& Lock, bool bMineDuel, bool bRestore)
{
	if (Duel.IsValid() && !Duel->bOver && !Duel->MySide.IsEmpty() && !bMineDuel) return;   // keep yours over a watched one
	const TSharedPtr<FJsonObject> A = FN7HudJson::Obj(Lock, TEXT("a")), B = FN7HudJson::Obj(Lock, TEXT("b"));
	TSharedPtr<FFNDuelSession> S = MakeShared<FFNDuelSession>();
	S->DuelId = FN7HudJson::Str(Lock, TEXT("duel_id"));
	S->A = FN7HudJson::Str(A, TEXT("demigod"));
	S->B = FN7HudJson::Str(B, TEXT("demigod"));
	S->AName = FN7HudJson::Str(A, TEXT("name"));
	S->BName = FN7HudJson::Str(B, TEXT("name"));
	if (S->AName.IsEmpty()) S->AName = NameOf(S->A);
	if (S->BName.IsEmpty()) S->BName = NameOf(S->B);
	// The server's skin when it sends one (kept for the whole duel), else the name rule every client shares.
	S->ASkin = FN7HudJson::Str(A, TEXT("skin"));
	S->BSkin = FN7HudJson::Str(B, TEXT("skin"));
	if (S->ASkin.IsEmpty()) S->ASkin = AFNDuelStage::SkinNames()[AFNDuelStage::SkinForName(S->AName)];
	if (S->BSkin.IsEmpty()) S->BSkin = AFNDuelStage::SkinNames()[AFNDuelStage::SkinForName(S->BName)];
	S->HpA = (float)FN7HudJson::Num(A, TEXT("health"), 10.0);
	S->HpB = (float)FN7HudJson::Num(B, TEXT("health"), 10.0);
	S->MaxA = (float)FN7HudJson::Num(A, TEXT("max_health"), 10.0);
	S->MaxB = (float)FN7HudJson::Num(B, TEXT("max_health"), 10.0);
	S->DeadlineMs = FN7HudJson::Num(Lock, TEXT("deadline_ms"), 0.0);
	S->MySide = MineRefs.Contains(S->A) ? TEXT("a") : MineRefs.Contains(S->B) ? TEXT("b") : TEXT("");
	Duel = S;
	KeysSentText.Reset();
	KeysLatestText.Reset();
	bKeysPending = false;
	if (Tray.IsValid()) Tray->ClearInput();
	UE_LOG(LogTemp, Display, TEXT("FN7 duel tray: %s %s (%s) vs %s (%s)%s%s"), *S->DuelId, *S->AName, *S->ASkin, *S->BName, *S->BSkin,
		S->MySide.IsEmpty() ? TEXT(" (watching)") : *FString::Printf(TEXT(" (you are %s)"), *S->MySide), bRestore ? TEXT(" (restored)") : TEXT(""));
	if (!S->MySide.IsEmpty())
	{
		// Yours: the WILL rolls up, the keyboard goes to your line once the tray is on screen.
		if (Layout.IsValid() && Layout->IsWillOpen()) Layout->RollUpWill();
		RegisterActiveTimer(0.12f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
		{
			if (Tray.IsValid()) Tray->FocusInput();
			return EActiveTimerReturnType::Stop;
		}));
	}
	if (!bDuelTimer)
	{
		bDuelTimer = true;
		RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNHudLive::TickDuelFast));
	}
}

void SFNHudLive::HandleLooks(const TSharedPtr<FJsonObject>& Pub)
{
	const TArray<TSharedPtr<FJsonValue>>* Looks = FN7HudJson::Arr(Pub, TEXT("looks"));
	if (!Looks) return;
	for (const TSharedPtr<FJsonValue>& V : *Looks)
	{
		const TSharedPtr<FJsonObject> L = V.IsValid() ? V->AsObject() : nullptr;
		if (!L.IsValid()) continue;
		const FString Ref = FN7HudJson::Str(L, TEXT("demigod"));
		const FString Key = FString::Printf(TEXT("%s|%d|%d"), *Ref, (int32)FN7HudJson::Num(L, TEXT("memory"), -1.0), FN7HudJson::Flag(L, TEXT("ready")) ? 1 : 0);
		if (SeenLooks.Contains(Key)) continue;
		SeenLooks.Add(Key);
		const TSharedPtr<FJsonObject> Gained = FN7HudJson::Obj(L, TEXT("gained"));
		const FString Item = FN7HudJson::Str(Gained, TEXT("name"));
		const TSharedPtr<FJsonObject> Por = FN7HudJson::Obj(L, TEXT("portrait_image")), Dio = FN7HudJson::Obj(L, TEXT("diorama_image"));
		const bool bReady = FN7HudJson::Flag(L, TEXT("ready"));
		UE_LOG(LogTemp, Display, TEXT("FN7 look: %s \"%s\" gained=%s image_change=%d kept=%s pending=%d ready=%d portrait=%s(%s) diorama=%s(%s)"),
			*Ref, *FN7HudJson::Str(L, TEXT("name")), Item.IsEmpty() ? TEXT("-") : *Item, FN7HudJson::Flag(L, TEXT("image_change")) ? 1 : 0,
			*FN7HudJson::Str(L, TEXT("kept")), FN7HudJson::Obj(L, TEXT("pending")).IsValid() ? 1 : 0, bReady ? 1 : 0,
			*FN7HudJson::Str(Por, TEXT("hash")).Left(10), *FN7HudJson::Str(Por, TEXT("status")), *FN7HudJson::Str(Dio, TEXT("hash")).Left(10), *FN7HudJson::Str(Dio, TEXT("status")));
		// The new pictures: fetch them, and draw them on its card and its diorama box at once.
		FSeatInfo* S = Seats.FindByPredicate([&Ref](const FSeatInfo& X) { return X.Ref == Ref; });
		auto Take = [this](const TSharedPtr<FJsonObject>& Img, FString* Into)
		{
			const FString H = FN7HudJson::Str(Img, TEXT("hash"));
			if (H.IsEmpty() || FN7HudJson::Str(Img, TEXT("status")) != TEXT("ready")) return;
			const FString Url = FN7HudJson::Str(Img, TEXT("url"));
			if (!Url.IsEmpty()) RequestPortrait(H, Url);
			if (Into) *Into = H;
		};
		Take(Por, S ? &S->PortraitHash : nullptr);
		Take(Dio, S ? &S->DioramaHash : nullptr);
		if (!bReady && !Item.IsEmpty())
		{
			AddNews(LOCTEXT("GearTag", "GEAR"), FN7HudJson::Str(L, TEXT("text")).IsEmpty() ? FString::Printf(TEXT("%s now carries the %s"), *FN7HudJson::Str(L, TEXT("name")), *Item) : FN7HudJson::Str(L, TEXT("text")));
			NewItems.Add(Ref, { Item, FPlatformTime::Seconds() });
			if (MineRefs.Contains(Ref)) Toast(FString::Printf(TEXT("NEW: %s"), *Item));
		}
	}
	Publish();
}

FString SFNHudLive::HowToText() const
{
	// Benito's game-design session writes the real text (docs/HOW_TO_PLAY.md); until then, this.
	FString Doc;
	if (FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/HOW_TO_PLAY.md"))) && !Doc.IsEmpty()) return Doc.Replace(TEXT("#"), TEXT("")).TrimStartAndEnd();
	return TEXT("You are a god at the table. Two demigods fight for you on the real Earth; the last one standing wins.\n\n")
		TEXT("YOUR WILL (bottom right): write what your demigods do, in plain words. Time slows while you write.\n\n")
		TEXT("POWERS: open the pouch and drag a power onto the board (a MINI, 1 spark) or onto the globe (a MEGA, 1 bolt).\n\n")
		TEXT("SUMMON: drag one of your cards onto the board or the globe to send that demigod there.\n\n")
		TEXT("CHALLENGE: double-click a rival's piece to duel it. In the box their line is on top, yours below: type your move, Enter strikes. Your words decide the blow.\n\n")
		TEXT("VIEW: drag to turn the globe, double-click the empty map to zoom in, right-click to zoom out, RESET VIEW (R) to come back.\n\n")
		TEXT("If both your demigods fall, plead to Death in your WILL.");
}

void SFNHudLive::CheckServer()
{
	LastServerCheck = FPlatformTime::Seconds();
	UFNNetClient* N = Net.Get();
	if (!N) return;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(TEXT("/api/house")));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(8.f);
	TWeakPtr<SFNHudLive> Weak = SharedThis(this);
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid() || Self->bInRoom) return;
		const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
		if (bOk && (Code == 200 || Code == 404))
		{
			if (Self->NetProblem.StartsWith(TEXT("Can't reach the game server"))) Self->NetProblem.Reset();
			return;
		}
		Self->NetProblem = (Code == 401 || Code == 403)
			? FString(TEXT("Can't reach the game server: this copy of the game has no access key. Retrying..."))
			: FString(TEXT("Can't reach the game server. Check your internet. Retrying..."));
		UE_LOG(LogTemp, Display, TEXT("FN7 server check: HTTP %d -> %s"), Code, *Self->NetProblem);
	});
	Req->ProcessRequest();
}

void SFNHudLive::UpdateBoardInfo()
{
	// The board's debug lines (ZOOM n of 4, map size, lat/lon, terrain, people) are the studio's 3D text over the table:
	// Chris wants them as debug text in a typewriter font under the title bar. Hide the 3D text, show its lines here.
	UTextRenderComponent* T = BoardInfoComp.Get();
	if (!T)
		if (AFNStudio* St = FindStudio())
		{
			TInlineComponentArray<UTextRenderComponent*> Texts(St);
			for (UTextRenderComponent* C : Texts)
				if (C && C->Text.ToString().StartsWith(TEXT("ZOOM "))) { T = C; BoardInfoComp = C; break; }
		}
	if (!T) return;
	if (T->IsVisible()) T->SetVisibility(false);
	BoardInfo = T->Text.ToString();
}

void SFNHudLive::OpenDuelBox()
{
	if (!Duel.IsValid()) return;
	AFNStudio* St = FindStudio();
	AFNDuelBox* Box = St ? AFNDuelBox::Get(St->GetWorld(), true) : nullptr;
	if (!Box) return;
	// Yours always takes the camera; another's when you're not in the middle of something.
	const bool bTake = !Duel->MySide.IsEmpty() || !IsBusyForGlide();
	TWeakObjectPtr<UFNNetClient> WN = Net;
	Box->Open(Duel, St, bTake, [WN]() { UFNNetClient* N = WN.Get(); return N ? N->NowServerMs() : 0.0; });
	Box->SetPortraits(MediaTexture(LoadedArt(Duel->A, false)), MediaTexture(LoadedArt(Duel->B, false)));
}

void SFNHudLive::CloseDuelBox()
{
	AFNStudio* St = FindStudio();
	if (AFNDuelBox* Box = St ? AFNDuelBox::Get(St->GetWorld(), false) : nullptr) Box->Close();
}

void SFNHudLive::StoreDuelPanels(const FString& DuelId, const TArray<TSharedPtr<FJsonValue>>* Panels)
{
	if (!Panels || !Duel.IsValid() || Duel->DuelId != DuelId) return;
	for (const TSharedPtr<FJsonValue>& V : *Panels)
		if (const TSharedPtr<FJsonObject> P = V.IsValid() ? V->AsObject() : nullptr)
			Duel->Panels.Add((int32)FN7HudJson::Num(P, TEXT("n"), -1.0), P);
}

void SFNHudLive::HandleDuelEvent(const TSharedPtr<FJsonObject>& Pub)
{
	if (!Pub.IsValid()) return;
	const FString Ev = FN7HudJson::Str(Pub, TEXT("event"));
	const FString DuelId = FN7HudJson::Str(Pub, TEXT("duel_id"));
	const bool bOurs = Duel.IsValid() && Duel->DuelId == DuelId;
	// An AI's typing script (schema 12): played as live masked typing in the top line, on the server's clock.
	auto ReadScript = [this](const FString& Side, const TSharedPtr<FJsonObject>& Sc)
	{
		if (!Duel.IsValid() || !Sc.IsValid() || (Side != TEXT("a") && Side != TEXT("b"))) return;
		FFNDuelSession::FSideLine& L = Duel->Line(Side);
		L.ScriptText = FN7HudJson::Str(Sc, TEXT("text"));
		L.ScriptStart = FN7HudJson::Num(Sc, TEXT("starts_at_ms"), 0.0);
		const double Cps = FN7HudJson::Num(Sc, TEXT("cps"), 7.0);
		L.ScriptKeyMs = FN7HudJson::Num(Sc, TEXT("key_ms"), Cps > 0.0 ? 1000.0 / Cps : 140.0);
		L.ScriptEnd = FN7HudJson::Num(Sc, TEXT("ends_at_ms"), L.ScriptStart + L.ScriptKeyMs * L.ScriptText.Len());
		L.ScriptPauses.Reset();
		if (const TArray<TSharedPtr<FJsonValue>>* Ps = FN7HudJson::Arr(Sc, TEXT("pauses")))
			for (const TSharedPtr<FJsonValue>& Pv : *Ps)
				if (const TSharedPtr<FJsonObject> P = Pv.IsValid() ? Pv->AsObject() : nullptr)
					L.ScriptPauses.Add(TPair<int32, double>((int32)FN7HudJson::Num(P, TEXT("after"), 0.0), FN7HudJson::Num(P, TEXT("ms"), 0.0)));
		UE_LOG(LogTemp, Display, TEXT("FN7 duel typing script: %s side %s %d chars from %.0f"), *Duel->DuelId, *Side, L.ScriptText.Len(), L.ScriptStart);
	};
	if (bOurs && Ev == TEXT("duel.scene")) Duel->Scene = Pub;
	else if (bOurs && Ev == TEXT("duel.typing_script")) ReadScript(FN7HudJson::Str(Pub, TEXT("by")), Pub);
	else if (bOurs && Ev == TEXT("duel.panel"))
	{
		Duel->Panels.Add((int32)FN7HudJson::Num(Pub, TEXT("n"), -1.0), Pub);
		for (const TCHAR* Side : { TEXT("a"), TEXT("b") })
			if (const TSharedPtr<FJsonObject> Sc = FN7HudJson::Obj(FN7HudJson::Obj(Pub, Side), TEXT("typing_script"))) ReadScript(Side, Sc);
	}
	else if (Ev == TEXT("challenge.end"))
	{
		// The provisional end in also[] (schema 12) is the end on screen; the full one at the next resolve is merged.
		TSharedPtr<FJsonObject> Wrap = MakeShared<FJsonObject>();
		Wrap->SetArrayField(TEXT("duels"), { MakeShared<FJsonValueObject>(Pub) });
		HandleDuelsOutcome(Wrap);
	}
	else if (bOurs && Ev == TEXT("duel.typing"))
	{
		const FString Side = FN7HudJson::Str(Pub, TEXT("by"));
		FFNDuelSession::FSideLine& L = Duel->Line(Side);
		const double AtMs = FN7HudJson::Num(Pub, TEXT("at_ms"), 0.0);
		const int32 Seq = (int32)FN7HudJson::Num(Pub, TEXT("seq"), 0.0);
		const bool bNewer = AtMs > L.AtMs || (AtMs == L.AtMs && Seq > L.Seq);
		if (bNewer && Side != Duel->MySide)   // your own line is drawn from your keyboard
		{
			L.Len = (int32)FN7HudJson::Num(Pub, TEXT("len"), 0.0);
			L.Tail = FN7HudJson::Str(Pub, TEXT("tail"));
			L.FrameAt = FPlatformTime::Seconds();
			L.AtMs = AtMs;
			L.Seq = Seq;
		}
	}
	else if (bOurs && Ev == TEXT("duel.blow"))
	{
		auto LogBlow = [this](const TSharedPtr<FJsonObject>& Bl, bool bCounter)
		{
			const FString By = FN7HudJson::Str(Bl, TEXT("by"));
			const FString Side = By == Duel->A ? TEXT("a") : TEXT("b");
			const FString ByName = FN7HudJson::Str(Bl, TEXT("by_name")).IsEmpty() ? NameOf(By) : FN7HudJson::Str(Bl, TEXT("by_name"));
			const FString Text = FN7HudJson::Str(Bl, TEXT("text"));
			const int32 Roll = (int32)FN7HudJson::Num(Bl, TEXT("roll"), 0.0), Mod = (int32)FN7HudJson::Num(Bl, TEXT("modifier"), 0.0);
			const int32 Total = (int32)FN7HudJson::Num(Bl, TEXT("total"), (double)(Roll + Mod)), Dc = (int32)FN7HudJson::Num(Bl, TEXT("target_dc"), 10.0);
			const double Dmg = FN7HudJson::Num(Bl, TEXT("damage"), 0.0);
			FString Flavor = FN7HudJson::Str(Bl, TEXT("flavor"));
			if (Flavor.Len() > 120) Flavor = Flavor.Left(117) + TEXT("...");
			Duel->AddLog(FString::Printf(TEXT("%s%s: \"%s\"  d20 %d%s = %d vs %d: %s, %.1f dmg.  %s"), bCounter ? TEXT("counter, ") : TEXT(""), *ByName, *Text,
				Roll, Mod ? *FString::Printf(TEXT(" %+d"), Mod) : TEXT(""), Total, Dc, *FN7HudJson::Str(Bl, TEXT("verdict")).Replace(TEXT("_"), TEXT(" ")).ToUpper(), Dmg, *Flavor));
			// An automatic counter (the AI's strikes come this way) clears nobody's typing, but its words still show on
			// top: they type themselves out (the AI's typing should be visible).
			if (FN7HudJson::Flag(Bl, TEXT("auto")) && !Text.IsEmpty() && Side != Duel->MySide)
			{
				FFNDuelSession::FSideLine& L = Duel->Line(Side);
				L.bTypeOut = !L.HasScript();   // a script already showed it being typed
				L.Sent = Text;
				L.SentAt = FPlatformTime::Seconds();
			}
			// A typed line clears its side's typing.
			if (!FN7HudJson::Flag(Bl, TEXT("auto")) && !Text.IsEmpty())
			{
				FFNDuelSession::FSideLine& L = Duel->Line(Side);
				// Nobody saw it typed (an AI's line, or keys that never came): it types itself out, *****p.
				L.bTypeOut = Side != Duel->MySide && !L.HasScript() && (L.FrameAt <= 0.0 || FPlatformTime::Seconds() - L.FrameAt > 4.0);
				L.Sent = Text;
				L.SentAt = FPlatformTime::Seconds();
				L.Len = 0;
				L.Tail.Reset();
			}
		};
		UE_LOG(LogTemp, Display, TEXT("FN7 duel blow: %s n=%d by %s auto=%d %s %.1f dmg \"%s\""), *DuelId, (int32)FN7HudJson::Num(Pub, TEXT("n"), 0.0),
			*FN7HudJson::Str(Pub, TEXT("by")), FN7HudJson::Flag(Pub, TEXT("auto")) ? 1 : 0, *FN7HudJson::Str(Pub, TEXT("verdict")),
			FN7HudJson::Num(Pub, TEXT("damage"), 0.0), *FN7HudJson::Str(Pub, TEXT("text")).Left(40));
		LogBlow(Pub, false);
		if (const TSharedPtr<FJsonObject> Counter = FN7HudJson::Obj(Pub, TEXT("counter"))) LogBlow(Counter, true);
		const TSharedPtr<FJsonObject> HpAfter = FN7HudJson::Obj(Pub, TEXT("hp_after")).IsValid() ? FN7HudJson::Obj(Pub, TEXT("hp_after")) : FN7HudJson::Obj(Pub, TEXT("hp"));
		Duel->HpA = (float)FN7HudJson::Num(HpAfter, TEXT("a"), Duel->HpA);
		Duel->HpB = (float)FN7HudJson::Num(HpAfter, TEXT("b"), Duel->HpB);
		const TSharedPtr<FJsonObject> Choreo = FN7HudJson::Obj(FN7HudJson::Obj(Pub, TEXT("counter")).IsValid() ? FN7HudJson::Obj(Pub, TEXT("counter")) : Pub, TEXT("choreo"));
		if (Choreo.IsValid())
		{
			Duel->Range = FN7HudJson::Str(Choreo, TEXT("range_after"));
			Duel->Initiative = FN7HudJson::Str(Choreo, TEXT("initiative_after"));
		}
	}
	// Everything that rode along (the panels a tick or a line made due): as if each had arrived on its own.
	if (const TArray<TSharedPtr<FJsonValue>>* Also = FN7HudJson::Arr(Pub, TEXT("also")))
		for (const TSharedPtr<FJsonValue>& V : *Also)
			if (const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr) HandleDuelEvent(E);
}

void SFNHudLive::DuelSendLine(const FString& Line)
{
	UFNNetClient* N = Net.Get();
	if (!N || !Duel.IsValid() || Duel->MySide.IsEmpty() || Duel->bOver) return;
	N->DuelLine(Round, Duel->DuelId, Line);   // a line is a blow: resolved at once by the server (duel.blow)
	FFNDuelSession::FSideLine& L = Duel->Line(Duel->MySide);
	L.Sent = Line;
	L.SentAt = FPlatformTime::Seconds();
	L.Len = 0;
	KeysSentText.Reset();
	KeysLatestText.Reset();
	bKeysPending = false;
	UE_LOG(LogTemp, Display, TEXT("FN7 duel line sent: %s (%d chars)"), *Duel->DuelId, Line.Len());
}

void SFNHudLive::DuelTyping(const FString& Text)
{
	KeysLatestText = Text;
	bKeysPending = Text != KeysSentText;
	TrySendDuelKeys();
}

void SFNHudLive::TrySendDuelKeys()
{
	UFNNetClient* N = Net.Get();
	if (!bKeysPending || !N || !Duel.IsValid() || Duel->MySide.IsEmpty() || Duel->bOver) return;
	const FString& Now = KeysLatestText;
	const FString Tail = Now.StartsWith(KeysSentText, ESearchCase::CaseSensitive) ? Now.Mid(KeysSentText.Len()).Right(8) : FString();   // "" after a deletion
	if (N->DuelKeys(Round, Duel->DuelId, Now.Len(), Tail))
	{
		KeysSentText = Now;
		bKeysPending = false;
	}
}

EActiveTimerReturnType SFNHudLive::TickDuelFast(double, float)
{
	if (!Duel.IsValid()) { bDuelTimer = false; return EActiveTimerReturnType::Stop; }
	// A quarter of the old 5 s hold until there are victory dances (Chris).
	if (Duel->bOver && FPlatformTime::Seconds() - Duel->OverAt > 1.25) { CloseDuelBox(); Duel.Reset(); bDuelTimer = false; return EActiveTimerReturnType::Stop; }
	UFNNetClient* N = Net.Get();
	if (N && !Duel->MySide.IsEmpty() && !Duel->bOver)
	{
		// The pacing tick: at each panel's input close, by the server's clock, plus ~30 ms (viewers never tick).
		const double NowMs = N->NowServerMs();
		for (const TPair<int32, TSharedPtr<FJsonObject>>& P : Duel->Panels)
		{
			const double Closes = FN7HudJson::Num(P.Value, TEXT("input_closes_ms"), 0.0);
			if (Closes <= 0.0 || Duel->Ticked.Contains(P.Key) || NowMs < Closes + 30.0 || NowMs > Closes + 4000.0) continue;
			Duel->Ticked.Add(P.Key);
			N->DuelTick(Round, Duel->DuelId);
			UE_LOG(LogTemp, Display, TEXT("FN7 duel tick: %s panel %d"), *Duel->DuelId, P.Key);
		}
		// One tick at the deadline closes the duel on the server (keys can't; schema 12).
		if (Duel->DeadlineMs > 0.0 && !Duel->bDeadlineTicked && NowMs >= Duel->DeadlineMs + 30.0 && NowMs < Duel->DeadlineMs + 5000.0)
		{
			Duel->bDeadlineTicked = true;
			N->DuelTick(Round, Duel->DuelId);
			UE_LOG(LogTemp, Display, TEXT("FN7 duel tick: %s at the deadline"), *Duel->DuelId);
		}
		TrySendDuelKeys();   // a keystroke the bucket held back
		// Your duel keeps the keyboard in your line the whole time (the input should simply be set to have focus),
		// unless you're typing in another box.
		if (Tray.IsValid() && !Tray->HasInputFocus() && !FN7TypingInText() && FN7GameHasFocus()) Tray->FocusInput();
		Duel->bSending = N->HeldDuelLines() > 0;
	}
	return EActiveTimerReturnType::Continue;
}

void SFNHudLive::RestoreDuels(const TSharedPtr<FJsonObject>& Present)
{
	const TArray<TSharedPtr<FJsonValue>>* Open = FN7HudJson::Arr(Present, TEXT("duels"));
	if (!Open) return;
	for (const TSharedPtr<FJsonValue>& V : *Open)
	{
		const TSharedPtr<FJsonObject> D = V.IsValid() ? V->AsObject() : nullptr;
		if (!D.IsValid() || FN7HudJson::Flag(D, TEXT("over"))) continue;
		const TSharedPtr<FJsonObject> Lock = FN7HudJson::Obj(D, TEXT("lock"));
		if (!Lock.IsValid()) continue;
		HandleChallengeLock(Lock, true);
		const FString DuelId = FN7HudJson::Str(D, TEXT("duel_id"));
		if (!Duel.IsValid() || Duel->DuelId != DuelId) continue;
		if (const TSharedPtr<FJsonObject> Sc = FN7HudJson::Obj(D, TEXT("scene"))) Duel->Scene = Sc;
		StoreDuelPanels(DuelId, FN7HudJson::Arr(D, TEXT("panels")));
		const TSharedPtr<FJsonObject> Hp = FN7HudJson::Obj(D, TEXT("hp"));
		Duel->HpA = (float)FN7HudJson::Num(Hp, TEXT("a"), Duel->HpA);
		Duel->HpB = (float)FN7HudJson::Num(Hp, TEXT("b"), Duel->HpB);
		Duel->Range = FN7HudJson::Str(D, TEXT("range"));
		Duel->Initiative = FN7HudJson::Str(D, TEXT("initiative"));
		if (const TSharedPtr<FJsonObject> Ty = FN7HudJson::Obj(D, TEXT("typing")))
			for (const TCHAR* Side : { TEXT("a"), TEXT("b") })
				if (FString(Side) != Duel->MySide)
					Duel->Line(Side).Len = (int32)FN7HudJson::Num(FN7HudJson::Obj(Ty, Side), TEXT("len"), 0.0);
		if (const TArray<TSharedPtr<FJsonValue>>* Blows = FN7HudJson::Arr(D, TEXT("blows")))
			for (int32 i = FMath::Max(0, Blows->Num() - 3); i < Blows->Num(); ++i)
				if (const TSharedPtr<FJsonObject> Bl = (*Blows)[i].IsValid() ? (*Blows)[i]->AsObject() : nullptr)
				{
					TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>(*Bl);
					Copy->SetStringField(TEXT("duel_id"), DuelId);
					Copy->RemoveField(TEXT("also"));
					HandleDuelEvent(Copy);
				}
		OpenDuelBox();   // the scene and the panels so far: the box seeks to the present
		UE_LOG(LogTemp, Display, TEXT("FN7 duel restored: %s (%d panels)"), *DuelId, Duel->Panels.Num());
	}
}

float SFNHudLive::VictoryAlpha() const
{
	if (!bVictory) return 0.f;
	const double Now = FPlatformTime::Seconds();
	const float In = (float)FMath::Clamp((Now - VictoryShownAt) / 0.5, 0.0, 1.0);
	const float Out = VictoryFadeAt > 0.0 ? (float)FMath::Clamp(1.0 - (Now - VictoryFadeAt) / 1.2, 0.0, 1.0) : 1.f;
	return In * Out;
}

void SFNHudLive::ShowVictory(const TSharedPtr<FJsonObject>& Summary)
{
	if (!Summary.IsValid()) return;
	// The champion: their seat (null for the built-in AI) and player name; the first champion demigod for the portrait.
	const bool bSeat = FN7HudJson::Has(Summary, TEXT("champion_seat"));
	const int32 ChampSeat = bSeat ? (int32)FN7HudJson::Num(Summary, TEXT("champion_seat"), -2.0) : -2;
	FString ChampPlayer = FN7HudJson::Str(Summary, TEXT("champion_player"));
	FString ChampRef;
	if (const TArray<TSharedPtr<FJsonValue>>* C = FN7HudJson::Arr(Summary, TEXT("champions")))
		if (C->Num() > 0 && (*C)[0].IsValid())
		{
			const TSharedPtr<FJsonObject> First = (*C)[0]->AsObject();
			ChampRef = FN7HudJson::Str(First, TEXT("demigod"));
			if (ChampPlayer.IsEmpty()) ChampPlayer = FN7HudJson::Str(First, TEXT("name"));
		}
	bVictoryMine = (MySeat >= 0 && ChampSeat == MySeat) || (!ChampRef.IsEmpty() && MineRefs.Contains(ChampRef));
	VictoryTitle = bVictoryMine ? LOCTEXT("VicMine", "VICTORY!")
		: FText::Format(LOCTEXT("VicChamp", "CHAMPION: {0}"), FText::FromString(ChampPlayer.IsEmpty() ? TEXT("?") : ChampPlayer.ToUpper()));
	VictorySub = FText::FromString(FN7HudJson::Str(FN7HudJson::Obj(Summary, TEXT("final_cut")), TEXT("title")));
	VictoryCaption = FText::FromString(FN7HudJson::Str(FN7HudJson::Obj(Summary, TEXT("last_caption")), TEXT("caption")));
	// The top of the standings: each player with their two demigods.
	FString Lines;
	if (const TArray<TSharedPtr<FJsonValue>>* St = FN7HudJson::Arr(Summary, TEXT("standings")))
		for (int32 i = 0; i < St->Num() && i < 4; ++i)
		{
			const TSharedPtr<FJsonObject> Row = (*St)[i].IsValid() ? (*St)[i]->AsObject() : nullptr;
			if (!Row.IsValid()) continue;
			FString Who = FN7HudJson::Str(Row, TEXT("player"));
			if (Who.IsEmpty()) Who = FN7HudJson::Str(Row, TEXT("name"));
			FString Pieces;
			if (const TArray<TSharedPtr<FJsonValue>>* Dg = FN7HudJson::Arr(Row, TEXT("demigods")))
				for (const TSharedPtr<FJsonValue>& V : *Dg)
				{
					const TSharedPtr<FJsonObject> D = V.IsValid() ? V->AsObject() : nullptr;
					if (!D.IsValid()) continue;
					const FString Nm = FN7HudJson::Str(D, TEXT("name"));
					const bool bUp = FN7HudJson::Flag(D, TEXT("alive"));
					Pieces += FString::Printf(TEXT("%s%s %s"), Pieces.IsEmpty() ? TEXT("") : TEXT(",  "), *Nm, bUp ? TEXT("(standing)") : TEXT("(fell)"));
				}
			const int32 Rank = (int32)FN7HudJson::Num(Row, TEXT("rank"), (double)(i + 1));
			Lines += FString::Printf(TEXT("%s%d.  %s%s%s"), Lines.IsEmpty() ? TEXT("") : TEXT("\n"), Rank, *Who, Pieces.IsEmpty() ? TEXT("") : TEXT(":  "), *Pieces);
		}
	VictoryStandings = FText::FromString(Lines);
	// The champion's portrait, when its art is here.
	bVictoryPortrait = false;
	if (UTexture2D* Tex = ChampRef.IsEmpty() ? nullptr : MediaTexture(LoadedArt(ChampRef, false), false))
	{
		VictoryPortrait = FSlateBrush();
		VictoryPortrait.SetResourceObject(Tex);
		VictoryPortrait.ImageSize = FVector2D(220.f, 220.f);
		VictoryPortrait.DrawAs = ESlateBrushDrawType::Image;
		bVictoryPortrait = true;
	}
	bVictory = true;
	VictoryShownAt = FPlatformTime::Seconds();
	VictoryFadeAt = 0.0;
	NextMatchAt = 0.0;
	FlashStart = VictoryShownAt;
	UE_LOG(LogTemp, Display, TEXT("FN7 victory: %s (champion %s, seat %d, mine %d)"), *VictoryTitle.ToString(), *ChampPlayer, ChampSeat, bVictoryMine ? 1 : 0);
	UE_LOG(LogTemp, Display, TEXT("FN7_Victory_Sting"));   // audio hook: the victory sting plays here once there's an audio path
	FetchNextMatch();
}

void SFNHudLive::FetchNextMatch()
{
	UFNNetClient* N = Net.Get();
	if (!N) return;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(TEXT("/api/house")));
	Req->SetVerb(TEXT("GET"));
	TWeakPtr<SFNHudLive> Weak = SharedThis(this);
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		const TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid() || !bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		TSharedPtr<FJsonObject> O;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), O) || !O.IsValid()) return;
		// Seconds to the next match, wherever the house says it (as the lobby reads it).
		const double NowMs = (double)(FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
		double Secs = -1.0;
		for (const TSharedPtr<FJsonObject>& Where : { O, FN7HudJson::Obj(O, TEXT("last_ended")), FN7HudJson::Obj(O, TEXT("next")) })
		{
			if (!Where.IsValid() || Secs >= 0.0) continue;
			double V = 0.0;
			for (const TCHAR* K : { TEXT("next_match_in_s"), TEXT("next_match_in"), TEXT("starts_in_s") })
				if (Secs < 0.0 && Where->TryGetNumberField(K, V)) Secs = FMath::Max(0.0, V);
			for (const TCHAR* K : { TEXT("next_match_at"), TEXT("next_match_at_ms"), TEXT("next_start_ms") })
				if (Secs < 0.0 && Where->TryGetNumberField(K, V) && V > 1.0e12) Secs = FMath::Max(0.0, (V - NowMs) / 1000.0);
		}
		if (Secs >= 0.0 && Self->bVictory && Self->VictoryFadeAt <= 0.0) Self->NextMatchAt = FPlatformTime::Seconds() + Secs;
		UE_LOG(LogTemp, Display, TEXT("FN7 victory: next match in %.0f s"), Secs);
	});
	Req->ProcessRequest();
}

bool SFNHudLive::IsBusyForGlide() const
{
	if (Layout.IsValid() && Layout->IsWillOpen()) return true;   // writing the WILL
	if (FN7TypingInText()) return true;
	if (!FSlateApplication::IsInitialized()) return false;
	FSlateApplication& App = FSlateApplication::Get();
	return App.IsDragDropping() || App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);   // dragging a piece or a power
}

void SFNHudLive::TickDuelView()
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	const double Now = FPlatformTime::Seconds();
	if (bGlidePending && !IsBusyForGlide())
	{
		if (!bHaveSavedView) { St->GetViewSpot(SavedLat, SavedLon, bSavedPicked); bHaveSavedView = true; }
		St->CenterBoardAndGlobe(GlideLat, GlideLon);
		bool bFightPicked = false;
		St->GetViewSpot(FightLat, FightLon, bFightPicked);
		ViewDuel = GlideDuel;
		bGlidePending = false;
		RestoreAt = 0.0;
		UE_LOG(LogTemp, Display, TEXT("FN7 duel view: glide to %s (%.3f, %.3f)"), *ViewDuel, GlideLat, GlideLon);
	}
	if (RestoreAt > 0.0 && Now >= RestoreAt && bHaveSavedView && !IsBusyForGlide())
	{
		double CurLat = 0.0, CurLon = 0.0;
		bool bCurPicked = false;
		St->GetViewSpot(CurLat, CurLon, bCurPicked);
		const bool bUntouched = FMath::Abs(CurLat - FightLat) < 1e-4 && FMath::Abs(CurLon - FightLon) < 1e-4;
		if (bUntouched) St->RestoreViewSpot(SavedLat, SavedLon, bSavedPicked);
		UE_LOG(LogTemp, Display, TEXT("FN7 duel view: %s"), bUntouched ? TEXT("back to your view") : TEXT("you moved it, left as is"));
		bHaveSavedView = false;
		RestoreAt = 0.0;
		ViewDuel.Reset();
	}
}

void SFNHudLive::HandleDuelsOutcome(const TSharedPtr<FJsonObject>& Pub)
{
	auto Hp = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Side, float Def) { return (float)FN7HudJson::Num(FN7HudJson::Obj(O, TEXT("hp")), Side, Def); };
	// Under way: the live score of your duel, in the status line (refreshed at every resolve while it lasts).
	if (const TArray<TSharedPtr<FJsonValue>>* Open = FN7HudJson::Arr(Pub, TEXT("duels_open")))
		for (const TSharedPtr<FJsonValue>& V : *Open)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			const FString DuelId = FN7HudJson::Str(O, TEXT("duel_id"));
			if (DuelId.IsEmpty()) continue;
			FDuelInfo& Info = Duels.FindOrAdd(DuelId);
			if (Info.A.IsEmpty())
			{
				// Joined mid-duel: no lock seen.
				Info.A = FN7HudJson::Str(O, TEXT("a"));
				Info.B = FN7HudJson::Str(O, TEXT("b"));
				Info.AName = NameOf(Info.A);
				Info.BName = NameOf(Info.B);
				Info.bMine = MineRefs.Contains(Info.A) || MineRefs.Contains(Info.B);
			}
			Info.HpA = Hp(O, TEXT("a"), Info.HpA);
			Info.HpB = Hp(O, TEXT("b"), Info.HpB);
			Info.Blows = (int32)FN7HudJson::Num(O, TEXT("blows"), Info.Blows);
			StoreDuelPanels(DuelId, FN7HudJson::Arr(O, TEXT("panels")));
			if (Duel.IsValid() && Duel->DuelId == DuelId)
			{
				Duel->HpA = Info.HpA;
				Duel->HpB = Info.HpB;
				if (FN7HudJson::Has(O, TEXT("range"))) Duel->Range = FN7HudJson::Str(O, TEXT("range"));
				if (FN7HudJson::Has(O, TEXT("initiative"))) Duel->Initiative = FN7HudJson::Str(O, TEXT("initiative"));
			}
			if (Info.bMine)
				Toast(FString::Printf(TEXT("DUEL  %s %.1f  -  %s %.1f   (%d blows)"), *Info.AName, Info.HpA, *Info.BName, Info.HpB, Info.Blows));
		}
	// Over: the result and its deed (everyone's in WORLD; yours as a banner too).
	if (const TArray<TSharedPtr<FJsonValue>>* Ended = FN7HudJson::Arr(Pub, TEXT("duels")))
		for (const TSharedPtr<FJsonValue>& V : *Ended)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			const FString DuelId = FN7HudJson::Str(O, TEXT("duel_id"));
			if (!DuelId.IsEmpty() && EndedDuels.Contains(DuelId)) continue;   // the provisional end already showed it
			if (!DuelId.IsEmpty()) EndedDuels.Add(DuelId);
			const FString Ar = FN7HudJson::Str(O, TEXT("a")), Br = FN7HudJson::Str(O, TEXT("b"));
			FString An = FN7HudJson::Str(O, TEXT("a_name")), Bn = FN7HudJson::Str(O, TEXT("b_name"));
			if (An.IsEmpty()) An = NameOf(Ar);
			if (Bn.IsEmpty()) Bn = NameOf(Br);
			const FString Winner = FN7HudJson::Str(O, TEXT("winner")), Reason = FN7HudJson::Str(O, TEXT("reason"));
			const bool bKo = FN7HudJson::Flag(O, TEXT("ko"));
			const FString Deed = FN7HudJson::Str(O, TEXT("deed"));
			const bool bMine = MineRefs.Contains(Ar) || MineRefs.Contains(Br);
			const FString WinName = Winner == Ar ? An : Winner == Br ? Bn : FString();
			const FString Line = !Deed.IsEmpty() ? Deed
				: WinName.IsEmpty() ? FString::Printf(TEXT("%s and %s fight to a draw"), *An, *Bn)
				: FString::Printf(TEXT("%s beats %s%s"), *WinName, *(Winner == Ar ? Bn : An), bKo ? TEXT(" (knockout)") : TEXT(""));
			UE_LOG(LogTemp, Display, TEXT("FN7 challenge.end: %s %s vs %s winner=%s reason=%s%s"), *DuelId, *An, *Bn, Winner.IsEmpty() ? TEXT("draw") : *Winner, *Reason, bMine ? TEXT(" (yours)") : TEXT(""));
			AddNews(LOCTEXT("DuelTag", "DUEL"), Line);
			if (bMine)
			{
				const bool bWon = !Winner.IsEmpty() && MineRefs.Contains(Winner);
				const FText Title = Winner.IsEmpty() ? LOCTEXT("DuelDraw", "DRAW") : bWon ? LOCTEXT("DuelWon", "VICTORY!") : LOCTEXT("DuelLost", "DEFEAT");
				const FLinearColor Col = Winner.IsEmpty() ? FLinearColor(0.75f, 0.75f, 0.78f) : bWon ? FLinearColor(1.f, 0.82f, 0.3f) : FLinearColor(0.95f, 0.25f, 0.2f);
				ShowBanner(Title, FText::FromString(Line), Col, 0.0, 6.0);
			}
			StoreDuelPanels(DuelId, FN7HudJson::Arr(O, TEXT("panels")));
			if (Duel.IsValid() && Duel->DuelId == DuelId && !Duel->bOver)
			{
				const bool bWonIt = !Winner.IsEmpty() && Duel->MySide == (Winner == Ar ? TEXT("a") : TEXT("b"));
				Duel->bOver = true;
				Duel->OverAt = FPlatformTime::Seconds();
				Duel->Result = Duel->MySide.IsEmpty() ? (WinName.IsEmpty() ? FString(TEXT("DRAW")) : WinName + TEXT(" WINS"))
					: Winner.IsEmpty() ? FString(TEXT("DRAW")) : bWonIt ? FString(TEXT("VICTORY")) : FString(TEXT("DEFEAT"));
				Duel->HpA = Hp(O, TEXT("a"), Duel->HpA);
				Duel->HpB = Hp(O, TEXT("b"), Duel->HpB);
				if (!Deed.IsEmpty()) Duel->AddLog(Deed);
				if (Tray.IsValid()) Tray->ClearInput();
			}
			if (bGlidePending && GlideDuel == DuelId) bGlidePending = false;
			if (bHaveSavedView && ViewDuel == DuelId) RestoreAt = FPlatformTime::Seconds() + 3.0;   // after the result banner
			Duels.Remove(DuelId);
		}
}

void SFNHudLive::NewMatch(const FString& Id)
{
	if (Id == MatchId) return;
	UE_LOG(LogTemp, Display, TEXT("FN7 match: %s -> %s (summon state reset)"), MatchId.IsEmpty() ? TEXT("none") : *MatchId, *Id);
	MatchId = Id;
	Duels.Reset();
	EndedDuels.Reset();
	SeenLooks.Reset();
	NewItems.Reset();
	if (Duel.IsValid()) CloseDuelBox();
	Duel.Reset();
	bWiped = false;
	MineRefs.Reset();
	MineAlive.Reset();
	LastMineLog.Reset();
	PendingSummonRef.Reset();
	WarpedAt.Reset();
	WarpRolled.Reset();
	SeenSummons.Reset();
	SelectedRef.Reset();
	SelectedUntil = 0.0;
	if (AFNTableGods* T = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr)) T->ResetSummons();
}

FString SFNHudLive::EventRef(const TSharedPtr<FJsonObject>& E) const
{
	const FString R = FN7HudJson::Str(E, TEXT("demigod_ref"));
	if (R.StartsWith(TEXT("dg:"))) return R;
	const FString D = FN7HudJson::Str(E, TEXT("demigod"));
	if (D.StartsWith(TEXT("dg:"))) return D;
	if (!D.IsEmpty()) for (const FSeatInfo& S : Seats) if (S.Name == D) return S.Ref;
	return FString();
}

void SFNHudLive::HandleSummoning(const TSharedPtr<FJsonObject>& Pub)
{
	const FString Ref = EventRef(Pub);
	AFNStudio* St = FindStudio();
	AFNTableGods* T = AFNTableGods::GetAny(St ? St->GetWorld() : nullptr);
	if (Ref.IsEmpty() || !T) return;
	double La = 0.0, Lo = 0.0;
	bool bFrom = FN7LonLat(Pub, TEXT("from"), La, Lo) || T->GetBoardPos(Ref, La, Lo);
	UE_LOG(LogTemp, Display, TEXT("FN7 summoning: %s from %.4f, %.4f%s"), *Ref, La, Lo, Ref == PendingSummonRef ? TEXT(" (yours, accepted)") : TEXT(""));
	const FLinearColor Aura = AuraOf(Ref);
	if (bFrom) T->PlaySummoning(Ref, La, Lo, Aura);
	if (Ref == PendingSummonRef) PendingSummonRef.Reset();   // accepted: no refusal is coming
	// The warp-in now, at the accept (the demigod's warp-in was not visible): the column rises, then the arc and the
	// big diorama card land it on its target. The resolve (`summoned`, ~a beat later) adds the -N, the d20, the deed.
	double ToLa = 0.0, ToLo = 0.0;
	bool bTo = FN7LonLat(Pub, TEXT("to"), ToLa, ToLo);
	if (!bTo)
		if (const TArray<TSharedPtr<FJsonValue>>* Su = FN7HudJson::Arr(Pub, TEXT("summons")))
			for (const TSharedPtr<FJsonValue>& V : *Su)
				if (const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr)
					if (EventRef(O) == Ref && FN7LonLat(O, TEXT("to"), ToLa, ToLo)) { bTo = true; break; }
	if (bTo && bFrom)
	{
		FVector Probe;
		if (IsMyDemigod(Ref) && !St->BoardPointFor(ToLa, ToLo, Probe)) St->FocusBoard(ToLa, ToLo, 3);   // your own, off the board
		const float AccLost = (float)FN7HudJson::Num(Pub, TEXT("health_lost"), 0.0);
		const int32 AccRoll = (int32)FN7HudJson::Num(Pub, TEXT("roll"), 0.0);
		T->PlayWarpIn(Ref, La, Lo, ToLa, ToLo, Aura, 0.6f, AccLost, AccRoll);
		WarpedAt.Add(Ref, FPlatformTime::Seconds());
		if (AccRoll > 0 || AccLost > 0.f) WarpRolled.Add(Ref); else WarpRolled.Remove(Ref);
		UE_LOG(LogTemp, Display, TEXT("FN7 warp-in: %s -> %.4f, %.4f"), *Ref, ToLa, ToLo);
	}
}

void SFNHudLive::HandleSummoned(const TSharedPtr<FJsonObject>& E, const TArray<TSharedPtr<FJsonValue>>* Dice, int32 Beat)
{
	const FString Ref = EventRef(E);
	double ToLa = 0.0, ToLo = 0.0, FromLa = 0.0, FromLo = 0.0;
	if (Ref.IsEmpty() || !(FN7LonLat(E, TEXT("to"), ToLa, ToLo) || FN7LonLat(E, TEXT("at"), ToLa, ToLo))) return;
	const FString Key = FString::Printf(TEXT("%s:%s:%d:%.3f,%.3f"), *MatchId, *Ref, Beat, ToLo, ToLa);
	if (SeenSummons.Contains(Key)) return;
	SeenSummons.Add(Key);
	AFNStudio* St = FindStudio();
	AFNTableGods* T = AFNTableGods::GetAny(St ? St->GetWorld() : nullptr);
	if (!St || !T) return;
	if (!FN7LonLat(E, TEXT("from"), FromLa, FromLo)) T->GetBoardPos(Ref, FromLa, FromLo);
	const float Lost = (float)FN7HudJson::Num(E, TEXT("health_lost"), 0.0);
	int32 Roll = (int32)FN7HudJson::Num(E, TEXT("roll"), 0.0);
	if (Roll <= 0 && Dice)
		for (const TSharedPtr<FJsonValue>& V : *Dice)
		{
			const TSharedPtr<FJsonObject> R = V.IsValid() ? V->AsObject() : nullptr;
			if (R.IsValid() && (FN7HudJson::Str(R, TEXT("who")) == Ref || FN7HudJson::Str(R, TEXT("who_ref")) == Ref) && FN7HudJson::Str(R, TEXT("reason")) == TEXT("a summoning")) { Roll = (int32)FN7HudJson::Num(R, TEXT("roll"), 0.0); break; }
		}
	// Yours: the board swoops to where it lands (when that isn't on the board already), so you see it arrive.
	const bool bMine = IsMyDemigod(Ref);
	FVector Probe;
	const bool bOnBoard = St->BoardPointFor(ToLa, ToLo, Probe);
	if (bMine && !bOnBoard) St->FocusBoard(ToLa, ToLo, 3);
	const FLinearColor Aura = AuraOf(Ref);
	const double* Warped = WarpedAt.Find(Ref);
	const bool bWarped = Warped && FPlatformTime::Seconds() - *Warped < 40.0;
	WarpedAt.Remove(Ref);
	const bool bRolled = WarpRolled.Contains(Ref);
	WarpRolled.Remove(Ref);
	if (bWarped) T->PlaySummonResult(Ref, ToLa, ToLo, bRolled ? 0.f : Lost, bRolled ? 0 : Roll);   // settle (+ -N, d20 unless shown at the accept)
	else T->PlaySummoned(Ref, FromLa, FromLo, ToLa, ToLo, Lost, Roll, Aura);
	if (Ref == PendingSummonRef) PendingSummonRef.Reset();
	FString Deed = FN7HudJson::Str(E, TEXT("deed"));
	if (Deed.IsEmpty()) Deed = FN7HudJson::Str(E, TEXT("text"));
	const FString Name = NameOf(Ref);
	UE_LOG(LogTemp, Display, TEXT("FN7 summoned: %s -> %.4f, %.4f lost=%.1f roll=%d %s%s"), *Ref, ToLa, ToLo, Lost, Roll, bOnBoard ? TEXT("on the board") : TEXT("off the board"), bMine && !bOnBoard ? TEXT(", swooped") : TEXT(""));
	FString Title = FString::Printf(TEXT("%s IS SUMMONED"), *Name.ToUpper());
	if (Roll > 0) Title += FString::Printf(TEXT("   -   d20: %d"), Roll);
	if (Lost > 0.f) Title += FString::Printf(TEXT("   -   %g health"), -FMath::RoundToFloat(Lost * 10.f) / 10.f);
	ShowBanner(FText::FromString(Title), FText::FromString(Deed.IsEmpty() ? FString::Printf(TEXT("%s arrives."), *Name) : Deed), Aura, bWarped ? 0.0 : 0.8, 4.5);
	AddNews(LOCTEXT("SummonedTag", "SUMMON"), Deed.IsEmpty() ? FString::Printf(TEXT("%s is summoned."), *Name) : Deed);
}

/** "Chris's Left Hand (2)" -> "Chris's Left Hand" (the server's de-dup suffix). */
static FString FN7BaseName(const FString& N)
{
	int32 Open = INDEX_NONE;
	if (!N.EndsWith(TEXT(")")) || !N.FindLastChar(TEXT('('), Open) || Open < 1 || N[Open - 1] != TEXT(' ')) return N;
	const FString Inner = N.Mid(Open + 1, N.Len() - Open - 2);
	return Inner.IsNumeric() ? N.Left(Open - 1) : N;
}

FString SFNHudLive::LoadedArt(const FString& Ref, bool bDiorama)
{
	const FSeatInfo* S = Seats.FindByPredicate([&Ref](const FSeatInfo& X) { return X.Ref == Ref; });
	if (!S) return FString();
	const FString& Own = bDiorama ? S->DioramaHash : S->PortraitHash;
	if (!Own.IsEmpty() && Portraits.Contains(Own)) return Own;
	const FString Base = FN7BaseName(S->Name);
	for (const FSeatInfo& O : Seats)
		if (O.Ref != Ref && FN7BaseName(O.Name) == Base)
		{
			const FString& H = bDiorama ? O.DioramaHash : O.PortraitHash;
			if (!H.IsEmpty() && Portraits.Contains(H)) return H;
		}
	const FString Player = FN7BaseName(S->Player);
	if (!Player.IsEmpty() && !S->Side.IsEmpty() && (S->Controller == TEXT("human") || IsMyDemigod(Ref)))
	{
		const FString Key = Player.ToLower() + TEXT("|") + S->Side + (bDiorama ? TEXT("|dio") : TEXT("|por"));
		if (const FString* H = ProfileArt.Find(Key)) { if (Portraits.Contains(*H)) return *H; }
		else FetchProfileArt(Player);
	}
	return Own;
}

void SFNHudLive::FetchProfileArt(const FString& Player)
{
	UFNNetClient* N = Net.Get();
	const FString Lower = Player.ToLower();
	if (!N || ProfileAsked.Contains(Lower)) return;
	ProfileAsked.Add(Lower);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(FString::Printf(TEXT("/api/profiles/%s"), *FGenericPlatformHttp::UrlEncode(Player))));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(15.f);
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak, Lower](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid() || !bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		TSharedPtr<FJsonObject> P;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, P) || !P.IsValid()) return;
		int32 Found = 0;
		if (const TArray<TSharedPtr<FJsonValue>>* Gs = FN7HudJson::Arr(P, TEXT("demigods")))
			for (const TSharedPtr<FJsonValue>& V : *Gs)
			{
				const TSharedPtr<FJsonObject> G = V.IsValid() ? V->AsObject() : nullptr;
				const FString Hand = FN7HudJson::Str(G, TEXT("hand"));
				if (Hand.IsEmpty()) continue;
				for (const TCHAR* Kind : { TEXT("dio"), TEXT("por") })
				{
					const FString Hash = FN7HudJson::Str(G, FCString::Strcmp(Kind, TEXT("dio")) == 0 ? TEXT("diorama_hash") : TEXT("portrait_hash"));
					if (Hash.IsEmpty()) continue;
					Self->ProfileArt.Add(Lower + TEXT("|") + Hand + TEXT("|") + Kind, Hash);
					Self->RequestPortrait(Hash, FString::Printf(TEXT("/media/%s.png"), *Hash));
					++Found;
				}
			}
		UE_LOG(LogTemp, Display, TEXT("FN7 media: profile art for \"%s\": %d images"), *Lower, Found);
		Self->Publish();
	});
	Req->ProcessRequest();
}

void SFNHudLive::OnDemigodDoubleClick(const FString& Ref)
{
	// Double-click a demigod: centre the table (same zoom) and the globe on it; yours also glows. A single click never moves anything.
	AFNStudio* St = FindStudio();
	AFNTableGods* T = AFNTableGods::GetAny(St ? St->GetWorld() : nullptr);
	double La = 0.0, Lo = 0.0;
	bool bPos = T && T->GetBoardPos(Ref, La, Lo);
	for (const FSeatInfo& S : Seats) if (!bPos && S.Ref == Ref && S.bHasPos && S.bAlive) { La = S.Lat; Lo = S.Lon; bPos = true; }
	const bool bMine = IsMyDemigod(Ref);
	UE_LOG(LogTemp, Display, TEXT("FN7 double-click: %s %s %s"), *Ref, bMine ? TEXT("(yours)") : TEXT("(rival)"), bPos ? *FString::Printf(TEXT("-> centre %.4f, %.4f"), La, Lo) : TEXT("(not on the map)"));
	if (!St || !bPos) { Toast(TEXT("That demigod isn't on the map.")); return; }
	// Chris reported that double clicking does not centre the globe map and table map: the table centres on it at the zoom you're at
	// (no deeper zoom), and the globe turns to face it (held there ~30 s against the auto-turn to your lead demigod).
	St->CenterBoardAndGlobe(La, Lo);
	GlobeHoldLat = La;
	GlobeHoldLon = Lo;
	GlobeHoldUntil = FPlatformTime::Seconds() + 30.0;
	if (!bMine)
	{
		// A rival: a challenge, always one confirm away (the challenge felt harder to reach than it should be). In reach (the two pieces
		// touching on the table, or within the server's range: max(120 km, the contact distance at this zoom)) it just
		// challenges; otherwise your demigod teleports beside the rival first (the summoning's mishap: about -1 to -2
		// health) and then challenges from there.
		static const double ContactKm[4] = { 0.015, 0.15, 15.0, 600.0 };
		static const TCHAR* LevelName[4] = { TEXT("people"), TEXT("houses"), TEXT("villages"), TEXT("regions") };
		const int32 Lv = FMath::Clamp(St->GetMapLevel(), 0, 3);
		const double Reach = FMath::Max(120.0, ContactKm[Lv]);
		auto Km = [](double A1, double O1, double A2, double O2)
		{
			const double L1 = FMath::DegreesToRadians(A1), L2 = FMath::DegreesToRadians(A2), DO = FMath::DegreesToRadians(O2 - O1);
			return FMath::Acos(FMath::Clamp(FMath::Sin(L1) * FMath::Sin(L2) + FMath::Cos(L1) * FMath::Cos(L2) * FMath::Cos(DO), -1.0, 1.0)) * 6371.0;
		};
		FString Mine;
		double Best = TNumericLimits<double>::Max();
		bool bReach = false, bDuelling = false;
		for (const FSeatInfo& S : Seats)
		{
			if (!MineRefs.Contains(S.Ref) || !S.bAlive || !S.bHasPos) continue;
			if (Duel.IsValid() && !Duel->bOver && (Duel->A == S.Ref || Duel->B == S.Ref)) { bDuelling = true; continue; }   // busy: the other one goes
			const double K = Km(S.Lat, S.Lon, La, Lo);
			const bool bIn = (T && T->PiecesTouch(S.Ref, Ref)) || K <= Reach;
			// The one you selected goes first, then one already in reach, then the nearest.
			const double Score = K - (S.Ref == SelectedRef ? 1e7 : 0.0) - (bIn ? 1e6 : 0.0);
			if (Score < Best) { Best = Score; Mine = S.Ref; bReach = bIn; }
		}
		if (Mine.IsEmpty()) { Toast(bDuelling ? TEXT("Your demigod is already in a duel: finish this one first.") : TEXT("Both of your demigods have fallen: no one to send into a challenge.")); return; }
		ConfirmMine = Mine;
		ConfirmTarget = Ref;
		ConfirmLevel = LevelName[Lv];
		ConfirmLat = La;
		ConfirmLon = Lo;
		bConfirmTeleport = !bReach;
		ConfirmTitle = FText::FromString(FString::Printf(TEXT("CHALLENGE %s?"), *NameOf(Ref).ToUpper()));
		ConfirmBody = bReach
			? FText::FromString(FString::Printf(TEXT("%s is within reach. The duel starts at once: the world holds its breath while you fight."), *NameOf(Mine)))
			: FText::FromString(FString::Printf(TEXT("%s teleports beside %s first: expect about -1 to -2 health from the summoning (it never drops you below 2.5). Then the duel starts."), *NameOf(Mine), *NameOf(Ref)));
		bConfirmOpen = true;
		UE_LOG(LogTemp, Display, TEXT("FN7 challenge confirm: %s -> %s (%s, %s)"), *Mine, *Ref, bReach ? TEXT("in reach") : TEXT("teleport first"), LevelName[Lv]);
		return;
	}
	if (T && bMine)
	{
		if (!SelectedRef.IsEmpty() && SelectedRef != Ref) T->SetFigureHeld(SelectedRef, false);
		SelectedRef = Ref;
		SelectedUntil = FPlatformTime::Seconds() + 2.0;
		T->SetFigureHeld(Ref, true);
	}
}

bool SFNHudLive::OnDemigodWheel(const FString& Ref, float Delta)
{
	// The wheel near a demigod: one level in (or out), centred on its exact position.
	AFNStudio* St = FindStudio();
	AFNTableGods* T = AFNTableGods::GetAny(St ? St->GetWorld() : nullptr);
	if (!St || FMath::IsNearlyZero(Delta)) return false;
	double La = 0.0, Lo = 0.0;
	bool bPos = T && T->GetBoardPos(Ref, La, Lo);
	for (const FSeatInfo& S : Seats) if (!bPos && S.Ref == Ref && S.bHasPos && S.bAlive) { La = S.Lat; Lo = S.Lon; bPos = true; }
	if (!bPos) return false;
	// One level per gesture: a wheel sends several notches in a few milliseconds (each used to rebuild the board), and
	// at the widest level a scroll out changes nothing at all.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastFigureWheelAt < 0.4) return true;
	const int32 Lv = FMath::Clamp(St->GetMapLevel() + (Delta > 0.f ? -1 : 1), 0, 3);
	if (St->IsSameBoardSpot(La, Lo, Lv)) return true;
	LastFigureWheelAt = Now;
	UE_LOG(LogTemp, Display, TEXT("FN7 wheel: %s -> level %d at %.5f, %.5f"), *Ref, Lv, La, Lo);
	St->FocusBoard(La, Lo, Lv);
	return true;
}

void SFNHudLive::ConfirmChallenge()
{
	bConfirmOpen = false;
	UFNNetClient* N = Net.Get();
	if (!N || ConfirmMine.IsEmpty() || ConfirmTarget.IsEmpty()) return;
	// A demigod already in a duel can't take another (the server answers "busy", after the teleport has already moved it).
	if (Duel.IsValid() && !Duel->bOver && (Duel->A == ConfirmMine || Duel->B == ConfirmMine))
	{
		Toast(FString::Printf(TEXT("%s is already in a duel."), *NameOf(ConfirmMine)));
		return;
	}
	if (!N->CanSendInstants(bConfirmTeleport ? 2 : 1)) { bConfirmOpen = true; Toast(TEXT("The gods need a breath: try again in a moment.")); return; }   // the card stays: click again
	if (bConfirmTeleport)
	{
		// Teleport beside the rival (the server nudges it onto a free neighbouring hex), then challenge from there.
		N->Summon(Round, ConfirmMine, ConfirmLon, ConfirmLat);
		if (AFNTableGods* T = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr))
		{
			double FromLa = 0.0, FromLo = 0.0;
			if (T->GetBoardPos(ConfirmMine, FromLa, FromLo)) T->PlaySummoning(ConfirmMine, FromLa, FromLo, AuraOf(ConfirmMine));
		}
		PendingSummonRef = ConfirmMine;
		PendingSummonAt = FPlatformTime::Seconds();
	}
	N->Challenge(Round, ConfirmMine, ConfirmTarget, ConfirmLevel);
	PendingChallengeRef = ConfirmTarget;
	PendingChallengeAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("FN7 challenge sent: %s -> %s (%s%s)"), *ConfirmMine, *ConfirmTarget, *ConfirmLevel, bConfirmTeleport ? TEXT(", after a teleport") : TEXT(""));
	Toast(FString::Printf(TEXT("CHALLENGE!  %s  vs  %s"), *NameOf(ConfirmMine), *NameOf(ConfirmTarget)));
}

void SFNHudLive::ShowBanner(const FText& Title, const FText& Text, const FLinearColor& Color, double Delay, double Secs)
{
	NextBannerTitle = Title;
	NextBanner = Text;
	NextBannerColor = Color;
	BannerAt = FPlatformTime::Seconds() + Delay;
	BannerUntil = BannerAt + Secs;
}

void SFNHudLive::PreviewSummon(int32 Step)
{
	AFNStudio* St = FindStudio();
	AFNTableGods* T = AFNTableGods::GetAny(St ? St->GetWorld() : nullptr);
	if (!T) return;
	const FString Ref = Step == 0 ? TEXT("dg:0") : TEXT("dg:1");
	double La = 46.9, Lo = 8.2;
	T->GetBoardPos(Ref, La, Lo);
	{
		// The accept push, as the server sends it: the column, then the warp-in at once.
		TSharedPtr<FJsonObject> Acc = MakeShared<FJsonObject>();
		Acc->SetStringField(TEXT("event"), TEXT("summoning"));
		Acc->SetStringField(TEXT("demigod"), Ref);
		TArray<TSharedPtr<FJsonValue>> F, To;
		F.Add(MakeShared<FJsonValueNumber>(Lo)); F.Add(MakeShared<FJsonValueNumber>(La));
		To.Add(MakeShared<FJsonValueNumber>(Step == 0 ? 11.8 : 6.6)); To.Add(MakeShared<FJsonValueNumber>(Step == 0 ? 45.6 : 46.0));
		Acc->SetArrayField(TEXT("from"), F);
		Acc->SetArrayField(TEXT("to"), To);
		HandleSummoning(Acc);
	}
	TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
	E->SetStringField(TEXT("event"), TEXT("summoned"));
	E->SetStringField(TEXT("demigod"), Ref);
	auto Pos = [](double Lon, double Lat) { TArray<TSharedPtr<FJsonValue>> A; A.Add(MakeShared<FJsonValueNumber>(Lon)); A.Add(MakeShared<FJsonValueNumber>(Lat)); return A; };
	TSharedPtr<FJsonObject> From = MakeShared<FJsonObject>(), To = MakeShared<FJsonObject>();
	From->SetArrayField(TEXT("pos"), Pos(Lo, La));
	To->SetArrayField(TEXT("pos"), Step == 0 ? Pos(11.8, 45.6) : Pos(6.6, 46.0));
	E->SetObjectField(TEXT("from"), From);
	E->SetObjectField(TEXT("to"), To);
	E->SetNumberField(TEXT("health_lost"), Step == 0 ? 2.0 : 1.0);
	E->SetNumberField(TEXT("roll"), Step == 0 ? 7 : 17);
	E->SetStringField(TEXT("deed"), Step == 0
		? TEXT("Mid-go to Northern Italy, Chris's Left Hand was folded into a paper airplane by an impatient god and bounced twice off a tram before it stuck the landing.")
		: TEXT("Chris's Right Hand stepped through a sulphur-scented rift and came out the other side holding someone else's umbrella."));
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7PreviewSummoned"), 3.0f, [Weak, E, Step](float)
	{
		if (const TSharedPtr<SFNHudLive> Self = Weak.Pin()) Self->HandleSummoned(E, nullptr, 900 + Step);
		return false;
	});
}

int32 SFNHudLive::RingTarget(const FVector2D& ScreenPosition, double& OutLat, double& OutLon, FVector& OutWorld, FVector2D& OutPixel) const
{
	AFNStudio* St = FindStudio();
	if (!St || !FN7ViewportPx::AbsToPixel(ScreenPosition, OutPixel) || SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return 0;
	if (St->GlobePointAtScreen(OutPixel, OutLat, OutLon, OutWorld)) return 2;   // MEGA: over the globe
	double HexM;
	if (St->HexAtScreen(OutPixel, OutLat, OutLon, OutWorld, HexM)) return 1;   // MINI: on the board
	return 0;
}

void SFNHudLive::OnPowerDragOver(const FString& Power, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	FVector2D Pixel;
	if (!St) return;
	const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();
	if (Op.IsValid() && Op->bFromRing)
	{
		// A hex in the hand: over the globe it is a MEGA (its area ringed on the globe), over the board a MINI.
		double La = 0.0, Lo = 0.0;
		FVector World;
		const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
		St->ShowMegaOnGlobe(Where == 2, La, Lo, RadiusFor(Power, TEXT("mega")));
		St->ShowAreaOfEffect(Where == 1, Pixel, RadiusFor(Power, TEXT("mini")));
		if ((Where == 2) != bOverGlobe)
		{
			bOverGlobe = Where == 2;
			FFNPowerDragDrop::OnMegaArmed().Broadcast(bOverGlobe, Power);
		}
		if (AFNPowerRing* Ring = AFNPowerRing::Get(St, false))
		{
			APlayerController* PC = St->GetWorld() ? St->GetWorld()->GetFirstPlayerController() : nullptr;
			const float S = (float)St->GetActorScale3D().X;
			if (Where == 2 && PC && PC->PlayerCameraManager)
				Ring->MoveLifted(World + (PC->PlayerCameraManager->GetCameraLocation() - World).GetSafeNormal() * 14.f * S);
			else if (Where == 1 && PC && PC->PlayerCameraManager)
			{
				// Over the board: small, higher, and a little beyond the target as seen from the camera, so the lit hexes
				// stay in view in front of it.
				const FVector Away = (World - PC->PlayerCameraManager->GetCameraLocation()).GetSafeNormal2D();
				Ring->MoveLifted(World + FVector(0, 0, 15.f * S) + Away * 9.f * S, 0.45f);
			}
			else if (PC)
			{
				// Anywhere else: hover over the table plane under the cursor.
				FVector O, D, C;
				double R;
				FRotator TR;
				if (PC->DeprojectScreenPositionToWorld(Pixel.X, Pixel.Y, O, D) && St->GetBoardRim(C, R, TR) && D.Z < -1e-3)
					Ring->MoveLifted(O + D * ((C.Z + 14.f * S - O.Z) / D.Z));
			}
		}
		return;
	}
	const FString Key = (Op.IsValid() && !Op->Tier.IsEmpty()) ? Power + TEXT(":") + Op->Tier : Power;
	const double* Radius = PowerRadiusKm.Find(Key);
	const bool bOver = FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) && !SFNPanelMover::IsOverAnyPanel(ScreenPosition);
	St->ShowAreaOfEffect(bOver, Pixel, Radius ? *Radius : 0.0);
}

void SFNHudLive::OnPowerDragEnded(const FString& Power)
{
	if (AFNStudio* St = FindStudio())
	{
		St->ShowAreaOfEffect(false, FVector2D::ZeroVector, 0.0);
		St->ShowMegaOnGlobe(false, 0.0, 0.0, 0.0);
		if (AFNPowerRing* Ring = AFNPowerRing::Get(St, false)) Ring->EndLift();   // the hex flies back to its place
	}
	if (bOverGlobe)
	{
		bOverGlobe = false;
		FFNPowerDragDrop::OnMegaArmed().Broadcast(false, Power);
	}
}

void SFNHudLive::OnPowerDropped(const FString& Power, const FVector2D& ScreenPosition)
{
	if (SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return;   // let go over a panel: nothing happens
	{
		const TSharedPtr<FFNPowerDragDrop> RingOp = FFNPowerDragDrop::GetActive();   // still set while OnDroppedOnWorld runs
		if (RingOp.IsValid() && RingOp->bFromRing)
		{
			// Where the hex lands decides: over the globe a MEGA (a bolt), on the board a MINI (a spark), else nothing.
			AFNStudio* RSt = FindStudio();
			UFNNetClient* RN = Net.Get();
			double La = 0.0, Lo = 0.0;
			FVector World;
			FVector2D Pixel;
			const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
			if (RN && RSt && Where > 0)
			{
				const bool bMega = Where == 2;
				if (!RN->CastAt(Round, Power, Lo, La, bMega ? TEXT("mega") : TEXT("mini"))) { Toast(TEXT("The gods need a breath: try again in a moment.")); return; }   // the power stays in the ring
				RSt->FlashAt(Lo, La);
				// No view hop (it should be used on the cell on the table, not hop the table view around ... it is a drop).
				SetArmed(FString());
				AddNews(bMega ? LOCTEXT("Mega", "MEGA") : LOCTEXT("Mini", "MINI"),
					FString::Printf(TEXT("%s at %.2f%s %.2f%s"), *Power, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
				Publish();
			}
			return;
		}
	}
	// Dropped on the board: cast at that hex. Anywhere else (the globe, empty table): arm it, write the target, SEND.
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	FVector2D Pixel;
	double La, Lo, HexM;
	FVector World;
	if (N && St && FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) && St->HexAtScreen(Pixel, La, Lo, World, HexM))
	{
		const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();   // still set while OnDroppedOnWorld runs
		const FString Tier = (Op.IsValid() && !Op->Tier.IsEmpty()) ? Op->Tier : FString(TEXT("mini"));
		if (!N->CastAt(Round, Power, Lo, La, Tier)) { Toast(TEXT("The gods need a breath: try again in a moment.")); return; }   // still in hand
		St->FlashAt(Lo, La);
		SetArmed(FString());
		AddNews(LOCTEXT("Cast", "CAST"), FString::Printf(TEXT("%s at %.2f%s %.2f%s"), *Power, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
		Publish();
		return;
	}
	{
		const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();   // still set while OnDroppedOnWorld runs
		SetArmed((Op.IsValid() && !Op->Tier.IsEmpty()) ? Power + TEXT(":") + Op->Tier : Power);
	}
	AddNews(LOCTEXT("Armed", "ARMED"), FString::Printf(TEXT("%s: drop it on a board hex, or write its target and SEND."), *Power));
	Publish();
}

void SFNHudLive::OnPower(const FString& Slug)
{
	SetArmed(ArmedPower == Slug ? FString() : Slug);
	Publish();
}

void SFNHudLive::SetArmed(const FString& Key)
{
	if (Key == ArmedPower) return;
	FString OldSlug, OldTier, NewSlug, NewTier;
	if (!ArmedPower.Split(TEXT(":"), &OldSlug, &OldTier)) OldSlug = ArmedPower;
	if (!Key.Split(TEXT(":"), &NewSlug, &NewTier)) NewSlug = Key;
	if (OldTier == TEXT("mega")) FFNPowerDragDrop::OnMegaArmed().Broadcast(false, OldSlug);
	ArmedPower = Key;
	if (NewTier == TEXT("mega")) FFNPowerDragDrop::OnMegaArmed().Broadcast(true, NewSlug);
}

FText SFNHudLive::GetTimer() const
{
	if (DeadlineMs <= 0.0) return FText::GetEmpty();
	const double NowMs = (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	const int32 Secs = FMath::Max(0, FMath::CeilToInt((DeadlineMs - NowMs) / 1000.0));
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60));
}

EActiveTimerReturnType SFNHudLive::TickTimer(double, float)
{
	UpdateBoardInfo();
	// A restart is coming (the Mac session writes Saved/FN7Notice.txt = "<epoch seconds of the restart> <text>"): say so on
	// screen first, so a fresh build never looks like a crash (the knockouts were reported to seem like a crash).
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("FN7Notice.txt");
		FString Raw;
		if (FPaths::FileExists(Path) && FFileHelper::LoadFileToString(Raw, *Path))
		{
			FString When, Text;
			if (!Raw.TrimStartAndEnd().Split(TEXT(" "), &When, &Text)) { When = Raw.TrimStartAndEnd(); Text.Reset(); }
			UpdateAt = FCString::Atod(*When);
			UpdateText = Text.IsEmpty() ? FString(TEXT("Updating to the newest build")) : Text;
		}
		else UpdateAt = 0.0;
	}
	// The first time this machine joins a match: the HOW TO PLAY tablet, once.
	if (bInRoom && !bHowToChecked)
	{
		bHowToChecked = true;
		if (!FPaths::FileExists(FPaths::ProjectSavedDir() / TEXT("FN7SeenHowToPlay.txt"))) bHowTo = true;
	}
	// Until we're in a room, ask the server every 10 s whether it can be reached (a judge with no key or no internet
	// sees why at once, not a normal-looking table that never connects).
	if (!bInRoom && FPlatformTime::Seconds() - LastServerCheck > 10.0) CheckServer();
	// The star world around the table (AFNStarBackdrop), once the studio has built the room.
	if (!StarSky.IsValid())
		if (AFNStudio* St = FindStudio()) StarSky = AFNStarBackdrop::Ensure(St->GetWorld());
	for (auto It = NewItems.CreateIterator(); It; ++It) if (FPlatformTime::Seconds() - It.Value().At > 6.5) { It.RemoveCurrent(); Publish(); break; }
	TickDuelView();   // a challenge's queued glide (once you stop typing / dragging) and the glide back after it
	if (bVictory && VictoryFadeAt > 0.0 && FPlatformTime::Seconds() - VictoryFadeAt > 1.25) { bVictory = false; VictoryFadeAt = 0.0; }
	// Every 10 s, the real frame rate into the log ("FN7 fps"), so a PIE session's speed can be read afterwards.
	{
		static uint64 FpsFrame = 0;
		static double FpsTime = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (FpsTime <= 0.0) { FpsTime = Now; FpsFrame = GFrameCounter; }
		else if (Now - FpsTime >= 10.0)
		{
			const double Fps = (double)(GFrameCounter - FpsFrame) / (Now - FpsTime);
			UE_LOG(LogTemp, Display, TEXT("FN7 fps %.1f (%.1f ms/frame)"), Fps, Fps > 0.0 ? 1000.0 / Fps : 0.0);
			FpsTime = Now;
			FpsFrame = GFrameCounter;
		}
	}
	if (AFNStudio* Sw = FindStudio()) Sw->HideWillBlock();   // the kit's WILL scroll replaces it, lobby included
	if (!bInRoom) UpdateRing();                               // the intro's bag stands on the table before any message
	if (ToastUntil > 0.0 && FPlatformTime::Seconds() > ToastUntil) { ToastUntil = 0.0; Model.Status = FText::GetEmpty(); Publish(); }
	{
		const double Now = FPlatformTime::Seconds();
		// (no re-assert loop: the studio keeps a picked spot itself now; re-asserting fought its own focus = the spinning)
		if (SelectedUntil > 0.0 && Now > SelectedUntil)
		{
			SelectedUntil = 0.0;
			if (AFNTableGods* Tg = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr)) Tg->SetFigureHeld(SelectedRef, false);
		}
		if (BannerAt > 0.0 && Now >= BannerAt)
		{
			BannerAt = 0.0;
			Model.BannerTitle = NextBannerTitle;
			Model.Banner = NextBanner;
			Model.BannerColor = NextBannerColor;
			Publish();
		}
		else if (BannerUntil > 0.0 && BannerAt <= 0.0 && Now > BannerUntil)
		{
			BannerUntil = 0.0;
			Model.Banner = FText::GetEmpty();
			Model.BannerTitle = FText::GetEmpty();
			Publish();
		}
	}
	// TAKE ANOTHER DEMIGOD unanswered for a few seconds: say when the next chance comes.
	if (bOut && bTakeAsked && FPlatformTime::Seconds() - TakeAskedAt > 6.0)
	{
		bTakeAsked = false;
		OutNote = Model.BeatsTotal > Model.Beat
			? FString::Printf(TEXT("No demigod is free right now. The next match starts in about %d beats."), Model.BeatsTotal - Model.Beat)
			: FString(TEXT("No demigod is free right now. You'll get one next match."));
	}
	// Time's live bill while you hold slowed time: debt(t) = min(cap, base + per_second * t), plus the bolt.
	const bool bHolding = MySeat >= 0 && WindowSeat == MySeat;
	FText Bill;
	if (bHolding)
	{
		if (bSlowBill)
		{
			UFNNetClient* N = Net.Get();
			const double Secs = FMath::Max(0.0, ((N ? N->NowServerMs() : SlowOpenedMs) - SlowOpenedMs) / 1000.0);
			FNumberFormattingOptions One;
			One.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
			Bill = FText::Format(LOCTEXT("BillNow", "{0} {0}|plural(one=bolt,other=bolts) + {1} debt"), BoltPaid, FText::AsNumber(FMath::Min(DebtCap, DebtBase + DebtPerS * Secs), &One));
		}
		else
		{
			Bill = LOCTEXT("BillPlain", "1 bolt + debt, growing while you write");
		}
	}
	if (Layout.IsValid()) Layout->SetTimeBill(Bill, bHolding);
	Model.TimeBill = Bill;
	Model.bHoldingTime = bHolding;
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

// --- mounting ---------------------------------------------------------------

namespace FN7HudMount
{
	TSharedPtr<SWidget> GMountWeak;
	TSharedPtr<SWidget> GMountLive;
	TArray<TWeakPtr<SWidget>> GHidden;

	void FindByType(const TSharedRef<SWidget>& W, const FName& Type, TArray<TSharedRef<SWidget>>& Out)
	{
		if (W->GetType() == Type) Out.Add(W);
		FChildren* Kids = W->GetChildren();
		if (!Kids) return;
		for (int32 i = 0; i < Kids->Num(); ++i) FindByType(Kids->GetChildAt(i), Type, Out);
	}

	void SetOldHudHidden(bool bHide)
	{
		if (!bHide)
		{
			for (const TWeakPtr<SWidget>& W : GHidden)
				if (TSharedPtr<SWidget> P = W.Pin()) P->SetVisibility(EVisibility::SelfHitTestInvisible);
			GHidden.Reset();
			return;
		}
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> VP = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!VP.IsValid()) return;
		TArray<TSharedRef<SWidget>> Found;
		FindByType(VP.ToSharedRef(), FName(TEXT("SFNRootWidget")), Found);
		for (const TSharedRef<SWidget>& W : Found)
		{
			W->SetVisibility(EVisibility::Collapsed);
			GHidden.Add(W);
		}
	}

	bool Mount(UFNNetClient* Net)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		if (!VC || !Net) return false;
		Unmount();
		GMountLive = SNew(SFNHudLive).Net(Net);
		GMountWeak = SNew(SWeakWidget).PossiblyNullContent(GMountLive);
		VC->AddViewportWidgetContent(GMountWeak.ToSharedRef(), 50);
		SetOldHudHidden(true);
		return true;
	}

	void Unmount()
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		if (VC && GMountWeak.IsValid()) VC->RemoveViewportWidgetContent(GMountWeak.ToSharedRef());
		GMountWeak.Reset();
		GMountLive.Reset();
		SetOldHudHidden(false);
	}

	bool IsMounted() { return GMountLive.IsValid(); }

	void Preview(bool bLobby, bool bFell, bool bWill)
	{
		if (GMountLive.IsValid()) StaticCastSharedPtr<SFNHudLive>(GMountLive)->DebugPreview(bLobby, bFell, bWill);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFN7HudCommand(
	TEXT("FN7.Hud"),
	TEXT("FN7.Hud on | off [connect] [shot] [quit] [lobby] [fell]: the live style-kit HUD over the game (hides the old HUD while on). ")
	TEXT("preview / lobby / fell / will = the sample table, plus the lobby over the game / the out card / the WILL open in slowed time (no game joined). ")
	TEXT("connect = start the net client now; shot = save Saved/FN7HudLive_<n>.png every 8 s; quit = exit after 4 shots."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const bool bOff = Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase);
		if (bOff) { FN7HudMount::Unmount(); return; }
		const bool bConnect = Args.Contains(TEXT("connect"));
		const bool bShot = Args.Contains(TEXT("shot"));
		const bool bQuit = Args.Contains(TEXT("quit"));
		const bool bAoe = Args.Contains(TEXT("aoe"));
		const bool bLobby = Args.Contains(TEXT("lobby"));
		const bool bFell = Args.Contains(TEXT("fell"));
		const bool bWill = Args.Contains(TEXT("will"));
		const bool bPreview = Args.Contains(TEXT("preview"));
		const bool bPour = Args.Contains(TEXT("pour"));
		const bool bMega = Args.Contains(TEXT("mega"));
		const bool bSummon = Args.Contains(TEXT("summon"));
		const bool bClose = Args.Contains(TEXT("close"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		// Wait for the player controller and its net client, then mount (and optionally connect / photograph).
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudMount"), 0.5f, [WeakWorld, bConnect, bShot, bQuit, bAoe, bLobby, bFell, bWill, bPreview, bPour, bMega, bSummon, bClose](float)
		{
			UWorld* W = WeakWorld.Get();
			AFNPlayerController* PC = W ? Cast<AFNPlayerController>(W->GetFirstPlayerController()) : nullptr;
			if (!PC || !PC->Net) return true;
			FN7HudMount::Mount(PC->Net);
			if (bConnect) PC->Net->Start();
			if (bLobby || bFell || bWill || bPreview) FN7HudMount::Preview(bLobby, bFell, bWill);
			if (bClose)
			{
				// Gate: the table close in (villages level) over the preview's Benito pieces, before the first shot.
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudClose"), 5.f, [WeakPC](float)
				{
					if (AFNPlayerController* P = WeakPC.Get()) if (AFNStudio* S = P->Studio.Get()) S->PreviewFocus(46.05, 10.9, 2);
					return false;
				});
			}
			if (bSummon)
			{
				// Test: the summon sequence, timed so shot 2 catches the big diorama card and shot 3 the landing, -N and d20.
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudSummon"), 14.9f, [](float)
				{
					if (TSharedPtr<SFNHudLive> H = StaticCastSharedPtr<SFNHudLive>(FN7HudMount::GMountLive)) H->PreviewSummon(0);
					return false;
				});
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudSummon2"), 20.2f, [](float)
				{
					if (TSharedPtr<SFNHudLive> H = StaticCastSharedPtr<SFNHudLive>(FN7HudMount::GMountLive)) H->PreviewSummon(1);
					return false;
				});
			}
			if (bMega)
			{
				// Test: a mega in hand from just before the second shot (the fault lines fade in), with its ring on the globe.
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudMega"), 15.4f, [WeakPC](float)
				{
					FFNPowerDragDrop::OnMegaArmed().Broadcast(true, TEXT("earthquake"));
					if (AFNPlayerController* P = WeakPC.Get())
						if (AFNStudio* S = P->Studio.Get()) S->ShowMegaOnGlobe(true, 35.7, 139.7, 900.0);
					return false;
				});
			}
			if (bPour)
			{
				// Test: click the pouch just before the second shot (shot 1 closed, 2 mid-burst, 3 landed).
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudPour"), 15.55f, [WeakPC](float)
				{
					AFNPlayerController* P = WeakPC.Get();
					if (AFNPowerRing* Ring = P ? AFNPowerRing::Get(P->Studio.Get(), false) : nullptr) Ring->TogglePouch();
					return false;
				});
			}
			if (bAoe)
			{
				// Test: light an Earthquake's area (250 km) around the board hex at 42% / 78% of the viewport, as a drag would.
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudAoe"), 13.f, [WeakPC](float)
				{
					AFNPlayerController* P = WeakPC.Get();
					UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
					if (!P || !P->Studio || !VC || !VC->Viewport) return false;
					const FIntPoint Px = VC->Viewport->GetSizeXY();
					const FVector2D Pixel(Px.X * 0.42, Px.Y * 0.78);
					double La = 0, Lo = 0, HexM = 0;
					FVector World;
					const bool bHit = P->Studio->HexAtScreen(Pixel, La, Lo, World, HexM);
					UE_LOG(LogTemp, Warning, TEXT("FN7 AoE test: hex hit=%d lat=%.3f lon=%.3f hex=%.0f m"), bHit ? 1 : 0, La, Lo, HexM);
					P->Studio->ShowAreaOfEffect(true, Pixel, 250.0);
					return false;
				});
			}
			if (bShot)
			{
				for (int32 i = 1; i <= 4; ++i)
				{
					FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudShot"), 8.f * i, [i](float)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), FString::Printf(TEXT("FN7HudLive_%d.png"), i)), true, false);
						return false;
					});
				}
				if (bQuit)
				{
					FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudQuit"), 36.f, [](float)
					{
						FPlatformMisc::RequestExit(false, TEXT("FN7.Hud quit"));
						return false;
					});
				}
			}
			return false;
		});
	}));

#undef LOCTEXT_NAMESPACE

// --- FN7.Fps [seconds]: the real frame rate, logged once a second (the log's frame counter wraps every 1000) -------
static FAutoConsoleCommand GFN7FpsCommand(
	TEXT("FN7.Fps"),
	TEXT("FN7.Fps [seconds=20]: log the frame rate and frame time once a second."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 Secs = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 600) : 20;
		if (Args.Contains(TEXT("dump")))
			FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7FpsDump"), 10.f, [](float)
			{
				if (GEngine) GEngine->Exec(nullptr, TEXT("stat dumpframe -ms=0.5"));
				return false;
			});
		struct FState { uint64 Frame = 0; double Time = 0.0; int32 Left = 0; };
		TSharedRef<FState> St = MakeShared<FState>();
		St->Frame = GFrameCounter;
		St->Time = FPlatformTime::Seconds();
		St->Left = Secs;
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7Fps"), 1.f, [St](float)
		{
			const uint64 F = GFrameCounter;
			const double T = FPlatformTime::Seconds();
			const double Fps = (double)(F - St->Frame) / FMath::Max(T - St->Time, 1e-3);
			UE_LOG(LogTemp, Display, TEXT("FN7 fps %.1f (%.1f ms/frame)"), Fps, Fps > 0.0 ? 1000.0 / Fps : 0.0);
			St->Frame = F;
			St->Time = T;
			return --St->Left > 0;
		});
	}));
