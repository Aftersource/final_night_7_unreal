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
#include "SFNPowerTile.h"
#include "FNWood.h"
#include "SFNScroll.h"
#include "SFNPanelMover.h"
#include "FNHudPrefs.h"
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

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(Space::XL)
		[
			SNew(SFNPanelMover).Id(TEXT("header"))[SAssignNew(HeaderHolder, SBox)]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(Space::XL)
		[
			SNew(SFNPanelMover).Id(TEXT("news"))[SAssignNew(NewsHolder, SBox)]
		]
		// Bottom row: you + seats on the left, the composer centred in what is left. One row, so they never overlap.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(Space::XL)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(SFNPanelMover).Id(TEXT("seats"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(SeatsHolder, SBox)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)[SAssignNew(YouHolder, SBox)]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(Space::XL, 0.f, 0.f, 0.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SFNPanelMover).Id(TEXT("scroll"))[MakeComposePanel()]
				]
				// Watching without a seat: one clear way in.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
				[
					SNew(SBorder)
					.BorderImage(FN7Style::Brush::Panel())
					.Padding(FMargin(FN7Style::Space::XL, FN7Style::Space::L))
					.Visibility_Lambda([this]() { return Model.bViewer ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock).TextStyle(&FN7Style::Type::Body()).Text(LOCTEXT("Watching", "You are watching the house game."))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, FN7Style::Space::M, 0.f, 0.f)
						[
							SNew(SBox).HeightOverride(48.f).WidthOverride(260.f)
							[
								SNew(SButton)
								.ButtonStyle(&FN7Style::AccentButton())
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.OnClicked_Lambda([this]() { OnTakeSeat.ExecuteIfBound(); return FReply::Handled(); })
								[
									SNew(STextBlock).TextStyle(&FN7Style::Type::Title()).ColorAndOpacity(FN7HudParts::HudInk).Text(LOCTEXT("TakeSeat", "TAKE A SEAT"))
								]
							]
						]
					]
				]
			]
		]
	];
	RefreshSections();
}

void SFNHudLayout::SetModel(const FFNHudModel& InModel)
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	Model = InModel;
	RefreshSections();
}

void SFNHudLayout::RefreshSections()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	HeaderHolder->SetContent(MakeHeader());
	NewsHolder->SetContent(MakeNews());
	const FString SeatsSig = MakeSeatsSignature();
	if (SeatsSig != SeatsSignature) { SeatsSignature = SeatsSig; SeatsHolder->SetContent(MakeSeats()); }
	YouHolder->SetContent(MakeYou());
	const FString PowersSig = MakePowersSignature();
	if (PowersSig != PowersSignature) { PowersSignature = PowersSig; PowersHolder->SetContent(MakePowers()); }
	NewsHolder->SetVisibility(Model.News.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

FString SFNHudLayout::MakeSeatsSignature() const
{
	FString S;
	for (const FFNHudSeat& Seat : Model.Seats)
		S += FString::Printf(TEXT("%s|%d|%d|%d|%p|%d;"), *Seat.Name.ToString(), (int32)Seat.Role, Seat.bIsYou ? 1 : 0, Seat.bHoldsTime ? 1 : 0, Seat.PortraitTexture, Seat.PortraitPng.Num());
	return S;
}

FString SFNHudLayout::MakePowersSignature() const
{
	FString S;
	for (const FFNHudPower& P : OrderedPowers()) S += FString::Printf(TEXT("%s|%d;"), *P.Slug, P.Cost);
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
	for (const FFNHudPower& P : OrderedPowers()) Slugs.Add(P.Slug);
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
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
		[
			SNew(SBox).WidthOverride(4.f).HeightOverride(20.f)[SNew(SImage).Image(Brush::AccentRule())]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Title()).Text(LOCTEXT("Game", "FINAL NIGHT 7"))
		];

	auto Add = [&Row](TSharedRef<SWidget> W)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::L, 0.f)[W];
	};
	if (!Model.Room.IsEmpty())
	{
		Add(Divider(true));
		Add(Stat(LOCTEXT("Room", "ROOM"), Model.Room));
	}
	if (Model.BeatsTotal > 0)
	{
		Add(Stat(LOCTEXT("Beat", "BEAT"), FText::Format(LOCTEXT("BeatFmt", "{0} / {1}"), Model.Beat, Model.BeatsTotal)));
		Add(Stat(LOCTEXT("Alive", "ALIVE"), FText::AsNumber(Model.Alive)));
	}
	if (!Model.Entropy.IsEmpty())
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("Entropy", "ENTROPY"))
		];
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[Chip(Brush::Pill(EFNSeatRole::AI), Model.Entropy, Color::AIAura())];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SBorder).BorderImage(Brush::Panel()).Padding(FMargin(Space::L, Space::S))[Row]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(Space::XS, Space::S, 0.f, 0.f)
		[
			SNew(STextBlock)
			.TextStyle(&Type::BodyOverImage())
			.ColorAndOpacity(Color::TextDim())
			.Text(Model.Status)
			.Visibility(Model.Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
		];
}

TSharedRef<SWidget> SFNHudLayout::MakeNews()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 i = 0; i < Model.News.Num(); ++i)
	{
		const FFNHudNews& N = Model.News[i];
		if (i > 0) List->AddSlot().AutoHeight().Padding(0.f, Space::S)[Divider(false)];
		List->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, Space::S, 0.f)
			[
				SNew(SBox).MinDesiredWidth(56.f).HAlign(HAlign_Center)[Chip(Brush::PillAccent(), N.Tag, HudInk)]
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
			SNew(SFNPanel).Title(LOCTEXT("World", "WORLD")).Padding(FMargin(Space::L))[List]
		];
}

