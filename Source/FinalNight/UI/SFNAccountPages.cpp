#include "SFNAccountPages.h"

#include "FNAccount.h"
#include "FNLooks.h"
#include "FNNetClient.h"
#include "FNHudPrefs.h"
#include "FNStyle.h"
#include "SFNTextBox.h"
#include "FNMedia.h"
#include "Containers/Ticker.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

// ---------------------------------------------------------------------------------------------------------------
// SETTINGS (ACCOUNTS.md §6): the whole document; PUT replaces it, so every key read is written back.

namespace FN7SettingsUi
{
	/** The settings the client applies itself, kept in Saved/FN7Hud.json (reduce flashing now, text size next run). */
	static void KeepLocal(const TSharedPtr<FJsonObject>& S)
	{
		// The volumes the studio's synth mixes by (master, music = the ambient bed, sfx = the sounds).
		const TSharedPtr<FJsonObject>* Au = nullptr;
		if (S.IsValid() && S->TryGetObjectField(TEXT("audio"), Au) && Au && Au->IsValid())
			for (const TCHAR* K : { TEXT("master"), TEXT("music"), TEXT("sfx"), TEXT("voice") })
			{
				double V = -1.0;
				if ((*Au)->TryGetNumberField(K, V) && V >= 0.0) FN7HudPrefs::SetInt(FString::Printf(TEXT("vol_%s_pct"), K), FMath::RoundToInt(FMath::Clamp(V, 0.0, 1.0) * 100.0));
			}
		const TSharedPtr<FJsonObject>* X = nullptr;
		if (!S.IsValid() || !S->TryGetObjectField(TEXT("accessibility"), X) || !X || !X->IsValid()) return;
		bool bRf = false; double Ts = 1.0;
		(*X)->TryGetBoolField(TEXT("reduce_flashing"), bRf);
		(*X)->TryGetNumberField(TEXT("text_scale"), Ts);
		FN7HudPrefs::SetInt(TEXT("reduce_flashing"), bRf ? 1 : 0);
		FN7HudPrefs::SetInt(TEXT("text_scale_pct"), FMath::RoundToInt(FMath::Clamp(Ts, 0.75, 2.0) * 100.0));
	}
	static TSharedPtr<FJsonObject> Copy(const TSharedPtr<FJsonObject>& O)
	{
		if (!O.IsValid()) return nullptr;
		FString S;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(O.ToSharedRef(), W);
		TSharedPtr<FJsonObject> Out;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(S);
		FJsonSerializer::Deserialize(R, Out);
		return Out;
	}
	static const TCHAR* Tabs[] = { TEXT("GENERAL"), TEXT("AUDIO"), TEXT("CAMERA"), TEXT("ACCESSIBILITY"), TEXT("CONTENT"), TEXT("HUD"), TEXT("KEYS") };
}

FSlateColor SFNSettingsPage::Ink() const { return FN7Looks::Ink(Look); }
FSlateColor SFNSettingsPage::Dim() const { return FN7Looks::Dim(Look); }
FSlateColor SFNSettingsPage::Accent() const { return FN7Looks::Accent(Look); }

void SFNSettingsPage::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Net = InArgs._Net;
	OnBack = InArgs._OnBack;
	Look = FN7Looks::Get(TEXT("settings"), 1);
	TSharedRef<SHorizontalBox> TabRow = SNew(SHorizontalBox);
	for (int32 i = 0; i < UE_ARRAY_COUNT(FN7SettingsUi::Tabs); ++i)
	{
		TabRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			FN7Looks::Chip(FText::FromString(FN7SettingsUi::Tabs[i]), [this, i]() { return Tab == i; }, [this, i]() { Tab = i; Rebuild(); }, 9, FMargin(10.f, 4.f))
		];
	}
	auto Btn = [](const FText& T, bool bAccent, TFunction<void()> F, TFunction<bool()> Enabled)
	{
		return SNew(SBox).HeightOverride(40.f).MinDesiredWidth(110.f)
			[
				SNew(SButton).ButtonStyle(bAccent ? &AccentButton() : &PillButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.IsEnabled_Lambda([Enabled]() { return Enabled(); })
				.OnClicked_Lambda([F]() { F(); return FReply::Handled(); })
				[ SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(bAccent ? FSlateColor(FLinearColor(0.08f, 0.05f, 0.02f)) : FSlateColor(Color::Text())).Text(T) ]
			];
	};
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ FN7AccountUi::Header(LOCTEXT("SetTitle", "SETTINGS"), LOCTEXT("SetWord", "THEY FOLLOW YOUR ACCOUNT"), TEXT("settings"), &Look) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)[ TabRow ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SBox).HeightOverride(380.f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(Body, SBox) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Body()).AutoWrapText(true)
			.ColorAndOpacity_Lambda([this]() { return bBad ? Accent() : Dim(); })
			.Text_Lambda([this]() { return FText::FromString(Status); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Btn(LOCTEXT("SetBack", "BACK"), false, [this]() { OnBack.ExecuteIfBound(); }, []() { return true; }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::S, 0.f)
			[ Btn(LOCTEXT("SetRevert", "REVERT"), false, [this]() { Doc = FN7SettingsUi::Copy(Server); Status = TEXT("Back to what the server has."); bBad = false; Rebuild(); }, [this]() { return Server.IsValid() && !bBusy; }) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Btn(LOCTEXT("SetApply", "APPLY"), true, [this]() { Apply(); }, [this]() { return Doc.IsValid() && !bBusy; }) ]
		]
	];
	Rebuild();
}

void SFNSettingsPage::Open()
{
	Status = TEXT("Reading your settings...");
	bBad = false;
	bBusy = true;
	TWeakPtr<SFNSettingsPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("GET"), TEXT("/api/me/settings"), nullptr, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNSettingsPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		const TSharedPtr<FJsonObject>* S = nullptr;
		if (Code == 200 && O.IsValid() && O->TryGetObjectField(TEXT("settings"), S) && S && S->IsValid())
		{
			Self->Server = *S;
			Self->Doc = FN7SettingsUi::Copy(*S);
			Self->Status.Reset();
			FN7SettingsUi::KeepLocal(*S);
			FString Pr; double Mu = -1.0; bool bRf = false;
			Self->Doc->TryGetStringField(TEXT("pronouns"), Pr);
			if (const TSharedPtr<FJsonObject> A = Self->Sec(TEXT("audio"))) A->TryGetNumberField(TEXT("music"), Mu);
			if (const TSharedPtr<FJsonObject> X = Self->Sec(TEXT("accessibility"))) X->TryGetBoolField(TEXT("reduce_flashing"), bRf);
			UE_LOG(LogTemp, Display, TEXT("FN7 settings: loaded (pronouns %s, music %.2f, reduce_flashing %d)"), *Pr, Mu, bRf ? 1 : 0);
		}
		else
		{
			Self->Status = FN7Account::ErrorText(Code, O);
			Self->bBad = true;
		}
		Self->Rebuild();
	});
}

