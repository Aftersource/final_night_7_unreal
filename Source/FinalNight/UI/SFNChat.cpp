#include "SFNChat.h"

#include "FNHudPrefs.h"
#include "FNStyle.h"
#include "FNWood.h"
#include "SFNScroll.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FN7Chat"

namespace FN7ChatLook
{
	static constexpr int32 Keep = 50;
	static constexpr float TickerBelow = 96.f;   // less free height than this: a one-line ticker
	static const TCHAR* Names[7] = { TEXT(""), TEXT("parchment strip"), TEXT("stone tablet"), TEXT("bubbles over the rim"), TEXT("dark glass"), TEXT("6-fan"), TEXT("bubbles by the seats") };
	static const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	static const FSlateBrush* Bubble() { static const FSlateRoundedBoxBrush B(FLinearColor::White, 10.f); return &B; }
	static const FSlateBrush* Card() { static const FSlateRoundedBoxBrush B(FLinearColor(0.93f, 0.87f, 0.72f), 6.f, FLinearColor(0.35f, 0.24f, 0.12f), 1.5f); return &B; }
	static bool IsList(int32 V) { return V == 1 || V == 2 || V == 4; }
	static bool IsFloat(int32 V) { return V == 3 || V == 6; }
	/** Ink and name colours for a look (names in the aura colour, darkened on the light looks). */
	static FLinearColor TextColor(int32 V) { return V == 1 || V == 5 ? FN7Wood::Ink() : V == 2 ? FLinearColor(0.012f, 0.011f, 0.01f) : FN7Style::Color::Text(); }   // stone: near-black, cut into it
	static FLinearColor NameColor(int32 V, const FLinearColor& Aura)
	{
		if (V == 2) return FLinearColor(Aura.R * 0.3f, Aura.G * 0.3f, Aura.B * 0.3f, 1.f);   // on the pale stone: deep
		return (V == 1 || V == 5) ? FLinearColor(Aura.R * 0.55f, Aura.G * 0.55f, Aura.B * 0.55f, 1.f) : Aura;
	}
}

void SFNChat::Construct(const FArguments& InArgs)
{
	FreeHeight = InArgs._FreeHeight;
	Width = InArgs._Width;
	Variant = FMath::Clamp(FN7HudPrefs::GetInt(TEXT("chat_variant"), 4), 1, 6);
	Input = SNew(SEditableTextBox)
		.Style(&FN7Style::TextBox())
		.Font(FN7Style::Type::Font(12))
		.HintText(LOCTEXT("Hint", "Say something to the room. Enter sends, Esc leaves."))
		.ClearKeyboardFocusOnCommit(false)
		.SelectAllTextWhenFocused(false)
		.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type How)
		{
			if (How != ETextCommit::OnEnter) return;
			const FString S = T.ToString().TrimStartAndEnd();
			if (!S.IsEmpty() && OnSend) OnSend(S.Left(240));
			Input->SetText(FText::GetEmpty());
			FSlateApplication::Get().SetKeyboardFocus(Input, EFocusCause::SetDirectly);   // ready for the next line
		});
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(Width)
		.MaxDesiredHeight_Lambda([this]() { return FMath::Max(FreeHeight.Get(400.f), 34.f); })
		[
			SAssignNew(Body, SBox)
		]
	];
	bTicker = FreeHeight.Get(400.f) < FN7ChatLook::TickerBelow;
	Rebuild();
}

void SFNChat::SetVariant(int32 V)
{
	V = FMath::Clamp(V, 1, 6);
	if (V == Variant) return;
	Variant = V;
	FN7HudPrefs::SetInt(TEXT("chat_variant"), V);
	UE_LOG(LogTemp, Display, TEXT("FN7 chat: variant %d (%s)"), V, FN7ChatLook::Names[V]);
	Rebuild();
}

bool SFNChat::HasChatFocus() const { return Input.IsValid() && Input->HasKeyboardFocus(); }

void SFNChat::LeaveFocus()
{
	if (HasChatFocus() && FSlateApplication::IsInitialized()) FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
}

void SFNChat::Add(const FFNChatMsg& M)
{
	Msgs.Add(M);
	Msgs.Last().Seen = FPlatformTime::Seconds();
	const bool bTrim = Msgs.Num() > FN7ChatLook::Keep;
	if (bTrim) Msgs.RemoveAt(0);
	if (FN7ChatLook::IsList(Variant) && !bTicker && List.IsValid())
	{
		// The list keeps its rows and grows at the bottom (the line you are typing is never rebuilt).
		List->AddSlot()[Row(Msgs.Last())];
		if (bTrim && List->GetChildren()->Num() > 0) List->RemoveSlot(List->GetChildren()->GetChildAt(0));
		List->ScrollToEnd();
	}
	else RefreshArea();
}

