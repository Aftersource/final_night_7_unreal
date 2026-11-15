#include "Features/FNEventGlide.h"
#include "Features/FNEventFx.h"
#include "Features/FNScenePlayer.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/UObjectIterator.h"

AFNEventGlide::AFNEventGlide()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFNEventGlide::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

bool AFNEventGlide::Busy()
{
	AFNStudio* St = nullptr;
	if (UWorld* W = GetWorld()) { if (TActorIterator<AFNStudio> It(W); It) St = *It; }
	const TCHAR* Why = nullptr;
	if (!St) Why = TEXT("no board");
	else if (St->IsZoomAnimating()) Why = TEXT("zooming");
	else if (St->IsHandHolding()) Why = TEXT("holding");
	else if (St->IsBoardPanning()) Why = TEXT("panning");
	else if (TActorIterator<AFNScenePlayer> Sp(GetWorld()); Sp && Sp->IsActive()) Why = TEXT("watching your scene");
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();
		const TSharedPtr<SWidget> F = App.GetKeyboardFocusedWidget();
		if (App.IsDragDropping() || App.GetPressedMouseButtons().Num() > 0) Why = TEXT("dragging");   // a piece, a power or the board
		else if (F.IsValid() && F->GetTypeAsString().Contains(TEXT("EditableText"))) Why = TEXT("typing");   // the WILL, chat
	}
	if (Why && LastWhy != Why) UE_LOG(LogTemp, Display, TEXT("FNGlide: waiting, you are %s"), Why);
	LastWhy = Why ? Why : TEXT("");
	return Why != nullptr;
}

void AFNEventGlide::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	if (!D.IsValid()) return;
	// yours: the seat view's demigods (as FNArmies reads them)
	const TSharedPtr<FJsonObject>* You = nullptr;
	const TSharedPtr<FJsonObject>* V = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if ((D->TryGetObjectField(TEXT("you"), You) && You && (*You)->TryGetObjectField(TEXT("private_state"), V) && V) || (D->TryGetObjectField(TEXT("private"), V) && V))
		if ((*V)->TryGetArrayField(TEXT("demigods"), L) && L)
		{
			MineKeys.Reset();
			for (const TSharedPtr<FJsonValue>& H : *L)
			{
				const TSharedPtr<FJsonObject> O = H.IsValid() ? H->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				const FString Ref = O->GetStringField(TEXT("ref")), Name = O->GetStringField(TEXT("name"));
				if (Ref.Len() >= 4) MineKeys.AddUnique(TEXT("\"") + Ref + TEXT("\""));   // quoted: dg:4 is not dg:41
				// "Left Hand" alone is everyone's: only "<you>'s Left Hand"
				FString Me;
				if (You && (*You)->TryGetStringField(TEXT("display_name"), Me) && !Me.IsEmpty() && !Name.IsEmpty()) MineKeys.AddUnique(Me + TEXT("'s ") + Name);
				else if (Name.Len() >= 4 && !Name.Contains(TEXT("Hand"))) MineKeys.AddUnique(Name);
			}
		}
	if (Type != TEXT("round.outcome")) return;
	if (MineKeys.Num() && LastKeys != FString::Join(MineKeys, TEXT("|"))) { LastKeys = FString::Join(MineKeys, TEXT("|")); UE_LOG(LogTemp, Display, TEXT("FNGlide: yours = %s"), *LastKeys); }
	const TSharedPtr<FJsonObject>* Pub = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
	if (MineKeys.Num() == 0 || !D->TryGetObjectField(TEXT("public"), Pub) || !Pub || !(*Pub)->TryGetArrayField(TEXT("events"), Ev) || !Ev) return;
	Queue.Reset();   // a new beat: only this beat's news is worth the trip
	for (const TSharedPtr<FJsonValue>& Val : *Ev)
	{
		const TSharedPtr<FJsonObject> E = Val.IsValid() ? Val->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
		if (!E.IsValid() || !E->TryGetArrayField(TEXT("at"), At) || !At || At->Num() < 2) continue;
		// Yours = YOUR demigod did it: the event's first ref is its doer (CLIENT_API round.outcome events[].refs: "Vex
		// casts flood" -> ["dg:2"]; a target, when there is one, comes after it). A rival's cast on your demigod
		// ("Oko's Left Hand casts storm clouds on <you>'s Left Hand") is theirs and never moves your view (Mac review
		// 2026-11-15: matching any string in the event glided the view to rivals' casts on you).
		const TArray<TSharedPtr<FJsonValue>>* Refs = nullptr;
		const bool bMine = E->TryGetArrayField(TEXT("refs"), Refs) && Refs && Refs->Num() > 0 && (*Refs)[0].IsValid()
			&& (*Refs)[0]->Type == EJson::String && MineKeys.Contains(TEXT("\"") + (*Refs)[0]->AsString() + TEXT("\""));
		if (!bMine) continue;
		FGo G;
		G.Lon = (*At)[0]->AsNumber(); G.Lat = (*At)[1]->AsNumber();
		G.Kind = E->GetStringField(TEXT("kind")) + TEXT(" ") + E->GetStringField(TEXT("text"));
		double Mag = 5.0; E->TryGetNumberField(TEXT("magnitude"), Mag); G.Mag = (float)Mag;
		const FString S = G.Kind.ToLower();
		bool bDisaster = false;
		for (const TCHAR* W : { TEXT("fire"), TEXT("flood"), TEXT("storm"), TEXT("quake"), TEXT("meteor"), TEXT("plague"), TEXT("lava"), TEXT("tsunami"), TEXT("hurricane") }) bDisaster |= S.Contains(W);
		G.Hold = bDisaster ? 4.f : 8.f;
		Queue.Add(G);
		UE_LOG(LogTemp, Display, TEXT("FNGlide: yours at %.2f,%.2f (%s, %.0f s): %s"), G.Lat, G.Lon, bDisaster ? TEXT("disaster") : TEXT("action"), G.Hold, *G.Kind.Left(90));
	}
	Queue.Sort([](const FGo& A, const FGo& B) { return A.Mag > B.Mag; });   // the biggest first
	if (Queue.Num() > 2) Queue.SetNum(2);
}

