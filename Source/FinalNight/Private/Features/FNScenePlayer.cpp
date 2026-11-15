#include "Features/FNScenePlayer.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "FNPlanet.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

namespace FNScenePlayerImpl
{
	static AFNStudio* Studio(UWorld* W) { if (W) { if (TActorIterator<AFNStudio> It(W); It) return *It; } return nullptr; }
	static bool IsMove(const FString& V) { return V == TEXT("walk") || V == TEXT("flee") || V == TEXT("enter") || V == TEXT("exit") || V == TEXT("search") || V == TEXT("hide"); }
	static bool IsToward(const FString& V) { return V == TEXT("embrace") || V == TEXT("fight") || V == TEXT("slap") || V == TEXT("give") || V == TEXT("take") || V == TEXT("talk") || V == TEXT("kneel"); }
	static FColor MoodCol(const FString& M)
	{
		if (M == TEXT("love")) return FColor(255, 120, 170);
		if (M == TEXT("anger")) return FColor(255, 70, 50);
		if (M == TEXT("fear")) return FColor(170, 140, 255);
		if (M == TEXT("joy")) return FColor(255, 220, 80);
		return FColor(240, 240, 240);
	}
}

AFNScenePlayer::AFNScenePlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AFNScenePlayer::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

UStaticMeshComponent* AFNScenePlayer::Piece(const TCHAR* Mesh, const FColor& Col)
{
	static UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	UTexture2D* Tex = UTexture2D::CreateTransient(2, 2, PF_B8G8R8A8);
	FColor* Px = static_cast<FColor*>(Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < 4; ++i) Px[i] = Col;
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->SRGB = true;
	Tex->UpdateResource();
	Colors.Add(Tex);
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	if (Unlit)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Unlit, C);
		M->SetTextureParameterValue(TEXT("SlateUI"), Tex);
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	Parts.Add(C);
	return C;
}

UTextRenderComponent* AFNScenePlayer::Label(const FColor& Col)
{
	UTextRenderComponent* L = NewObject<UTextRenderComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetHorizontalAlignment(EHTA_Center);
	L->SetVerticalAlignment(EVRTA_TextBottom);
	L->SetTextRenderColor(Col);
	L->SetCastShadow(false);
	L->RegisterComponent();
	L->SetVisibility(false);
	return L;
}

bool AFNScenePlayer::Busy()
{
	AFNStudio* St = FNScenePlayerImpl::Studio(GetWorld());
	const TCHAR* Why = nullptr;
	if (!St) Why = TEXT("no board");
	else if (St->IsZoomAnimating()) Why = TEXT("zooming");
	else if (St->IsHandHolding()) Why = TEXT("holding");
	else if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();
		const TSharedPtr<SWidget> F = App.GetKeyboardFocusedWidget();
		if (App.IsDragDropping() || App.GetPressedMouseButtons().Num() > 0) Why = TEXT("dragging");
		else if (F.IsValid() && F->GetTypeAsString().Contains(TEXT("EditableText"))) Why = TEXT("typing");
	}
	if (Why && LastWhy != Why) UE_LOG(LogTemp, Display, TEXT("FNScene: waiting, you are %s"), Why);
	LastWhy = Why ? Why : TEXT("");
	return Why != nullptr;
}

bool AFNScenePlayer::CellPos(const FString& Cell, FVector& Out) const
{
	// "face:level:q:r", the board's own key format (AFNStudio::BoardCellKey)
	AFNStudio* St = FNScenePlayerImpl::Studio(GetWorld());
	TArray<FString> P;
	if (!St || Cell.ParseIntoArray(P, TEXT(":")) != 4) return false;
	const int32 Face = FCString::Atoi(*P[0]), Q = FCString::Atoi(*P[2]), R = FCString::Atoi(*P[3]);
	float Tp;
	if (St->BoardCellKey(Q, R) == Cell) return St->BoardHexTop(Q, R, true, Tp, Out);
	static const TCHAR* Names[4] = { TEXT("people"), TEXT("houses"), TEXT("villages"), TEXT("regions") };
	static const double HexM[4] = { 3.0, 30.0, 3000.0, 120000.0 };
	for (int32 L = 0; L < 4; ++L)
		if (P[1] == Names[L])
		{
			double La, Lo;
			const FVector2D Pl = FNPlanet::HexToPlane(Q, R, HexM[L]);
			FNPlanet::VecToLatLon(FNPlanet::PlaneToVec(Face, Pl.X, Pl.Y), La, Lo);
			return St->BoardPointFor(La, Lo, Out);
		}
	return false;
}