void SFNSettingsPage::Apply()
{
	if (!Doc.IsValid() || bBusy) return;
	bBusy = true;
	Status = TEXT("Saving...");
	bBad = false;
	FString OldName, NewName;
	if (Server.IsValid()) Server->TryGetStringField(TEXT("display_name"), OldName);
	Doc->TryGetStringField(TEXT("display_name"), NewName);
	TWeakPtr<SFNSettingsPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("PUT"), TEXT("/api/me/settings"), Doc, [Weak, bRenamed = OldName != NewName](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNSettingsPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		const TSharedPtr<FJsonObject>* S = nullptr;
		if (Code == 200 && O.IsValid() && O->TryGetObjectField(TEXT("settings"), S) && S && S->IsValid())
		{
			Self->Server = *S;
			Self->Doc = FN7SettingsUi::Copy(*S);
			FN7SettingsUi::KeepLocal(*S);
			Self->Status = bRenamed ? TEXT("Saved. Your name changed: it is what the other gods see from your next login.") : TEXT("Saved.");
			UE_LOG(LogTemp, Display, TEXT("FN7 settings: saved%s"), bRenamed ? TEXT(" (renamed)") : TEXT(""));
			if (bRenamed) FN7Account::FetchMe(Self->Net.Get(), nullptr);
			Self->Rebuild();
			return;
		}
		Self->Status = FN7Account::ErrorText(Code, O);   // the server's detail: "audio.music must be between 0 and 1", name_taken...
		Self->bBad = true;
		UE_LOG(LogTemp, Display, TEXT("FN7 settings: not saved (%d)"), Code);
	});
}

TSharedPtr<FJsonObject> SFNSettingsPage::Sec(const FString& Key) const
{
	if (!Doc.IsValid()) return nullptr;
	if (Key.IsEmpty()) return Doc;
	const TSharedPtr<FJsonObject>* O = nullptr;
	if (Doc->TryGetObjectField(Key, O) && O && O->IsValid()) return *O;
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();   // a section the server left out: start it (its defaults fill on save)
	Doc->SetObjectField(Key, N);
	return N;
}

void SFNSettingsPage::Rebuild()
{
	if (Body.IsValid()) Body->SetContent(TabBody(Tab));
}

TSharedRef<SWidget> SFNSettingsPage::Row(const FText& Label, TSharedRef<SWidget> Control) const
{
	using namespace FN7Style;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(230.f).Padding(0.f, 0.f, 8.f, 0.f)[ SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNSettingsPage::Ink).AutoWrapText(true).Text(Label) ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ Control ];
}

TSharedRef<SWidget> SFNSettingsPage::Slider(const FText& Label, const FString& SecKey, const FString& Key, float Min, float Max, float Def, bool bPercent)
{
	using namespace FN7Style;
	auto Get = [this, SecKey, Key, Def]() { double V = Def; if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->TryGetNumberField(Key, V); return (float)V; };
	return Row(Label, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SSlider).MinValue(Min).MaxValue(Max).StepSize(bPercent ? 0.01f : 0.05f)
			.Value_Lambda(Get)
			.OnValueChanged_Lambda([this, SecKey, Key](float V) { if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->SetNumberField(Key, FMath::RoundToDouble(V * 100.0) / 100.0); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(48.f)
			[
				SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNSettingsPage::Ink)
				.Text_Lambda([Get, bPercent]() { const float V = Get(); return FText::FromString(bPercent ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f)) : FString::Printf(TEXT("%.2fx"), V)); })
			]
		]);
}

TSharedRef<SWidget> SFNSettingsPage::Toggle(const FText& Label, const FString& SecKey, const FString& Key, bool bDef)
{
	return Row(Label, SNew(SCheckBox)
		.IsChecked_Lambda([this, SecKey, Key, bDef]() { bool B = bDef; if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->TryGetBoolField(Key, B); return B ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, SecKey, Key](ECheckBoxState St) { if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->SetBoolField(Key, St == ECheckBoxState::Checked); }));
}

TSharedRef<SWidget> SFNSettingsPage::Choice(const FText& Label, const FString& SecKey, const FString& Key, const TArray<FString>& Values, const TArray<FText>& Labels)
{
	using namespace FN7Style;
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f));
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		const FString V = Values[i];
		auto Is = [this, SecKey, Key, V, Def = Values[0]]() { FString Cur = Def; if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->TryGetStringField(Key, Cur); return Cur == V; };
		Wrap->AddSlot()
		[
			FN7Looks::Chip(Labels.IsValidIndex(i) ? Labels[i] : FText::FromString(V), Is, [this, SecKey, Key, V]() { if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->SetStringField(Key, V); })
		];
	}
	return Row(Label, Wrap);
}

TSharedRef<SWidget> SFNSettingsPage::Field(const FText& Label, const FString& SecKey, const FString& Key, const FText& Hint, int32 MaxLen)
{
	FString Cur;
	if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->TryGetStringField(Key, Cur);
	return Row(Label, SNew(SFNTextBox).Text(FText::FromString(Cur)).HintText(Hint)
		.OnTextChanged_Lambda([this, SecKey, Key, MaxLen](const FText& T) { if (const TSharedPtr<FJsonObject> S = Sec(SecKey)) S->SetStringField(Key, T.ToString().Left(MaxLen)); }));
}

TSharedRef<SWidget> SFNSettingsPage::Keybinds()
{
	using namespace FN7Style;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const TSharedPtr<FJsonObject> K = Sec(TEXT("keybinds"));
	if (K.IsValid())
		for (const auto& KV : K->Values)
		{
			const FString Action(*KV.Key);   // UE 5.8 JSON keys are shared strings
			List->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				Row(FText::FromString(Action), SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[ SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(this, &SFNSettingsPage::Accent).Text(FText::FromString(KV.Value->AsString())) ]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).ButtonStyle(&PillButton())
						.OnClicked_Lambda([this, Action]() { if (const TSharedPtr<FJsonObject> S = Sec(TEXT("keybinds"))) S->RemoveField(Action); Rebuild(); return FReply::Handled(); })
						[ SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("KeyRemove", "REMOVE")) ]
					])
			];
		}
	if (!K.IsValid() || K->Values.Num() == 0)
		List->AddSlot().AutoHeight()[ SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNSettingsPage::Dim).Text(LOCTEXT("KeyNone", "No overrides: every key is the game's default.")) ];
	List->AddSlot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, Space::S, 0.f)[ SAssignNew(BindAction, SFNTextBox).HintText(LOCTEXT("KeyAction", "action, e.g. camera.pan_left")) ]
		+ SHorizontalBox::Slot().FillWidth(0.6f).Padding(0.f, 0.f, Space::S, 0.f)[ SAssignNew(BindChord, SFNTextBox).HintText(LOCTEXT("KeyChord", "key, e.g. A or Ctrl+Q")) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton).ButtonStyle(&PillButton())
			.OnClicked_Lambda([this]()
			{
				const FString A = BindAction.IsValid() ? BindAction->GetText().ToString().TrimStartAndEnd() : FString();
				const FString C = BindChord.IsValid() ? BindChord->GetText().ToString().TrimStartAndEnd() : FString();
				if (A.IsEmpty() || C.IsEmpty() || A.Len() > 40 || C.Len() > 40) { Status = TEXT("An override is an action (up to 40 characters) and a key."); bBad = true; return FReply::Handled(); }
				if (const TSharedPtr<FJsonObject> S = Sec(TEXT("keybinds"))) S->SetStringField(A.ToLower(), C);
				Rebuild();
				return FReply::Handled();
			})
			[ SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("KeyAdd", "ADD")) ]
		]
	];
	List->AddSlot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
	[ SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNSettingsPage::Dim).AutoWrapText(true).Text(LOCTEXT("KeyNote", "Actions are lower_snake (dots allowed); keys are letters, digits and + _ - (up to 64 overrides).")) ];
	return List;
}