void SFNChat::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Folding to a ticker and back has a little hysteresis, so a scroll rolling past the line doesn't flicker it.
	const float Free = FreeHeight.Get(400.f);
	const bool bWant = bTicker ? Free < FN7ChatLook::TickerBelow + 12.f : Free < FN7ChatLook::TickerBelow;
	if (bWant != bTicker) { bTicker = bWant; Rebuild(); }
	const float Goal = (Variant == 5 && IsHovered()) ? 1.f : 0.f;
	FanT = FMath::FInterpTo(FanT, Goal, InDeltaTime, 9.f);
}

TSharedRef<SWidget> SFNChat::Buttons() const
{
	// Six little switches for Benito: the look changes live and is remembered.
	TSharedRef<SHorizontalBox> B = SNew(SHorizontalBox);
	for (int32 V = 1; V <= 6; ++V)
	{
		B->AddSlot().AutoWidth().Padding(1.f, 0.f)
		[
			SNew(SBox).WidthOverride(20.f).HeightOverride(17.f)
			[
				SNew(SButton)
				.ButtonStyle(&FN7Style::PillButton())
				.ContentPadding(FMargin(0.f))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ButtonColorAndOpacity(V == Variant ? FN7Style::Color::Accent() : FLinearColor(1.f, 1.f, 1.f, 0.55f))
				.ToolTipText(FText::FromString(FN7ChatLook::Names[V]))
				.OnClicked_Lambda([this, V]() { const_cast<SFNChat*>(this)->SetVariant(V); return FReply::Handled(); })
				[
					SNew(STextBlock).Font(FN7Style::Type::Font(9, TEXT("Bold"))).ColorAndOpacity(V == Variant ? FLinearColor(0.05f, 0.03f, 0.02f) : FN7Style::Color::Text()).Text(FText::AsNumber(V))
				]
			]
		];
	}
	return B;
}

TSharedRef<SWidget> SFNChat::Row(const FFNChatMsg& M) const
{
	using namespace FN7ChatLook;
	const FLinearColor Ink = TextColor(Variant);
	const bool bChisel = Variant == 2;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 1.f, 6.f, 1.f)
		[
			SNew(STextBlock)
			.Font(FN7Style::Type::Font(11, TEXT("Bold")))
			.ColorAndOpacity(NameColor(Variant, M.Color))
			.ShadowOffset(bChisel ? FVector2D(0.f, 1.f) : FVector2D::ZeroVector)
			.ShadowColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.5f))
			.Text(FText::FromString(M.Name.Left(20)))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top).Padding(0.f, 1.f)
		[
			SNew(STextBlock)
			.Font(FN7Style::Type::Font(11))
			.ColorAndOpacity(Ink)
			.ShadowOffset(bChisel ? FVector2D(0.f, 1.f) : FVector2D::ZeroVector)
			.ShadowColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.5f))
			.AutoWrapText(true)
			.Text(FText::FromString(M.Text))
		];
}

