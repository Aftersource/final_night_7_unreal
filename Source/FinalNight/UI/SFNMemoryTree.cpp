#include "SFNMemoryTree.h"

#include "FNLooks.h"
#include "FNStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FN7MemoryTree"

namespace FN7Tree
{
	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O.IsValid()) O->TryGetStringField(K, V); return V; }
	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D = 0.0) { double V = D; if (O.IsValid()) O->TryGetNumberField(K, V); return V; }
	static bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { bool V = false; if (O.IsValid()) O->TryGetBoolField(K, V); return V; }
	static TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O.IsValid() && O->TryGetObjectField(K, V) && V ? *V : nullptr; }
	static const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>* V = nullptr; return O.IsValid() && O->TryGetArrayField(K, V) ? V : nullptr; }
	static FString Plain(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
		case EJson::String: return V->AsString();
		case EJson::Number: { const double D = V->AsNumber(); return FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)) ? FString::Printf(TEXT("%lld"), (long long)D) : FString::Printf(TEXT("%.2f"), D); }
		case EJson::Boolean: return V->AsBool() ? TEXT("yes") : TEXT("no");
		default: return FString();
		}
	}
	static const FSlateBrush* Dot() { static const FSlateRoundedBoxBrush B(FLinearColor::White, 3.f); return &B; }
	/** A true circle of radius R (a rounded box whose corner is exactly half its size; cached per pixel radius). */
	static const FSlateBrush* Circle(float R, bool bRing = false)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Fill, Rim;
		const int32 K = FMath::Clamp(FMath::RoundToInt(R), 1, 80);
		TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>>& M = bRing ? Rim : Fill;
		if (TSharedPtr<FSlateRoundedBoxBrush>* B = M.Find(K)) return B->Get();
		TSharedPtr<FSlateRoundedBoxBrush> N = bRing ? MakeShared<FSlateRoundedBoxBrush>(FLinearColor::Transparent, (float)K, FLinearColor::White, 2.f, FVector2D(K * 2.f, K * 2.f))
			: MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, (float)K, FVector2D(K * 2.f, K * 2.f));
		M.Add(K, N);
		return N.Get();
	}
	static const FSlateBrush* Card() { static const FSlateRoundedBoxBrush B(FLinearColor(0.02f, 0.03f, 0.08f, 0.82f), 8.f, FLinearColor(0.85f, 0.72f, 0.4f, 0.45f), 1.f); return &B; }
	static const FSlateBrush* Lit() { static const FSlateRoundedBoxBrush B(FLinearColor(0.16f, 0.11f, 0.03f, 0.92f), 8.f, FLinearColor(1.f, 0.8f, 0.36f, 0.95f), 1.5f); return &B; }
	/** Density of detail: -5.5 (the planet) blue ... +6 (a 3 m hex) gold-white, one ramp for every tier. */
	static FLinearColor Ramp(double L)
	{
		const float T = FMath::Clamp((float)((L + 5.5) / 11.5), 0.f, 1.f);
		const FLinearColor A(0.22f, 0.42f, 1.f), B(0.55f, 0.35f, 0.95f), C(1.f, 0.55f, 0.25f), D(1.f, 0.93f, 0.7f);
		return T < 0.33f ? FMath::Lerp(A, B, T / 0.33f) : T < 0.66f ? FMath::Lerp(B, C, (T - 0.33f) / 0.33f) : FMath::Lerp(C, D, (T - 0.66f) / 0.34f);
	}
	static const FLinearColor Gold(1.f, 0.8f, 0.36f), Ink(0.9f, 0.93f, 1.f), Dim(0.62f, 0.66f, 0.8f);
}