TSharedRef<SWidget> SFNSettingsPage::TabBody(int32 InTab)
{
	using namespace FN7Style;
	if (!Doc.IsValid())
		return SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNSettingsPage::Dim).Text(LOCTEXT("SetNone", "Your settings appear here once the server answers."));
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&V](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight().Padding(0.f, 5.f)[W]; };
	switch (InTab)
	{
	case 0:
		Add(Field(LOCTEXT("SName", "Display name"), FString(), TEXT("display_name"), LOCTEXT("SNameH", "1 to 24 characters (renames your account)"), 24));
		Add(Choice(LOCTEXT("SPron", "Pronouns"), FString(), TEXT("pronouns"), { TEXT("unspecified"), TEXT("she"), TEXT("he"), TEXT("they"), TEXT("it") },
			{ LOCTEXT("P0", "unspecified"), LOCTEXT("P1", "she"), LOCTEXT("P2", "he"), LOCTEXT("P3", "they"), LOCTEXT("P4", "it") }));
		Add(Field(LOCTEXT("SLang", "Language"), FString(), TEXT("language"), LOCTEXT("SLangH", "a language tag, e.g. en, es, pt-BR"), 35));
		break;
	case 1:
		Add(Slider(LOCTEXT("AMaster", "Master"), TEXT("audio"), TEXT("master"), 0.f, 1.f, 0.8f, true));
		Add(Slider(LOCTEXT("AMusic", "Music"), TEXT("audio"), TEXT("music"), 0.f, 1.f, 0.6f, true));
		Add(Slider(LOCTEXT("ASfx", "Sound effects"), TEXT("audio"), TEXT("sfx"), 0.f, 1.f, 0.8f, true));
		Add(Slider(LOCTEXT("AVoice", "Voices"), TEXT("audio"), TEXT("voice"), 0.f, 1.f, 1.f, true));
		Add(Toggle(LOCTEXT("AMute", "Mute when in the background"), TEXT("audio"), TEXT("mute_when_unfocused"), true));
		break;
	case 2:
		Add(Slider(LOCTEXT("CPan", "Pan speed"), TEXT("camera"), TEXT("pan_speed"), 0.1f, 5.f, 1.f, false));
		Add(Slider(LOCTEXT("CZoom", "Zoom speed"), TEXT("camera"), TEXT("zoom_speed"), 0.1f, 5.f, 1.f, false));
		Add(Toggle(LOCTEXT("CEdge", "Pan at the screen edge"), TEXT("camera"), TEXT("edge_pan"), true));
		Add(Toggle(LOCTEXT("CInv", "Invert zoom"), TEXT("camera"), TEXT("invert_zoom"), false));
		break;
	case 3:
		Add(Choice(LOCTEXT("XPal", "Aura colours"), TEXT("accessibility"), TEXT("aura_palette"), { TEXT("default"), TEXT("deuteranopia"), TEXT("protanopia"), TEXT("tritanopia"), TEXT("high_contrast") },
			{ LOCTEXT("X0", "default"), LOCTEXT("X1", "deuteranopia"), LOCTEXT("X2", "protanopia"), LOCTEXT("X3", "tritanopia"), LOCTEXT("X4", "high contrast") }));
		Add(Toggle(LOCTEXT("XFlash", "Reduce flashing"), TEXT("accessibility"), TEXT("reduce_flashing"), false));
		Add(Slider(LOCTEXT("XText", "Text size (next time you open the game)"), TEXT("accessibility"), TEXT("text_scale"), 0.75f, 2.f, 1.f, false));
		Add(Toggle(LOCTEXT("XSubs", "Subtitles"), TEXT("accessibility"), TEXT("subtitles"), true));
		break;
	case 4:
		Add(Choice(LOCTEXT("NGore", "Gore"), TEXT("content"), TEXT("gore"), { TEXT("full"), TEXT("reduced"), TEXT("off") }, { LOCTEXT("G0", "full"), LOCTEXT("G1", "reduced"), LOCTEXT("G2", "off") }));
		Add(Choice(LOCTEXT("NProf", "Swearing"), TEXT("content"), TEXT("profanity"), { TEXT("on"), TEXT("masked") }, { LOCTEXT("F0", "on"), LOCTEXT("F1", "masked") }));
		Add(Toggle(LOCTEXT("NPers", "Show other players' demigod personalities"), TEXT("content"), TEXT("show_player_personas"), true));
		break;
	case 5:
		Add(Choice(LOCTEXT("HLay", "Layout"), TEXT("hud"), TEXT("layout"), { TEXT("default"), TEXT("compact"), TEXT("minimal") }, { LOCTEXT("L0", "default"), LOCTEXT("L1", "compact"), LOCTEXT("L2", "minimal") }));
		Add(Slider(LOCTEXT("HScale", "Scale"), TEXT("hud"), TEXT("scale"), 0.5f, 2.f, 1.f, false));
		Add(Choice(LOCTEXT("HCards", "Demigod cards"), TEXT("hud"), TEXT("demigod_cards"), { TEXT("lower_left"), TEXT("lower_right") }, { LOCTEXT("D0", "lower left"), LOCTEXT("D1", "lower right") }));
		Add(Toggle(LOCTEXT("HMini", "Minimap"), TEXT("hud"), TEXT("show_minimap"), true));
		Add(Toggle(LOCTEXT("HChat", "Chat"), TEXT("hud"), TEXT("show_chat"), true));
		Add(Toggle(LOCTEXT("HHints", "Hints"), TEXT("hud"), TEXT("show_hints"), true));
		break;
	default:
		Add(Keybinds());
		break;
	}
	return V;
}

// ---------------------------------------------------------------------------------------------------------------
// DEMIGODS (ACCOUNTS.md §7): two sheets; the player's words are stored exactly unless they break a content rule.

namespace FN7DemigodUi
{
	static const TCHAR* Fields[] = { TEXT("name"), TEXT("personality"), TEXT("style"), TEXT("creed") };
	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O.IsValid()) O->TryGetStringField(K, V); return V; }
}

