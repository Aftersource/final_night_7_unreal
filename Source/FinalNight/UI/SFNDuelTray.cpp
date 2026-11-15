#include "SFNDuelTray.h"

#include "FNStyle.h"
#include "FNWood.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FN7DuelTray"

namespace FN7Tray
{
	static constexpr double OwnFlash = 0.35;    // your newest letter stays readable this long
	static constexpr double TailFlash = 0.6;    // an opponent frame's tail stays readable this long
	static constexpr double SentStep = 0.035;   // a typed line, once sent, reveals one letter every SentStep...
	static constexpr double SentHold = 0.25;    // ...each readable this long
	static constexpr double TypeRate = 13.0;    // a line nobody saw typed (an AI's) types itself out at this many chars/s
	static constexpr double TypeHold = 0.3;     // each of its letters readable this long, then a star

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

	static const FSlateBrush* Solid() { return FCoreStyle::Get().GetBrush("GenericWhiteBox"); }
	static const FLinearColor Ink(0.95f, 0.91f, 0.82f);
	static const FLinearColor Gold(1.f, 0.84f, 0.48f);
	static const FLinearColor Gilt(0.62f, 0.43f, 0.15f, 0.95f);
	static const FLinearColor Night(0.045f, 0.03f, 0.02f, 0.9f);

	/** A dark carved plate with a thin gilt line, the box's colours. */
	static TSharedRef<SWidget> Plate(const TSharedRef<SWidget>& Content, float Width)
	{
		return SNew(SBox).WidthOverride(Width)
		[
			SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(Gilt).Padding(2.f)
			[
				SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(Night).Padding(FMargin(22.f, 12.f))
				[
					Content
				]
			]
		];
	}
}

FString SFNDuelTray::TopSide() const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	return (S.IsValid() && S->MySide == TEXT("b")) ? TEXT("a") : TEXT("b");   // the opponent; a watcher sees b on top
}

FString SFNDuelTray::BottomSide() const
{
	return TopSide() == TEXT("a") ? TEXT("b") : TEXT("a");
}