/** The tiers, the nodes and the roll-up edges, painted; a click picks a node. */
class STreeCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(STreeCanvas) {}
	SLATE_END_ARGS()
	void Construct(const FArguments&, SFNMemoryTree* InOwner) { Owner = InOwner; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(600.f, 520.f); }

	static float Radius(const SFNMemoryTree::FNode& N) { return FMath::Clamp(5.f + 6.f * FMath::LogX(10.f, (float)N.Facts + 1.f), 5.f, N.bCollapsed ? 13.f : 30.f); }

	TSet<FString> Lit() const
	{
		TSet<FString> Out;
		const SFNMemoryTree* O = Owner;
		if (!O || O->Path < 0) return Out;
		const TArray<TSharedPtr<FJsonValue>>* Ps = FN7Tree::Arr(O->Data, TEXT("recall_paths"));
		if (!Ps || !Ps->IsValidIndex(O->Path)) return Out;
		const TArray<TSharedPtr<FJsonValue>>* H = FN7Tree::Arr((*Ps)[O->Path]->AsObject(), TEXT("highlight"));
		if (!H) return Out;
		// lit one tier at a time from the world down (0.35 s each): the recall walks the tree
		const int32 Shown = FMath::FloorToInt((float)(FPlatformTime::Seconds() - O->PathAt) / 0.35f) + 1;
		for (int32 i = 0; i < H->Num() && i < Shown; ++i) Out.Add((*H)[i]->AsString());
		return Out;
	}

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		using namespace FN7Tree;
		if (!Owner) return Layer;
		const FVector2D S = G.GetLocalSize();
		const TArray<SFNMemoryTree::FNode>& Ns = Owner->Nodes;
		const TSharedRef<FSlateFontMeasure> Fm = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo Small = FN7Style::Type::Font(9), Label = FN7Style::Type::Font(10, TEXT("Bold"));
		auto Px = [&S](const FVector2D& U) { return FVector2D(U.X * S.X, U.Y * S.Y); };
		// the tier rows and their names
		const int32 Rows = 7;
		for (int32 L = 0; L < Rows; ++L)
		{
			const float Y = (0.06f + 0.88f * L / (Rows - 1)) * S.Y;
			TArray<FVector2f> Ln = { FVector2f(0.f, Y), FVector2f(S.X, Y) };
			FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Ln, ESlateDrawEffect::None, FLinearColor(0.6f, 0.65f, 1.f, 0.07f), true, 1.f);
			if (Owner->LevelNames.IsValidIndex(L))
				FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(200.f, 14.f), FSlateLayoutTransform(FVector2f(4.f, Y - 15.f))), Owner->LevelNames[L], Small, ESlateDrawEffect::None, FLinearColor(0.7f, 0.75f, 1.f, 0.45f));
		}
		const TSet<FString> LitIds = Lit();
		// the roll-up edges (child -> parent), thickness = the child's facts
		for (const SFNMemoryTree::FNode& N : Ns)
		{
			if (!Ns.IsValidIndex(N.Parent)) continue;
			const SFNMemoryTree::FNode& P = Ns[N.Parent];
			const bool bLit = LitIds.Contains(N.Id) && LitIds.Contains(P.Id);
			const float W = FMath::Clamp(0.6f + 1.2f * FMath::LogX(10.f, (float)N.Facts + 1.f), 0.6f, 5.f);
			const FLinearColor C = bLit ? Gold : N.bOnPath ? FLinearColor(1.f, 0.8f, 0.36f, 0.55f) : FLinearColor(0.6f, 0.7f, 1.f, N.bCollapsed ? 0.14f : 0.3f);
			TArray<FVector2f> E = { FVector2f(Px(N.At)), FVector2f(Px(P.At)) };
			FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), E, ESlateDrawEffect::None, C, true, bLit ? W + 1.5f : W);
		}
		// the nodes: glow (recency), the density fill, the rings for selected / lit, the labels
		for (int32 i = 0; i < Ns.Num(); ++i)
		{
			const SFNMemoryTree::FNode& N = Ns[i];
			const float R = Radius(N);
			const FVector2D C = Px(N.At);
			const float Age = (float)FMath::Max(0.0, Owner->Beat - N.LastBeat);
			const float Glow = FMath::Clamp(1.f - Age / 12.f, 0.f, 1.f) * (N.bCollapsed ? 0.35f : 1.f);
			const FLinearColor Fill = Ramp(N.Density) * FLinearColor(1.f, 1.f, 1.f, N.bCollapsed ? 0.45f : 0.95f);
			if (Glow > 0.f)
				for (int32 k = 3; k >= 1; --k)
				{
					const float Rg = R + 4.f * k;
					FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(FVector2f(Rg * 2.f, Rg * 2.f), FSlateLayoutTransform(FVector2f(C - FVector2D(Rg, Rg)))), Circle(Rg), ESlateDrawEffect::None,
						FLinearColor(Fill.R, Fill.G, Fill.B, 0.07f * Glow * (4 - k)));
				}
			FSlateDrawElement::MakeBox(Out, Layer + 4, G.ToPaintGeometry(FVector2f(R * 2.f, R * 2.f), FSlateLayoutTransform(FVector2f(C - FVector2D(R, R)))), Circle(R), ESlateDrawEffect::None, Fill);
			const bool bLit = LitIds.Contains(N.Id);
			if (i == Owner->Selected || bLit || N.bOnPath)
			{
				const float Rr = R + (i == Owner->Selected ? 4.f : 2.5f);
				FSlateDrawElement::MakeBox(Out, Layer + 5, G.ToPaintGeometry(FVector2f(Rr * 2.f, Rr * 2.f), FSlateLayoutTransform(FVector2f(C - FVector2D(Rr, Rr)))), Circle(Rr, true), ESlateDrawEffect::None,
					i == Owner->Selected ? FLinearColor::White : bLit ? Gold : FLinearColor(1.f, 0.8f, 0.36f, 0.5f));
			}
			const FString Count = FString::Printf(TEXT("%d"), (int32)N.Facts);
			const bool bNamed = !N.bCollapsed || bLit || i == Owner->Selected;   // a lit or picked node always says what it is
			const FString Text = bNamed ? FString::Printf(TEXT("%s  %s"), *N.Name.Left(22), *Count) : Count;
			const FSlateFontInfo& F = bNamed ? Label : Small;
			const FVector2D Ts = Fm->Measure(Text, F);
			FSlateDrawElement::MakeText(Out, Layer + 6, G.ToPaintGeometry(FVector2f(Ts), FSlateLayoutTransform(FVector2f(C + FVector2D(-Ts.X * 0.5f, R + 3.f)))), Text, F, ESlateDrawEffect::None,
				N.bCollapsed ? FLinearColor(0.7f, 0.75f, 0.95f, 0.6f) : (bLit || i == Owner->Selected) ? Gold : Ink);
		}
		return Layer + 7;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
	{
		if (!Owner) return FReply::Handled();
		const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition()), S = G.GetLocalSize();
		int32 Best = -1; float BestD = 1e9f;
		for (int32 i = 0; i < Owner->Nodes.Num(); ++i)
		{
			const FVector2D C(Owner->Nodes[i].At.X * S.X, Owner->Nodes[i].At.Y * S.Y);
			const float D = FVector2D::Distance(C, P);
			if (D < Radius(Owner->Nodes[i]) + 8.f && D < BestD) { BestD = D; Best = i; }
		}
		if (Best >= 0) Owner->Select(Best);
		return FReply::Handled();
	}

