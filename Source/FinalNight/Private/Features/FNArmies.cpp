#include "Features/FNArmies.h"
#include "../../UI/FN7KitHitTest.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "../../UI/FNMedia.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

namespace FNArmiesImpl
{
	static constexpr float FigH = 7.f, Gap = 4.2f;       // cm on the table: taller than the tree canopies so they read from the chair
	static constexpr double StaleS = 40.0;              // a group the server stopped listing goes away
	static constexpr double WalkS = 3.0;                // seconds to walk from its old spot to its new one
	static const FLinearColor Neutral(0.55f, 0.53f, 0.5f), MobCol(0.75f, 0.22f, 0.18f);

	bool LonLat(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& Lon, double& Lat)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O.IsValid() || !O->TryGetArrayField(Key, A) || !A || A->Num() < 2) return false;
		Lon = (*A)[0]->AsNumber(); Lat = (*A)[1]->AsNumber();
		return true;
	}
	int32 Figures(int32 Kind, int32 Size)
	{
		const double L = FMath::LogX(10.0, FMath::Max(Size, 1));
		return Kind == 0 ? FMath::Clamp(FMath::RoundToInt(L * 5.0), 4, 36) : FMath::Clamp(FMath::RoundToInt(L * 3.0), 3, 18);
	}
}

AFNArmies::AFNArmies()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Mat.Succeeded()) BaseMat = Mat.Object;
}

void AFNArmies::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

void AFNArmies::ReadDemigods(const TArray<TSharedPtr<FJsonValue>>& List)
{
	for (const TSharedPtr<FJsonValue>& V : List)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		FString Ref, A;
		if (!O.IsValid() || !O->TryGetStringField(TEXT("ref"), Ref)) continue;
		if (O->TryGetStringField(TEXT("aura"), A) && A.StartsWith(TEXT("#")) && A.Len() >= 7) Aura.Add(Ref, FLinearColor::FromSRGBColor(FColor::FromHex(A)));
	}
}

void AFNArmies::Upsert(const TSharedPtr<FJsonObject>& O, int32 Kind)
{
	using namespace FNArmiesImpl;
	FString Ref;
	if (!O.IsValid() || !O->TryGetStringField(TEXT("ref"), Ref)) return;
	FGroup G;
	G.Kind = Kind;
	if (!LonLat(O, Kind == 0 ? TEXT("pos") : TEXT("at"), G.Lon, G.Lat) && !LonLat(O, TEXT("pos"), G.Lon, G.Lat)) return;
	O->TryGetStringField(TEXT("name"), G.Name);
	double N = 1; O->TryGetNumberField(TEXT("size"), N); G.Size = FMath::Max(1, (int32)N);
	if (Kind == 0) O->TryGetStringField(TEXT("demigod_ref"), G.Owner);
	else
	{
		// a crowd leans to the demigod with the highest allegiance (none above ~0.2 = neutral)
		const TArray<TSharedPtr<FJsonValue>>* Al = nullptr;
		double Best = 0.2;
		if (O->TryGetArrayField(TEXT("allegiance"), Al) && Al)
			for (const TSharedPtr<FJsonValue>& V : *Al)
			{
				const TSharedPtr<FJsonObject> A = V.IsValid() ? V->AsObject() : nullptr;
				double Val = 0;
				if (A.IsValid() && A->TryGetNumberField(TEXT("value"), Val) && Val > Best) { Best = Val; A->TryGetStringField(TEXT("demigod_ref"), G.Owner); }
			}
	}
	G.Ref = Ref;
	G.SeenAt = FPlatformTime::Seconds();
	const FString Key = Kind == 2 ? TEXT("mob:") + Ref : Ref;
	G.FromLon = G.Lon; G.FromLat = G.Lat;
	if (const FGroup* Old = Groups.Find(Key))
	{
		// it moved: walk from where it stands now to the new spot (between beats, not a jump)
		const float U = FMath::Clamp((float)((G.SeenAt - Old->MovedAt) / WalkS), 0.f, 1.f);
		G.FromLon = FMath::Lerp(Old->FromLon, Old->Lon, U); G.FromLat = FMath::Lerp(Old->FromLat, Old->Lat, U);
		G.MovedAt = (FMath::Abs(G.Lon - Old->Lon) + FMath::Abs(G.Lat - Old->Lat) > 1e-6) ? G.SeenAt : Old->MovedAt;
		if (G.MovedAt == Old->MovedAt) { G.FromLon = Old->FromLon; G.FromLat = Old->FromLat; }
		G.PinLat = Old->PinLat; G.PinLon = Old->PinLon; G.PinLevel = Old->PinLevel;   // keep where a zoom-in put it
		G.DrawnLat = Old->DrawnLat; G.DrawnLon = Old->DrawnLon; G.DrawnLevel = Old->DrawnLevel; G.DrawnAt = Old->DrawnAt; G.DrawnR = Old->DrawnR;
	}
	Groups.Add(Key, G);
}

