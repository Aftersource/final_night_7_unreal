#include "SFNHudLayout.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SFNPanel.h"
#include "SFNPortraitCard.h"
#include "SFNTextBox.h"
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

using namespace FN7Style;

namespace
{
	TSharedRef<SWidget> Divider(bool bVertical)
	{
		return SNew(SBox)
			.WidthOverride(bVertical ? 1.f : FOptionalSize())
			.HeightOverride(bVertical ? 18.f : 1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SImage).Image(Brush::Swatch(Color::PanelEdge())).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.6f))
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

	TSharedRef<SWidget> Pill(const FSlateBrush* B, const FText& T, const FLinearColor& TextColor)
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
	Model = InArgs._Model;
	Rebuild();
}

void SFNHudLayout::SetModel(const FFNHudModel& InModel)
{
	Model = InModel;
	Rebuild();
}

void SFNHudLayout::Rebuild()
{
	Cards.Reset();
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(Space::XL)
		[
			MakeHeader()
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(Space::XL)
		[
			MakeNews()
		]
		// Bottom row: you + seats on the left, compose centred in what is left. One row, so they never overlap.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(Space::XL)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MakeSeats()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)[MakeYou()]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(Space::XL, 0.f, 0.f, 0.f)
			[
				MakeCompose()
			]
		]
	];
}

TSharedRef<SWidget> SFNHudLayout::MakeHeader()
{
	return SNew(SBorder)
		.BorderImage(Brush::Panel())
		.Padding(FMargin(Space::L, Space::S))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
			[
				SNew(SBox).WidthOverride(4.f).HeightOverride(20.f)[SNew(SImage).Image(Brush::AccentRule())]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Title()).Text(LOCTEXT("Game", "FINAL NIGHT 7"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)[Divider(true)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)
			[
				Stat(LOCTEXT("Room", "ROOM"), Model.Room)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)
			[
				Stat(LOCTEXT("Beat", "BEAT"), FText::Format(LOCTEXT("BeatFmt", "{0} / {1}"), Model.Beat, Model.BeatsTotal))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)
			[
				Stat(LOCTEXT("Alive", "ALIVE"), FText::AsNumber(Model.Alive))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("Entropy", "ENTROPY"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Pill(Brush::Pill(EFNSeatRole::AI), Model.Entropy, Color::AIAura())
				]
			]
		];
}

TSharedRef<SWidget> SFNHudLayout::MakeNews()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 i = 0; i < Model.News.Num(); ++i)
	{
		const FFNHudNews& N = Model.News[i];
		if (i > 0)
		{
			List->AddSlot().AutoHeight().Padding(0.f, Space::S)[Divider(false)];
		}
		List->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, Space::S, 0.f)
			[
				SNew(SBox).MinDesiredWidth(52.f).HAlign(HAlign_Center)
				[
					Pill(Brush::PillAccent(), N.Tag, FLinearColor(0.1f, 0.06f, 0.02f))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).TextStyle(&Type::Body()).AutoWrapText(true).Text(N.Text)
			]
		];
	}

	return SNew(SBox)
		.WidthOverride(400.f)
		[
			SNew(SFNPanel)
			.Title(LOCTEXT("World", "WORLD"))
			.Padding(FMargin(Space::L))
			[
				List
			]
		];
}

TSharedRef<SWidget> SFNHudLayout::MakeYou()
{
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	const int32 Max = FMath::Max(1, Model.MaxHealth);
	for (int32 i = 0; i < Max; ++i)
	{
		const bool bFull = i < Model.Health;
		Bar->AddSlot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)
		[
			SNew(SBox).WidthOverride(22.f).HeightOverride(8.f)
			[
				SNew(SImage).Image(Brush::Segment(bFull ? Color::HumanAura() : Color::PanelEdge()))
			]
		];
	}

	return SNew(SBox)
		.WidthOverride(480.f)
		[
			SNew(SBorder)
			.BorderImage(Brush::Panel())
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
						Pill(Brush::PillAccent(), FText::Format(LOCTEXT("Bolts", "{0} BOLTS"), Model.Bolts), FLinearColor(0.1f, 0.06f, 0.02f))
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

TSharedRef<SWidget> SFNHudLayout::MakeSeats()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const FFNHudSeat& S : Model.Seats)
	{
		TSharedPtr<SFNPortraitCard> Card;
		Row->AddSlot().AutoWidth().Padding(0.f, Space::M, Space::S, 0.f)
		[
			SAssignNew(Card, SFNPortraitCard)
			.Name(S.Name)
			.Role(S.Role)
			.bIsYou(S.bIsYou)
			.bShowHourglass(S.bHoldsTime)
			.CardWidth(112.f)
		];
		if (S.PortraitPng.Num() > 0) Card->SetImageFromPng(S.PortraitPng);
		Cards.Add(Card);
	}
	return Row;
}