private:
	SFNMemoryTree* Owner = nullptr;
};

void SFNMemoryTree::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	using namespace FN7Tree;
	OnClose = InArgs._OnClose;
	auto Section = [](const FText& T) { return SNew(STextBlock).Font(Type::Font(10, TEXT("Bold"))).ColorAndOpacity(Gold).Text(T); };
	TSharedRef<SWidget> Inner =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).TextStyle(&Type::Display()).ColorAndOpacity(Ink).Text(LOCTEXT("Title", "TREE OF CONSEQUENCE")) ]
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Dim).AutoWrapText(true).Text(this, &SFNMemoryTree::HeaderLine) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SButton).ButtonStyle(&PillButton()).OnClicked_Lambda([this]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })
				[ SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("Close", "CLOSE  (Esc)")) ]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			// the tree
			+ SHorizontalBox::Slot().FillWidth(0.62f)
			[
				SNew(SBorder).BorderImage(Card()).Padding(6.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)[ SAssignNew(Canvas, STreeCanvas, this) ]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(Type::Font(9)).ColorAndOpacity(Dim).AutoWrapText(true)
						.Text(LOCTEXT("Legend", "size = facts remembered (log)  ·  glow = how recent  ·  colour = density of detail, blue (the planet) to gold-white (a 3 m hex)  ·  lines = roll-ups, thicker = more facts  ·  left: the DM's places, right: the society's minds  ·  click a node"))
					]
				]
			]
			// the card, the recall paths, the chain
			+ SHorizontalBox::Slot().FillWidth(0.38f).Padding(10.f, 0.f, 0.f, 0.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[ Section(LOCTEXT("NodeHead", "THE NODE")) ]
				+ SScrollBox::Slot().Padding(0.f, 4.f, 0.f, 10.f)[ SAssignNew(CardBox, SBox) ]
				+ SScrollBox::Slot()[ Section(LOCTEXT("PathsHead", "RECALL PATHS: how one fact is remembered")) ]
				+ SScrollBox::Slot().Padding(0.f, 4.f, 0.f, 6.f)[ SAssignNew(PathsBox, SBox) ]
				+ SScrollBox::Slot()[ SAssignNew(ChainBox, SBox) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBorder).BorderImage(Card()).Padding(FMargin(10.f, 6.f))
			[ SNew(STextBlock).Font(Type::Font(10)).ColorAndOpacity(Ink).AutoWrapText(true).Text(this, &SFNMemoryTree::TotalsLine) ]
		];
	ChildSlot
	[
		SNew(SBorder).BorderImage(Brush::Segment(FLinearColor(0.f, 0.f, 0.02f, 0.72f))).Padding(FMargin(36.f, 28.f))
		[
			FN7Looks::Frame(6, Inner, FMargin(26.f, 20.f))   // the star map
		]
	];
	CardBox->SetContent(MakeCard());
	PathsBox->SetContent(MakePaths());
}