FSlateColor SFNDemigodsPage::Ink() const { return FN7Looks::Ink(Look); }
FSlateColor SFNDemigodsPage::Dim() const { return FN7Looks::Dim(Look); }
FSlateColor SFNDemigodsPage::Accent() const { return FN7Looks::Accent(Look); }

void SFNDemigodsPage::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Net = InArgs._Net;
	OnBack = InArgs._OnBack;
	Look = FN7Looks::Get(TEXT("demigods"), 1);
	Cards[0].Hand = TEXT("left");
	Cards[1].Hand = TEXT("right");
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ FN7AccountUi::Header(LOCTEXT("DgTitle", "YOUR DEMIGODS"), LOCTEXT("DgWord", "THEY FIGHT IN YOUR NAME"), TEXT("demigods"), &Look) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNDemigodsPage::Dim).AutoWrapText(true)
			.Text(LOCTEXT("DgTag", "Your words are kept exactly as you write them. Only text that breaks a content rule (hate, sexual content, attacks on real people, instructions to the game, personal data) is rewritten or refused."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, Space::M, 0.f)[ MakeCard(0) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)[ MakeCard(1) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).HeightOverride(40.f).MinDesiredWidth(110.f)
				[
					SNew(SButton).ButtonStyle(&PillButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked_Lambda([this]() { OnBack.ExecuteIfBound(); return FReply::Handled(); })
					[ SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("DgBack", "BACK")) ]
				]
			]
		]
	];
}

TSharedRef<SWidget> SFNDemigodsPage::Labeled(const FText& Label, TSharedPtr<SFNTextBox>& Box, int32 LimitIdx, bool bMulti, float Height, const FText& Hint)
{
	using namespace FN7Style;
	TSharedPtr<SFNTextBox>* BoxPtr = &Box;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNDemigodsPage::Dim).Text(Label) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).TextStyle(&Type::Small())
				.ColorAndOpacity_Lambda([this, BoxPtr, LimitIdx]() { const int32 N = BoxPtr->IsValid() ? (*BoxPtr)->GetText().ToString().Len() : 0; return N > Limit[LimitIdx] ? Accent() : Dim(); })
				.Text_Lambda([this, BoxPtr, LimitIdx]() { const int32 N = BoxPtr->IsValid() ? (*BoxPtr)->GetText().ToString().Len() : 0; return FText::FromString(FString::Printf(TEXT("%d / %d"), N, Limit[LimitIdx])); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(Height)[ SAssignNew(Box, SFNTextBox).bMultiLine(bMulti).HintText(Hint) ]
		];
}

TSharedRef<SWidget> SFNDemigodsPage::MakeCard(int32 i)
{
	using namespace FN7Style;
	FCard& C = Cards[i];
	const FText HandName = i == 0 ? LOCTEXT("DgLeft", "LEFT HAND") : LOCTEXT("DgRight", "RIGHT HAND");
	return SNew(SBorder).BorderImage(Brush::Segment(FLinearColor(0.f, 0.f, 0.f, 0.22f))).Padding(FMargin(Space::M))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(this, &SFNDemigodsPage::Accent).Text(HandName) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom).Padding(Space::S, 0.f, 0.f, 2.f)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNDemigodsPage::Dim)
					.Text_Lambda([this, i]() { return Cards[i].MatchName.IsEmpty() ? FText::GetEmpty() : FText::FromString(TEXT("in a match: ") + Cards[i].MatchName); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[ Labeled(LOCTEXT("DgName", "NAME (optional)"), C.Name, 0, false, 34.f, LOCTEXT("DgNameH", "empty = <your name>'s Left / Right Hand")) ]
			+ SVerticalBox::Slot().AutoHeight()[ Labeled(LOCTEXT("DgPers", "PERSONALITY"), C.Personality, 1, true, 118.f, LOCTEXT("DgPersH", "who they are: temper, past, fears, what they want")) ]
			+ SVerticalBox::Slot().AutoHeight()[ Labeled(LOCTEXT("DgStyle", "SPEAKING STYLE"), C.Style, 2, true, 46.f, LOCTEXT("DgStyleH", "how they talk")) ]
			+ SVerticalBox::Slot().AutoHeight()[ Labeled(LOCTEXT("DgCreed", "CREED"), C.Creed, 3, true, 46.f, LOCTEXT("DgCreedH", "the one thing they live by")) ]
			// The answer: saved / the reason it was refused / the rewrite beside your words.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Body()).AutoWrapText(true)
				.ColorAndOpacity_Lambda([this, i]() { return Cards[i].bBad ? Accent() : Dim(); })
				.Visibility_Lambda([this, i]() { return Cards[i].Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this, i]() { return FText::FromString(Cards[i].Status); })
			]
			+ SVerticalBox::Slot().AutoHeight()[ Rephrased(i) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, Space::S, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(40.f).MinDesiredWidth(120.f)
				[
					SNew(SButton).ButtonStyle(&AccentButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.IsEnabled_Lambda([this, i]() { return !Cards[i].bBusy; })
					.OnClicked_Lambda([this, i]() { Save(i); return FReply::Handled(); })
					[ SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(FLinearColor(0.08f, 0.05f, 0.02f)).Text(LOCTEXT("DgSave", "SAVE")) ]
				]
			]
		];
}

TSharedRef<SWidget> SFNDemigodsPage::Rephrased(int32 i)
{
	using namespace FN7Style;
	// Only the fields the rewrite changed: yours on the left, what was saved on the right.
	auto Side = [this, i](bool bFinal)
	{
		return SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true)
			.ColorAndOpacity_Lambda([this, bFinal]() { return bFinal ? Ink() : Dim(); })
			.Text_Lambda([this, i, bFinal]()
			{
				const FCard& C = Cards[i];
				FString Out;
				for (const TCHAR* F : FN7DemigodUi::Fields)
				{
					const FString O = FN7DemigodUi::Str(C.Original, F), N = FN7DemigodUi::Str(C.Final, F);
					if (O != N) Out += FString::Printf(TEXT("%s: %s\n"), F, *(bFinal ? N : O));
				}
				return FText::FromString(Out.TrimEnd());
			});
	};
	return SNew(SVerticalBox).Visibility_Lambda([this, i]() { return Cards[i].Final.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNDemigodsPage::Accent).AutoWrapText(true)
			.Text_Lambda([this, i]() { return FText::FromString(TEXT("Rewritten: ") + Cards[i].Reason); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, Space::S, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNDemigodsPage::Dim).Text(LOCTEXT("DgYours", "YOU WROTE")) ]
				+ SVerticalBox::Slot().AutoHeight()[ Side(false) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNDemigodsPage::Dim).Text(LOCTEXT("DgSaved", "SAVED (THE MATCH USES THIS)")) ]
				+ SVerticalBox::Slot().AutoHeight()[ Side(true) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::XS, 0.f)
			[ FN7Looks::Chip(LOCTEXT("DgKeep", "KEEP THIS"), []() { return true; }, [this, i]() { Cards[i].Final.Reset(); Cards[i].Original.Reset(); Cards[i].Status = TEXT("Saved (the rewrite)."); }) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ FN7Looks::Chip(LOCTEXT("DgEdit", "EDIT"), []() { return false; }, [this, i]() { SetTexts(i, Cards[i].Original); Cards[i].Final.Reset(); Cards[i].Original.Reset(); Cards[i].Status = TEXT("Your words are back in the editor: change them and SAVE again."); Cards[i].bBad = false; }) ]
		];
}

