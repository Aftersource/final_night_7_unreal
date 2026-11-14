#include "SFNDuelTray.h"

#include "FNStyle.h"
#include "FNWood.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FN7DuelTray"

namespace FN7Tray
{
	static constexpr double OwnFlash = 0.35;    // your newest letter stays readable this long
	static constexpr double TailFlash = 0.6;    // an opponent frame's tail stays readable this long
	static constexpr double SentStep = 0.035;   // a sent line reveals one letter every SentStep...
	static constexpr double SentHold = 0.25;    // ...each readable this long

	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def)
	{
		double V = Def;
		if (O.IsValid() && O->HasTypedField<EJson::Number>(K)) V = O->GetNumberField(K);
		return V;
	}

	/** len characters: stars, the last Reveal.Len() of them replaced by Reveal. */
	static FString Masked(int32 Len, const FString& Reveal)
	{
		Len = FMath::Clamp(Len, 0, 120);
		const FString R = Reveal.Right(FMath::Min(Len, Reveal.Len()));
		return FString::ChrN(Len - R.Len(), TEXT('*')) + R;
	}
}

void SFNDuelTray::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Session = InArgs._Session;
	ServerNowMs = InArgs._ServerNowMs;
	OnSend = InArgs._OnSend;
	OnTyping = InArgs._OnTyping;
	const FLinearColor Ink(0.94f, 0.9f, 0.82f);

	auto SideColumn = [this, Ink](const FString& Side)
	{
		const bool bA = Side == TEXT("a");
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(bA ? HAlign_Left : HAlign_Right)
			[
				SNew(STextBlock).Font(Type::Font(15, TEXT("Bold"))).ColorAndOpacity(Ink)
				.Text_Lambda([this, Side]() { return NameText(Side); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 4.f)
			[
				SNew(SBox).HeightOverride(10.f)
				[
					SNew(SProgressBar)
					.BarFillType(bA ? EProgressBarFillType::LeftToRight : EProgressBarFillType::RightToLeft)
					.FillColorAndOpacity(bA ? FLinearColor(0.95f, 0.45f, 0.25f) : FLinearColor(0.35f, 0.65f, 1.f))
					.Percent_Lambda([this, Side]() { return HpFrac(Side); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(bA ? HAlign_Left : HAlign_Right)
			[
				SNew(STextBlock).Font(Type::Font(18, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.86f, 0.5f))
				.Text_Lambda([this, Side]() { return SideText(Side); })
			];
	};

	ChildSlot
	[
		SNew(SBox).WidthOverride(820.f)
		[
			SNew(SBorder)
			.BorderImage(FN7Wood::StoneBrush())
			.Padding(FMargin(26.f, 16.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ SideColumn(TEXT("a")) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(16.f, 2.f)
					[
						SNew(STextBlock).Font(Type::Font(12, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.8f, 0.76f, 0.68f))
						.Justification(ETextJustify::Center)
						.Text_Lambda([this]() { return MidText(); })
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[ SideColumn(TEXT("b")) ]
				]
				// Your line (the two duellists only): masked like a password; Enter strikes.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					SAssignNew(Input, SEditableTextBox)
					.IsPassword(true)
					.Font(Type::Font(16, TEXT("Regular")))
					.HintText(LOCTEXT("TrayHint", "Type your move, then Enter to strike (your words decide the blow)"))
					.Visibility_Lambda([this]() { return IsMine() ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnTextChanged_Lambda([this](const FText& T)
					{
						FString S = T.ToString().Left(120);
						if (S.Len() > MyText.Len()) MyTypedAt = FPlatformTime::Seconds();
						MyText = S;
						if (const TSharedPtr<FFNDuelSession> Ses = Session.Get())
						{
							FFNDuelSession::FSideLine& L = Ses->Line(Ses->MySide);
							L.Len = MyText.Len();
						}
						OnTyping.ExecuteIfBound(MyText);
					})
					.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type How)
					{
						if (How != ETextCommit::OnEnter) return;
						const FString Line = T.ToString().TrimStartAndEnd().Left(120);
						if (!Line.IsEmpty()) OnSend.ExecuteIfBound(Line);
						ClearInput();
						FocusInput();   // keep typing: the next move
					})
				]
				// The typing window (it fills over the panel; LOCKED after the marker) and the duel's 30 s.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SBox).HeightOverride(8.f)
						[
							SNew(SProgressBar)
							.FillColorAndOpacity(FLinearColor(1.f, 0.78f, 0.35f))
							.Percent_Lambda([this]() { return WindowFrac(); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(Type::Font(12, TEXT("Bold"))).ColorAndOpacity(Ink)
						.Text_Lambda([this]() { return WindowText(); })
					]
				]
				// The latest blows: who, what, the d20, the damage, the flavour.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Type::Font(12, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.86f, 0.84f, 0.8f))
					.WrapTextAt(760.f)
					.Text_Lambda([this]() { return LogText(); })
				]
			]
		]
	];
}

bool SFNDuelTray::IsMine() const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	return S.IsValid() && !S->MySide.IsEmpty() && !S->bOver;
}

void SFNDuelTray::FocusInput()
{
	if (Input.IsValid() && IsMine()) FSlateApplication::Get().SetKeyboardFocus(Input, EFocusCause::SetDirectly);
}

bool SFNDuelTray::HasInputFocus() const
{
	return Input.IsValid() && Input->HasKeyboardFocus();
}

void SFNDuelTray::ClearInput()
{
	MyText.Reset();
	MyTypedAt = 0.0;
	if (Input.IsValid()) Input->SetText(FText::GetEmpty());
}

FText SFNDuelTray::NameText(const FString& Side) const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return FText::GetEmpty();
	const bool bA = Side == TEXT("a");
	const FString Name = bA ? S->AName : S->BName;
	const float Hp = bA ? S->HpA : S->HpB;
	const bool bMe = S->MySide == Side;
	return FText::FromString(FString::Printf(TEXT("%s%s  %.1f"), bMe ? TEXT("YOU: ") : TEXT(""), *Name, Hp));
}

TOptional<float> SFNDuelTray::HpFrac(const FString& Side) const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return 0.f;
	const bool bA = Side == TEXT("a");
	return FMath::Clamp((bA ? S->HpA : S->HpB) / FMath::Max(1.f, bA ? S->MaxA : S->MaxB), 0.f, 1.f);
}

FText SFNDuelTray::SideText(const FString& Side) const
{
	using namespace FN7Tray;
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return FText::GetEmpty();
	if (S->MySide == Side && S->bSending) return LOCTEXT("TraySending", "sending...");
	const FFNDuelSession::FSideLine& L = Side == TEXT("b") ? S->LineB : S->LineA;
	const double Now = FPlatformTime::Seconds();
	// A line just sent: its letters flash one after another, then it clears.
	if (!L.Sent.IsEmpty())
	{
		const double T = Now - L.SentAt, Dur = SentStep * L.Sent.Len() + SentHold;
		if (T < Dur)
		{
			FString Out;
			for (int32 i = 0; i < L.Sent.Len(); ++i)
			{
				const double From = i * SentStep;
				Out.AppendChar((T >= From && T < From + SentHold) ? L.Sent[i] : TEXT('*'));
			}
			return FText::FromString(Out);
		}
	}
	// Your own line, from your keyboard: stars, the newest letter readable for an instant.
	if (S->MySide == Side && !S->bOver)
	{
		if (MyText.IsEmpty()) return FText::FromString(TEXT("..."));
		const bool bFlash = Now - MyTypedAt < OwnFlash;
		return FText::FromString(Masked(MyText.Len(), bFlash ? MyText.Right(1) : FString()));
	}
	// The other side, as duel.typing told it.
	if (L.Len <= 0) return FText::FromString(TEXT("..."));
	const bool bTail = Now - L.FrameAt < TailFlash;
	return FText::FromString(Masked(L.Len, bTail ? L.Tail : FString()));
}

TOptional<float> SFNDuelTray::WindowFrac() const
{
	using namespace FN7Tray;
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return 0.f;
	const double Now = ServerNowMs.Get();
	for (const TPair<int32, TSharedPtr<FJsonObject>>& P : S->Panels)
	{
		const double Opens = Num(P.Value, TEXT("input_opens_ms"), 0.0), Closes = Num(P.Value, TEXT("input_closes_ms"), 0.0);
		if (Closes > Opens && Now >= Opens && Now < Closes) return (float)((Now - Opens) / (Closes - Opens));
	}
	if (S->DeadlineMs > 0.0) return (float)FMath::Clamp(1.0 - (S->DeadlineMs - Now) / 30000.0, 0.0, 1.0);
	return 1.f;
}

FText SFNDuelTray::WindowText() const
{
	using namespace FN7Tray;
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return FText::GetEmpty();
	if (S->bOver) return FText::FromString(S->Result);
	const double Now = ServerNowMs.Get();
	FString Window = TEXT("LOCKED");
	for (const TPair<int32, TSharedPtr<FJsonObject>>& P : S->Panels)
	{
		const double Opens = Num(P.Value, TEXT("input_opens_ms"), 0.0), Closes = Num(P.Value, TEXT("input_closes_ms"), 0.0);
		if (Closes > Opens && Now >= Opens && Now < Closes) { Window = FString::Printf(TEXT("TYPE  %.1f s"), (Closes - Now) / 1000.0); break; }
	}
	if (S->Panels.Num() == 0) Window = TEXT("TYPE");
	if (S->DeadlineMs > 0.0)
	{
		const int32 Left = FMath::Max(0, FMath::CeilToInt((S->DeadlineMs - Now) / 1000.0));
		return FText::FromString(FString::Printf(TEXT("%s    %d:%02d"), *Window, Left / 60, Left % 60));
	}
	return FText::FromString(Window);
}

FText SFNDuelTray::LogText() const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid() || S->Log.Num() == 0) return LOCTEXT("TrayNoBlows", "No blows yet: the first words strike first.");
	return FText::FromString(FString::Join(S->Log, TEXT("\n")));
}

FText SFNDuelTray::MidText() const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return FText::GetEmpty();
	const FString Init = S->Initiative == TEXT("a") ? S->AName : S->Initiative == TEXT("b") ? S->BName : FString();
	FString Out = TEXT("DUEL");
	if (!S->Range.IsEmpty()) Out += TEXT("\n") + S->Range.ToUpper();
	if (!Init.IsEmpty()) Out += TEXT("\ninitiative: ") + Init;
	return FText::FromString(Out);
}

#undef LOCTEXT_NAMESPACE