void SFNDuelTray::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	using namespace FN7Tray;
	Session = InArgs._Session;
	ServerNowMs = InArgs._ServerNowMs;
	CaptionText = InArgs._Caption;
	OnSend = InArgs._OnSend;
	OnTyping = InArgs._OnTyping;

	auto HeaderRow = [this](bool bTop)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
			[
				SNew(STextBlock).Font(Type::Font(17, TEXT("Bold"))).ColorAndOpacity(Ink)
				.Text_Lambda([this, bTop]() { return NameText(bTop ? TopSide() : BottomSide()); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SBox).HeightOverride(10.f)
				[
					SNew(SProgressBar)
					.FillColorAndOpacity_Lambda([this, bTop]() { return (bTop ? TopSide() : BottomSide()) == TEXT("a") ? FLinearColor(0.95f, 0.45f, 0.25f) : FLinearColor(0.35f, 0.65f, 1.f); })
					.Percent_Lambda([this, bTop]() { return HpFrac(bTop ? TopSide() : BottomSide()); })
				]
			];
	};

	ChildSlot
	[
		SNew(SOverlay)
		// TOP: your opponent's line as they type it (stars, the newest letters readable for an instant).
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 56.f, 0.f, 0.f))
		[
			Plate(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ HeaderRow(true) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Type::Font(34, TEXT("Bold"))).ColorAndOpacity(Gold)
					.Text_Lambda([this]() { return SideText(TopSide()); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Type::Font(11, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.75f, 0.7f, 0.62f))
					.Text_Lambda([this]() { return StatusText(TopSide()); })
				], 820.f)
		]
		// The caption over the box: the panel's line (who hit whom, and the wild reason it worked).
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 250.f))
		[
			SNew(SBox).MaxDesiredWidth(900.f)
			.Visibility_Lambda([this]() { return CaptionNow().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			[
				SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(FLinearColor(0.12f, 0.08f, 0.04f, 1.f)).Padding(2.f)
				[
					SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(FLinearColor(0.93f, 0.87f, 0.7f, 0.97f)).Padding(FMargin(18.f, 8.f))
					[
						SNew(STextBlock).Font(Type::Font(16, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.12f, 0.08f, 0.05f))
						.WrapTextAt(860.f).Justification(ETextJustify::Center)
						.Text_Lambda([this]() { return FText::FromString(CaptionNow()); })
					]
				]
			]
		]
		// BOTTOM: you (your line, masked like theirs; Enter strikes), the typing window and the latest blow.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 18.f))
		[
			Plate(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ HeaderRow(false) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 4.f)
				[
					SNew(STextBlock).Font(Type::Font(26, TEXT("Bold"))).ColorAndOpacity(Gold)
					.Text_Lambda([this]() { return SideText(BottomSide()); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SAssignNew(Input, SEditableTextBox)
					.IsPassword(true)
					.Font(Type::Font(16, TEXT("Regular")))
					.HintText(LOCTEXT("TrayHint", "Type your move, Enter to strike: your words decide the blow"))
					.Visibility_Lambda([this]() { return IsMine() ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnTextChanged_Lambda([this](const FText& T)
					{
						FString S = T.ToString().Left(120);
						if (S.Len() > MyText.Len()) MyTypedAt = FPlatformTime::Seconds();
						MyText = S;
						if (const TSharedPtr<FFNDuelSession> Ses = Session.Get()) Ses->Line(Ses->MySide).Len = MyText.Len();
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
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SBox).HeightOverride(8.f)
						[
							SNew(SProgressBar).FillColorAndOpacity(FLinearColor(1.f, 0.78f, 0.35f))
							.Percent_Lambda([this]() { return WindowFrac(); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(Type::Font(13, TEXT("Bold"))).ColorAndOpacity(Ink)
						.Text_Lambda([this]() { return WindowText(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Type::Font(12, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.85f, 0.82f, 0.76f))
					.WrapTextAt(770.f)
					.Text_Lambda([this]() { return LogText(); })
				], 820.f)
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
	return FText::FromString(FString::Printf(TEXT("%s%s  %.1f"), S->MySide == Side ? TEXT("YOU: ") : TEXT(""), bA ? *S->AName : *S->BName, bA ? S->HpA : S->HpB));
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
	if (!L.Sent.IsEmpty())
	{
		const double T = Now - L.SentAt;
		const int32 N = L.Sent.Len();
		if (L.bTypeOut)
		{
			// Nobody saw it typed (an AI): it types itself out, *****p, then holds a moment.
			if (T < N / TypeRate + 0.9)
			{
				const int32 Shown = FMath::Min(N, FMath::FloorToInt(T * TypeRate) + 1);
				FString Out;
				for (int32 i = 0; i < Shown; ++i) Out.AppendChar((T - i / TypeRate) < TypeHold ? L.Sent[i] : TEXT('*'));
				return FText::FromString(Out);
			}
		}
		else if (T < SentStep * N + SentHold)
		{
			FString Out;
			for (int32 i = 0; i < N; ++i)
			{
				const double From = i * SentStep;
				Out.AppendChar((T >= From && T < From + SentHold) ? L.Sent[i] : TEXT('*'));
			}
			return FText::FromString(Out);
		}
	}
	if (S->MySide == Side && !S->bOver)
	{
		if (MyText.IsEmpty()) return FText::FromString(TEXT("..."));
		return FText::FromString(Masked(MyText.Len(), Now - MyTypedAt < OwnFlash ? MyText.Right(1) : FString()));
	}
	if (L.Len <= 0) return FText::FromString(TEXT("..."));
	return FText::FromString(Masked(L.Len, Now - L.FrameAt < TailFlash ? L.Tail : FString()));
}

FText SFNDuelTray::StatusText(const FString& Side) const
{
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	if (!S.IsValid()) return FText::GetEmpty();
	if (S->bOver) return FText::FromString(S->Result);
	const FFNDuelSession::FSideLine& L = Side == TEXT("b") ? S->LineB : S->LineA;
	const double Now = FPlatformTime::Seconds();
	if (!L.Sent.IsEmpty() && Now - L.SentAt < 2.5) return LOCTEXT("TrayStruck", "strikes!");
	if (L.Len > 0 && Now - L.FrameAt < 1.5) return LOCTEXT("TrayTyping", "typing...");
	if (L.Len > 0 && L.FrameAt > 0.0 && Now - L.FrameAt > 10.0) return LOCTEXT("TrayGone", "gone quiet");
	return LOCTEXT("TrayWatch", "read their move as they type it");
}

FString SFNDuelTray::CaptionNow() const
{
	const FString C = CaptionText.Get();
	if (!C.IsEmpty()) return C;
	const TSharedPtr<FFNDuelSession> S = Session.Get();
	return (S.IsValid() && S->bOver) ? S->Result : FString();
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
	FString Window = S->Panels.Num() == 0 ? FString(TEXT("TYPE")) : FString(TEXT("LOCKED"));
	for (const TPair<int32, TSharedPtr<FJsonObject>>& P : S->Panels)
	{
		const double Opens = Num(P.Value, TEXT("input_opens_ms"), 0.0), Closes = Num(P.Value, TEXT("input_closes_ms"), 0.0);
		if (Closes > Opens && Now >= Opens && Now < Closes) { Window = FString::Printf(TEXT("TYPE  %.1f s"), (Closes - Now) / 1000.0); break; }
	}
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
	return FText::FromString(S->Log.Last());
}

#undef LOCTEXT_NAMESPACE