void AFNArmies::AskMedia(const FString& Ref)
{
	UFNNetClient* N = Net.Get();
	const double Now = FPlatformTime::Seconds();
	if (!N || ArmyTex.Contains(Ref)) return;
	if (const double* At = MediaAskedAt.Find(Ref)) if (Now - *At < 12.0) return;   // pending images: ask again now and then
	MediaAskedAt.Add(Ref, Now);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetStringField(TEXT("kind"), TEXT("army"));
	A->SetStringField(TEXT("ref"), Ref);
	N->Query(TEXT("game.media"), A);
	MediaQueue.Remove(Ref);
	MediaQueue.Add(Ref);
}

void AFNArmies::OnMedia(const TSharedPtr<FJsonObject>& D)
{
	FString Ref, Kind;
	const TSharedPtr<FJsonObject>* Img = nullptr;
	FString Status, Url, Hash;
	if (!D->TryGetObjectField(TEXT("image"), Img) || !Img) return;
	// the reply carries kind (and maybe ref) inside "image"; it answers our oldest open ask otherwise
	if (!(*Img)->TryGetStringField(TEXT("kind"), Kind)) D->TryGetStringField(TEXT("kind"), Kind);
	if (!Kind.IsEmpty() && Kind != TEXT("army")) return;
	if (!(*Img)->TryGetStringField(TEXT("ref"), Ref)) D->TryGetStringField(TEXT("ref"), Ref);
	if (Ref.IsEmpty() && MediaQueue.Num() > 0) Ref = MediaQueue[0];
	MediaQueue.Remove(Ref);
	if (Ref.IsEmpty()) return;
	(*Img)->TryGetStringField(TEXT("status"), Status);
	(*Img)->TryGetStringField(TEXT("url"), Url);
	(*Img)->TryGetStringField(TEXT("hash"), Hash);
	UE_LOG(LogTemp, Display, TEXT("FNArmies: image for %s %s %s"), *Ref, *Status, *Hash.Left(12));
	if (Status != TEXT("ready") || Url.IsEmpty() || Hash.IsEmpty() || Downloading.Contains(Hash)) return;
	TArray<uint8> Png;
	if (FN7Media::LoadCached(Hash, Png)) { if (UTexture2D* T = FN7Media::TextureFromPng(Png)) ArmyTex.Add(Ref, T); return; }
	UFNNetClient* N = Net.Get();
	if (!N) return;
	Downloading.Add(Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(FN7Media::PathFor(Hash)));   // FN7 (Mac session, lead: pictures by their full hash only, from /media/<hash>.png; the cache checks the server byte length)
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakObjectPtr<AFNArmies> W(this);
	Req->OnProcessRequestComplete().BindLambda([W, Hash, Ref](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (!W.IsValid()) return;
		W->Downloading.Remove(Hash);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { UE_LOG(LogTemp, Display, TEXT("FNArmies: image download failed for %s (HTTP %d)"), *Ref, Resp.IsValid() ? Resp->GetResponseCode() : 0); return; }
		const TArray<uint8> Data = Resp->GetContent();
		if (UTexture2D* T = FN7Media::TextureFromPng(Data))
		{
			FN7Media::SaveCached(Hash, Data);
			W->ArmyTex.Add(Ref, T);
			UE_LOG(LogTemp, Display, TEXT("FNArmies: image for %s drawn (%dx%d)"), *Ref, T->GetSizeX(), T->GetSizeY());
		}
	});
	Req->ProcessRequest();
}