TSharedRef<SWidget> SFNHudLayout::MakeCompose()
{
	TSharedRef<SWrapBox> Powers = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(Space::S, Space::S));
	for (const FFNHudPower& P : Model.Powers)
	{
		Powers->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(P.bArmed ? &PillButtonArmed() : &PillButton())
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).Text(P.Name)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(P.bArmed ? FLinearColor(0.1f, 0.06f, 0.02f) : Color::Accent()).Text(FText::AsNumber(P.Cost))
				]
			]
		];
	}

	return SNew(SBox)
		.MinDesiredWidth(560.f)
		.MaxDesiredWidth(860.f)
		[
			SNew(SBorder)
			.BorderImage(Brush::Panel())
			.Padding(FMargin(Space::L))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, Space::S)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::Accent()).Text(LOCTEXT("Powers", "ENTROPY POWERS"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).TextStyle(&Type::Small()).Text(Model.PowersNote)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[Powers]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SFNTextBox).bMultiLine(true).HintText(Model.ComposeHint)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::M, 0.f, 0.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, Space::S)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
							[
								SNew(SFNHourglass).Color(Color::Accent())
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(Color::Accent()).Text(Model.Timer)
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(FLinearColor(0.1f, 0.06f, 0.02f)).Text(LOCTEXT("Send", "SEND  WILL"))
							]
						]
					]
				]
			]
		];
}

FFNHudModel SFNHudLayout::SampleModel()
{
	FFNHudModel M;
	M.Room = FText::FromString(TEXT("2ZAHKZ"));
	M.Beat = 8;
	M.BeatsTotal = 30;
	M.Alive = 4;
	M.Entropy = FText::FromString(TEXT("CALM 8"));
	M.YouName = FText::FromString(TEXT("Chris"));
	M.Health = 10;
	M.MaxHealth = 10;
	M.Bolts = 21;
	M.Place = FText::FromString(TEXT("Vienna, Western Europe"));
	M.ArenaLine = FText::FromString(TEXT("Time's circle: 8637 km around Kinshasa. Stay inside."));
	M.PowersNote = FText::FromString(TEXT("21 bolts  -  every bolt you spend gives each rival +2"));
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

	auto News = [](const TCHAR* Tag, const TCHAR* Text) { FFNHudNews N; N.Tag = FText::FromString(Tag); N.Text = FText::FromString(Text); return N; };
	M.News.Add(News(TEXT("DAY 7"), TEXT("Oko slowed time and the crowds went wild. Tashkent's army pulled back.")));
	M.News.Add(News(TEXT("DAY 7"), TEXT("The Monterrey bread riot under Isabela Valdez breaks open the granaries: stores fall to 45%.")));
	M.News.Add(News(TEXT("DAY 7"), TEXT("The displaced doomsayers of Tashkent take up arms for Mara: 3289 volunteers.")));
	M.News.Add(News(TEXT("DAY 6"), TEXT("The Dar es Salaam bread riot breaks open the city's granaries: stores fall to 59%.")));

	auto Power = [](const TCHAR* Name, int32 Cost, bool bArmed) { FFNHudPower P; P.Name = FText::FromString(Name); P.Cost = Cost; P.bArmed = bArmed; return P; };
	M.Powers.Add(Power(TEXT("Lightning bolt"), 1, true));
	M.Powers.Add(Power(TEXT("Storm clouds"), 1, false));
	M.Powers.Add(Power(TEXT("Volcano"), 3, false));
	M.Powers.Add(Power(TEXT("Earthquake"), 2, false));
	M.Powers.Add(Power(TEXT("Flood"), 2, false));
	M.Powers.Add(Power(TEXT("Wildfire"), 2, false));
	M.Powers.Add(Power(TEXT("Meteor"), 4, false));
	M.Powers.Add(Power(TEXT("Raise land"), 2, false));
	M.Powers.Add(Power(TEXT("Sink land"), 3, false));
	M.Powers.Add(Power(TEXT("Grow forest"), 1, false));
	M.Powers.Add(Power(TEXT("Swarm"), 2, false));
	return M;
}

// --- FN7.HudPreview [clean] [shot] [quit] -------------------------------------
// Shows the layout over the game viewport with SampleModel(). "clean" removes the other viewport widgets first
// (preview only), "shot" saves Saved/FN7HudLayout.png with the UI, "quit" exits after the shot.

namespace
{
	TSharedPtr<SWidget> GHud;
	TSharedPtr<SWidget> GHudContent;

	void ShowHud(UGameViewportClient* VC, bool bClean)
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
		ShowHud(VC, bClean);
		if (!bShot) return;
		// Give the player controller time to add its own widgets, then clear them again and take the shot.
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudPreviewClean"), 4.f, [bClean](float)
		{
			if (UGameViewportClient* V = GEngine ? GEngine->GameViewport : nullptr) ShowHud(V, bClean);
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