void SFNDemigodsPage::SetTexts(int32 i, const TSharedPtr<FJsonObject>& Sheet)
{
	FCard& C = Cards[i];
	TSharedPtr<SFNTextBox>* Boxes[] = { &C.Name, &C.Personality, &C.Style, &C.Creed };
	for (int32 k = 0; k < 4; ++k) if (Boxes[k]->IsValid()) (*Boxes[k])->SetText(FText::FromString(FN7DemigodUi::Str(Sheet, FN7DemigodUi::Fields[k])));
}

void SFNDemigodsPage::Fill(int32 i, const TSharedPtr<FJsonObject>& D)
{
	if (!D.IsValid()) return;
	Cards[i].MatchName = FN7DemigodUi::Str(D, TEXT("match_name"));
	SetTexts(i, D);
}

void SFNDemigodsPage::Open()
{
	for (FCard& C : Cards) { C.Status = TEXT("Reading..."); C.bBad = false; C.Final.Reset(); C.Original.Reset(); }
	TWeakPtr<SFNDemigodsPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("GET"), TEXT("/api/me/demigods"), nullptr, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNDemigodsPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (Code != 200 || !O.IsValid()) { for (FCard& C : Self->Cards) { C.Status = FN7Account::ErrorText(Code, O); C.bBad = true; } return; }
		const TSharedPtr<FJsonObject>* L = nullptr;
		if (O->TryGetObjectField(TEXT("limits"), L) && L && L->IsValid())
			for (int32 k = 0; k < 4; ++k) { double V = 0.0; if ((*L)->TryGetNumberField(FN7DemigodUi::Fields[k], V) && V > 0.0) Self->Limit[k] = (int32)V; }
		for (int32 i = 0; i < 2; ++i)
		{
			const TSharedPtr<FJsonObject>* D = nullptr;
			if (O->TryGetObjectField(Self->Cards[i].Hand, D) && D) Self->Fill(i, *D);
			bool bSaved = false;
			if (D && D->IsValid()) (*D)->TryGetBoolField(TEXT("saved"), bSaved);
			Self->Cards[i].Status = bSaved ? FString() : FString(TEXT("Not written yet."));
		}
		UE_LOG(LogTemp, Display, TEXT("FN7 demigods: loaded (limits %d/%d/%d/%d)"), Self->Limit[0], Self->Limit[1], Self->Limit[2], Self->Limit[3]);
	});
}

void SFNDemigodsPage::Save(int32 i)
{
	FCard& C = Cards[i];
	if (C.bBusy) return;
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
	TSharedPtr<SFNTextBox>* Boxes[] = { &C.Name, &C.Personality, &C.Style, &C.Creed };
	for (int32 k = 0; k < 4; ++k)
	{
		const FString V = Boxes[k]->IsValid() ? (*Boxes[k])->GetText().ToString() : FString();
		if (V.Len() > Limit[k]) { C.Status = FString::Printf(TEXT("%s is limited to %d characters (this is %d)."), FN7DemigodUi::Fields[k], Limit[k], V.Len()); C.bBad = true; return; }
		B->SetStringField(FN7DemigodUi::Fields[k], k == 0 ? V.TrimStartAndEnd() : V);   // the texts go exactly as typed
	}
	C.bBusy = true;
	C.bBad = false;
	C.Status = TEXT("Saving (one moderation pass)...");
	C.Final.Reset();
	C.Original.Reset();
	TWeakPtr<SFNDemigodsPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("PUT"), TEXT("/api/me/demigods/") + C.Hand, B, [Weak, i](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNDemigodsPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		FCard& C = Self->Cards[i];
		C.bBusy = false;
		const FString Verdict = FN7DemigodUi::Str(O, TEXT("verdict")), Err = FN7DemigodUi::Str(O, TEXT("error")), Model = FN7DemigodUi::Str(O, TEXT("model"));
		const TSharedPtr<FJsonObject>* D = nullptr;
		if (O.IsValid() && O->TryGetObjectField(TEXT("demigod"), D) && D && D->IsValid()) C.MatchName = FN7DemigodUi::Str(*D, TEXT("match_name"));
		UE_LOG(LogTemp, Display, TEXT("FN7 demigods: %s hand -> %d %s%s (%s)"), *C.Hand, Code, *Verdict, Err.IsEmpty() ? TEXT("") : *(TEXT(" ") + Err), *Model);
		if (Code == 200 && Verdict == TEXT("ok")) { C.Status = TEXT("Saved exactly as you wrote it."); return; }
		if (Code == 200 && Verdict == TEXT("rephrase"))
		{
			const TSharedPtr<FJsonObject>* Or = nullptr; const TSharedPtr<FJsonObject>* Fi = nullptr;
			if (O->TryGetObjectField(TEXT("original"), Or) && Or) C.Original = *Or;
			if (O->TryGetObjectField(TEXT("final"), Fi) && Fi) C.Final = *Fi;
			C.Reason = FN7DemigodUi::Str(O, TEXT("reason"));
			C.Status.Reset();
			if (D && D->IsValid()) Self->SetTexts(i, *D);   // the editor shows what is saved; EDIT brings yours back
			return;
		}
		if (Code == 422)
		{
			const FString Reason = FN7DemigodUi::Str(O, TEXT("reason"));
			C.Status = TEXT("Not saved: ") + (Reason.IsEmpty() ? FN7Account::ErrorText(Code, O) : Reason) + TEXT(" Your text is still here to change.");
			C.bBad = true;
			return;
		}
		if (Err == TEXT("name_conflict")) { C.Status = TEXT("The name: ") + FN7Account::ErrorText(Code, O); C.bBad = true; return; }
		C.Status = FN7Account::ErrorText(Code, O);   // 400 limits / control characters, 429 wait N s, ...
		C.bBad = true;
	});
}

// ---------------------------------------------------------------------------------------------------------------
// Your god (ACCOUNTS.md §14): an entry box where people choose how their stone godform looks, and is textured.

namespace FN7GodUi
{
	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O.IsValid()) O->TryGetStringField(K, V); return V; }
	static TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O.IsValid() && O->TryGetObjectField(K, V) && V ? *V : nullptr; }
}

FSlateColor SFNGodPage::Ink() const { return FN7Looks::Ink(Look); }
FSlateColor SFNGodPage::Dim() const { return FN7Looks::Dim(Look); }
FSlateColor SFNGodPage::Accent() const { return FN7Looks::Accent(Look); }

