#include "Features/FNTurnBar.h"
#include "../../UI/FNStyle.h"
#include "FNNetClient.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace FNTurnBarImpl
{
	double Km(double Lon1, double Lat1, double Lon2, double Lat2)
	{
		const double A = FMath::DegreesToRadians(Lat1), B = FMath::DegreesToRadians(Lat2);
		const double C = FMath::Sin(A) * FMath::Sin(B) + FMath::Cos(A) * FMath::Cos(B) * FMath::Cos(FMath::DegreesToRadians(Lon2 - Lon1));
		return 6371.0 * FMath::Acos(FMath::Clamp(C, -1.0, 1.0));
	}
	bool LonLat(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& Lon, double& Lat)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O.IsValid() || !O->TryGetArrayField(Key, A) || !A || A->Num() < 2) return false;
		Lon = (*A)[0]->AsNumber(); Lat = (*A)[1]->AsNumber();
		return true;
	}
	FString Hand(const FString& Side) { return Side.Equals(TEXT("right"), ESearchCase::IgnoreCase) ? TEXT("Right Hand") : TEXT("Left Hand"); }
	static constexpr double NearKm = 300.0;   // "a rival is close" (a duel is a real threat)
}

AFNTurnBar::AFNTurnBar()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
}

double AFNTurnBar::NowMs() const
{
	if (const UFNNetClient* N = Net.Get()) return N->NowServerMs();
	return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
}

void AFNTurnBar::BeginPlay()
{
	Super::BeginPlay();
	if (!GEngine || !GEngine->GameViewport) return;
	TWeakObjectPtr<AFNTurnBar> W(this);
	// FN7 (Mac session, the backend's go-ahead to fit it to the HUD; Benito's content unchanged): the kit's font and
	// panel, and 92 px down, clear of the HUD's header strip (it reaches ~78 px at 1080p and x ~940, where this bar's
	// left end is at 54 px). The HUD's own top-centre banner moves below this bar while it shows.
	auto Font = [](int32 Size, const TCHAR* Style) { return FN7Style::Type::Font(Size, Style); };
	Widget = SNew(SBox)
		.HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(26.f, 84.f, 0.f, 0.f))   // under the title bar (62 sat on its lower rod), clear of the duel box at the top centre
		.Visibility_Lambda([W]() { return W.IsValid() && W->IsShown() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SBorder)
			.BorderImage(FN7Style::Brush::Panel())
			.Padding(FMargin(18.f, 8.f))
			[
				SNew(SBox).WidthOverride(380.f)   // narrow enough to end before the duel box at the top centre (it starts ~460 px in)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(Font(17, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.98f))
						.Text_Lambda([W]() { return W.IsValid() ? W->LineBeat() : FText::GetEmpty(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
					[
						SNew(SBox).HeightOverride(4.f)
						[
							SNew(SProgressBar).FillColorAndOpacity(FLinearColor(1.f, 0.75f, 0.25f))
							.Percent_Lambda([W]() { return W.IsValid() ? TOptional<float>(W->BeatLeft()) : TOptional<float>(0.f); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(FLinearColor(1.f, 0.8f, 0.3f)).AutoWrapText(true).Justification(ETextJustify::Center)
						.Text_Lambda([W]() { return W.IsValid() ? W->LineHint() : FText::GetEmpty(); })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(Font(11, TEXT("Regular"))).ColorAndOpacity(FLinearColor(0.7f, 0.72f, 0.8f)).AutoWrapText(true).Justification(ETextJustify::Center)
						.Text_Lambda([W]() { return W.IsValid() ? W->LineGoal() : FText::GetEmpty(); })
					]
				]
			]
		];
	GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 60);
}

void AFNTurnBar::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	if (Widget.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	Super::EndPlay(Reason);
}

void AFNTurnBar::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNTurnBar::OnMessage); break; }
	const FString H = LineHint().ToString();
	if (H != LastHint) { LastHint = H; UE_LOG(LogTemp, Display, TEXT("FNTurn: beat %d/%d hint: %s"), Beat, BeatsTotal, *H); }
}

