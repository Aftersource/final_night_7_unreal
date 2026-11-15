#include "SFNHudLayout.h"
#include "Widgets/SToolTip.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "SFNFoil.h"
#include "FNMedia.h"
#include "Engine/Texture2D.h"
#include "Input/DragAndDrop.h"
#include "Styling/CoreStyle.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SFNPanel.h"
#include "SFNPortraitCard.h"
#include "SFNTextBox.h"
#include "SFNPowerTile.h"
#include "FNWood.h"
#include "SFNScroll.h"
#include "SFNPanelMover.h"
#include "FNHudPrefs.h"
#include "SFNChat.h"
#include "Features/FNTurnBar.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNHudLayout"


/** The kit's tooltip (tooltips looked poor): a stone tablet with readable type instead of the engine's grey box. */
static TSharedRef<SToolTip> FN7StoneTip(const FText& Text)
{
	return SNew(SToolTip)
		.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
		.TextMargin(FMargin(0.f))
		[
			SNew(SBorder).BorderImage(FN7Wood::StoneBrush()).Padding(FMargin(12.f, 8.f))
			[
				SNew(STextBlock).Font(FN7Style::Type::Font(13, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.95f, 0.92f, 0.85f))
				.WrapTextAt(320.f).Text(Text)
			]
		];
}

namespace FN7HudParts
{
	using namespace FN7Style;

	const FLinearColor HudInk(0.1f, 0.06f, 0.02f);

	TSharedRef<SWidget> Divider(bool bVertical)
	{
		return SNew(SBox)
			.WidthOverride(bVertical ? 1.f : FOptionalSize())
			.HeightOverride(bVertical ? 18.f : 1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SImage).Image(Brush::Segment(Color::PanelEdge())).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.6f))
			];
	}

	TSharedRef<SWidget> Stat(const FText& Key, const FText& Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(Key)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(13, TEXT("Bold"))).Text(Value)
			];
	}

	TSharedRef<SWidget> Chip(const FSlateBrush* B, const FText& T, const FLinearColor& TextColor)
	{
		return SNew(SBorder)
			.BorderImage(B)
			.Padding(FMargin(Space::S, 2.f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(TextColor).Text(T)
			];
	}
}