void SFNGodPage::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Net = InArgs._Net;
	OnBack = InArgs._OnBack;
	Look = FN7Looks::Get(TEXT("god"), 1);
	Stones = { {TEXT("white_marble"), TEXT("White marble")}, {TEXT("black_onyx"), TEXT("Black onyx")}, {TEXT("granite"), TEXT("Granite")}, {TEXT("sandstone"), TEXT("Sandstone")},
		{TEXT("jade"), TEXT("Jade")}, {TEXT("bronze"), TEXT("Bronze")}, {TEXT("gold_leaf"), TEXT("Gold leaf")} };
	Poses = { {TEXT("thinker"), TEXT("The Thinker")}, {TEXT("david"), TEXT("Michelangelo's David")}, {TEXT("moses_seated"), TEXT("Moses, seated")}, {TEXT("discus_thrower"), TEXT("The Discus Thrower")},
		{TEXT("atlas"), TEXT("Atlas")}, {TEXT("winged_victory"), TEXT("Winged Victory")}, {TEXT("boxer_at_rest"), TEXT("The Boxer at Rest")}, {TEXT("custom"), TEXT("Your own")} };
	ImageBrush.DrawAs = ESlateBrushDrawType::Image;
	ImageBrush.ImageSize = FVector2D(300.f, 300.f);
	auto Btn = [](const FText& T, bool bAccent, TFunction<void()> F, TFunction<bool()> Enabled, TAttribute<FText> Label = TAttribute<FText>())
	{
		return SNew(SBox).HeightOverride(40.f).MinDesiredWidth(110.f)
			[
				SNew(SButton).ButtonStyle(bAccent ? &AccentButton() : &PillButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
				.IsEnabled_Lambda([Enabled]() { return Enabled(); })
				.OnClicked_Lambda([F]() { F(); return FReply::Handled(); })
				[ SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(bAccent ? FSlateColor(FLinearColor(0.08f, 0.05f, 0.02f)) : FSlateColor(Color::Text())).Text(Label.IsSet() ? Label : TAttribute<FText>(T)) ]
			];
	};
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ FN7AccountUi::Header(LOCTEXT("GodTitle", "YOUR GOD"), LOCTEXT("GodWord", "THE STATUE AT YOUR SEAT"), TEXT("god"), &Look) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			// The form.
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, Space::L, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(this, &SFNGodPage::Ink).Text(LOCTEXT("GodAsk", "How does your stone godform look?")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
					[
						SNew(STextBlock).TextStyle(&Type::Small())
						.ColorAndOpacity_Lambda([this]() { return Desc.IsValid() && Desc->GetText().ToString().Len() > DescLimit ? Accent() : Dim(); })
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d / %d"), Desc.IsValid() ? Desc->GetText().ToString().Len() : 0, DescLimit)); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
				[
					SNew(SBox).HeightOverride(110.f)
					[ SAssignNew(Desc, SFNTextBox).bMultiLine(true).HintText(LOCTEXT("GodDescH", "In your words: the figure, the pose, what it holds. \"A hunched old god with a lantern, half-buried in sand.\"")) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, Space::XS)[ SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNGodPage::Dim).Text(LOCTEXT("GodStone", "THE STONE")) ]
				+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(StoneRow, SBox)[ Picker(true) ] ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
				[
					SNew(SBox).Visibility_Lambda([this]() { return bStoneOwn ? EVisibility::Visible : EVisibility::Collapsed; })
					[ SAssignNew(StoneOwn, SFNTextBox).HintText(LOCTEXT("GodStoneOwn", "your own words, up to 60 characters: \"veined obsidian\", \"weathered limestone\"")) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, Space::XS)[ SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNGodPage::Dim).Text(LOCTEXT("GodPose", "THE POSE (Your own = say it in the box above)")) ]
				+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(PoseRow, SBox)[ Picker(false) ] ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).AutoWrapText(true)
					.ColorAndOpacity_Lambda([this]() { return bBad ? Accent() : Dim(); })
					.Text_Lambda([this]() { return FText::FromString(Status); })
				]
			]
			// The picture.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(Brush::Segment(FLinearColor(0.f, 0.f, 0.f, 0.35f))).Padding(4.f)
					[
						SNew(SBox).WidthOverride(300.f).HeightOverride(300.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(SOverlay)
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(SImage).Image(&ImageBrush).Visibility_Lambda([this]() { return Tex.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }) ]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Color::TextDim()).Justification(ETextJustify::Center).AutoWrapText(true)
								.Visibility_Lambda([this]() { return Tex.IsValid() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
								.Text_Lambda([this]() { return PreviewStatus == TEXT("pending") ? LOCTEXT("GodDrawing", "Drawing your god...") : LOCTEXT("GodNoPic", "SAVE your god, then PREVIEW: the server draws it here."); })
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::XS, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNGodPage::Accent)
					.Text_Lambda([this]()
					{
						if (bPreviewAccepted) return FText::FromString(StatueStatus.IsEmpty() ? TEXT("YOUR SEAT SHOWS THIS") : TEXT("YOUR SEAT SHOWS THIS  -  STATUE ") + StatueStatus.ToUpper());
						if (!PreviewHash.IsEmpty()) return LOCTEXT("GodPreviewWord", "PREVIEW (not yet yours: ACCEPT it)");
						return FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNGodPage::Dim)
					.Text_Lambda([this]() { return PreviewsLeft < 0 ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("%d new previews left today (a picture you already have is free)"), PreviewsLeft)); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Btn(LOCTEXT("GodBack", "BACK"), false, [this]() { OnBack.ExecuteIfBound(); }, []() { return true; }) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::S, 0.f)[ Btn(LOCTEXT("GodSave", "SAVE"), false, [this]() { Save(); }, [this]() { return !bBusy; }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::S, 0.f)[ Btn(LOCTEXT("GodPreview", "PREVIEW"), false, [this]() { Preview(); }, [this]() { return !bBusy && bSaved && PreviewStatus != TEXT("pending"); }) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Btn(LOCTEXT("GodAccept", "ACCEPT"), true, [this]() { Accept(); }, [this]() { return !bBusy && PreviewStatus == TEXT("ready") && !bPreviewAccepted; },
					TAttribute<FText>::CreateLambda([this]() { return bAcceptAsk ? LOCTEXT("GodAcceptSure", "YES, MAKE IT MY GOD") : LOCTEXT("GodAccept2", "ACCEPT"); }))
			]
		]
	];
}

TSharedRef<SWidget> SFNGodPage::Picker(bool bStones)
{
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f));
	const TArray<TPair<FString, FString>>& L = bStones ? Stones : Poses;
	for (const TPair<FString, FString>& O : L)
	{
		const FString Id = O.Key;
		if (bStones)
			Wrap->AddSlot()[ FN7Looks::Chip(FText::FromString(O.Value), [this, Id]() { return !bStoneOwn && Stone == Id; }, [this, Id]() { Stone = Id; bStoneOwn = false; }) ];
		else
			Wrap->AddSlot()[ FN7Looks::Chip(FText::FromString(O.Value), [this, Id]() { return Pose == Id; }, [this, Id]() { Pose = Id; }) ];
	}
	if (bStones) Wrap->AddSlot()[ FN7Looks::Chip(LOCTEXT("GodStoneOwnChip", "Your own words"), [this]() { return bStoneOwn; }, [this]() { bStoneOwn = true; }) ];
	return Wrap;
}