void AFNEventGlide::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNEventGlide::OnMessage); break; }
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;
	if (!St) return;

	if (bAway)
	{
		HoldLeft -= DeltaSeconds;
		if (HoldLeft > 0.f || Busy()) return;
		double Lat, Lon; bool bPicked;
		St->GetViewSpot(Lat, Lon, bPicked);
		if (FMath::Abs(Lat - AwayLat) > 1e-4 || FMath::Abs(Lon - AwayLon) > 1e-4)
		{
			UE_LOG(LogTemp, Display, TEXT("FNGlide: you moved the view, left as is"));
			bAway = false; Queue.Reset(); return;
		}
		if (Queue.Num() == 0)
		{
			St->RestoreViewSpot(SavedLat, SavedLon, bSavedPicked);
			UE_LOG(LogTemp, Display, TEXT("FN7 glide: back"));
			bAway = false; return;
		}
	}
	if (Queue.Num() == 0 || Busy()) return;
	const FGo G = Queue[0];
	Queue.RemoveAt(0);
	FVector Spot;
	if (St->BoardPointFor(G.Lat, G.Lon, Spot))   // already in front of you (its effect plays there): no trip
	{
		UE_LOG(LogTemp, Display, TEXT("FNGlide: already on your board, no glide"));
		if (bAway && Queue.Num() == 0) { St->RestoreViewSpot(SavedLat, SavedLon, bSavedPicked); bAway = false; }
		return;
	}
	if (!bAway) St->GetViewSpot(SavedLat, SavedLon, bSavedPicked);   // where you were (kept through a chain of two)
	St->CenterBoardAndGlobe(G.Lat, G.Lon);
	bool bP;
	St->GetViewSpot(AwayLat, AwayLon, bP);
	if (TActorIterator<AFNEventFx> Fx(W); Fx) Fx->Play(G.Lon, G.Lat, G.Kind, G.Mag, 0, 0.8f);   // it was off the board when it came in
	HoldLeft = G.Hold;
	bAway = true;
	UE_LOG(LogTemp, Display, TEXT("FN7 glide: to %.2f,%.2f (%s, %.0f s)"), G.Lat, G.Lon, *G.Kind.Left(60), G.Hold);
}

bool UFNEventGlideSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNEventGlideSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNEventGlide>(AFNEventGlide::StaticClass(), FTransform::Identity, P);
}