TSharedRef<SWidget> SFNChat::TickerLine() const
{
	using namespace FN7ChatLook;
	const FFNChatMsg* M = Msgs.Num() ? &Msgs.Last() : nullptr;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock).Font(FN7Style::Type::Font(11, TEXT("Bold"))).ColorAndOpacity(M ? NameColor(Variant, M->Color) : FN7Style::Color::TextDim())
			.Text(M ? FText::FromString(M->Name.Left(16)) : LOCTEXT("Chat", "CHAT"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(FN7Style::Type::Font(11)).ColorAndOpacity(TextColor(Variant)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(M ? FText::FromString(M->Text) : LOCTEXT("Quiet", "no one has spoken yet"))
		];
}

TSharedRef<SWidget> SFNChat::Fan()
{
	// A fanned deck of the six newest messages, one card each, newest in front; hover and it fans wide open.
	using namespace FN7ChatLook;
	TSharedRef<SOverlay> O = SNew(SOverlay);
	const int32 N = FMath::Min(6, Msgs.Num());
	for (int32 k = 0; k < N; ++k)
	{
		const FFNChatMsg& M = Msgs[Msgs.Num() - N + k];
		const float C = k - (N - 1) * 0.5f;
		O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		[
			SNew(SBox).WidthOverride(150.f).HeightOverride(86.f)
			.RenderTransformPivot(FVector2D(0.5f, 1.f))
			.RenderTransform_Lambda([this, C]()
			{
				const float A = C * (5.f + 7.f * FanT);
				return FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(A)), FVector2D(C * (26.f + 52.f * FanT), FMath::Abs(C) * (3.f + 5.f * FanT) - 10.f * FanT - 18.f));   // the deck sits clear of the typing line
			})
			[
				SNew(SBorder).BorderImage(Card()).Padding(FMargin(8.f, 6.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(FN7Style::Type::Font(10, TEXT("Bold"))).ColorAndOpacity(NameColor(5, M.Color)).Text(FText::FromString(M.Name.Left(18)))
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(STextBlock).Font(FN7Style::Type::Font(10)).ColorAndOpacity(FN7Wood::Ink()).AutoWrapText(true).Text(FText::FromString(M.Text.Left(90)))
					]
				]
			]
		];
	}
	if (N == 0) O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(STextBlock).Font(FN7Style::Type::Font(10)).ColorAndOpacity(FN7Style::Color::TextDim()).Text(LOCTEXT("NoCards", "the deck is empty: say something"))];
	return SNew(SBox).HeightOverride(132.f).Padding(FMargin(0.f, 10.f, 0.f, 0.f)).Clipping(EWidgetClipping::ClipToBounds)[O];
}

TSharedRef<SWidget> SFNChat::Frame(TSharedRef<SWidget> Inner, bool bSlim) const
{
	// The look's material around the chat.
	using namespace FN7ChatLook;
	const FMargin Pad = bSlim ? FMargin(10.f, 4.f) : FMargin(14.f, 8.f);
	switch (Variant)
	{
	case 1: return SNew(SFNScroll).bStartOpen(true).RodHeight(bSlim ? 10.f : 14.f).ContentPadding(bSlim ? FMargin(18.f, 3.f) : FMargin(22.f, 6.f))[Inner];
	case 2:
		// A pale tablet (the stone texture under a chalky wash) so the cut letters read dark.
		// (nested borders size to their content; an image would add the texture's own size)
		return SNew(SBorder).BorderImage(FN7Wood::StoneBrush()).Padding(0.f)
			[
				SNew(SBorder).BorderImage(Bubble()).BorderBackgroundColor(FLinearColor(0.93f, 0.91f, 0.86f, 0.55f)).Padding(Pad)[Inner]
			];
	case 5: return SNew(SBorder).BorderImage(FN7Style::Brush::Panel()).BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.6f)).Padding(Pad)[Inner];
	default: return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f)).Padding(Pad)[Inner];   // 3, 4, 6: dark glass
	}
}

void SFNChat::RefreshArea()
{
	using namespace FN7ChatLook;
	if (!Area.IsValid()) return;
	if (bTicker) Area->SetContent(TickerLine());
	else if (Variant == 5) Area->SetContent(Fan());
}

void SFNChat::Rebuild()
{
	using namespace FN7ChatLook;
	BuiltVariant = Variant;
	bBuiltTicker = bTicker;
	List.Reset();
	Input->SetStyle(Variant == 1 ? &FN7Wood::InkTextBox() : &FN7Style::TextBox());
	SAssignNew(Area, SBox);
	TSharedRef<SWidget> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).TextStyle(&FN7Style::Type::Label()).ColorAndOpacity((Variant == 1 || Variant == 2) ? FN7Wood::RedInk() : FN7Style::Color::TextDim())
			.Text(LOCTEXT("ChatTitle", "CHAT"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Buttons()];
	if (bTicker)
	{
		// One line: the newest message, a short box to type in, the switches.
		Area->SetContent(TickerLine());
		Body->SetContent(Frame(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Area.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)[SNew(SBox).WidthOverride(150.f)[Input.ToSharedRef()]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Buttons()], true));
		return;
	}
	if (IsFloat(Variant))
	{
		// The bubbles float over the table (SFNChatFloat); docked, only the line you type and the switches.
		Body->SetContent(Frame(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[Header]
			+ SVerticalBox::Slot().AutoHeight()[Input.ToSharedRef()], true));
		return;
	}
	if (Variant == 5) Area->SetContent(Fan());
	else
	{
		SAssignNew(List, SScrollBox).ScrollBarThickness(FVector2D(4.f, 4.f));
		for (const FFNChatMsg& M : Msgs) List->AddSlot()[Row(M)];
		List->ScrollToEnd();
		Area->SetContent(List.ToSharedRef());
	}
	Body->SetContent(Frame(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[Header]
		+ SVerticalBox::Slot().FillHeight(1.f)[Area.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[Input.ToSharedRef()], false));
}