AFNArmies::FCard& AFNArmies::CardFor(const FString& Ref)
{
	if (FCard* C = Cards.Find(Ref)) return *C;
	static UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Masked.Widget3DPassThrough_Masked"));
	auto Make = [&](const TCHAR* Mesh)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->RegisterComponent();
		return C;
	};
	FCard K;
	K.Image = Make(TEXT("/Engine/BasicShapes/Cube.Cube"));   // a thin card: the picture on both faces
	if (Unlit) { UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Unlit, K.Image); if (TObjectPtr<UTexture2D>* T = ArmyTex.Find(Ref)) M->SetTextureParameterValue(TEXT("SlateUI"), *T); K.Image->SetMaterial(0, M); }
	K.Base = Make(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	K.Rim = Make(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (UStaticMeshComponent* C : { K.Base, K.Rim })
		if (BaseMat) C->SetMaterial(0, UMaterialInstanceDynamic::Create(BaseMat, C));
	return Cards.Add(Ref, K);
}

void AFNArmies::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	if (!D.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	const TSharedPtr<FJsonObject>* O = nullptr;
	// auras: every public demigod list
	if (D->TryGetArrayField(TEXT("demigods"), L) && L) ReadDemigods(*L);
	if (D->TryGetObjectField(TEXT("public_state"), O) && O && (*O)->TryGetArrayField(TEXT("demigods"), L) && L) ReadDemigods(*L);
	// yours: the seat view
	const TSharedPtr<FJsonObject>* You = nullptr;
	const TSharedPtr<FJsonObject>* V = nullptr;
	if ((D->TryGetObjectField(TEXT("you"), You) && You && (*You)->TryGetObjectField(TEXT("private_state"), V) && V) || (D->TryGetObjectField(TEXT("private"), V) && V))
		if ((*V)->TryGetArrayField(TEXT("demigods"), L) && L)
		{
			Mine.Reset();
			for (const TSharedPtr<FJsonValue>& H : *L) if (H.IsValid() && H->AsObject().IsValid()) Mine.Add(H->AsObject()->GetStringField(TEXT("ref")));
		}
	if (Type == TEXT("game.media")) { OnMedia(D); return; }
	if (Type != TEXT("game.globe") && Type != TEXT("game.region")) return;
	if (D->TryGetArrayField(TEXT("armies"), L) && L) for (const TSharedPtr<FJsonValue>& A : *L) Upsert(A->AsObject(), 0);
	if (D->TryGetArrayField(TEXT("mobs"), L) && L) for (const TSharedPtr<FJsonValue>& A : *L) Upsert(A->AsObject(), 2);
	if (D->TryGetObjectField(TEXT("crowds"), O) && O && (*O)->TryGetArrayField(TEXT("items"), L) && L) for (const TSharedPtr<FJsonValue>& A : *L) Upsert(A->AsObject(), 1);
}

AFNArmies::FSet& AFNArmies::SetFor(const FLinearColor& C)
{
	const uint32 Key = C.ToFColor(true).ToPackedARGB();
	if (FSet* S = Sets.Find(Key)) return *S;
	auto Make = [&](const TCHAR* Mesh, const FLinearColor& Col)
	{
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
		I->SetupAttachment(RootComponent);
		I->SetMobility(EComponentMobility::Movable);
		I->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetCastShadow(false);
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, I);
			M->SetVectorParameterValue(TEXT("Color"), Col);
			I->SetMaterial(0, M);
		}
		I->RegisterComponent();
		Owned.Add(I);
		return I;
	};
	FSet S;
	S.Body = Make(TEXT("/Engine/BasicShapes/Cone.Cone"), C);
	S.Head = Make(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FLinearColor(0.85f, 0.78f, 0.68f));
	S.Flag = Make(TEXT("/Engine/BasicShapes/Cube.Cube"), C * 1.4f);
	S.Tile = Make(TEXT("/Engine/BasicShapes/Cube.Cube"), C * 0.55f);
	return Sets.Add(Key, S);
}