FReply SFNMemoryTree::OnKeyDown(const FGeometry&, const FKeyEvent& K)
{
	if (K.GetKey() == EKeys::Escape) { OnClose.ExecuteIfBound(); return FReply::Handled(); }
	return FReply::Unhandled();
}

void SFNMemoryTree::Flatten(const TSharedPtr<FJsonObject>& J, int32 Parent)
{
	using namespace FN7Tree;
	if (!J.IsValid()) return;
	FNode N;
	N.J = J;
	N.Id = Str(J, TEXT("id"));
	N.Name = Str(J, TEXT("name"));
	N.Kind = Str(J, TEXT("kind"));
	N.Store = Str(J, TEXT("store"));
	N.Level = (int32)Num(J, TEXT("level"));
	N.Facts = Num(J, TEXT("facts_total"));
	N.LastBeat = Num(J, TEXT("last_beat"));
	N.Density = Num(Obj(J, TEXT("density")), TEXT("log10_per_km2"), -5.5);
	N.bOnPath = Flag(J, TEXT("on_path"));
	N.bCollapsed = Flag(J, TEXT("collapsed"));
	N.Parent = Parent;
	const int32 Me = Nodes.Add(N);
	if (const TArray<TSharedPtr<FJsonValue>>* Ch = Arr(J, TEXT("children")))
		for (const TSharedPtr<FJsonValue>& C : *Ch) if (C.IsValid()) Flatten(C->AsObject(), Me);
}

void SFNMemoryTree::Layout()
{
	// One row per tier; the DM's places on the left, the society's minds (from the district down) on the right.
	TMap<int32, TArray<int32>> Dm, Minds;
	for (int32 i = 0; i < Nodes.Num(); ++i) (Nodes[i].Store == TEXT("minds") ? Minds : Dm).FindOrAdd(Nodes[i].Level).Add(i);
	const bool bSociety = Minds.Num() > 0;
	auto Place = [this](TMap<int32, TArray<int32>>& Rows, float X0, float X1)
	{
		for (TPair<int32, TArray<int32>>& R : Rows)
		{
			TArray<int32>& Row = R.Value;
			// the path node in the middle of its row, its collapsed siblings either side
			Row.Sort([this](int32 A, int32 B) { return Nodes[A].bOnPath != Nodes[B].bOnPath ? Nodes[A].bOnPath : Nodes[A].Facts > Nodes[B].Facts; });
			TArray<int32> Order;
			for (int32 k = 0; k < Row.Num(); ++k) { if (k % 2 == 0) Order.Add(Row[k]); else Order.Insert(Row[k], 0); }
			const float Y = 0.06f + 0.88f * FMath::Clamp(R.Key, 0, 6) / 6.f;
			for (int32 k = 0; k < Order.Num(); ++k) Nodes[Order[k]].At = FVector2D(X0 + (X1 - X0) * (k + 0.5f) / Order.Num(), Y);
		}
	};
	Place(Dm, 0.12f, bSociety ? 0.66f : 0.98f);
	Place(Minds, 0.70f, 0.98f);
}

