#include "SFNAccountPages.h"

#include "FNAccount.h"
#include "FNLooks.h"
#include "FNNetClient.h"
#include "FNStyle.h"
#include "SFNTextBox.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FN7AccountUi"

TSharedRef<SWidget> FN7AccountUi::Header(const FText& Title, const FText& Word, const FString& Screen, int32* Look)
{
	using namespace FN7Style;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
			[
				SNew(STextBlock).TextStyle(&Type::Display()).ColorAndOpacity_Lambda([Look]() { return FSlateColor(FN7Looks::Ink(*Look)); }).Text(Title)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::M, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity_Lambda([Look]() { return FSlateColor(FN7Looks::Dim(*Look)); }).Text(Word)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::S, 0.f, 0.f, 4.f)
			[
				FN7Looks::Switch(TAttribute<int32>::CreateLambda([Look]() { return *Look; }), [Look, Screen](int32 V) { *Look = V; FN7Looks::Set(Screen, V); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(64.f).HeightOverride(3.f).HAlign(HAlign_Left)[SNew(SImage).Image(Brush::AccentRule())]
		];
}

FSlateColor SFNWelcomePage::Ink() const { return FN7Looks::Ink(Look); }
FSlateColor SFNWelcomePage::Dim() const { return FN7Looks::Dim(Look); }
FSlateColor SFNWelcomePage::Accent() const { return FN7Looks::Accent(Look); }

void SFNWelcomePage::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Net = InArgs._Net;
	OnSignedIn = InArgs._OnSignedIn;
	OnSkip = InArgs._OnSkip;
	Look = FN7Looks::Get(TEXT("login"), 1);
	const FLinearColor StartInk(0.1f, 0.06f, 0.02f);
	auto NotWaiting = [this]() { return bGoogleWait ? EVisibility::Collapsed : EVisibility::Visible; };

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			FN7AccountUi::Header(LOCTEXT("Title", "FINAL NIGHT 7"), LOCTEXT("Word", "WELCOME"), TEXT("login"), &Look)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNWelcomePage::Dim).AutoWrapText(true)
			.Text(LOCTEXT("Tag", "Demigods fight over the real Earth. Pick the name the other gods will know you by; your settings and your two demigods follow it."))
		]
		// The name (and, for a profile claimed with a secret, the secret).
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, Space::XS)
		[
			SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNWelcomePage::Dim).Visibility_Lambda(NotWaiting).Text(LOCTEXT("NameLabel", "YOUR NAME"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).Visibility_Lambda(NotWaiting)
			[
				SAssignNew(NameBox, SFNTextBox)
				.Text(FText::FromString(InArgs._Name))
				.HintText(LOCTEXT("NameHint", "1 to 24 characters"))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type C) { if (C == ETextCommit::OnEnter) PlayAsGuest(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(SVerticalBox).Visibility_Lambda([this]() { return bNeedSecret && !bGoogleWait ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, Space::XS)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNWelcomePage::Dim).Text(LOCTEXT("SecretLabel", "THIS NAME'S CLAIM SECRET"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(SecretBox, SEditableTextBox).Style(&FN7Style::TextBox()).IsPassword(true)
				.HintText(LOCTEXT("SecretHint", "the secret this name's profile was claimed with"))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type C) { if (C == ETextCommit::OnEnter) PlayAsGuest(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNWelcomePage::Accent).AutoWrapText(true)
			.Visibility_Lambda([this]() { return Error.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text_Lambda([this]() { return FText::FromString(Error); })
		]
		// PLAY AS GUEST.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
		[
			SNew(SBox).HeightOverride(52.f).Visibility_Lambda(NotWaiting)
			[
				SNew(SButton).ButtonStyle(&AccentButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.IsEnabled_Lambda([this]() { return !bBusy && FN7Account::Config().bGuest; })
				.OnClicked_Lambda([this]() { PlayAsGuest(); return FReply::Handled(); })
				[
					SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(StartInk).Text(LOCTEXT("Guest", "PLAY AS GUEST"))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNWelcomePage::Dim).Visibility_Lambda(NotWaiting)
			.Text(LOCTEXT("GuestUnder", "no email, no password: this computer keeps you signed in"))
		]
		// SIGN IN WITH GOOGLE (only when the server offers it).
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SBox).HeightOverride(44.f)
			.Visibility_Lambda([this]() { return FN7Account::Config().bGoogle && !bGoogleWait ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SButton).ButtonStyle(&PillButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.IsEnabled_Lambda([this]() { return !bBusy; })
				.OnClicked_Lambda([this]() { SignInGoogle(); return FReply::Handled(); })
				[
					SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("Google", "SIGN IN WITH GOOGLE"))
				]
			]
		]
		// Waiting on the browser.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
		[
			SNew(SVerticalBox).Visibility_Lambda([this]() { return bGoogleWait ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(this, &SFNWelcomePage::Ink).Text(LOCTEXT("Browser", "Continue in your browser..."))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::XS, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNWelcomePage::Dim)
				.Text(LOCTEXT("BrowserUnder", "Sign in with Google there; this screen moves on by itself."))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::M, 0.f, 0.f)
			[
				SNew(SButton).ButtonStyle(&PillButton())
				.OnClicked_Lambda([this]() { FN7Account::CancelGoogle(); bGoogleWait = false; bBusy = false; Error.Reset(); return FReply::Handled(); })
				[
					SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("Cancel", "CANCEL"))
				]
			]
		]
		// Without an account (the lobby as before).
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::L, 0.f, 0.f)
		[
			SNew(SButton).ButtonStyle(&FN7Style::PillButton()).Visibility_Lambda(NotWaiting)
			.OnClicked_Lambda([this]() { UE_LOG(LogTemp, Display, TEXT("FN7 login: without an account")); OnSkip.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("Skip", "play without an account"))
			]
		]
	];
}

void SFNWelcomePage::PlayAsGuest()
{
	UFNNetClient* N = Net.Get();
	const FString Name = NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (!N || bBusy) return;
	if (Name.IsEmpty() || Name.Len() > 24) { Error = TEXT("Your name is 1 to 24 characters."); return; }
	bBusy = true;
	Error.Reset();
	const FString Secret = bNeedSecret && SecretBox.IsValid() ? SecretBox->GetText().ToString().TrimStartAndEnd() : FString();
	TWeakPtr<SFNWelcomePage> Weak = SharedThis(this);
	FN7Account::Guest(N, Name, Secret, [Weak, Name](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNWelcomePage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		FString E;
		if (O.IsValid()) O->TryGetStringField(TEXT("error"), E);
		if (Code == 201)
		{
			FFileHelper::SaveStringToFile(Name + TEXT("\n"), *FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7DisplayName.txt")));
			Self->bNeedSecret = false;
			Self->OnSignedIn.ExecuteIfBound();
			return;
		}
		if (E == TEXT("profile_claimed")) { Self->bNeedSecret = true; Self->Error = TEXT("This name's profile was claimed with a secret: type it below."); return; }
		if (E == TEXT("name_taken")) { Self->Error = TEXT("Someone already plays under that name. Pick another."); return; }
		if (E == TEXT("name_lookalike")) { Self->Error = TEXT("That reads like someone else's name. Pick another."); return; }
		Self->Error = FN7Account::ErrorText(Code, O);
	});
}

void SFNWelcomePage::SignInGoogle()
{
	UFNNetClient* N = Net.Get();
	if (!N || bBusy) return;
	bBusy = true;
	bGoogleWait = true;
	Error.Reset();
	const FString Name = NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd() : FString();
	TWeakPtr<SFNWelcomePage> Weak = SharedThis(this);
	FN7Account::Google(N, false, Name, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNWelcomePage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		Self->bGoogleWait = false;
		if (Code == 200) { Self->OnSignedIn.ExecuteIfBound(); return; }
		Self->Error = Code == 408 ? FString(TEXT("The sign-in took too long. Try again.")) : Code == 404 ? FString(TEXT("That sign-in expired. Try again.")) : FN7Account::ErrorText(Code, O);
	});
}

#undef LOCTEXT_NAMESPACE