void AFNScenePlayer::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	if (!D.IsValid()) return;
	const TSharedPtr<FJsonObject>* You = nullptr;
	const TSharedPtr<FJsonObject>* V = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if ((D->TryGetObjectField(TEXT("you"), You) && You && (*You)->TryGetObjectField(TEXT("private_state"), V) && V) || (D->TryGetObjectField(TEXT("private"), V) && V))
		if ((*V)->TryGetArrayField(TEXT("demigods"), L) && L)
		{
			MineKeys.Reset();
			for (const TSharedPtr<FJsonValue>& H : *L) if (H.IsValid() && H->AsObject().IsValid()) MineKeys.AddUnique(H->AsObject()->GetStringField(TEXT("ref")));
		}
	if (You && (*You)->HasField(TEXT("seat"))) MySeat = (int32)(*You)->GetNumberField(TEXT("seat"));
	if (Type != TEXT("round.outcome")) return;
	const TSharedPtr<FJsonObject>* Pub = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Sc = nullptr;
	if (!D->TryGetObjectField(TEXT("public"), Pub) || !Pub || !(*Pub)->TryGetArrayField(TEXT("scenes"), Sc) || !Sc) return;
	for (const TSharedPtr<FJsonValue>& SV : *Sc)
	{
		const TSharedPtr<FJsonObject> S = SV.IsValid() ? SV->AsObject() : nullptr;
		if (!S.IsValid()) continue;
		FScene N;
		N.Id = S->GetStringField(TEXT("scene_id"));
		const FString Dg = S->GetStringField(TEXT("demigod"));
		double Seat = -1; S->TryGetNumberField(TEXT("seat"), Seat);
		const bool bMine = MineKeys.Contains(Dg) || (MySeat >= 0 && (int32)Seat == MySeat);
		UE_LOG(LogTemp, Display, TEXT("FNScene: scene %s for %s (seat %d) %s"), *N.Id, *Dg, (int32)Seat, bMine ? TEXT("YOURS") : TEXT("not yours"));
		if (!bMine || Seen.Contains(N.Id)) continue;
		Seen.Add(N.Id);
		const TSharedPtr<FJsonObject>* Pl = nullptr;
		if (S->TryGetObjectField(TEXT("place"), Pl) && Pl)
		{
			N.PlaceCell = (*Pl)->GetStringField(TEXT("cell"));
			N.PlaceName = (*Pl)->GetStringField(TEXT("name"));
			const TSharedPtr<FJsonObject>* B = nullptr;
			if ((*Pl)->TryGetObjectField(TEXT("building"), B) && B) N.RoofCell = (*B)->GetStringField(TEXT("cell"));
		}
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (S->TryGetArrayField(TEXT("actors"), A) && A)
			for (const TSharedPtr<FJsonValue>& AV : *A)
			{
				const TSharedPtr<FJsonObject> O = AV.IsValid() ? AV->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				FActorTok Tk;
				Tk.Id = O->GetStringField(TEXT("id")); Tk.Name = O->GetStringField(TEXT("name")); Tk.Kind = O->GetStringField(TEXT("kind"));
				Tk.Cell = O->GetStringField(TEXT("start_cell"));
				if (MineKeys.Contains(O->GetStringField(TEXT("ref")))) Tk.Kind = TEXT("yours");
				N.Actors.Add(Tk);
			}
		if (S->TryGetArrayField(TEXT("steps"), A) && A)
			for (const TSharedPtr<FJsonValue>& StV : *A)
			{
				const TSharedPtr<FJsonObject> O = StV.IsValid() ? StV->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				FStep P;
				double Ms = 0; O->TryGetNumberField(TEXT("t_ms"), Ms); P.T = Ms / 1000.0;
				Ms = 1000; O->TryGetNumberField(TEXT("dur_ms"), Ms); P.Dur = FMath::Max(Ms / 1000.0, 0.1);
				P.Actor = O->GetStringField(TEXT("actor")); P.Verb = O->GetStringField(TEXT("verb")).ToLower();
				P.ToCell = O->GetStringField(TEXT("to_cell")); P.Target = O->GetStringField(TEXT("target_actor"));
				P.Caption = O->GetStringField(TEXT("caption")); P.Mood = O->GetStringField(TEXT("mood"));
				const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
				if (O->TryGetArrayField(TEXT("path"), Path) && Path) for (const TSharedPtr<FJsonValue>& C : *Path) if (C.IsValid() && C->Type == EJson::String) P.Path.Add(C->AsString());
				N.Steps.Add(P);
			}
		const TSharedPtr<FJsonObject>* Dice = nullptr;
		if (S->TryGetObjectField(TEXT("dice"), Dice) && Dice)
		{
			double X = 0;
			if ((*Dice)->TryGetNumberField(TEXT("roll"), X)) N.Roll = (int32)X;
			if ((*Dice)->TryGetNumberField(TEXT("modifier"), X)) N.Mod = (int32)X;
			if ((*Dice)->TryGetNumberField(TEXT("at_ms"), X)) N.DiceAt = X / 1000.0;
			N.Verdict = (*Dice)->GetStringField(TEXT("verdict"));
		}
		const TSharedPtr<FJsonObject>* Out = nullptr;
		if (S->TryGetObjectField(TEXT("outcome"), Out) && Out) { N.OutcomeText = (*Out)->GetStringField(TEXT("text")); N.Result = (*Out)->GetStringField(TEXT("result")); }
		double Tot = 9000; S->TryGetNumberField(TEXT("total_ms"), Tot); N.TotalS = FMath::Clamp(Tot / 1000.0, 2.0, 12.0);
		UE_LOG(LogTemp, Display, TEXT("FNScene: queued %s at %s (%s): %d actors, %d steps, d20 %d at %.1fs, %.1fs"), *N.Id, *N.PlaceCell, *N.PlaceName, N.Actors.Num(), N.Steps.Num(), N.Roll, N.DiceAt, N.TotalS);
		Queue.Add(MoveTemp(N));
	}
	if (Queue.Num() > 2) Queue.RemoveAt(0, Queue.Num() - 2);   // keep the newest
}

