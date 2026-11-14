#include "SFNStyleKitPreview.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "FNStyle.h"
#include "SFNPanel.h"
#include "SFNPortraitCard.h"
#include "SFNTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNStyleKitPreview"

namespace
{
	TSharedRef<SWidget> Swatch(const FLinearColor& C, const TCHAR* Name, const TCHAR* Hex)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(56.f).HeightOverride(36.f)
				[
					SNew(SImage).Image(FN7Style::Brush::Swatch(C))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, FN7Style::Space::XS, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&FN7Style::Type::Label()).Text(FText::FromString(Name))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).TextStyle(&FN7Style::Type::Small()).Text(FText::FromString(Hex))
			];
	}

	bool LoadProjectImage(const TSharedPtr<SFNPortraitCard>& Card, const TCHAR* RelativePath)
	{
		TArray<uint8> Bytes;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), RelativePath);
		return Card.IsValid() && FFileHelper::LoadFileToArray(Bytes, *Path) && Card->SetImageFromPng(Bytes);
	}

	TSharedPtr<SWidget> GPreview;         // the SWeakWidget handed to the viewport
	TSharedPtr<SWidget> GPreviewContent;  // the strong reference that keeps the sheet alive
}

void SFNStyleKitPreview::Construct(const FArguments&)
{
	using namespace FN7Style;
	TSharedPtr<SFNPortraitCard> You, Ally, Rival, Down;

	auto CardSlot = [](TSharedRef<SWidget> Card)
	{
		return SNew(SBox).Padding(FMargin(Space::S, Space::L, Space::S, 0.f))[Card];
	};

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(760.f)
		[
			SNew(SFNPanel)
			.Title(LOCTEXT("Title", "FINAL NIGHT 7  -  HUD STYLE KIT"))
			.Padding(FMargin(Space::XL))
			[
				SNew(SVerticalBox)

				// Palette.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(Space::M, Space::S))
					+ SWrapBox::Slot()[Swatch(Color::Cobalt(), TEXT("COBALT"), TEXT("#1B2A6B"))]
					+ SWrapBox::Slot()[Swatch(Color::DeepViolet(), TEXT("VIOLET"), TEXT("#2A1747"))]
					+ SWrapBox::Slot()[Swatch(Color::Panel(), TEXT("PANEL"), TEXT("#12152A 88%"))]
					+ SWrapBox::Slot()[Swatch(Color::Accent(), TEXT("ACCENT"), TEXT("#FF8A2B"))]
					+ SWrapBox::Slot()[Swatch(Color::HumanAura(), TEXT("HUMAN"), TEXT("#E8B84A"))]
					+ SWrapBox::Slot()[Swatch(Color::AIAura(), TEXT("AI"), TEXT("#3FD6E8"))]
					+ SWrapBox::Slot()[Swatch(Color::Fallen(), TEXT("FALLEN"), TEXT("#7A7F8C"))]
					+ SWrapBox::Slot()[Swatch(Color::Text(), TEXT("TEXT"), TEXT("#EDE6D6"))]
				]

				// Type scale.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
					[
						SNew(STextBlock).TextStyle(&Type::Display()).Text(LOCTEXT("Display", "Display"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::L, 0.f, 0.f, 4.f)
					[
						SNew(STextBlock).TextStyle(&Type::Title()).Text(LOCTEXT("TitleSample", "Title"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::L, 0.f, 0.f, 6.f)
					[
						SNew(STextBlock).TextStyle(&Type::Body()).Text(LOCTEXT("Body", "Body text for briefs and news"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::L, 0.f, 0.f, 7.f)
					[
						SNew(STextBlock).TextStyle(&Type::Small()).Text(LOCTEXT("Small", "small / secondary"))
					]
				]

				// Seats.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::M, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CardSlot(SAssignNew(You, SFNPortraitCard).Name(LOCTEXT("Chris", "Chris")).Role(EFNSeatRole::Human)
							.bIsYou(true).bShowHourglass(true).HourglassText(LOCTEXT("Secs", "12s")))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CardSlot(SAssignNew(Ally, SFNPortraitCard).Name(LOCTEXT("Benito", "Benito")).Role(EFNSeatRole::Human))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CardSlot(SAssignNew(Rival, SFNPortraitCard).Name(LOCTEXT("Vex", "Vex")).Role(EFNSeatRole::AI))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CardSlot(SAssignNew(Down, SFNPortraitCard).Name(LOCTEXT("Oko", "Oko")).Role(EFNSeatRole::Fallen))
					]
				]

				// Input.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XL, 0.f, 0.f)
				[
					SNew(SFNTextBox)
					.bMultiLine(true)
					.HintText(LOCTEXT("Hint", "Tell your hands what to do. Time slows while you write..."))
				]
			]
		]
	];

	// Real pictures from the approved look, where the repo has them; the others show their initial.
	LoadProjectImage(You, TEXT("docs/style-refs/approved/player_portrait.png"));
	LoadProjectImage(Rival, TEXT("docs/style-refs/approved/event_maxzoom.png"));
	LoadProjectImage(Down, TEXT("docs/style-refs/approved/diorama_maxzoom.png"));
}

static FAutoConsoleCommand GFN7StyleKitCommand(
	TEXT("FN7.StyleKit"),
	TEXT("Toggle the Final Night 7 HUD style kit test sheet over the game viewport."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		if (!VC) return;
		if (GPreview.IsValid())
		{
			VC->RemoveViewportWidgetContent(GPreview.ToSharedRef());
			GPreview.Reset();
			GPreviewContent.Reset();
			return;
		}
		GPreviewContent = SNew(SFNStyleKitPreview);
		GPreview = SNew(SWeakWidget).PossiblyNullContent(GPreviewContent);
		VC->AddViewportWidgetContent(GPreview.ToSharedRef(), 100);
	}));

#undef LOCTEXT_NAMESPACE