void SFNHudLayout::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	Model = InArgs._Model;
	TimerAttr = InArgs._Timer;
	OnSend = InArgs._OnSend;
	OnTypingStarted = InArgs._OnTypingStarted;
	OnPower = InArgs._OnPower;
	OnTakeSeat = InArgs._OnTakeSeat;
	OnLobby = InArgs._OnLobby;
	OnResetView = InArgs._OnResetView;
	OnHowToPlay = InArgs._OnHowToPlay;
	OnCardDrag = InArgs._OnCardDrag;
	OnCardDoubleClick = InArgs._OnCardDoubleClick;

	// Screen map v2: four corners, the centre (the hex board and the globe) completely clear.
	ChildSlot
	[
		SNew(SOverlay)
		// Top left: the header strip and LOBBY.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(Space::L)
		[
			SNew(SFNPanelMover).Id(TEXT("header"))
			[
				SAssignNew(HeaderHolder, SBox)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						SNew(SFNScroll).bStartOpen(true).RodHeight(16.f).ContentPadding(FMargin(22.f, 6.f, 22.f, 4.f))
						[
							SAssignNew(HeaderBody, SBox)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FN7Style::Space::XS, FN7Style::Space::S, 0.f, 0.f)
					[
						SNew(STextBlock)
						.TextStyle(&FN7Style::Type::BodyOverImage())
						.ColorAndOpacity(FN7Style::Color::TextDim())
						.Text_Lambda([this]() { return Model.Status; })
						.Visibility_Lambda([this]() { return Model.Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					]
				]
			]
		]
		// Top right: WORLD, collapsible.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(Space::L)
		[
			SNew(SFNPanelMover).Id(TEXT("news"))
			[
				SAssignNew(NewsHolder, SBox)
				.WidthOverride_Lambda([this]() { return bNewsCollapsed ? 200.f : 380.f; })
				[
					SNew(SFNScroll)
					.bUnrolled_Lambda([this]() { return bNewsOpen; })
					.Duration(0.9f)
					.bSmoothHeight(true)
					.RodHeight(24.f)
					.ContentPadding(FMargin(22.f, 12.f, 22.f, 10.f))
					[
						SAssignNew(NewsBody, SBox)
					]
				]
			]
		]
		// Lower left: YOU, your two demigods.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(Space::L)
		[
			SNew(SFNPanelMover).Id(TEXT("you"))[SAssignNew(YouHolder, SBox)]
		]
		// Lower right: the compact power panel (the fallback) above the WILL scroll.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(Space::L)
		[
			SNew(SFNPanelMover).Id(TEXT("will"))
			[
				SNew(SVerticalBox)
				// The room chat, docked above the WILL: as tall as the room between the WORLD scroll and the WILL allows.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, Space::S)
				[
					SAssignNew(Chat, SFNChat).Width(470.f).FreeHeight_Lambda([this]() { return ChatFree; })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
				SAssignNew(WillStack, SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, Space::S)
				[
					SNew(SBorder)
					.BorderImage(Brush::Panel())
					.Padding(FMargin(Space::M, Space::S))
					.Visibility_Lambda([this]() { return (Model.bPowerPanel && Model.Powers.Num() > 0 && !Model.bViewer) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
					[
						SAssignNew(PowersHolder, SBox)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					MakeComposePanel()
				]
				]
			]
		]
		// A big moment (a summon's deed): top centre, over the board, for a few seconds. Never takes a click.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(0.f, TurnBarShown() ? 200.f : 76.f, 0.f, 0.f); }))   // below Benito's turn bar while it shows
		[
			SNew(SBox)
			.MaxDesiredWidth(760.f)
			.Visibility_Lambda([this]() { return Model.Banner.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			[
				SNew(SBorder)
				.BorderImage(FN7Style::Brush::Panel())
				.Padding(FMargin(FN7Style::Space::L, FN7Style::Space::M))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, FN7Style::Space::XS)
					[
						SNew(STextBlock)
						.TextStyle(&FN7Style::Type::Label())
						.Font(FN7Style::Type::Font(13, TEXT("Bold")))
						.ColorAndOpacity_Lambda([this]() { return FSlateColor(Model.BannerColor); })
						.Text_Lambda([this]() { return Model.BannerTitle; })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(&FN7Style::Type::Body())
						.WrapTextAt(700.f)   // a fixed width: auto-wrap lags a frame and clipped long deeds
						.Justification(ETextJustify::Center)
						.Text_Lambda([this]() { return Model.Banner; })
					]
				]
			]
		]
		// Watching without a seat: one clear way in, bottom centre (below the board).
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(Space::L)
		[
			SNew(SBorder)
			.BorderImage(FN7Style::Brush::Panel())
			.Padding(FMargin(FN7Style::Space::L, FN7Style::Space::M))
			.Visibility_Lambda([this]() { return Model.bViewer ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, FN7Style::Space::M, 0.f)
				[
					SNew(STextBlock).TextStyle(&FN7Style::Type::Body()).Text(LOCTEXT("Watching", "You are watching the house game."))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).HeightOverride(40.f).WidthOverride(200.f)
					[
						SNew(SButton)
						.ButtonStyle(&FN7Style::AccentButton())
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.OnClicked_Lambda([this]() { OnTakeSeat.ExecuteIfBound(); return FReply::Handled(); })
						[
							SNew(STextBlock).TextStyle(&FN7Style::Type::Label()).Font(FN7Style::Type::Font(12, TEXT("Bold"))).ColorAndOpacity(FN7HudParts::HudInk).Text(LOCTEXT("TakeSeat", "TAKE A SEAT"))
						]
					]
				]
			]
		]
		// The chat's floating looks (bubbles over the table rim, or beside each speaker's figure): paint only.
		+ SOverlay::Slot()
		[
			SAssignNew(ChatFloat, SFNChatFloat, Chat, [this](int32 Seat, FVector2D& Px) { return ChatAnchor ? ChatAnchor(Seat, Px) : false; })
			.Visibility(EVisibility::HitTestInvisible)
		]
	];
	RefreshSections();
}

void SFNHudLayout::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The chat's room: from the WORLD scroll's bottom edge (unrolled or not, folded or not) down to the top of the powers
	// panel and the WILL, as they stand this frame (they may have been moved). Never overlapping either.
	if (NewsHolder.IsValid() && WillStack.IsValid())
	{
		const FGeometry& Ng = NewsHolder->GetTickSpaceGeometry();
		const FGeometry& Wg = WillStack->GetTickSpaceGeometry();
		const float NewsBottom = (float)AllottedGeometry.AbsoluteToLocal(Ng.LocalToAbsolute(FVector2D(0.f, Ng.GetLocalSize().Y))).Y;
		const float WillTop = (float)AllottedGeometry.AbsoluteToLocal(Wg.LocalToAbsolute(FVector2D::ZeroVector)).Y;
		ChatFree = FMath::Max(0.f, WillTop - NewsBottom - FN7Style::Space::L - FN7Style::Space::S);
	}
}

bool SFNHudLayout::TurnBarShown() const
{
	if (!TurnBar.IsValid() && GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld())
		if (TActorIterator<AFNTurnBar> It(GEngine->GameViewport->GetWorld()); It) const_cast<SFNHudLayout*>(this)->TurnBar = *It;
	return TurnBar.IsValid() && TurnBar->IsShown();
}

bool SFNHudLayout::ChatHasFocus() const { return Chat.IsValid() && Chat->HasChatFocus(); }
void SFNHudLayout::ChatLeaveFocus() { if (Chat.IsValid()) Chat->LeaveFocus(); }

void SFNHudLayout::SetModel(const FFNHudModel& InModel)
{
	Model = InModel;
	RefreshSections();
}

void SFNHudLayout::RefreshSections()
{
	{
		const FString Sig = FString::Printf(TEXT("%s|%d|%d|%d|%d|%s|%d"), *Model.Room.ToString(), Model.Beat, Model.BeatsTotal, Model.Alive, Model.AliveOf,
			*Model.Entropy.ToString(), Model.bReplay ? 1 : 0);
		if (Sig != HeaderSignature) { HeaderSignature = Sig; HeaderBody->SetContent(MakeHeader()); }
	}
	{
		FString Sig = bNewsCollapsed ? TEXT("C|") : TEXT("O|");
		for (const FFNHudNews& N : Model.News) Sig += N.Tag.ToString() + TEXT("|") + N.Text.ToString() + TEXT("\n");
		if (Sig != NewsSignature)
		{
			NewsSignature = Sig;
			// The first time it unrolls; after that the new inscription goes straight on and the open cloth just eases to
			// its new height (it should just expand and scroll normally, without folding and unfolding).
			NewsBody->SetContent(MakeNews());
			if (!bNewsBuilt) { bNewsBuilt = true; bNewsOpen = true; }
		}
	}
	const FString YouSig = MakeYouSignature();
	if (YouSig != YouSignature) { YouSignature = YouSig; YouHolder->SetContent(MakeYou()); }
	const FString PowersSig = MakePowersSignature();
	if (PowersSig != PowersSignature) { PowersSignature = PowersSig; PowersHolder->SetContent(MakePowers()); }
	NewsHolder->SetVisibility(Model.News.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

EActiveTimerReturnType SFNHudLayout::StartNewsRoll(double, float)
{
	bNewsOpen = false;
	UE_LOG(LogTemp, Display, TEXT("FN7 news roll"));
	RegisterActiveTimer(0.68f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNHudLayout::SwapNews));
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SFNHudLayout::SwapNews(double, float)
{
	bNewsSwapPending = false;
	NewsBody->SetContent(MakeNews());   // the latest news, whatever arrived while it waited and rolled up
	bNewsOpen = true;
	NextNewsRollAt = FPlatformTime::Seconds() + 8.0;
	return EActiveTimerReturnType::Stop;
}

FString SFNHudLayout::MakeYouSignature() const
{
	FString S = FString::Printf(TEXT("%s|%d|%d|%d|%d|%.1f|%s|%s|%s|%d;"), *Model.YouName.ToString(), Model.Health, Model.MaxHealth, Model.Bolts, Model.Sparks,
		Model.Debt, *Model.Place.ToString(), *Model.ArenaLine.ToString(), *Model.YourAura.ToFColor(true).ToHex(), Model.bViewer ? 1 : 0);
	for (const FFNHudDemigod& D : Model.Mine)
		S += FString::Printf(TEXT("%s|%s|%s|%d|%d|%d|%p|%d|%p|%d|%s|%s;"), *D.Ref, *D.Name.ToString(), *D.Side, D.Health, D.MaxHealth, D.bAlive ? 1 : 0, D.PortraitTexture, D.PortraitPng.Num(), D.InsetTexture, D.InsetPng.Num(), *D.Gear, *D.NewItem);
	return S;
}

FString SFNHudLayout::MakePowersSignature() const
{
	FString S;
	for (const FFNHudPower& P : OrderedPowers()) S += FString::Printf(TEXT("%s|%d;"), *P.Key(), P.Cost);
	return S;
}

TArray<FFNHudPower> SFNHudLayout::OrderedPowers() const
{
	const TArray<FString>& Order = FN7HudPrefs::GetPowerOrder();
	TArray<FFNHudPower> Out = Model.Powers;
	Out.StableSort([&Order](const FFNHudPower& A, const FFNHudPower& B)
	{
		const int32 IA = Order.IndexOfByKey(A.Slug), IB = Order.IndexOfByKey(B.Slug);
		return (IA == INDEX_NONE ? MAX_int32 : IA) < (IB == INDEX_NONE ? MAX_int32 : IB);
	});
	return Out;
}

void SFNHudLayout::ReorderPower(const FString& Dragged, const FString& Before)
{
	TArray<FString> Slugs;
	for (const FFNHudPower& P : OrderedPowers()) Slugs.AddUnique(P.Slug);
	if (!Slugs.Contains(Dragged) || !Slugs.Contains(Before)) return;
	Slugs.Remove(Dragged);
	Slugs.Insert(Dragged, Slugs.IndexOfByKey(Before));
	FN7HudPrefs::SetPowerOrder(Slugs);
	PowersSignature = MakePowersSignature();
	PowersHolder->SetContent(MakePowers());
}

TSharedRef<SWidget> SFNHudLayout::MakeHeader()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	// The header is a slim cloth scroll between two turned rods, the will scroll's style (it should be scroll-like, the same
	// STYLE"): ink on linen, the buttons inscribed in red ink.
	const FLinearColor InkC = FN7Wood::Ink(), Red = FN7Wood::RedInk();
	auto InkStat = [&](const FText& Key, const FText& Value) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(InkC * 0.75f).Text(Key)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(13, TEXT("Bold"))).ColorAndOpacity(InkC).Text(Value)
			];
	};

	// Lobby, reset view and "?" as real stone buttons right after the title (Benito noted there is no leave-to-lobby button):
	// they were small ink links at the far end of the bar, at the screen edge).
	auto StoneButton = [](const FText& Label, const FText& Tip, TFunction<void()> OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0.f))
			.ToolTip(FN7StoneTip(Tip))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(SBorder).BorderImage(FN7Wood::StoneBrush()).Padding(FMargin(11.f, 3.f))
				[
					SNew(STextBlock).Font(Type::Font(12, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.97f, 0.93f, 0.84f)).Text(Label)
				]
			];
	};
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::M, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Title()).Font(Type::Font(15, TEXT("Bold"))).ColorAndOpacity(Red).Text(LOCTEXT("Game", "FINAL NIGHT 7"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
		[
			StoneButton(LOCTEXT("LobbyPill", "LOBBY"), LOCTEXT("LobbyTip", "Back to the lobby (Esc)"), [this]() { OnLobby.ExecuteIfBound(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
		[
			StoneButton(LOCTEXT("ResetPill", "RESET VIEW"), LOCTEXT("ResetTip", "The board, the camera and the panels back where they were (R or Home)"), [this]() { OnResetView.ExecuteIfBound(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::M, 0.f)
		[
			StoneButton(LOCTEXT("HowPill", "?"), LOCTEXT("HowTip", "How to play"), [this]() { OnHowToPlay.ExecuteIfBound(); })
		];
	auto Add = [&Row](TSharedRef<SWidget> W)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::M, 0.f)[W];
	};
	if (!Model.Room.IsEmpty()) Add(InkStat(LOCTEXT("Room", "ROOM"), Model.Room));
	if (Model.BeatsTotal > 0)
	{
		Add(InkStat(LOCTEXT("Beat", "BEAT"), FText::Format(LOCTEXT("BeatFmt", "{0}/{1}"), Model.Beat, Model.BeatsTotal)));
		Add(InkStat(LOCTEXT("Alive", "ALIVE"), Model.AliveOf > 0 ? FText::Format(LOCTEXT("AliveOf", "{0}/{1}"), Model.Alive, Model.AliveOf) : FText::AsNumber(Model.Alive)));
	}
	if (!Model.Entropy.IsEmpty()) Add(InkStat(LOCTEXT("Entropy", "ENTROPY"), Model.Entropy));
	if (Model.bReplay) Add(InkStat(FText::GetEmpty(), LOCTEXT("Replay", "REPLAY")));

	return Row;
}

TSharedRef<SWidget> SFNHudLayout::MakeNews()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	// The title row is the toggle: WORLD  (n)  -  click to fold the list away, click again to bring it back.
	TSharedRef<SWidget> Title = SNew(SButton)
		.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
		.ContentPadding(FMargin(0.f))
		// (no tooltip: folding the news says itself)
		.OnClicked_Lambda([this]()
		{
			bNewsCollapsed = !bNewsCollapsed;
			RefreshSections();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Title()).Font(Type::Font(15, TEXT("Bold"))).ColorAndOpacity(FN7Wood::RedInk()).Text(LOCTEXT("World", "WORLD"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(FN7Wood::Ink() * 0.75f)
				.Text(bNewsCollapsed ? FText::Format(LOCTEXT("NewsFolded", "{0}  +"), Model.News.Num()) : LOCTEXT("NewsOpen", "-"))
			]
		];

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight()[Title];
	if (!bNewsCollapsed)
	{
		for (int32 i = 0; i < Model.News.Num(); ++i)
		{
			const FFNHudNews& N = Model.News[i];
			List->AddSlot().AutoHeight().Padding(0.f, i == 0 ? Space::S : 0.f, 0.f, 0.f)
			[
				i > 0 ? StaticCastSharedRef<SWidget>(SNew(SBox).HeightOverride(1.f)[SNew(SImage).Image(Brush::Segment(FN7Wood::Ink())).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.25f))])
					: SNullWidget::NullWidget
			];
			List->AddSlot().AutoHeight().Padding(0.f, i > 0 ? Space::S : 0.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, Space::S, 0.f)
				[
					SNew(SBox).MinDesiredWidth(52.f).HAlign(HAlign_Center)[Chip(Brush::PillAccent(), N.Tag, HudInk)]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(12)).ColorAndOpacity(FN7Wood::Ink()).AutoWrapText(true).Text(N.Text)
				]
			];
		}
	}
	return List;   // inscribed on the persistent WORLD scroll (built once in Construct)
}

const FSlateBrush* SFNHudLayout::AuraFrame()
{
	// One rounded frame per aura colour, kept for the session (old cards may still point at the last one).
	static TMap<uint32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
	const FColor Key = Model.YourAura.ToFColor(true);
	TSharedPtr<FSlateRoundedBoxBrush>& B = Cache.FindOrAdd(Key.ToPackedRGBA());
	if (!B.IsValid())
		B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(Model.YourAura.R, Model.YourAura.G, Model.YourAura.B, 0.14f), 10.f, Model.YourAura, 2.5f);
	return B.Get();
}

const FSlateBrush* SFNHudLayout::InsetBrush(const FFNHudDemigod& D)
{
	UTexture2D* Tex = D.InsetTexture;
	if (!Tex && D.InsetPng.Num() > 0)
		if ((Tex = FN7Media::TextureFromPng(D.InsetPng)) != nullptr) InsetKeep.Add(TStrongObjectPtr<UTexture2D>(Tex));
	if (!Tex) return nullptr;
	TSharedPtr<FSlateBrush>& B = InsetBrushes.FindOrAdd(Tex);
	if (!B.IsValid())
	{
		B = MakeShared<FSlateBrush>();
		B->SetResourceObject(Tex);
		B->ImageSize = FVector2D(54.f, 54.f);
		B->DrawAs = ESlateBrushDrawType::RoundedBox;
		B->OutlineSettings = FSlateBrushOutlineSettings(6.f, FSlateColor(Model.YourAura), 1.5f);
	}
	return B.Get();
}

/** Press and drag on one of your demigod cards: the HUD asks for a summon drag. A plain click does nothing here. */
class SFN7CardDragSource : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFN7CardDragSource) {}
		SLATE_ARGUMENT(FString, Ref)
		SLATE_EVENT(FOnFNHudCardDrag, OnCardDrag)
		SLATE_EVENT(FOnFNHudCardDouble, OnCardDouble)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs)
	{
		Ref = InArgs._Ref;
		OnCardDrag = InArgs._OnCardDrag;
		OnCardDouble = InArgs._OnCardDouble;
		ChildSlot[InArgs._Content.Widget];
	}
	virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& E) override
	{
		if (E.GetEffectingButton() != EKeys::LeftMouseButton || Ref.IsEmpty()) return FReply::Unhandled();
		OnCardDouble.ExecuteIfBound(Ref);
		return FReply::Handled();
	}
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
	{
		if (E.GetEffectingButton() != EKeys::LeftMouseButton || Ref.IsEmpty()) return FReply::Unhandled();
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}
	virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent& E) override
	{
		if (!OnCardDrag.IsBound()) return FReply::Unhandled();
		const TSharedPtr<FDragDropOperation> Op = OnCardDrag.Execute(Ref, E.GetScreenSpacePosition());
		return Op.IsValid() ? FReply::Handled().BeginDragDrop(Op.ToSharedRef()) : FReply::Handled();
	}
	virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
	{
		return Ref.IsEmpty() ? FCursorReply::Unhandled() : FCursorReply::Cursor(EMouseCursor::GrabHand);
	}