void AFNScenePlayer::Clear()
{
	for (UStaticMeshComponent* C : Parts) if (C) C->DestroyComponent();
	Parts.Reset();
	Colors.Reset();
	for (FActorTok& A : Now.Actors) if (A.Text) A.Text->DestroyComponent();
	if (Banner) { Banner->DestroyComponent(); Banner = nullptr; }
	Roof = nullptr;
}

void AFNScenePlayer::Start()
{
	using namespace FNScenePlayerImpl;
	AFNStudio* St = Studio(GetWorld());
	Now = Queue[0];
	Queue.RemoveAt(0);
	TArray<FString> P;
	if (!St || Now.PlaceCell.ParseIntoArray(P, TEXT(":")) != 4) { UE_LOG(LogTemp, Warning, TEXT("FNScene: bad place cell '%s'"), *Now.PlaceCell); return; }
	static const TCHAR* Names[4] = { TEXT("people"), TEXT("houses"), TEXT("villages"), TEXT("regions") };
	static const double HexM[4] = { 3.0, 30.0, 3000.0, 120000.0 };
	int32 L = 0;
	for (int32 i = 0; i < 4; ++i) if (P[1] == Names[i]) L = i;
	double La, Lo;
	const FVector2D Pl = FNPlanet::HexToPlane(FCString::Atoi(*P[2]), FCString::Atoi(*P[3]), HexM[L]);
	FNPlanet::VecToLatLon(FNPlanet::PlaneToVec(FCString::Atoi(*P[0]), Pl.X, Pl.Y), La, Lo);
	St->GetViewSpot(SavedLat, SavedLon, bSavedPicked);
	SavedLevel = St->GetMapLevel();
	St->FocusBoard(La, Lo, 0);   // the camera drops to the people at the place
	bool bP; St->GetViewSpot(PlayLat, PlayLon, bP);
	T = -1.2f;   // the drop settles first
	bPlaying = true; bDiceDone = false;
	const FColor Pal[5] = { FColor(240, 180, 60), FColor(230, 110, 150), FColor(90, 170, 240), FColor(140, 210, 110), FColor(200, 200, 200) };
	for (int32 i = 0; i < Now.Actors.Num(); ++i)
	{
		FActorTok& A = Now.Actors[i];
		const FColor C = A.Kind == TEXT("yours") ? FColor(255, 200, 40) : Pal[(i + 1) % 5];
		A.Body = Piece(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), C);
		A.Head = Piece(TEXT("/Engine/BasicShapes/Sphere.Sphere"), C);
		A.Text = Label(FColor::White);
		if (!CellPos(A.Cell, A.Pos)) A.Pos = FVector::ZeroVector;
	}
	if (!Now.RoofCell.IsEmpty()) Roof = Piece(TEXT("/Engine/BasicShapes/Cone.Cone"), FColor(170, 60, 40));
	Banner = Label(FColor(255, 230, 150));
	UE_LOG(LogTemp, Display, TEXT("FNScene: playing %s at %.4f,%.4f (from %.2f,%.2f level %d)"), *Now.Id, La, Lo, SavedLat, SavedLon, SavedLevel);
}