// --- the floating looks ---------------------------------------------------------------------------------------------

void SFNChatFloat::Construct(const FArguments&, TSharedPtr<SFNChat> InChat, TFunction<bool(int32, FVector2D&)> InAnchor)
{
	Chat = InChat;
	Anchor = MoveTemp(InAnchor);
	// Repaint every frame while bubbles are up (they fade and follow the figures).
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Continue;
	}));
}

int32 SFNChatFloat::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle&, bool) const
{
	using namespace FN7ChatLook;
	const TSharedPtr<SFNChat> C = Chat.Pin();
	if (!C.IsValid() || !IsFloat(C->GetVariant()) || !FSlateApplication::IsInitialized()) return LayerId;
	const bool bSeats = C->GetVariant() == 6;
	const double Now = FPlatformTime::Seconds();
	const FVector2D Size = G.GetLocalSize();
	const FSlateFontInfo Font = FN7Style::Type::Font(12), Bold = FN7Style::Type::Font(12, TEXT("Bold"));
	const TSharedRef<FSlateFontMeasure> Fm = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const TArray<FFNChatMsg>& Ms = C->Messages();
	TMap<int32, float> StackBySeat;     // look 6: each speaker's bubbles stack upward over its figure
	float RimY = Size.Y * 0.78f;        // look 3: the newest just over the table's near rim, older ones above
	for (int32 i = Ms.Num() - 1; i >= 0 && i >= Ms.Num() - 6; --i)
	{
		const FFNChatMsg& M = Ms[i];
		const float Age = (float)(Now - M.Seen);
		if (Age > 9.f) continue;
		const float A = FMath::Clamp(Age < 0.25f ? Age / 0.25f : 1.f - (Age - 6.f) / 3.f, 0.f, 1.f);
		const FString Name = M.Name.Left(18) + TEXT("  ");
		const FString Text = M.Text.Left(90);
		const FVector2D Wn = Fm->Measure(Name, Bold), Wt = Fm->Measure(Text, Font);
		const float W = FMath::Min(560.f, (float)(Wn.X + Wt.X) + 22.f), H = FMath::Max((float)Wt.Y, (float)Wn.Y) + 12.f;
		FVector2D At;
		if (bSeats)
		{
			FVector2D Px;
			if (Anchor && Anchor(M.Seat, Px)) At = Px / FMath::Max(0.01f, G.Scale) + FVector2D(-W * 0.5f, -70.f);
			else At = FVector2D(Size.X * 0.5f - W * 0.5f, Size.Y * 0.62f);   // no figure on the table: by the rim
			float& Up = StackBySeat.FindOrAdd(M.Seat);
			At.Y -= Up;
			Up += H + 6.f;
		}
		else { At = FVector2D(Size.X * 0.5f - W * 0.5f, RimY - H); RimY -= H + 6.f; }
		At.X = FMath::Clamp(At.X, 8.f, Size.X - W - 8.f);
		At.Y = FMath::Clamp(At.Y, 8.f, Size.Y - H - 8.f);
		const FLinearColor Paper = bSeats ? FLinearColor(0.05f, 0.05f, 0.08f, 0.82f * A) : FLinearColor(0.96f, 0.93f, 0.85f, 0.94f * A);
		const FLinearColor Ink = bSeats ? FLinearColor(0.93f, 0.9f, 0.84f, A) : FLinearColor(0.12f, 0.08f, 0.05f, A);
		FLinearColor NameC = bSeats ? M.Color : M.Color * 0.55f;
		NameC.A = A;
		FSlateDrawElement::MakeBox(Out, LayerId, G.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(At))), Bubble(), ESlateDrawEffect::None, Paper);
		FSlateDrawElement::MakeText(Out, LayerId + 1, G.ToPaintGeometry(FVector2f((float)Wn.X + 2.f, H), FSlateLayoutTransform(FVector2f(At + FVector2D(11.f, 6.f)))), Name, Bold, ESlateDrawEffect::None, NameC);
		FSlateDrawElement::MakeText(Out, LayerId + 1, G.ToPaintGeometry(FVector2f((float)Wt.X + 2.f, H), FSlateLayoutTransform(FVector2f(At + FVector2D(11.f + Wn.X, 6.f)))), Text, Font, ESlateDrawEffect::None, Ink);
	}
	return LayerId + 2;
}

#undef LOCTEXT_NAMESPACE