private:
	FString Ref;
	FOnFNHudCardDrag OnCardDrag;
	FOnFNHudCardDouble OnCardDouble;
};

TSharedRef<SWidget> SFNHudLayout::MakeMine()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	const float CardW = 166.f;   // about 220 px tall with the name: the diorama has room
	const FLinearColor Aura = Model.YourAura;
	TSharedRef<SHorizontalBox> Pair = SNew(SHorizontalBox);
	for (int32 i = 0; i < Model.Mine.Num() && i < 2; ++i)
	{
		const FFNHudDemigod& D = Model.Mine[i];
		const bool bRight = D.Side == TEXT("right");
		// "<player>'s Left Hand" -> "Left Hand".
		FString Short = D.Name.ToString();
		const int32 Cut = Short.Find(TEXT("'s "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Cut != INDEX_NONE) Short = Short.Mid(Cut + 3);
		if (Short.IsEmpty()) Short = bRight ? TEXT("Right Hand") : TEXT("Left Hand");

		TSharedPtr<SFNPortraitCard> Card;
		TSharedRef<SWidget> CardW_ = SAssignNew(Card, SFNPortraitCard)
			.Name(FText::FromString(Short))
			.Role(D.bAlive ? EFNSeatRole::Human : EFNSeatRole::Fallen)
			.bIsYou(false)
			.bShowHourglass(false)
			.CardWidth(CardW);
		if (D.PortraitTexture) Card->SetTexture(D.PortraitTexture);
		else if (D.PortraitPng.Num() > 0) Card->SetImageFromPng(D.PortraitPng);

		// Its own health: one segment per point, in your aura colour (grey once it has fallen).
		TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
		const int32 Max = FMath::Clamp(D.MaxHealth, 1, 20);
		const float SegW = FMath::Max(3.f, (CardW - 2.f * (Max - 1)) / Max);
		for (int32 h = 0; h < Max; ++h)
		{
			const FLinearColor Fill = !D.bAlive ? Color::Fallen() * FLinearColor(1.f, 1.f, 1.f, 0.5f) : (h < D.Health ? Aura : Color::PanelEdge());
			Bar->AddSlot().AutoWidth().Padding(0.f, 0.f, h + 1 < Max ? 2.f : 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(SegW).HeightOverride(7.f)[SNew(SImage).Image(Brush::Segment(Fill))]
			];
		}

		Pair->AddSlot().AutoWidth().Padding(i > 0 ? Space::M : 0.f, 0.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SFN7CardDragSource)
					.Ref(D.bAlive ? D.Ref : FString())
					.OnCardDrag(OnCardDrag)
					.OnCardDouble(OnCardDoubleClick)
					.ToolTip(FN7StoneTip(D.bAlive ? LOCTEXT("DragToSummon", "Drag onto the board or the globe to summon this demigod there") : LOCTEXT("HandFallenTip", "This demigod has fallen")))
					[
						SNew(SBorder).BorderImage(AuraFrame()).Padding(3.f)[CardW_]
					]
				]
				// The portrait, small, when the diorama leads.
				+ SOverlay::Slot().HAlign(bRight ? HAlign_Left : HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(bRight ? 10.f : 0.f, 10.f, bRight ? 0.f : 10.f, 0.f))
				[
					SNew(SBox).WidthOverride(54.f).HeightOverride(54.f)
					.Visibility(InsetBrush(D) ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
					[
						SNew(SImage).Image(InsetBrush(D))
					]
				]
				// The L / R notch.
				+ SOverlay::Slot().HAlign(bRight ? HAlign_Right : HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(bRight ? 0.f : 8.f, 8.f, bRight ? 8.f : 0.f, 0.f))
				[
					SNew(SBorder)
					.BorderImage(Brush::Swatch(Aura))
					.Padding(FMargin(6.f, 1.f))
					[
						SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(HudInk).Text(bRight ? LOCTEXT("R", "R") : LOCTEXT("L", "L"))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(3.f, Space::S, 3.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Bar]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Small())
				.Text(D.bAlive ? FText::Format(LOCTEXT("HealthOf", "{0}/{1}"), D.Health, D.MaxHealth) : LOCTEXT("HandFallen", "FALLEN"))
			]
			// Gear looks (schema 11): a just-gained item flashes, then the loadout sits under the health.
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(SBorder).BorderImage(Brush::PillAccent()).Padding(FMargin(6.f, 1.f))
				.Visibility(D.NewItem.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(HudInk)
					.Text(FText::FromString(TEXT("NEW: ") + D.NewItem))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(3.f, 2.f, 3.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(FLinearColor(1.f, 0.84f, 0.5f))
				.Visibility(D.Gear.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
				.WrapTextAt(140.f).Justification(ETextJustify::Center)
				.Text(FText::FromString(D.Gear))
			]
		];
	}

	// Bolts, sparks, debt; Time's live bill while you hold slowed time.
	TSharedRef<SHorizontalBox> Purse = SNew(SHorizontalBox);
	Purse->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
	[
		Chip(Brush::PillAccent(), Model.Bolts == 1 ? LOCTEXT("OneBolt", "1 BOLT") : FText::Format(LOCTEXT("Bolts", "{0} BOLTS"), Model.Bolts), HudInk)
	];
	if (Model.Sparks >= 0)
		Purse->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
		[
			Chip(Brush::Pill(EFNSeatRole::AI), Model.Sparks == 1 ? LOCTEXT("OneSpark", "1 SPARK") : FText::Format(LOCTEXT("Sparks", "{0} SPARKS"), Model.Sparks), Color::AIAura())
		];
	if (Model.Debt >= 0.0)
	{
		FNumberFormattingOptions One;
		One.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
		Purse->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			Chip(Brush::Pill(EFNSeatRole::Fallen), FText::Format(LOCTEXT("Debt", "DEBT {0}"), FText::AsNumber(Model.Debt, &One)), Color::Text())
		];
	}

	return SNew(SBorder)
		.BorderImage(FN7Wood::StoneBrush())   // a carved stone tablet (these should use a stone tablet background)
		.Padding(FMargin(Space::L + 6.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, Space::S)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Aura).Text(LOCTEXT("YouLabel", "YOU"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&Type::Name()).Font(Type::Font(14, TEXT("Bold"))).Text(Model.YouName)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[Pair]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)[Purse]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return (Model.bHoldingTime && !Model.TimeBill.IsEmpty()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
				[
					SNew(SFNHourglass).Color(Color::HumanAura())
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(12, TEXT("Bold"))).ColorAndOpacity(Color::HumanAura())
					.Text_Lambda([this]() { return FText::Format(LOCTEXT("TimeBill", "TIME'S BILL  {0}"), Model.TimeBill); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(2.f * CardW + Space::M + 6.f)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true)
					.Text(Model.Place.IsEmpty() ? Model.ArenaLine : (Model.ArenaLine.IsEmpty() ? Model.Place : FText::Format(LOCTEXT("PlaceArena", "{0}  -  {1}"), Model.Place, Model.ArenaLine)))
				]
			]
		];
}