void SFNGodPage::Read(const TSharedPtr<FJsonObject>& G, bool bForm)
{
	using namespace FN7GodUi;
	if (!G.IsValid()) return;
	G->TryGetBoolField(TEXT("saved"), bSaved);
	G->TryGetBoolField(TEXT("statue_meshy"), bMeshy);
	if (bForm)   // the poll never overwrites what is being typed
	{
		if (Desc.IsValid()) Desc->SetText(FText::FromString(Str(G, TEXT("description"))));
		const FString St = Str(G, TEXT("stone"));
		bool bCustom = false;
		G->TryGetBoolField(TEXT("stone_custom"), bCustom);
		if (!St.IsEmpty()) { Stone = St; bStoneOwn = bCustom; if (bCustom && StoneOwn.IsValid()) StoneOwn->SetText(FText::FromString(St)); }
		if (!Str(G, TEXT("pose")).IsEmpty()) Pose = Str(G, TEXT("pose"));
	}
	if (const TSharedPtr<FJsonObject> Lim = Obj(G, TEXT("limits")))
	{
		double V = 0.0;
		if (Lim->TryGetNumberField(TEXT("description"), V) && V > 0.0) DescLimit = (int32)V;
		if (Lim->TryGetNumberField(TEXT("previews_left_today"), V)) PreviewsLeft = (int32)V;
	}
	if (const TSharedPtr<FJsonObject> Op = Obj(G, TEXT("options")))
	{
		for (int32 k = 0; k < 2; ++k)
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!Op->TryGetArrayField(k == 0 ? TEXT("stones") : TEXT("poses"), A) || !A || A->Num() == 0) continue;
			TArray<TPair<FString, FString>>& L = k == 0 ? Stones : Poses;
			L.Reset();
			for (const TSharedPtr<FJsonValue>& V : *A) if (const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr) L.Add({ Str(O, TEXT("id")), Str(O, TEXT("label")) });
		}
		if (StoneRow.IsValid()) StoneRow->SetContent(Picker(true));
		if (PoseRow.IsValid()) PoseRow->SetContent(Picker(false));
	}
	// The newest preview (or, if none, the accepted portrait).
	TSharedPtr<FJsonObject> P = Obj(G, TEXT("preview"));
	const TSharedPtr<FJsonObject> Portrait = Obj(G, TEXT("portrait"));
	if (!P.IsValid()) P = Portrait;
	PreviewHash = Str(P, TEXT("hash"));
	PreviewStatus = Str(P, TEXT("status"));
	PreviewUrl = Str(P, TEXT("url"));
	bPreviewAccepted = false;
	if (P.IsValid()) P->TryGetBoolField(TEXT("accepted"), bPreviewAccepted);
	if (Portrait.IsValid() && Str(Portrait, TEXT("hash")) == PreviewHash) bPreviewAccepted = true;
	StatueStatus.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Statues = nullptr;
	if (G->TryGetArrayField(TEXT("statues"), Statues) && Statues && Statues->Num() > 0)
		if (const TSharedPtr<FJsonObject> S = (*Statues).Last()->AsObject()) StatueStatus = Str(S, TEXT("status"));
	if (PreviewStatus == TEXT("ready") && !PreviewUrl.IsEmpty()) LoadImage(PreviewHash, PreviewUrl);
}

void SFNGodPage::Open()
{
	Status = TEXT("Reading your god...");
	bBad = false;
	bAcceptAsk = false;
	TWeakPtr<SFNGodPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("GET"), TEXT("/api/me/god"), nullptr, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNGodPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (Code != 200) { Self->Status = Code == 404 ? FString(TEXT("This server has no god form yet.")) : FN7Account::ErrorText(Code, O); Self->bBad = true; return; }
		Self->Read(O);
		Self->Status = Self->bSaved ? FString() : FString(TEXT("Until you make one, your seat shows a marble god with your Left Hand's likeness."));
		UE_LOG(LogTemp, Display, TEXT("FN7 god: loaded (saved %d, stone %s, pose %s, preview %s, statues %s)"), Self->bSaved ? 1 : 0, *Self->Stone, *Self->Pose,
			Self->PreviewStatus.IsEmpty() ? TEXT("none") : *Self->PreviewStatus, Self->bMeshy ? TEXT("on") : TEXT("off"));
	});
}

void SFNGodPage::Save()
{
	if (bBusy) return;
	const FString D = Desc.IsValid() ? Desc->GetText().ToString() : FString();
	const FString St = bStoneOwn ? (StoneOwn.IsValid() ? StoneOwn->GetText().ToString().TrimStartAndEnd() : FString()) : Stone;
	if (D.Len() > DescLimit) { Status = FString::Printf(TEXT("The description is limited to %d characters (this is %d)."), DescLimit, D.Len()); bBad = true; return; }
	if (bStoneOwn && (St.IsEmpty() || St.Len() > 60)) { Status = TEXT("Your own stone is 1 to 60 characters."); bBad = true; return; }
	if (Pose == TEXT("custom") && D.TrimStartAndEnd().IsEmpty()) { Status = TEXT("Your own pose: say it in the box."); bBad = true; return; }
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("description"), D);
	B->SetStringField(TEXT("stone"), St);
	B->SetStringField(TEXT("pose"), Pose);
	bBusy = true;
	bBad = false;
	Status = TEXT("Saving (one moderation pass)...");
	TWeakPtr<SFNGodPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("PUT"), TEXT("/api/me/god"), B, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		using namespace FN7GodUi;
		const TSharedPtr<SFNGodPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		const FString Verdict = Str(O, TEXT("verdict"));
		UE_LOG(LogTemp, Display, TEXT("FN7 god: save -> %d %s (%s)"), Code, *Verdict, *Str(O, TEXT("model")));
		if (const TSharedPtr<FJsonObject> G = Obj(O, TEXT("god"))) if (Code == 200) Self->Read(G);
		if (Code == 200 && Verdict == TEXT("ok")) { Self->Status = TEXT("Saved exactly as you wrote it. PREVIEW to see it drawn."); return; }
		if (Code == 200 && Verdict == TEXT("rephrase")) { Self->Status = TEXT("Saved with a small rewrite: ") + Str(O, TEXT("reason")) + TEXT(" The box shows what was saved."); return; }
		if (Code == 422) { Self->Status = TEXT("Not saved: ") + Str(O, TEXT("reason")) + TEXT(" Your words are still here."); Self->bBad = true; return; }
		Self->Status = FN7Account::ErrorText(Code, O);
		Self->bBad = true;
	});
}