void SFNMemoryTree::SetData(const TSharedPtr<FJsonObject>& D)
{
	using namespace FN7Tree;
	Data = D;
	Nodes.Reset();
	LevelNames.Reset();
	Beat = Num(D, TEXT("beat"));
	if (const TArray<TSharedPtr<FJsonValue>>* Ls = Arr(D, TEXT("levels")))
		for (const TSharedPtr<FJsonValue>& V : *Ls)
		{
			const TSharedPtr<FJsonObject> L = V.IsValid() ? V->AsObject() : nullptr;
			const int32 Lv = (int32)Num(L, TEXT("level"));
			while (LevelNames.Num() <= Lv) LevelNames.Add(FString());
			LevelNames[Lv] = Str(L, TEXT("name")) + TEXT("  ·  ") + Str(L, TEXT("scale"));
		}
	Flatten(Obj(D, TEXT("tree")), -1);
	Layout();
	// select the focus (the deepest node on the path)
	Selected = -1;
	for (int32 i = 0; i < Nodes.Num(); ++i) if (Nodes[i].bOnPath && (Selected < 0 || Nodes[i].Level >= Nodes[Selected].Level)) Selected = i;
	Path = -1;
	Waiting.Reset();
	if (CardBox.IsValid()) CardBox->SetContent(MakeCard());
	if (PathsBox.IsValid()) PathsBox->SetContent(MakePaths());
	if (ChainBox.IsValid()) ChainBox->SetContent(SNullWidget::NullWidget);
	UE_LOG(LogTemp, Display, TEXT("FN7 tree: beat %.0f, %d nodes, %d recall paths, focus %s"), Beat, Nodes.Num(), Arr(D, TEXT("recall_paths")) ? Arr(D, TEXT("recall_paths"))->Num() : 0, *Str(Obj(D, TEXT("focus")), TEXT("name")));
}

void SFNMemoryTree::Select(int32 i)
{
	Selected = i;
	if (CardBox.IsValid()) CardBox->SetContent(MakeCard());
	UE_LOG(LogTemp, Display, TEXT("FN7 tree: node %s"), Nodes.IsValidIndex(i) ? *Nodes[i].Name : TEXT("-"));
}

void SFNMemoryTree::PickPath(int32 p)
{
	Path = p;
	PathAt = FPlatformTime::Seconds();
	if (ChainBox.IsValid()) ChainBox->SetContent(MakeChain());
	if (PathsBox.IsValid()) PathsBox->SetContent(MakePaths());
	UE_LOG(LogTemp, Display, TEXT("FN7 tree: recall path %d"), p);
}

FText SFNMemoryTree::HeaderLine() const
{
	using namespace FN7Tree;
	if (!Data.IsValid()) return FText::FromString(Waiting);
	const TSharedPtr<FJsonObject> F = Obj(Data, TEXT("focus"));
	const FString Why = Str(F, TEXT("why"));
	return FText::FromString(FString::Printf(TEXT("The DM's memory of place and time, exactly as stored, at beat %.0f.  Focus: %s (%s)%s"), Beat, *Str(F, TEXT("name")), *Str(F, TEXT("kind")),
		Why.IsEmpty() ? TEXT("") : *(TEXT(": ") + Why)));
}

FText SFNMemoryTree::TotalsLine() const
{
	using namespace FN7Tree;
	const TSharedPtr<FJsonObject> T = Obj(Data, TEXT("totals"));
	if (!T.IsValid()) return LOCTEXT("NoTotals", "The totals appear with the answer.");
	TArray<FString> Bits;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : T->Values)
	{
		const FString K(*KV.Key);
		if (KV.Value->Type == EJson::Number) Bits.Add(FString::Printf(TEXT("%s %s"), *K.Replace(TEXT("_"), TEXT(" ")), *Plain(KV.Value)));
		else if (KV.Value->Type == EJson::Object)
		{
			// a table: its total (and for the state, the biggest tables)
			double Sum = 0.0; TArray<TPair<FString, double>> Parts;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : KV.Value->AsObject()->Values) if (P.Value->Type == EJson::Number) { Sum += P.Value->AsNumber(); Parts.Add({ FString(*P.Key), P.Value->AsNumber() }); }
			if (Parts.Num() == 0) continue;
			Parts.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value > B.Value; });
			FString Top;
			for (int32 i = 0; i < Parts.Num() && i < 3; ++i) Top += FString::Printf(TEXT("%s%s %s"), i ? TEXT(", ") : TEXT(""), *Parts[i].Key, *FString::Printf(TEXT("%.0f"), Parts[i].Value));
			const bool bBytes = K.Contains(TEXT("bytes"));
			Bits.Add(bBytes ? FString::Printf(TEXT("state %.0f KB (%s)"), Sum / 1024.0, *Top) : FString::Printf(TEXT("%s %.0f (%s)"), *K.Replace(TEXT("_"), TEXT(" ")), Sum, *Top));
		}
	}
	return FText::FromString(TEXT("TOTALS  ") + FString::Join(Bits, TEXT("   ·   ")));
}