TSharedRef<SWidget> SFNHudLayout::MakeYou()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	if (Model.YouName.IsEmpty() && Model.Mine.Num() == 0) return SNullWidget::NullWidget;
	if (Model.Mine.Num() > 0) return MakeMine();

	// A one-demigod server: the classic panel (one health bar).
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	const int32 Max = FMath::Clamp(Model.MaxHealth, 1, 20);
	for (int32 i = 0; i < Max; ++i)
	{
		Bar->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
		[
			SNew(SBox).WidthOverride(22.f).HeightOverride(8.f)
			[
				SNew(SImage).Image(Brush::Segment(i < Model.Health ? Color::HumanAura() : Color::PanelEdge()))
			]
		];
	}
	return SNew(SBox)
		.WidthOverride(440.f)
		[
			SNew(SBorder)
			.BorderImage(FN7Wood::StoneBrush())
			.Padding(FMargin(Space::L, Space::M))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).TextStyle(&Type::Name()).Text(Model.YouName)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
					[
						Chip(Brush::PillAccent(), FText::Format(LOCTEXT("BoltsClassic", "{0} BOLTS"), Model.Bolts), HudInk)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::XS, 0.f, 0.f, 0.f)
					[
						SNew(SBox).Visibility(Model.Sparks >= 0 ? EVisibility::Visible : EVisibility::Collapsed)
						[
							Chip(Brush::Pill(EFNSeatRole::AI), FText::Format(LOCTEXT("SparksClassic", "{0} SPARKS"), FMath::Max(0, Model.Sparks)), Color::AIAura())
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("Health", "HEALTH"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Bar]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Small()).Text(FText::Format(LOCTEXT("HealthFmt", "{0}/{1}"), Model.Health, Model.MaxHealth))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).Text(Model.Place)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true).Text(Model.ArenaLine)
				]
			]
		];
}