void AFNScenePlayer::Finish(bool bReturn)
{
	Clear();
	bPlaying = false;
	AFNStudio* St = FNScenePlayerImpl::Studio(GetWorld());
	if (St && bReturn)
	{
		St->FocusBoard(SavedLat, SavedLon, SavedLevel);
		if (!bSavedPicked) St->MapHome();   // you were following your demigod: follow it again
	}
	UE_LOG(LogTemp, Display, TEXT("FNScene: done %s, %s"), *Now.Id, bReturn ? TEXT("back to your view") : TEXT("you moved the view, left as is"));
}

void AFNScenePlayer::Tick(float DeltaSeconds)
{
	using namespace FNScenePlayerImpl;
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNScenePlayer::OnMessage); break; }
	AFNStudio* St = Studio(W);
	if (!St) return;
	if (!bPlaying)
	{
		if (Queue.Num() > 0 && !Busy()) Start();
		return;
	}
	double La, Lo; bool bP;
	St->GetViewSpot(La, Lo, bP);
	if (FMath::Abs(La - PlayLat) > 1e-6 || FMath::Abs(Lo - PlayLon) > 1e-6 || St->GetMapLevel() != 0) { Finish(false); return; }
	if (St->IsZoomAnimating()) return;
	T += DeltaSeconds;
	if (T < 0.f) return;

	// the size of a people hexagon on the board now
	int32 Q0, R0; float Tp; FVector C0, C1;
	const float D = St->BoardCentreHex(Q0, R0) && St->BoardHexTop(Q0, R0, true, Tp, C0) && St->BoardHexTop(Q0 + 1, R0, true, Tp, C1) ? FMath::Max(FVector::Dist2D(C0, C1), 0.5f) : 5.f;
	FVector Cam = FVector::ZeroVector;
	if (APlayerController* PC = W->GetFirstPlayerController()) if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	auto Face = [&](USceneComponent* C, const FVector& P) { C->SetWorldLocation(P); C->SetWorldRotation((Cam - P).GetSafeNormal2D().Rotation()); };

	// where each token is now: its start, then every step that has begun
	float RoofLift = 0.f;
	FVector RoofPos;
	const bool bRoof = Roof && CellPos(Now.RoofCell, RoofPos);
	for (FActorTok& A : Now.Actors)
	{
		FVector P0;
		if (!CellPos(A.Cell, P0)) continue;
		FVector Pos = P0;
		bool bInside = false, bFallen = false;
		float Hop = 0.f, Jab = 0.f, Kneel = 0.f;
		FVector JabDir = FVector::ZeroVector;
		FString Say, SayMood;
		for (const FStep& S : Now.Steps)
		{
			if (S.Actor != A.Id || T < S.T) continue;
			const float U = FMath::Clamp((T - S.T) / S.Dur, 0.f, 1.f);
			FVector To = Pos;
			if (!S.ToCell.IsEmpty()) CellPos(S.ToCell, To);
			for (const FActorTok& B : Now.Actors)
				if (B.Id == S.Target && !S.Target.IsEmpty() && IsToward(S.Verb)) { To = B.Pos + (Pos - B.Pos).GetSafeNormal2D() * D * (S.Verb == TEXT("embrace") ? 0.35f : 0.8f); JabDir = (B.Pos - Pos).GetSafeNormal2D(); }
			if (S.Path.Num() > 0 && IsMove(S.Verb))
			{
				// along the path's cells, evenly
				TArray<FVector> Pts{ Pos };
				for (const FString& C : S.Path) { FVector X; if (CellPos(C, X)) Pts.Add(X); }
				if (!S.ToCell.IsEmpty()) Pts.Add(To);
				const float F = U * (Pts.Num() - 1);
				const int32 I = FMath::Min((int32)F, Pts.Num() - 2);
				Pos = Pts.Num() > 1 ? FMath::Lerp(Pts[I], Pts[I + 1], F - I) : Pos;
			}
			else if (IsMove(S.Verb) || IsToward(S.Verb)) Pos = FMath::Lerp(Pos, To, U);
			if (S.Verb == TEXT("enter") || S.Verb == TEXT("hide")) { bInside = U >= 1.f; if (bRoof) RoofLift = FMath::Max(RoofLift, FMath::Min((T - S.T) / 0.6f, 1.f)); }
			if (S.Verb == TEXT("exit")) bInside = U < 0.05f;
			if (S.Verb == TEXT("fall")) bFallen = U > 0.4f;
			if (S.Verb == TEXT("kneel")) Kneel = U;
			if (S.Verb == TEXT("celebrate") && U < 1.f) Hop = FMath::Abs(FMath::Sin(U * PI * 4.f)) * D * 0.4f;
			if ((S.Verb == TEXT("fight") || S.Verb == TEXT("slap")) && U < 1.f) Jab = FMath::Abs(FMath::Sin(U * PI * (S.Verb == TEXT("slap") ? 2.f : 5.f))) * D * 0.25f;
			if (S.Verb == TEXT("flee")) Hop = FMath::Abs(FMath::Sin(T * 14.f)) * D * 0.1f;
			if (!S.Caption.IsEmpty() && T < S.T + FMath::Max(S.Dur, 2.5f)) { Say = S.Caption; SayMood = S.Mood; }
			if (T - S.T < DeltaSeconds * 1.5f) UE_LOG(LogTemp, Display, TEXT("FNScene: %.1fs %s %s%s%s"), T, *A.Name, *S.Verb, S.Target.IsEmpty() ? TEXT("") : TEXT(" -> "), *S.Target);
		}
		A.Pos = Pos;
		const float Ht = D * 1.8f * (1.f - 0.4f * Kneel), Wd = D * 0.6f;   // big game pieces: they read from the chair
		const FVector Base = Pos + JabDir * Jab + FVector(0, 0, Hop);
		A.Body->SetVisibility(!bInside); A.Head->SetVisibility(!bInside);
		const FRotator Tip = bFallen ? FRotator(0.f, (Cam - Pos).Rotation().Yaw + 90.f, 85.f) : FRotator::ZeroRotator;
		const FVector Up = Tip.RotateVector(FVector::UpVector);
		A.Body->SetWorldTransform(FTransform(Tip, Base + Up * Ht * 0.45f, FVector(Wd, Wd, Ht * 0.9f) / 100.f));
		A.Head->SetWorldTransform(FTransform(Tip, Base + Up * (Ht * 0.9f + Wd * 0.5f), FVector(Wd * 1.1f) / 100.f));
		// the name always, what they say while they say it
		A.Text->SetVisibility(!bInside || !Say.IsEmpty());
		A.Text->SetText(FText::FromString(Say.IsEmpty() ? A.Name : FString::Printf(TEXT("%s: \"%s\""), *A.Name, *Say)));
		A.Text->SetTextRenderColor(Say.IsEmpty() ? FColor(230, 230, 230) : MoodCol(SayMood));
		A.Text->SetWorldSize(D * (Say.IsEmpty() ? 0.6f : 0.85f));
		Face(A.Text, Base + FVector(0, 0, Ht + Wd * 1.3f + D * 0.1f));
	}
	if (bRoof)
	{
		// the roof of the house lifts while someone is inside, so you see in
		const float Wd = D * 2.4f;
		Roof->SetWorldTransform(FTransform(FRotator::ZeroRotator, RoofPos + FVector(0, 0, D * 1.6f + RoofLift * D * 2.5f), FVector(Wd, Wd, D * 1.1f) / 100.f));
	}
	if (!bDiceDone && Now.DiceAt >= 0.f && T >= Now.DiceAt)
	{
		bDiceDone = true;
		St->RollDice(Now.Roll, Now.Mod, Now.Verdict);
		UE_LOG(LogTemp, Display, TEXT("FNScene: d20 %d %+d %s"), Now.Roll, Now.Mod, *Now.Verdict);
	}
	FVector Place;
	if (Banner && T >= Now.TotalS - 2.f && CellPos(Now.PlaceCell, Place) && !Now.OutcomeText.IsEmpty())
	{
		Banner->SetVisibility(true);
		Banner->SetText(FText::FromString(Now.OutcomeText.Left(70)));
		Banner->SetWorldSize(D * 0.6f);
		Face(Banner, Place + FVector(0, 0, D * 3.2f));
	}
	if (T >= Now.TotalS + 2.5f && !Busy()) Finish(true);
}

bool UFNScenePlayerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNScenePlayerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNScenePlayer>(AFNScenePlayer::StaticClass(), FTransform::Identity, P);
}