TSharedRef<SWidget> SFNMemoryTree::MakeCard() const
{
	using namespace FN7Style;
	using namespace FN7Tree;
	if (!Nodes.IsValidIndex(Selected))
		return SNew(STextBlock).Font(Type::Font(11)).ColorAndOpacity(Dim).AutoWrapText(true).Text(FText::FromString(Data.IsValid() ? TEXT("Click a node.") : Waiting));
	const FNode& N = Nodes[Selected];
	const TSharedPtr<FJsonObject>& J = N.J;
	const TSharedPtr<FJsonObject> Den = Obj(J, TEXT("density"));
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Line = [&V](const FString& S, const FLinearColor& C, int32 Size = 10, bool bBold = false)
	{
		V->AddSlot().AutoHeight().Padding(0.f, 1.f)[ SNew(STextBlock).Font(Type::Font(Size, bBold ? TEXT("Bold") : TEXT("Regular"))).ColorAndOpacity(C).AutoWrapText(true).Text(FText::FromString(S)) ];
	};
	Line(N.Name, Gold, 14, true);
	Line(FString::Printf(TEXT("%s  ·  tier %d  ·  %s%s"), *N.Kind, N.Level, N.Store == TEXT("minds") ? TEXT("the society's minds") : TEXT("the DM's memory"),
		Str(J, TEXT("hex_level")).IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  ·  %s hex"), *Str(J, TEXT("hex_level")))), Dim);
	Line(FString::Printf(TEXT("%.0f facts (%.0f right here, %.0f private: counted, never shown)  ·  beats %.0f-%.0f"), N.Facts, Num(J, TEXT("facts_here")), Num(J, TEXT("private_facts")), Num(J, TEXT("first_beat")), N.LastBeat), Ink);
	Line(FString::Printf(TEXT("density of detail: 10^%.1f facts per km2  ·  %.2f a beat"), N.Density, Num(Den, TEXT("per_beat"))), Ramp(N.Density));
	if (!Str(J, TEXT("earth_line")).IsEmpty()) Line(TEXT("the earth here: ") + Str(J, TEXT("earth_line")), Ink);
	// the kinds, as bars
	if (const TSharedPtr<FJsonObject> K = Obj(J, TEXT("kinds")))
	{
		TArray<TPair<FString, double>> Ks;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : K->Values) Ks.Add({ FString(*KV.Key), KV.Value->AsNumber() });
		Ks.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Value > B.Value; });
		const double Max = Ks.Num() ? FMath::Max(1.0, Ks[0].Value) : 1.0;
		for (int32 i = 0; i < Ks.Num() && i < 7; ++i)
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(130.f)[ SNew(STextBlock).Font(Type::Font(9)).ColorAndOpacity(Dim).Text(FText::FromString(Ks[i].Key)) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(FMath::Max(2.f, (float)(150.0 * Ks[i].Value / Max))).HeightOverride(7.f)[ SNew(SBorder).BorderImage(Dot()).BorderBackgroundColor(Gold * FLinearColor(1, 1, 1, 0.8f)) ] ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)[ SNew(STextBlock).Font(Type::Font(9)).ColorAndOpacity(Ink).Text(FText::AsNumber((int32)Ks[i].Value)) ]
			];
	}
	// the samples, newest first
	if (const TArray<TSharedPtr<FJsonValue>>* Sm = Arr(J, TEXT("sample_facts")))
		for (const TSharedPtr<FJsonValue>& F : *Sm)
		{
			const TSharedPtr<FJsonObject> O = F->AsObject();
			Line(FString::Printf(TEXT("beat %.0f  ·  %s  ·  %s"), Num(O, TEXT("beat")), *Str(O, TEXT("kind")), *Str(O, TEXT("text"))), Ink, 9);
		}
	if (const TSharedPtr<FJsonObject> Ru = Obj(J, TEXT("rollup")))
		Line(FString::Printf(TEXT("its latest roll-up (beat %.0f, cites %.0f entries): %s"), Num(Ru, TEXT("beat")), Num(Ru, TEXT("cites_total")), *Str(Ru, TEXT("text"))), Gold, 9);
	if (const TArray<TSharedPtr<FJsonValue>>* Rf = Arr(J, TEXT("rolled_up_from")))
	{
		TArray<FString> Fs;
		for (const TSharedPtr<FJsonValue>& F : *Rf) Fs.Add(FString::Printf(TEXT("%s %.0f"), *Str(F->AsObject(), TEXT("name")), Num(F->AsObject(), TEXT("facts"))));
		if (Fs.Num()) Line(TEXT("rolled up from: ") + FString::Join(Fs, TEXT(", ")), Dim, 9);
	}
	return SNew(SBorder).BorderImage(Card()).Padding(10.f)[V];
}