TSharedRef<SWidget> SFNHudLayout::MakePowers()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	const TArray<FFNHudPower> All = OrderedPowers();
	bool bTiers = false;
	for (const FFNHudPower& P : All) bTiers |= !P.Tier.IsEmpty();

	auto Row = [this, &All](const FString& Tier) -> TSharedRef<SWrapBox>
	{
		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(Space::XS, Space::XS));
		for (const FFNHudPower& P : All)
		{
			if (P.Tier != Tier) continue;
			const FString Slug = P.Slug, Key = P.Key();
			const int32 Cost = P.Cost;
			Wrap->AddSlot()
			[
				SNew(SFNPowerTile)
				.Slug(Slug)
				.Tier(Tier)
				.Name(P.Name)
				.Cost(Cost)
				.bArmed_Lambda([this, Key]() { for (const FFNHudPower& Q : Model.Powers) if (Q.Key() == Key) return Q.bArmed; return false; })
				.bEnabled_Lambda([this, Cost, Tier]()
				{
					if (Tier == TEXT("mini")) return Model.Sparks < 0 || Model.Sparks >= Cost;
					return Model.Bolts >= Cost;
				})
				.TileSize(Tier == TEXT("mini") ? 54.f : 58.f)
				.OnClicked_Lambda([this, Key]() { OnPower.ExecuteIfBound(Key); })
				.OnReorder_Lambda([this, Slug](const FString& Dragged) { ReorderPower(Dragged, Slug); })
			];
		}
		return Wrap;
	};

	// Compact, lower right, on the dark panel: ENTROPY POWERS, the MEGA row (a bolt: the globe, level 1) and the MINI row
	// (a spark: the board). A fallback until the powers live as hexes on the board's edge.
	if (!bTiers) return SNew(SBox).WidthOverride(700.f)[Row(FString())];

	auto Label = [](const FText& Title, const FText& Note, const FLinearColor& C)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(10, TEXT("Bold"))).ColorAndOpacity(C).Text(Title)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::TextDim()).Text(Note)
			];
	};
	return SNew(SBox)
		.WidthOverride(700.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[
				Label(LOCTEXT("Mega", "MEGA"), LOCTEXT("MegaNote", "a bolt each  -  drop on the globe: the whole map (every other god gains a bolt)"), Color::HumanAura())
			]
			+ SVerticalBox::Slot().AutoHeight()[Row(TEXT("mega"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 2.f)
			[
				Label(LOCTEXT("Mini", "MINI"), LOCTEXT("MiniNote", "a spark each  -  slide onto the board"), Color::AIAura())
			]
			+ SVerticalBox::Slot().AutoHeight()[Row(TEXT("mini"))]
		];
}

TSharedRef<SWidget> SFNHudLayout::MakeComposePanel()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	const FLinearColor InkDim = FN7Wood::Ink() * FLinearColor(1.f, 1.f, 1.f, 0.72f);
	return SNew(SBox)
		.WidthOverride(470.f)
		[
			SNew(SOverlay)
			// The WILL: a slim old scroll, rolled up until you open it, rolled up again after SEND.
			+ SOverlay::Slot()
			[
				SNew(SFNScroll)
				.Duration(0.9f)
				.ContentPadding(FMargin(26.f, 14.f))
				.bUnrolled_Lambda([this]() { return bWillOpen && !Model.bViewer; })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(FN7Wood::RedInk()).Text(LOCTEXT("Will", "YOUR WILL"))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
						[
							SNew(SFNHourglass).Color(FN7Wood::RedInk())
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).TextStyle(&Type::Name()).Font(Type::Font(14, TEXT("Bold"))).ColorAndOpacity(FN7Wood::RedInk())
							.Text_Lambda([this]() { return TimerAttr.IsBound() || TimerAttr.IsSet() ? TimerAttr.Get() : Model.Timer; })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
					[
						SNew(SBox)
						.MinDesiredHeight(54.f)
						[
							SAssignNew(Compose, SFNTextBox)
							.bMultiLine(true)
							.Style(&FN7Wood::InkTextBox())
							.HintText_Lambda([this]() { return Model.ComposeHint; })
							.OnTextChanged(this, &SFNHudLayout::HandleTextChanged)
							.OnTextCommitted(this, &SFNHudLayout::HandleTextCommitted)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).TextStyle(&Type::Small()).Font(Type::Font(10, TEXT("Italic"))).ColorAndOpacity(InkDim).AutoWrapText(true)
							.Text(LOCTEXT("HandsHint", "l: for your Left Hand, r: for your Right Hand"))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.HAlign(HAlign_Center)
							.OnClicked(this, &SFNHudLayout::HandleSend)
							[
								SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(HudInk).Text(LOCTEXT("Send", "SEND"))
							]
						]
					]
				]
			]
			// The shiny finish over the whole scroll: rainbow foil, a streak that follows the mouse, sparkles,
			// and a breathing gold edge while time is slowed for you.
			+ SOverlay::Slot()
			[
				SNew(SFNFoil)
				.Visibility(EVisibility::HitTestInvisible)
				.bHot_Lambda([this]() { return Model.bHoldingTime && !bWillOpen; })   // nothing flashes on the open scroll
				.Strength_Lambda([this]() { return bWillOpen ? 0.f : 1.1f; })   // the shine invites the click; open, it rests
				.CornerRadius(6.f)
			]
			// Rolled up: the whole strip is the way in.
			+ SOverlay::Slot()
			[
				SNew(SButton)
				.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
				.ContentPadding(FMargin(0.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				// (no tooltip: the bar reads "YOUR WILL - click to write")
				.Visibility_Lambda([this]() { return (!bWillOpen && !Model.bViewer) ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([this]() { OpenWill(); return FReply::Handled(); })
				[
					SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(12, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.93f, 0.78f))
					.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
					.Text_Lambda([this]()
					{
						const FText T = TimerAttr.IsBound() || TimerAttr.IsSet() ? TimerAttr.Get() : Model.Timer;
						return T.IsEmpty() ? LOCTEXT("WillStrip", "YOUR WILL  -  click to write")
							: FText::Format(LOCTEXT("WillStripTimer", "YOUR WILL  -  click to write  -  {0}"), T);
					})
				]
			]
		];
}

void SFNHudLayout::RollUpWill()
{
	bWillOpen = false;
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
}

void SFNHudLayout::OpenWill()
{
	bWillOpen = true;
	if (Compose.IsValid() && FSlateApplication::IsInitialized())
		if (TSharedPtr<SWidget> Edit = Compose->GetEditable())
			FSlateApplication::Get().SetKeyboardFocus(Edit, EFocusCause::SetDirectly);
}

void SFNHudLayout::HandleTextChanged(const FText& Text)
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	if (!bTypingSignalled && !Text.IsEmpty())
	{
		bTypingSignalled = true;
		OnTypingStarted.ExecuteIfBound();
	}
}