void AFNArmies::Tick(float DeltaSeconds)
{
	using namespace FNArmiesImpl;
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	SinceDraw += DeltaSeconds;
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNArmies::OnMessage); break; }
	if (SinceDraw < 0.05f && !FN7Kit::BoardMovingAny(W)) return;   // 20 a second at rest; FN7 (Mac session): every frame while the map moves
	SinceDraw = 0.f;
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;

	// The board's hex grid in world space: the centre hex and its two axial neighbours give the lattice, so every piece
	// stands exactly on hexagons (Benito: armies share 1-3 hexagons zoomed out, one figure per hexagon fully zoomed in).
	int32 Q0 = 0, R0 = 0;
	float T0 = 0.f, Tq = 0.f, Tr = 0.f;
	FVector C0 = FVector::ZeroVector, Cq = FVector::ZeroVector, Cr = FVector::ZeroVector;
	const bool bGrid = St && St->BoardCentreHex(Q0, R0) && St->BoardHexTop(Q0, R0, true, T0, C0) && St->BoardHexTop(Q0 + 1, R0, true, Tq, Cq) && St->BoardHexTop(Q0, R0 + 1, true, Tr, Cr);
	const FVector2D Ea(Cq.X - C0.X, Cq.Y - C0.Y), Eb(Cr.X - C0.X, Cr.Y - C0.Y);
	const float Det = Ea.X * Eb.Y - Ea.Y * Eb.X;
	const float D = Ea.Size();   // centre-to-centre = flat-to-flat width of one hexagon (cm)
	const int32 Level = St ? St->GetMapLevel() : 3;
	// the hex under a world point, and that hex's top centre
	auto HexOf = [&](const FVector& P, int32& Q, int32& R)
	{
		const float Dx = P.X - C0.X, Dy = P.Y - C0.Y;
		const float X = (Dx * Eb.Y - Dy * Eb.X) / Det, Y = (Ea.X * Dy - Ea.Y * Dx) / Det;   // fractional axial
		float Z = -X - Y, Rx = FMath::RoundToFloat(X), Ry = FMath::RoundToFloat(Y), Rz = FMath::RoundToFloat(Z);
		const float Ex = FMath::Abs(Rx - X), Ey = FMath::Abs(Ry - Y), Ez = FMath::Abs(Rz - Z);
		if (Ex > Ey && Ex > Ez) Rx = -Ry - Rz; else if (Ey > Ez) Ry = -Rx - Rz;
		Q = Q0 + (int32)Rx; R = R0 + (int32)Ry;
	};
	auto HexTop = [&](int32 Q, int32 R, FVector& Out) { float Tp; return St->BoardHexTop(Q, R, true, Tp, Out); };
	// hexagons around a centre, nearest first (rings 0..11 = 397 hexagons)
	static TArray<FIntPoint> Spiral;
	if (Spiral.Num() == 0)
	{
		static const FIntPoint Dir[6] = { {1,0}, {1,-1}, {0,-1}, {-1,0}, {-1,1}, {0,1} };
		Spiral.Add(FIntPoint(0, 0));
		for (int32 K = 1; K <= 11; ++K)
		{
			FIntPoint H = Dir[4] * K;
			for (int32 Side = 0; Side < 6; ++Side) for (int32 Step = 0; Step < K; ++Step) { Spiral.Add(H); H += Dir[Side]; }
		}
	}
	// zooming in on a group's hexagon must land on that group (Benito): a group whose spot lies inside the coarser
	// hexagon the board was opened on stands at the board's centre at this finer level
	static constexpr double LevelHexM[] = { 3.0, 30.0, 3000.0, 120000.0 };
	double CLat = 0.0, CLon = 0.0;
	const bool bCentre = bGrid && St->BoardHexLatLon(Q0, R0, CLat, CLon);
	auto Meters = [](double La1, double Lo1, double La2, double Lo2)
	{
		const double A = FMath::DegreesToRadians(La1), B = FMath::DegreesToRadians(La2);
		return 6371000.0 * FMath::Acos(FMath::Clamp(FMath::Sin(A) * FMath::Sin(B) + FMath::Cos(A) * FMath::Cos(B) * FMath::Cos(FMath::DegreesToRadians(Lo2 - Lo1)), -1.0, 1.0));
	};

	const double Now = FPlatformTime::Seconds();
	for (auto It = Groups.CreateIterator(); It; ++It) if (Now - It.Value().SeenAt > StaleS) It.RemoveCurrent();
	FVector Cam = FVector::ZeroVector;
	if (APlayerController* PC = W->GetFirstPlayerController()) if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	TSet<FString> CardsShown;
	struct FBuf { TArray<FTransform> Body, Head, Flag, Tile; };
	TMap<uint32, FBuf> Bufs;
	FString ArmyLog;
	TMap<FString, FBox2D> NewRects;
	TSet<int64> Taken;   // one piece per hexagon (board-game rule): a later group steps to the next free one
	int32 Shown = 0, ShownMine = 0;
	// zooming in on a group's hexagon must land on that group (Benito: double-click the army's hexagon to dive into its
	// soldiers): once a zoom settles one level finer, a group whose hexagon held the new centre is pinned there
	const bool bSettled = St && !St->IsZoomAnimating();
	// the spot the player double-clicked: where the pointer was when the zoom began
	// (the level can switch a frame before the zoom animation starts, so either one marks the start)
	if (St && ClickLevel < 0 && StableLevel >= 0 && ((!bSettled && !bWasAnimating) || Level != SeenLevel))
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			float Mx, My; FVector Wc; double Hm;
			if (PC->GetMousePosition(Mx, My) && St->HexAtScreen(FVector2D(Mx, My), ClickLat, ClickLon, Wc, Hm))
			{
				ClickLevel = StableLevel;
				// a click on a standing picture counts as on that army (the hexagon under the pointer lies behind it)
				ClickedCards.Reset();
				for (const TPair<FString, FBox2D>& R : CardRects) if (R.Value.IsInside(FVector2D(Mx, My))) ClickedCards.Add(R.Key);
				UE_LOG(LogTemp, Display, TEXT("FNArmies: zoom from level %d at %.3f,%.3f (%d pictures under the pointer)"), StableLevel, ClickLat, ClickLon, ClickedCards.Num());
				for (const TPair<FString, FBox2D>& R : CardRects) UE_LOG(LogTemp, Display, TEXT("FNArmies:   pointer %.0f,%.0f picture %s at %.0f,%.0f - %.0f,%.0f"), Mx, My, *R.Key, R.Value.Min.X, R.Value.Min.Y, R.Value.Max.X, R.Value.Max.Y);
			}
		}
	bWasAnimating = !bSettled;
	SeenLevel = Level;
	const bool bZoomedIn = bSettled && ClickLevel >= 0 && StableLevel >= 0 && Level < StableLevel;
	for (TPair<FString, FGroup>& It : Groups)
	{
		FGroup& G = It.Value;
		if (G.PinLevel >= 0 && Level >= G.PinLevel) G.PinLevel = -1;
		if (bZoomedIn && (ClickedCards.Contains(G.Ref) || G.Owner.StartsWith(TEXT("dg:0")) || G.Owner.StartsWith(TEXT("dg:4"))))
			UE_LOG(LogTemp, Display, TEXT("FNArmies: check %s drawn L%d %.1fs ago %.0f km from click (reach %.0f km) card %d"), *G.Ref, G.DrawnLevel, Now - G.DrawnAt, Meters(G.DrawnLat, G.DrawnLon, ClickLat, ClickLon) / 1000.0, G.DrawnR / 1000.0, ClickedCards.Contains(G.Ref) ? 1 : 0);   // zoomed back out: it stands where it really is
		if (bZoomedIn && G.DrawnLevel == StableLevel && Now - G.DrawnAt < 5.0 && (ClickedCards.Contains(G.Ref) || Meters(G.DrawnLat, G.DrawnLon, ClickLat, ClickLon) <= G.DrawnR))
		{
			// the zoom lands centred on the clicked hexagon: stand there
			G.PinLat = bCentre ? CLat : ClickLat; G.PinLon = bCentre ? CLon : ClickLon;
			G.PinLevel = FMath::Max(G.PinLevel, StableLevel); UE_LOG(LogTemp, Display, TEXT("FNArmies: %s pinned to the double-clicked spot"), *G.Ref);
		}
		FVector Foot;
		const double Wk = FMath::Clamp((Now - G.MovedAt) / WalkS, 0.0, 1.0);
		const bool bPinned = G.PinLevel >= 0;
		const double GLat = bPinned ? G.PinLat : FMath::Lerp(G.FromLat, G.Lat, Wk), GLon = bPinned ? G.PinLon : FMath::Lerp(G.FromLon, G.Lon, Wk);
		if (!bGrid || Det == 0.f) continue;
		if (G.Kind == 0) ArmyLog += FString::Printf(TEXT(" %s[%s %d @%.1f,%.1f%s]"), *G.Ref, *G.Owner, G.Size, GLat, GLon, bPinned ? TEXT(" pinned") : TEXT(""));
		if (!St->BoardPointFor(GLat, GLon, Foot)) continue;
		if (G.Kind == 0) ArmyLog += TEXT("*");   // * = on the board
		const bool bMine = !G.Owner.IsEmpty() && Mine.Contains(G.Owner);
		const FLinearColor* A = G.Owner.IsEmpty() ? nullptr : Aura.Find(G.Owner);
		FLinearColor C = G.Kind == 2 ? MobCol : A ? *A : Neutral;
		if (bMine) C *= 1.35f;
		FSet& S = SetFor(C);
		FBuf& B = Bufs.FindOrAdd(C.ToFColor(true).ToPackedARGB());
		int32 Qc, Rc;
		HexOf(Foot, Qc, Rc);
		// how many hexagons it covers: fully zoomed in, one person per hexagon (up to the 19 around its centre);
		// farther out the whole group is one piece on 1, 2 or 3 hexagons by its size
		// fully zoomed in: one person per hexagon; one level out (houses): the group spreads over up to ~100 hexagons with a
		// few soldiers on each; farther out: one piece on 1, 2 or 3 hexagons by its size
		const int32 N = Level == 0 ? FMath::Clamp(G.Size, 1, FMath::Min(19, Spiral.Num())) : Level == 1 ? FMath::Clamp(G.Size / 10, 7, 127) : (G.Size >= 10000 ? 3 : G.Size >= 1000 ? 2 : 1);
		TArray<FVector, TInlineAllocator<128>> Hexes;
		for (int32 i = 0, Placed = 0; i < Spiral.Num() && Placed < N; ++i)
		{
			const int32 Q = Qc + Spiral[i].X, R = Rc + Spiral[i].Y;
			const int64 Key = ((int64)Q << 32) ^ (uint32)R;
			FVector Hc;
			if (Taken.Contains(Key) || !HexTop(Q, R, Hc)) continue;
			Taken.Add(Key);
			// the main hexagon (the picture's): remember where it is on the map, for the next zoom in
			if (Placed == 0 && bSettled && St->BoardHexLatLon(Q, R, G.DrawnLat, G.DrawnLon)) { G.DrawnLevel = Level; G.DrawnAt = Now; }
			Hexes.Add(Hc);
			++Placed;
		}
		if (Hexes.Num() == 0) continue;
		// how far from its main hexagon a double-click still counts as on it: its spread, plus a hexagon or so (a click on
		// the standing picture lands behind it)
		G.DrawnR = LevelHexM[Level] * (1.6 + FMath::Sqrt(Hexes.Num() / 3.0));
		const float Fig = D * (Level == 0 ? 0.8f : 0.45f);   // a figure's height: fills its hexagon up close, three to a hexagon one level out
		auto Person = [&](const FVector& At, float H)
		{
			B.Body.Add(FTransform(FRotator::ZeroRotator, At + FVector(0, 0, H * 0.4f), FVector(H * 0.32f, H * 0.32f, H * 0.8f) / 100.f));
			B.Head.Add(FTransform(FRotator::ZeroRotator, At + FVector(0, 0, H * 0.92f), FVector(H * 0.26f) / 100.f));
		};
		if (Level <= 1)
		{
			// Benito described one picture of a distant army that splits into different images as the player gets closer: one level out
			// (houses) the army's picture stands as a few smaller cards across its hexagons, one per company; the main
			// picture's double-click outline is unchanged (these extra cards are not clickable)
			TObjectPtr<UTexture2D>* Tex1 = Level == 1 && G.Kind == 0 ? ArmyTex.Find(G.Ref) : nullptr;
			if (Level == 1 && G.Kind == 0) AskMedia(G.Ref);
			if (Tex1)
			{
				const int32 Nc = FMath::Clamp(Hexes.Num() / 12, 2, 5);
				const float Wd = D * (bMine ? 1.3f : 1.05f), Ht = Wd * (*Tex1)->GetSizeY() / FMath::Max(1, (*Tex1)->GetSizeX());
				for (int32 c = 0; c < Nc; ++c)
				{
					const FString Key = FString::Printf(TEXT("%s#%d"), *G.Ref, c);
					FCard& K = CardFor(Key);
					CardsShown.Add(Key);
					const FVector& At = Hexes[c * Hexes.Num() / Nc];
					if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(K.Image->GetMaterial(0))) M->SetTextureParameterValue(TEXT("SlateUI"), *Tex1);
					K.Image->SetWorldLocation(At + FVector(0, 0, Ht * 0.5f));
					K.Image->SetWorldRotation(FRotator(0.f, (Cam - At).GetSafeNormal2D().Rotation().Yaw + 90.f, 0.f));
					K.Image->SetWorldScale3D(FVector(Wd, Wd * 0.03f, Ht) / 100.f);
					K.Base->SetVisibility(false); K.Rim->SetVisibility(false);
				}
			}
			// individuals, standing still: one per hexagon fully zoomed in, three per hexagon one level out (when no picture yet)
			for (const FVector& H : Hexes)
			{
				if (Level == 0) { Person(H, Fig); continue; }
				if (Tex1) continue;
				for (int32 i = 0; i < 3; ++i)
				{
					const float Ang = 2.f * PI * i / 3.f + 0.5f;
					Person(H + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * D * 0.22f, Fig);
				}
			}
			if (G.Kind == 0)   // the banner at its centre
			{
				const float PoleH = D * (Level == 0 ? 2.2f : 1.4f) * (bMine ? 1.3f : 1.f);
				B.Flag.Add(FTransform(FRotator::ZeroRotator, Hexes[0] + FVector(0, 0, PoleH * 0.5f), FVector(D * 0.04f, D * 0.04f, PoleH) / 100.f));
				B.Flag.Add(FTransform(FRotator::ZeroRotator, Hexes[0] + FVector(0, D * 0.25f, PoleH - D * 0.18f), FVector(D * 0.02f, D * 0.5f, D * 0.32f) / 100.f));
			}
		}
		else
		{
			// the group's tile: a raised hexagon in its colour on each hexagon it holds
			for (const FVector& H : Hexes)
				for (int32 k = 0; k < 3; ++k)
					B.Tile.Add(FTransform(FRotator(0.f, 60.f * k + 30.f, 0.f), H + FVector(0, 0, D * 0.03f), FVector(D * 0.9f, D * 0.9f / 1.7320508f, D * 0.06f) / 100.f));
			TObjectPtr<UTexture2D>* Tex = G.Kind == 0 ? ArmyTex.Find(G.Ref) : nullptr;
			if (G.Kind == 0) AskMedia(G.Ref);
			if (Tex)
			{
				// its generated picture stands on the main hexagon, turned to you
				FCard& K = CardFor(G.Ref);
				CardsShown.Add(G.Ref);
				const float Wd = D * (bMine ? 2.0f : 1.6f), Ht = Wd * (*Tex)->GetSizeY() / FMath::Max(1, (*Tex)->GetSizeX());   // bigger than its hexagon so it reads from the chair
				if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(K.Image->GetMaterial(0))) M->SetTextureParameterValue(TEXT("SlateUI"), *Tex);
				K.Image->SetWorldLocation(Hexes[0] + FVector(0, 0, D * 0.06f + Ht * 0.5f));
				K.Image->SetWorldRotation(FRotator(0.f, (Cam - Hexes[0]).GetSafeNormal2D().Rotation().Yaw + 90.f, 0.f));
				K.Image->SetWorldScale3D(FVector(Wd, Wd * 0.03f, Ht) / 100.f);
				// where the picture sits on screen, so a double-click on it dives into this army
				if (APlayerController* PC = W->GetFirstPlayerController())
				{
					const FVector Bot = K.Image->GetComponentLocation() - FVector(0, 0, Ht * 0.5f), Top = Bot + FVector(0, 0, Ht);
					FVector2D Sb, St2;
					if (PC->ProjectWorldLocationToScreen(Bot, Sb) && PC->ProjectWorldLocationToScreen(Top, St2))
					{
						const float Hp = FMath::Abs(Sb.Y - St2.Y), Wp = Hp * Wd / FMath::Max(Ht, 1.f);
						NewRects.Add(G.Ref, FBox2D(FVector2D(Sb.X - Wp * 0.5f, FMath::Min(Sb.Y, St2.Y)), FVector2D(Sb.X + Wp * 0.5f, FMath::Max(Sb.Y, St2.Y) + Hp * 0.15f)));
					}
				}
				K.Base->SetVisibility(false); K.Rim->SetVisibility(false);
			}
			// (no picture yet: just its tile and banner, no little blobs)
			if (G.Kind == 0)   // the banner on the main hexagon (taller for yours)
			{
				const float PoleH = D * (bMine ? 1.2f : 0.8f);
				const FVector Corner = Hexes[0] + FVector(-D * 0.36f, -D * 0.2f, D * 0.06f);
				B.Flag.Add(FTransform(FRotator::ZeroRotator, Corner + FVector(0, 0, PoleH * 0.5f), FVector(D * 0.025f, D * 0.025f, PoleH) / 100.f));
				B.Flag.Add(FTransform(FRotator::ZeroRotator, Corner + FVector(0, D * 0.14f, PoleH - D * 0.1f), FVector(D * 0.015f, D * 0.28f, D * 0.18f) / 100.f));
			}
		}
		++Shown;
		if (bMine) ++ShownMine;
	}
	for (TPair<uint32, FSet>& It : Sets)
	{
		FBuf* B = Bufs.Find(It.Key);
		auto Fill = [](UInstancedStaticMeshComponent* I, const TArray<FTransform>* T)
		{
			if (!T || T->Num() == 0) { if (I->GetInstanceCount() > 0) I->ClearInstances(); return; }
			if (I->GetInstanceCount() != T->Num()) { I->ClearInstances(); I->AddInstances(*T, false, true); }
			else I->BatchUpdateInstancesTransforms(0, *T, true, true);
		};
		Fill(It.Value.Body, B ? &B->Body : nullptr);
		Fill(It.Value.Head, B ? &B->Head : nullptr);
		Fill(It.Value.Flag, B ? &B->Flag : nullptr);
		Fill(It.Value.Tile, B ? &B->Tile : nullptr);
	}
	for (TPair<FString, FCard>& It : Cards)
	{
		const bool bOn = CardsShown.Contains(It.Key);
		if (It.Value.Image->IsVisible() != bOn) It.Value.Image->SetVisibility(bOn);
		if (!bOn) { It.Value.Base->SetVisibility(false); It.Value.Rim->SetVisibility(false); }
	}
	const FString Log = FString::Printf(TEXT("%d known, %d on the board (%d yours, %d as pictures) at level %d, centre %.1f,%.1f"), Groups.Num(), Shown, ShownMine, CardsShown.Num(), Level, FMath::RoundToDouble(CLat), FMath::RoundToDouble(CLon));
	if (bSettled) CardRects = MoveTemp(NewRects);
	if (bSettled) { if (bZoomedIn || Level != StableLevel) ClickLevel = -1; StableLevel = Level; }
	if (Log != LastLog) { LastLog = Log; UE_LOG(LogTemp, Display, TEXT("FNArmies: %s; armies:%s"), *Log, *ArmyLog); }
}

bool UFNArmiesSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNArmiesSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNArmies>(AFNArmies::StaticClass(), FTransform::Identity, P);
}