void SFNGodPage::Preview()
{
	if (bBusy) return;
	bBusy = true;
	bBad = false;
	Status = TEXT("Asking for a picture...");
	TWeakPtr<SFNGodPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("POST"), TEXT("/api/me/god/preview"), nullptr, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		using namespace FN7GodUi;
		const TSharedPtr<SFNGodPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		bool bCharged = false;
		if (O.IsValid()) { O->TryGetBoolField(TEXT("charged"), bCharged); double V = 0.0; if (O->TryGetNumberField(TEXT("previews_left_today"), V)) Self->PreviewsLeft = (int32)V; }
		UE_LOG(LogTemp, Display, TEXT("FN7 god: preview -> %d %s%s"), Code, *Str(O, TEXT("status")), bCharged ? TEXT(" (a new picture)") : TEXT(" (free: already drawn)"));
		if (Code != 200) { Self->Status = Code == 404 ? FString(TEXT("SAVE your god first.")) : FN7Account::ErrorText(Code, O); Self->bBad = true; return; }
		Self->PreviewHash = Str(O, TEXT("hash"));
		Self->PreviewStatus = Str(O, TEXT("status"));
		Self->PreviewUrl = Str(O, TEXT("url"));
		Self->bPreviewAccepted = false;
		Self->Tex.Reset();
		Self->Status = Self->PreviewStatus == TEXT("ready") ? FString(TEXT("Here it is. ACCEPT makes it your god.")) : FString(TEXT("Drawing your god (this takes a little while)..."));
		if (Self->PreviewStatus == TEXT("ready") && !Self->PreviewUrl.IsEmpty()) Self->LoadImage(Self->PreviewHash, Self->PreviewUrl);
		else { Self->PollUntil = FPlatformTime::Seconds() + 180.0; Self->PollPreview(); }
	});
}

void SFNGodPage::PollPreview()
{
	TWeakPtr<SFNGodPage> Weak = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7GodPoll"), 2.5f, [Weak](float)
	{
		const TSharedPtr<SFNGodPage> Self = Weak.Pin();
		if (!Self.IsValid() || Self->PreviewStatus != TEXT("pending")) return false;
		if (FPlatformTime::Seconds() > Self->PollUntil) { Self->Status = TEXT("The picture is taking long: come back to this screen in a minute."); return false; }
		FN7Account::Call(Self->Net.Get(), TEXT("GET"), TEXT("/api/me/god"), nullptr, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
		{
			const TSharedPtr<SFNGodPage> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			if (Code == 200)
			{
				const FString Hash = Self->PreviewHash;
				const TSharedPtr<FJsonObject> P = FN7GodUi::Obj(O, TEXT("preview"));
				if (P.IsValid() && FN7GodUi::Str(P, TEXT("hash")) == Hash && FN7GodUi::Str(P, TEXT("status")) != TEXT("pending"))
				{
					Self->Read(O, false);
					UE_LOG(LogTemp, Display, TEXT("FN7 god: preview %s"), *Self->PreviewStatus);
					Self->Status = Self->PreviewStatus == TEXT("ready") ? FString(TEXT("Here it is. ACCEPT makes it your god.")) : FString(TEXT("The picture failed: PREVIEW again."));
					return;
				}
			}
			Self->PollPreview();
		});
		return false;
	});
}

void SFNGodPage::LoadImage(const FString& Hash, const FString& Url)
{
	if (Hash.IsEmpty() || Hash == TexHash) return;
	TArray<uint8> Bytes;
	if (FN7Media::LoadCached(Hash, Bytes))
		if (UTexture2D* T = FN7Media::TextureFromPng(Bytes))
		{
			Tex.Reset(T); TexHash = Hash; ImageBrush.SetResourceObject(T);
			ImageBrush.ImageSize = FVector2D(T->GetSizeX(), T->GetSizeY()) * (300.f / FMath::Max(1, FMath::Max(T->GetSizeX(), T->GetSizeY())));
			return;
		}
	UFNNetClient* N = Net.Get();
	if (!N) return;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakPtr<SFNGodPage> Weak = SharedThis(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Hash](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		const TSharedPtr<SFNGodPage> Self = Weak.Pin();
		if (!Self.IsValid() || !bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { UE_LOG(LogTemp, Display, TEXT("FN7 god: picture not fetched (%d)"), Resp.IsValid() ? Resp->GetResponseCode() : 0); return; }
		const TArray<uint8>& B = Resp->GetContent();
		if (UTexture2D* T = FN7Media::TextureFromPng(B))
		{
			FN7Media::SaveCached(Hash, B);
			Self->Tex.Reset(T);
			Self->TexHash = Hash;
			Self->ImageBrush.SetResourceObject(T);
			Self->ImageBrush.ImageSize = FVector2D(T->GetSizeX(), T->GetSizeY()) * (300.f / FMath::Max(1, FMath::Max(T->GetSizeX(), T->GetSizeY())));   // its own shape (the server draws 2:3)
			UE_LOG(LogTemp, Display, TEXT("FN7 god: picture shown (%dx%d)"), T->GetSizeX(), T->GetSizeY());
		}
	});
	Req->ProcessRequest();
}

void SFNGodPage::Accept()
{
	if (bBusy || PreviewHash.IsEmpty()) return;
	if (!bAcceptAsk)
	{
		// Accepting is final for the seat, and on a server with statues it also makes the account's one 3D statue.
		bAcceptAsk = true;
		Status = bMeshy ? TEXT("This becomes your god, and the server makes your one 3D statue from it. Press again to accept.") : TEXT("This becomes your god at your seat. Press again to accept.");
		bBad = false;
		return;
	}
	bAcceptAsk = false;
	bBusy = true;
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("hash"), PreviewHash);
	TWeakPtr<SFNGodPage> Weak = SharedThis(this);
	FN7Account::Call(Net.Get(), TEXT("POST"), TEXT("/api/me/god/accept"), B, [Weak](int32 Code, TSharedPtr<FJsonObject> O)
	{
		const TSharedPtr<SFNGodPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bBusy = false;
		FString Statue;
		if (O.IsValid() && !O->TryGetStringField(TEXT("statue"), Statue)) if (const TSharedPtr<FJsonObject> S = FN7GodUi::Obj(O, TEXT("statue"))) Statue = FN7GodUi::Str(S, TEXT("status"));
		UE_LOG(LogTemp, Display, TEXT("FN7 god: accept -> %d (statue %s)"), Code, *Statue);
		if (Code != 200) { Self->Status = FN7Account::ErrorText(Code, O); Self->bBad = true; return; }
		Self->bPreviewAccepted = true;
		Self->StatueStatus = Statue == TEXT("not_enabled") || Statue == TEXT("not_configured") ? FString() : Statue;
		Self->Status = Statue == TEXT("queued") ? FString(TEXT("Your god is set; the statue is being made (it appears at your seat when ready).")) : FString(TEXT("Your god is set: your seat shows it."));
	});
}

#undef LOCTEXT_NAMESPACE