TSharedRef<SWidget> SFNMemoryTree::MakePaths()
{
	using namespace FN7Style;
	using namespace FN7Tree;
	const TArray<TSharedPtr<FJsonValue>>* Ps = Arr(Data, TEXT("recall_paths"));
	if (!Ps || Ps->Num() == 0) return SNew(STextBlock).Font(Type::Font(10)).ColorAndOpacity(Dim).Text(LOCTEXT("NoPaths", "No recall paths yet."));
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	for (int32 p = 0; p < Ps->Num(); ++p)
	{
		const TSharedPtr<FJsonObject> O = (*Ps)[p]->AsObject();
		V->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([this, p]() { PickPath(p); return FReply::Handled(); })
			[
				SNew(SBorder).BorderImage(p == Path ? Lit() : Card()).Padding(FMargin(8.f, 5.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[ SNew(STextBlock).Font(Type::Font(9, TEXT("Bold"))).ColorAndOpacity(Gold).Text(FText::FromString(Str(O, TEXT("fact")).ToUpper())) ]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[ SNew(STextBlock).Font(Type::Font(10)).ColorAndOpacity(Ink).AutoWrapText(true).Text(FText::FromString(Str(O, TEXT("title")))) ]
				]
			]
		];
	}
	return V;
}

TSharedRef<SWidget> SFNMemoryTree::MakeChain() const
{
	using namespace FN7Style;
	using namespace FN7Tree;
	const TArray<TSharedPtr<FJsonValue>>* Ps = Arr(Data, TEXT("recall_paths"));
	if (!Ps || !Ps->IsValidIndex(Path)) return SNullWidget::NullWidget;
	const TArray<TSharedPtr<FJsonValue>>* Steps = Arr((*Ps)[Path]->AsObject(), TEXT("steps"));
	if (!Steps) return SNullWidget::NullWidget;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	int32 k = 0;
	for (const TSharedPtr<FJsonValue>& Sv : *Steps)
	{
		const TSharedPtr<FJsonObject> S = Sv.IsValid() ? Sv->AsObject() : nullptr;
		if (!S.IsValid()) continue;
		const FString Step = Str(S, TEXT("step"));
		FString Head, Body;
		if (Step == TEXT("query_key"))
		{
			Head = TEXT("1  THE KEY");
			Body = FString::Printf(TEXT("%s%s\n%s\nasked again: %s"), *Str(S, TEXT("key")), Flag(S, TEXT("reproduces")) ? TEXT("  (the die reproduces)") : TEXT(""), *Str(S, TEXT("derivation")), *Str(S, TEXT("asked_again")));
		}
		else if (Step == TEXT("stored_roll"))
		{
			Head = TEXT("2  THE STORED DIE");
			Body = FString::Printf(TEXT("%s rolled %.0f %+.0f = %.0f vs DC %.0f: %s (%s)"), *Str(S, TEXT("who_name")), Num(S, TEXT("roll")), Num(S, TEXT("modifier")), Num(S, TEXT("total")), Num(S, TEXT("target_dc")), *Str(S, TEXT("verdict")), *Str(S, TEXT("reason")));
		}
		else if (Step == TEXT("memory_entry"))
		{
			const TSharedPtr<FJsonObject> E = Obj(S, TEXT("entry"));
			Head = TEXT("3  THE MEMORY ENTRY");
			Body = FString::Printf(TEXT("#%.0f  beat %.0f  ·  %s  ·  scope %s\n%s"), Num(E, TEXT("id")), Num(E, TEXT("beat")), *Str(E, TEXT("kind")), *Str(E, TEXT("scope")), *Str(E, TEXT("text")));
		}
		else if (Step == TEXT("rollups") || Step == TEXT("prompts"))
		{
			Head = Step == TEXT("rollups") ? TEXT("4  THE ROLL-UPS THAT COUNT IT") : TEXT("5  THE PROMPT LINES THAT USE IT NOW");
			if (const TArray<TSharedPtr<FJsonValue>>* Items = Arr(S, TEXT("items")))
				for (const TSharedPtr<FJsonValue>& Iv : *Items)
				{
					const TSharedPtr<FJsonObject> I = Iv->AsObject();
					if (Step == TEXT("rollups"))
						Body += FString::Printf(TEXT("%s %s, beat %.0f: %s\n"), Flag(I, TEXT("cites_this")) ? TEXT("✓") : TEXT("·"), *Str(I, TEXT("name")), Num(I, TEXT("beat")), *Str(I, TEXT("text")));
					else
					{
						Body += FString::Printf(TEXT("%s %s  →  %s%s\n"), Flag(I, TEXT("included")) ? TEXT("✓") : TEXT("✗"), *Str(I, TEXT("prompt")), *Str(I, TEXT("reader")),
							Str(I, TEXT("why")).IsEmpty() ? TEXT("") : *(TEXT(" (") + Str(I, TEXT("why")) + TEXT(")")));
						if (!Str(I, TEXT("line")).IsEmpty()) Body += TEXT("      \"") + Str(I, TEXT("line")) + TEXT("\"\n");
						if (const TArray<TSharedPtr<FJsonValue>>* Ls = Arr(I, TEXT("lines"))) for (int32 l = 0; l < Ls->Num() && l < 6; ++l) Body += TEXT("      ") + (*Ls)[l]->AsString() + TEXT("\n");
					}
				}
			Body = Body.TrimEnd();
		}
		else
		{
			// any other step (an earth field, a ledger row, a mind's memory...): its fields as they are
			Head = FString::Printf(TEXT("%d  %s"), k + 1, *Step.Replace(TEXT("_"), TEXT(" ")).ToUpper());
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : S->Values)
			{
				const FString Kk(*KV.Key);
				if (Kk == TEXT("step")) continue;
				const FString Pv = Plain(KV.Value);
				if (!Pv.IsEmpty()) Body += FString::Printf(TEXT("%s: %s\n"), *Kk.Replace(TEXT("_"), TEXT(" ")), *Pv);
				else if (KV.Value->Type == EJson::Object) for (const TPair<FString, TSharedPtr<FJsonValue>>& Sub : KV.Value->AsObject()->Values) { const FString Sp = Plain(Sub.Value); if (!Sp.IsEmpty()) Body += FString::Printf(TEXT("%s %s: %s\n"), *Kk, *FString(*Sub.Key), *Sp); }
			}
			Body = Body.TrimEnd();
		}
		const int32 Me = k++;
		// each card fades in after the one before it: the recall, step by step
		V->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SBorder).BorderImage(Card()).Padding(10.f)
			.ColorAndOpacity_Lambda([this, Me]() { const float A = FMath::Clamp((float)(FPlatformTime::Seconds() - PathAt - 0.6 * Me) / 0.4f, 0.f, 1.f); return FLinearColor(1.f, 1.f, 1.f, A); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Type::Font(10, TEXT("Bold"))).ColorAndOpacity(Gold).Text(FText::FromString(Head)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[ SNew(STextBlock).Font(Type::Font(9)).ColorAndOpacity(Ink).AutoWrapText(true).Text(FText::FromString(Body)) ]
			]
		];
	}
	return V;
}

#undef LOCTEXT_NAMESPACE