TSharedRef<SWidget> SFNHudLayout::MakeYou()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	if (Model.YouName.IsEmpty()) return SNullWidget::NullWidget;

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
						Chip(Brush::PillAccent(), FText::Format(LOCTEXT("Bolts", "{0} BOLTS"), Model.Bolts), HudInk)
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
	using namespace FN7Style;
	using namespace FN7HudParts;
	Cards.Reset();
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
			.CardWidth(Model.Seats.Num() > 5 ? 92.f : 112.f)
		];
		if (S.PortraitTexture) Card->SetTexture(S.PortraitTexture);
		else if (S.PortraitPng.Num() > 0) Card->SetImageFromPng(S.PortraitPng);
		Cards.Add(Card);
	}
	return Row;
}

TSharedRef<SWidget> SFNHudLayout::MakePowers()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	TSharedRef<SWrapBox> Powers = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(Space::S, Space::S));
	for (const FFNHudPower& P : OrderedPowers())
	{
		const FString Slug = P.Slug;
		const int32 Cost = P.Cost;
		Powers->AddSlot()
		[
			SNew(SFNPowerTile)
			.Slug(Slug)
			.Name(P.Name)
			.Cost(P.Cost)
			.bArmed_Lambda([this, Slug]() { for (const FFNHudPower& Q : Model.Powers) if (Q.Slug == Slug) return Q.bArmed; return false; })
			.bEnabled_Lambda([this, Cost]() { return Model.Bolts >= Cost; })
			.TileSize(96.f)
			.OnClicked_Lambda([this, Slug]() { OnPower.ExecuteIfBound(Slug); })
			.OnReorder_Lambda([this, Slug](const FString& Dragged) { ReorderPower(Dragged, Slug); })
		];
	}
	return Powers;
}

TSharedRef<SWidget> SFNHudLayout::MakeComposePanel()
{
	using namespace FN7Style;
	using namespace FN7HudParts;
	const FLinearColor InkDim = FN7Wood::Ink() * FLinearColor(1.f, 1.f, 1.f, 0.72f);
	return SNew(SBox)
		.MinDesiredWidth(600.f)
		.MaxDesiredWidth(900.f)
		[
			// The old scroll: rolled up in the lobby, it unrolls as the match begins.
			SNew(SFNScroll)
			.bUnrolled_Lambda([this]() { return Model.BeatsTotal > 0 && !Model.bViewer; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, Space::S)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::M, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(FN7Wood::RedInk()).Text(LOCTEXT("Powers", "ENTROPY POWERS"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).TextStyle(&Type::Small()).Font(Type::Font(11, TEXT("Italic"))).ColorAndOpacity(InkDim).AutoWrapText(true)
						.Text_Lambda([this]() { return Model.PowersNote; })
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(PowersHolder, SBox)]
				// An inked rule between the powers and the will.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, Space::S)
				[
					SNew(SBox).HeightOverride(1.5f)
					[
						SNew(SImage).Image(Brush::Segment(FN7Wood::Ink() * FLinearColor(1.f, 1.f, 1.f, 0.35f)))
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(FN7Wood::RedInk()).Text(LOCTEXT("Will", "YOUR WILL"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SBox)
						.MinDesiredHeight(64.f)
						[
							SAssignNew(Compose, SFNTextBox)
							.bMultiLine(true)
							.Style(&FN7Wood::InkTextBox())
							.HintText_Lambda([this]() { return Model.ComposeHint; })
							.OnTextChanged(this, &SFNHudLayout::HandleTextChanged)
							.OnTextCommitted(this, &SFNHudLayout::HandleTextCommitted)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::M, 0.f, 0.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, Space::S)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::XS, 0.f)
							[
								SNew(SFNHourglass).Color(FN7Wood::RedInk())
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(FN7Wood::RedInk())
								.Text_Lambda([this]() { return TimerAttr.IsBound() || TimerAttr.IsSet() ? TimerAttr.Get() : Model.Timer; })
							]
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.HAlign(HAlign_Center)
							.OnClicked(this, &SFNHudLayout::HandleSend)
							[
								SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(HudInk).Text(LOCTEXT("Send", "SEND  WILL"))
							]
						]
					]
				]
			]
		];
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

void SFNHudLayout::HandleTextCommitted(const FText&, ETextCommit::Type)
{
	using namespace FN7Style;
	using namespace FN7HudParts;
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
	M.Bolts = 3;
	M.Place = FText::FromString(TEXT("Vienna, Western Europe"));
	M.ArenaLine = FText::FromString(TEXT("Time's circle: 8637 km around Kinshasa. Stay inside."));
	M.PowersNote = FText::FromString(TEXT("3 bolts  -  click to arm, or drag a block onto the globe. Every bolt you spend gives each rival +2."));
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
	M.News.Add(News(TEXT("BEAT 7"), TEXT("Oko slowed time and the crowds went wild. Tashkent's army pulled back.")));
	M.News.Add(News(TEXT("BEAT 7"), TEXT("The Monterrey bread riot under Isabela Valdez breaks open the granaries: stores fall to 45%.")));
	M.News.Add(News(TEXT("BEAT 7"), TEXT("The displaced doomsayers of Tashkent take up arms for Mara: 3289 volunteers.")));
	M.News.Add(News(TEXT("BEAT 6"), TEXT("The Dar es Salaam bread riot breaks open the city's granaries: stores fall to 59%.")));

	auto Power = [](const TCHAR* Slug, const TCHAR* Name, int32 Cost, bool bArmed) { FFNHudPower P; P.Slug = Slug; P.Name = FText::FromString(Name); P.Cost = Cost; P.bArmed = bArmed; return P; };
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