void SFNHudLayout::HandleTextCommitted(const FText&, ETextCommit::Type CommitType)
{
	// Enter in the will sends it (Enter in the will should just send the will, not add a newline).
	if (CommitType == ETextCommit::OnEnter) HandleSend();
}

FReply SFNHudLayout::HandleSend()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	const FString Text = Compose.IsValid() ? Compose->GetText().ToString().TrimStartAndEnd() : FString();
	if (Text.IsEmpty()) return FReply::Handled();
	OnSend.ExecuteIfBound(Text);
	Compose->SetText(FText::GetEmpty());
	bTypingSignalled = false;
	bWillOpen = false;   // rolls up after SEND
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
	return FReply::Handled();
}

FFNHudModel SFNHudLayout::SampleModel()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	FFNHudModel M;
	M.Room = FText::FromString(TEXT("2ZAHKZ"));
	M.Beat = 8;
	M.BeatsTotal = 30;
	M.Alive = 4;
	M.Entropy = FText::FromString(TEXT("CALM 8"));
	M.YouName = FText::FromString(TEXT("Chris"));
	M.Health = 10;
	M.MaxHealth = 10;
	M.Bolts = 1;
	M.Sparks = 5;
	M.Place = FText::FromString(TEXT("Vienna, Western Europe"));
	M.ArenaLine = FText::FromString(TEXT("Time's circle: 8637 km around Kinshasa. Stay inside."));
	M.PowersNote = FText::FromString(TEXT("Drag a block onto the board to cast it there, or arm it and write its target."));
	M.ComposeHint = FText::FromString(TEXT("Write what you'll do today. Time slows while you write..."));
	M.Timer = FText::FromString(TEXT("0:05"));

	auto Seat = [](const TCHAR* Name, EFNSeatRole Role, bool bYou, bool bTime, const TCHAR* Png)
	{
		FFNHudSeat S;
		S.Name = FText::FromString(Name);
		S.Role = Role;
		S.bIsYou = bYou;
		S.bHoldsTime = bTime;
		if (Png) FFileHelper::LoadFileToArray(S.PortraitPng, *FPaths::Combine(FPaths::ProjectDir(), Png));
		return S;
	};
	M.Seats.Add(Seat(TEXT("Chris"), EFNSeatRole::Human, true, false, TEXT("docs/style-refs/approved/player_portrait.png")));
	M.Seats.Add(Seat(TEXT("Oko"), EFNSeatRole::AI, false, true, TEXT("docs/style-refs/approved/event_maxzoom.png")));
	M.Seats.Add(Seat(TEXT("Mara"), EFNSeatRole::AI, false, false, TEXT("docs/style-refs/approved/diorama_maxzoom.png")));
	M.Seats.Add(Seat(TEXT("Cortes"), EFNSeatRole::Fallen, false, false, nullptr));
	// Rules 0.7.0: your two demigods, each with its own health.
	auto Hand = [](const TCHAR* Name, const TCHAR* Side, int32 Health, const TCHAR* Diorama, const TCHAR* Portrait)
	{
		FFNHudDemigod D;
		D.Name = FText::FromString(Name);
		D.Side = Side;
		D.Health = Health;
		D.MaxHealth = 10;
		D.bAlive = Health > 0;
		if (Diorama) FFileHelper::LoadFileToArray(D.PortraitPng, *FPaths::Combine(FPaths::ProjectDir(), Diorama));
		if (Portrait) FFileHelper::LoadFileToArray(D.InsetPng, *FPaths::Combine(FPaths::ProjectDir(), Portrait));
		return D;
	};
	M.Mine.Add(Hand(TEXT("Chris's Left Hand"), TEXT("left"), 10, TEXT("docs/style-refs/approved/diorama_maxzoom.png"), TEXT("docs/style-refs/approved/player_portrait.png")));
	M.Mine.Add(Hand(TEXT("Chris's Right Hand"), TEXT("right"), 6, TEXT("docs/style-refs/approved/event_maxzoom.png"), TEXT("docs/style-refs/approved/player_portrait.png")));
	M.Mine[0].Ref = TEXT("dg:0");
	M.Mine[1].Ref = TEXT("dg:1");
	M.YourAura = FLinearColor::FromSRGBColor(FColor(0xE6, 0x9F, 0x00));
	M.Debt = 1.2;
	M.AliveOf = 8;
	M.Alive = 7;
	M.Entropy = FText::FromString(TEXT("RESTLESS 34"));
	M.TimeBill = FText::FromString(TEXT("1 bolt + 2.4 debt"));
	M.Timer = FText::FromString(TEXT("0:22"));
	M.ComposeHint = FText::FromString(TEXT("Write what you'll do. Time slows while you write."));
	const int32 SeatBolts[] = { 1, 3, 2, 0 }, SeatSparks[] = { 5, 4, 2, 0 };
	for (int32 i = 0; i < M.Seats.Num() && i < 4; ++i) { M.Seats[i].Bolts = SeatBolts[i]; M.Seats[i].Sparks = SeatSparks[i]; }

	auto News = [](const TCHAR* InTag, const TCHAR* InText) { FFNHudNews N; N.Tag = FText::FromString(InTag); N.Text = FText::FromString(InText); return N; };
	M.News.Add(News(TEXT("BEAT 7"), TEXT("Oko slowed time and the crowds went wild. Tashkent's army pulled back.")));
	M.News.Add(News(TEXT("BEAT 7"), TEXT("The Monterrey bread riot under Isabela Valdez breaks open the granaries: stores fall to 45%.")));
	M.News.Add(News(TEXT("BEAT 7"), TEXT("The displaced doomsayers of Tashkent take up arms for Mara: 3289 volunteers.")));
	M.News.Add(News(TEXT("BEAT 6"), TEXT("The Dar es Salaam bread riot breaks open the city's granaries: stores fall to 59%.")));

	// The powers contract: every power in both tiers, a bolt (mega) or a spark (mini) each.
	auto Power = [&M](const TCHAR* Slug, const TCHAR* Name, int32, bool bArmed)
	{
		FFNHudPower Mega; Mega.Slug = Slug; Mega.Name = FText::FromString(Name); Mega.Cost = 1; Mega.Tier = TEXT("mega"); Mega.bArmed = bArmed;
		FFNHudPower Mini = Mega; Mini.Tier = TEXT("mini"); Mini.bArmed = false;
		M.Powers.Add(Mega);
		return Mini;
	};
	M.Powers.Add(Power(TEXT("lightning"), TEXT("Lightning bolt"), 1, true));
	M.Powers.Add(Power(TEXT("storm_clouds"), TEXT("Storm clouds"), 1, false));
	M.Powers.Add(Power(TEXT("volcano"), TEXT("Volcano"), 3, false));
	M.Powers.Add(Power(TEXT("earthquake"), TEXT("Earthquake"), 2, false));
	M.Powers.Add(Power(TEXT("flood"), TEXT("Flood"), 2, false));
	M.Powers.Add(Power(TEXT("wildfire"), TEXT("Wildfire"), 2, false));
	M.Powers.Add(Power(TEXT("meteor"), TEXT("Meteor"), 4, false));
	M.Powers.Add(Power(TEXT("raise_land"), TEXT("Raise land"), 2, false));
	M.Powers.Add(Power(TEXT("sink_land"), TEXT("Sink land"), 3, false));
	M.Powers.Add(Power(TEXT("grow_forest"), TEXT("Grow forest"), 1, false));
	M.Powers.Add(Power(TEXT("swarm"), TEXT("Swarm"), 2, false));
	return M;
}