void AFNTurnBar::ReadPublic(const TSharedPtr<FJsonObject>& P)
{
	using namespace FNTurnBarImpl;
	if (!P.IsValid()) return;
	double N = 0;
	if (P->TryGetNumberField(TEXT("beat"), N)) Beat = (int32)N;
	if (P->TryGetNumberField(TEXT("beats_total"), N)) BeatsTotal = (int32)N;
	if (P->TryGetNumberField(TEXT("alive"), N)) AliveAll = (int32)N;
	const TSharedPtr<FJsonObject>* Arena = nullptr;
	if (P->TryGetObjectField(TEXT("arena"), Arena) && Arena && LonLat(*Arena, TEXT("center"), ArenaLon, ArenaLat)) (*Arena)->TryGetNumberField(TEXT("radius_km"), ArenaKm);
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!P->TryGetArrayField(TEXT("demigods"), List) || !List) return;
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid()) continue;
		FDg& D = Dgs.FindOrAdd(O->GetStringField(TEXT("ref")));
		D.Ref = O->GetStringField(TEXT("ref"));
		O->TryGetStringField(TEXT("name"), D.Name);
		O->TryGetStringField(TEXT("side"), D.Side);
		O->TryGetBoolField(TEXT("alive"), D.bAlive);
		if (O->TryGetNumberField(TEXT("seat"), N)) D.Seat = (int32)N;
		D.bHasPos = LonLat(O, TEXT("pos"), D.Lon, D.Lat);
	}
}

void AFNTurnBar::ReadMine(const TSharedPtr<FJsonObject>& View)
{
	if (!View.IsValid()) return;
	// the server's next-move hint + goal (state schema 18); ours stay as the fallback when absent
	const TSharedPtr<FJsonObject>* Hint = nullptr;
	if (View->TryGetObjectField(TEXT("hint"), Hint) && Hint)
	{
		const FString Tx = (*Hint)->GetStringField(TEXT("text")), Why = (*Hint)->GetStringField(TEXT("why"));
		ServerHint = Why.IsEmpty() ? Tx : Tx + TEXT("  ") + Why;
	}
	const TSharedPtr<FJsonObject>* GoalO = nullptr;
	FString GoalStr;
	if (View->TryGetStringField(TEXT("goal"), GoalStr)) ServerGoal = GoalStr;
	else if (View->TryGetObjectField(TEXT("goal"), GoalO) && GoalO) ServerGoal = (*GoalO)->GetStringField(TEXT("text"));
	const TArray<TSharedPtr<FJsonValue>>* Hands = nullptr;
	if (!View->TryGetArrayField(TEXT("demigods"), Hands) || !Hands) return;
	for (TPair<FString, FDg>& It : Dgs) It.Value.bMine = false;
	for (const TSharedPtr<FJsonValue>& V : *Hands)
	{
		const TSharedPtr<FJsonObject> G = V.IsValid() ? V->AsObject() : nullptr;
		if (!G.IsValid()) continue;
		FDg& D = Dgs.FindOrAdd(G->GetStringField(TEXT("ref")));
		D.Ref = G->GetStringField(TEXT("ref"));
		D.bMine = true;
		G->TryGetStringField(TEXT("side"), D.Side);
		G->TryGetBoolField(TEXT("alive"), D.bAlive);
		double N = 0;
		if (G->TryGetNumberField(TEXT("health"), N)) D.Health = (int32)N;
		if (G->TryGetNumberField(TEXT("max_health"), N)) D.MaxHealth = FMath::Max(1, (int32)N);
	}
}

void AFNTurnBar::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	if (!D.IsValid()) return;
	const TSharedPtr<FJsonObject>* O = nullptr;
	const TSharedPtr<FJsonObject>* V = nullptr;
	if (D->TryGetObjectField(TEXT("public_state"), O) && O) ReadPublic(*O);
	if (D->TryGetObjectField(TEXT("public"), O) && O) ReadPublic(*O);
	if (D->TryGetObjectField(TEXT("you"), O) && O && (*O)->TryGetObjectField(TEXT("private_state"), V) && V) ReadMine(*V);
	if (D->TryGetObjectField(TEXT("private"), V) && V) ReadMine(*V);
	if (D->TryGetObjectField(TEXT("state"), V) && V) ReadMine(*V);
	if (Type == TEXT("game.me")) ReadMine(D);
	const double Now = NowMs();
	if (Type == TEXT("phase.changed"))
	{
		FString Phase; double Dl = 0;
		if (D->TryGetStringField(TEXT("phase"), Phase) && Phase == TEXT("compose") && D->TryGetNumberField(TEXT("deadline"), Dl) && Dl > Now)
		{
			DeadlineMs = Dl; BeatStartMs = Now; BeatLenMs = FMath::Max(Dl - Now, 1000.0);
		}
	}
	else if (Type == TEXT("round.outcome"))
	{
		// no deadline from the server: the beat lasts as long as the last one did
		if (LastBeatAtMs > 0) BeatLenMs = FMath::Clamp(Now - LastBeatAtMs, 4000.0, 120000.0);
		LastBeatAtMs = Now;
		if (DeadlineMs < Now) { BeatStartMs = Now; DeadlineMs = Now + BeatLenMs; }
	}
	else if (Type == TEXT("match.ended")) Beat = 0;
	else if (Type == TEXT("match.started")) { Dgs.Reset(); ServerHint.Reset(); ServerGoal.Reset(); }
}

