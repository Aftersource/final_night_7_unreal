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
		Add(Slider(LOCTEXT("XText", "Text size"), TEXT("accessibility"), TEXT("text_scale"), 0.75f, 2.f, 1.f, false));
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

#undef LOCTEXT_NAMESPACE