// --- FN7.HudPreview [clean] [shot] [quit] -------------------------------------
// The layout over the game viewport with SampleModel(). "clean" removes the other viewport widgets first (preview
// only), "shot" saves Saved/FN7HudLayout.png with the UI, "quit" exits after the shot.

namespace FN7HudPreview
{
	TSharedPtr<SWidget> GHud;
	TSharedPtr<SWidget> GHudContent;

	void Show(UGameViewportClient* VC, bool bClean)
	{
		if (bClean) VC->RemoveAllViewportWidgets();
		else if (GHud.IsValid()) VC->RemoveViewportWidgetContent(GHud.ToSharedRef());
		GHudContent = SNew(SFNHudLayout).Model(SFNHudLayout::SampleModel());
		GHud = SNew(SWeakWidget).PossiblyNullContent(GHudContent);
		VC->AddViewportWidgetContent(GHud.ToSharedRef(), 100);
	}
}

static FAutoConsoleCommand GFN7HudPreviewCommand(
	TEXT("FN7.HudPreview"),
	TEXT("Show the FN7 HUD layout with sample data. Args: clean (hide other viewport widgets), shot (save Saved/FN7HudLayout.png), quit (exit after the shot)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace FN7HudPreview;
		const bool bClean = Args.Contains(TEXT("clean"));
		const bool bShot = Args.Contains(TEXT("shot"));
		const bool bQuit = Args.Contains(TEXT("quit"));
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		if (!VC) return;
		if (!bClean && !bShot && GHud.IsValid())
		{
			VC->RemoveViewportWidgetContent(GHud.ToSharedRef());
			GHud.Reset();
			GHudContent.Reset();
			return;
		}
		Show(VC, bClean);
		if (!bShot) return;
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudPreviewClean"), 4.f, [bClean](float)
		{
			if (UGameViewportClient* V = GEngine ? GEngine->GameViewport : nullptr) Show(V, bClean);
			return false;
		});
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudPreviewShot"), 6.f, [](float)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FN7HudLayout.png")), true, false);
			return false;
		});
		if (bQuit)
		{
			FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudPreviewQuit"), 9.f, [](float)
			{
				FPlatformMisc::RequestExit(false, TEXT("FN7.HudPreview quit"));
				return false;
			});
		}
	}));

#undef LOCTEXT_NAMESPACE