float AFNTurnBar::BeatLeft() const
{
	if (DeadlineMs <= 0) return 0.f;
	return (float)FMath::Clamp((DeadlineMs - NowMs()) / FMath::Max(BeatLenMs, 1.0), 0.0, 1.0);
}

FText AFNTurnBar::LineBeat() const
{
	const int32 Secs = DeadlineMs > 0 ? FMath::Max(0, FMath::CeilToInt((DeadlineMs - NowMs()) / 1000.0)) : 0;
	const FString Total = BeatsTotal > 0 ? FString::Printf(TEXT(" / %d"), BeatsTotal) : FString();
	return FText::FromString(Secs > 0
		? FString::Printf(TEXT("BEAT %d%s      YOUR MOVE   %d:%02d"), Beat, *Total, Secs / 60, Secs % 60)
		: FString::Printf(TEXT("BEAT %d%s      the world moves..."), Beat, *Total));
}

FText AFNTurnBar::LineHint() const
{
	using namespace FNTurnBarImpl;
	TArray<const FDg*> Mine, Rivals;
	for (const TPair<FString, FDg>& It : Dgs)
	{
		if (!It.Value.bAlive) continue;
		(It.Value.bMine ? Mine : Rivals).Add(&It.Value);
	}
	bool bAnyMine = false;
	for (const TPair<FString, FDg>& It : Dgs) bAnyMine |= It.Value.bMine;
	if (!bAnyMine) return FText::FromString(TEXT("Watching. Take a seat to play."));
	if (!ServerHint.IsEmpty()) return FText::FromString(ServerHint);
	if (Mine.Num() == 0) return FText::FromString(TEXT("Both your demigods have fallen.  Write to DEATH in your WILL to bring one back."));
	// 1) outside Time's circle: it gets hurt every beat
	if (ArenaKm > 0)
		for (const FDg* D : Mine)
			if (D->bHasPos && Km(D->Lon, D->Lat, ArenaLon, ArenaLat) > ArenaKm)
				return FText::FromString(FString::Printf(TEXT("Your %s is OUTSIDE Time's circle and gets hurt every beat.  Drag its card into the circle."), *Hand(D->Side)));
	// 2) a rival close to one of yours
	const FDg* Near = nullptr; const FDg* Who = nullptr; double Best = NearKm;
	for (const FDg* D : Mine)
		for (const FDg* R : Rivals)
			if (D->bHasPos && R->bHasPos) { const double K = Km(D->Lon, D->Lat, R->Lon, R->Lat); if (K < Best) { Best = K; Near = D; Who = R; } }
	// 3) the weakest of yours
	const FDg* Weak = nullptr;
	for (const FDg* D : Mine) if (D->Health * 10 <= D->MaxHealth * 4 && (!Weak || D->Health < Weak->Health)) Weak = D;
	if (Near && Weak == Near)
		return FText::FromString(FString::Printf(TEXT("DANGER: %s is %.0f km from your weak %s (%d/%d).  Move it away: drag its card somewhere safe."), *Who->Name, Best, *Hand(Near->Side), Near->Health, Near->MaxHealth));
	if (Weak)
		return FText::FromString(FString::Printf(TEXT("Your %s is weak (%d/%d).  Keep it away from rivals; write a WILL to heal or hide."), *Hand(Weak->Side), Weak->Health, Weak->MaxHealth));
	if (Near)
		return FText::FromString(FString::Printf(TEXT("%s is %.0f km from your %s.  Strike first (double-click it to duel) or cast a power on it."), *Who->Name, Best, *Hand(Near->Side)));
	return FText::FromString(TEXT("You're safe this beat.  Cast a power on a rival, or write a bold WILL to grow stronger."));
}

FText AFNTurnBar::LineGoal() const
{
	if (!ServerGoal.IsEmpty()) return FText::FromString(ServerGoal);
	int32 MineAlive = 0;
	for (const TPair<FString, FDg>& It : Dgs) if (It.Value.bMine && It.Value.bAlive) ++MineAlive;
	const FString Left = BeatsTotal > 0 ? FString::Printf(TEXT("   |   %d beats to go"), FMath::Max(0, BeatsTotal - Beat)) : FString();
	return FText::FromString(FString::Printf(TEXT("GOAL: be the last one standing   |   yours alive: %d of 2   |   demigods left: %d%s"), MineAlive, AliveAll, *Left));
}

bool UFNTurnBarSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNTurnBarSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	InWorld.SpawnActor<AFNTurnBar>(AFNTurnBar::StaticClass());
}
