#include "Features/FNHouseCards.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "../../UI/FNMedia.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

namespace FNHouseCardsImpl
{
	/** "<face>:<level>:<q>:<r>" -> its parts. */
	static bool ParseCell(const FString& Cell, FString& Level, int32& Q, int32& R)
	{
		TArray<FString> P;
		Cell.ParseIntoArray(P, TEXT(":"));
		if (P.Num() != 4) return false;
		Level = P[1];
		Q = FCString::Atoi(*P[2]);
		R = FCString::Atoi(*P[3]);
		return true;
	}
	/** How big a building stands, in hexagon widths, by kind. */
	static float SizeOf(const FString& Kind)
	{
		const FString K = Kind.ToLower();
		if (K.Contains(TEXT("tower")) || K.Contains(TEXT("temple")) || K.Contains(TEXT("manor")) || K.Contains(TEXT("keep"))) return 1.5f;
		if (K.Contains(TEXT("hut")) || K.Contains(TEXT("stall"))) return 0.9f;
		return 1.15f;
	}
}

AFNHouseCards::AFNHouseCards()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AFNHouseCards::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

void AFNHouseCards::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	using namespace FNHouseCardsImpl;
	if (Type != TEXT("game.buildings") || !D.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!D->TryGetArrayField(TEXT("buildings"), Arr))
	{
		const TSharedPtr<FJsonObject>* Res = nullptr;
		if (!D->TryGetObjectField(TEXT("result"), Res) || !Res || !(*Res)->TryGetArrayField(TEXT("buildings"), Arr)) return;
	}
	if (!Arr) return;
	int32 WithPicture = 0;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> B = V.IsValid() ? V->AsObject() : nullptr;
		if (!B.IsValid()) continue;
		FString Cell;
		B->TryGetStringField(TEXT("cell"), Cell);
		FBuilding Bd;
		if (!ParseCell(Cell, Bd.Level, Bd.Q, Bd.R)) continue;
		B->TryGetStringField(TEXT("kind"), Bd.Kind);
		B->TryGetStringField(TEXT("settlement_name"), Bd.Settlement);
		B->TryGetBoolField(TEXT("wrecked"), Bd.bWrecked);
		const TSharedPtr<FJsonObject>* Sp = nullptr;
		if (B->TryGetObjectField(TEXT("sprite"), Sp) && Sp)
		{
			FString Status;
			(*Sp)->TryGetStringField(TEXT("status"), Status);
			if (Status.IsEmpty() || Status == TEXT("ready") || Status == TEXT("done"))
			{
				(*Sp)->TryGetStringField(TEXT("hash"), Bd.Hash);
				(*Sp)->TryGetStringField(TEXT("url"), Bd.Url);
			}
		}
		if (!Bd.Hash.IsEmpty()) { ++WithPicture; FetchPicture(Bd.Hash, Bd.Url); }
		Buildings.Add(Cell, Bd);
	}
	UE_LOG(LogTemp, Display, TEXT("FNHouses: game.buildings gave %d buildings (%d with a picture)"), Arr->Num(), WithPicture);
}

void AFNHouseCards::FetchPicture(const FString& Hash, const FString& Url)
{
	if (Tex.Contains(Hash) || Loading.Contains(Hash)) return;
	TArray<uint8> Png;
	if (FN7Media::LoadCached(Hash, Png))
	{
		if (UTexture2D* T = FN7Media::TextureFromPng(Png)) { Tex.Add(Hash, T); TexGrey.Add(Hash, FN7Media::TextureFromPng(Png, true)); }
		return;
	}
	UFNNetClient* N = Net.Get();
	if (!N || Url.IsEmpty()) return;
	Loading.Add(Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakObjectPtr<AFNHouseCards> Wk(this);
	const FString H = Hash;
	Req->OnProcessRequestComplete().BindLambda([Wk, H](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (!Wk.IsValid()) return;
		Wk->Loading.Remove(H);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		const TArray<uint8> Data = Resp->GetContent();
		if (UTexture2D* T = FN7Media::TextureFromPng(Data))
		{
			FN7Media::SaveCached(H, Data);
			Wk->Tex.Add(H, T);
			Wk->TexGrey.Add(H, FN7Media::TextureFromPng(Data, true));
		}
	});
	Req->ProcessRequest();
}

UStaticMeshComponent* AFNHouseCards::CardAt(int32 Index)
{
	while (Pool.Num() <= Index)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->RegisterComponent();
		Pool.Add(C);
	}
	return Pool[Index];
}

void AFNHouseCards::Tick(float DeltaSeconds)
{
	using namespace FNHouseCardsImpl;
	Super::Tick(DeltaSeconds);
	SinceAsk += DeltaSeconds;
	SinceDraw += DeltaSeconds;
	UWorld* W = GetWorld();
	if (!W || SinceDraw < 1.f / 30.f) return;
	SinceDraw = 0.f;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNHouseCards::OnMessage); break; }
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;   // (Mac clang -Werror: a loop that breaks at once is an error)

	int32 Q0 = 0, R0 = 0;
	float Tp = 0.f;
	FVector C0, C1;
	const int32 Level = St ? St->GetMapLevel() : -1;   // 0 people, 1 houses
	const bool bOn = St && (Level == 0 || Level == 1) && St->BoardCentreHex(Q0, R0)
		&& St->BoardHexTop(Q0, R0, true, Tp, C0) && St->BoardHexTop(Q0 + 1, R0, true, Tp, C1);
	int32 Used = 0;
	if (bOn)
	{
		const float D = FMath::Max(FVector::Dist2D(C0, C1), 0.5f);
		// ask for the buildings under the board when it moves (at most every 6 s)
		const FString Cell = St->BoardCellKey(Q0, R0);
		if (UFNNetClient* N = Net.Get())
			if (SinceAsk > 6.f && Cell != AskedCell)
			{
				SinceAsk = 0.f;
				AskedCell = Cell;
				Buildings.Reset();
				TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
				A->SetStringField(TEXT("cell"), Cell);
				N->Query(TEXT("game.buildings"), A);
			}
		FString LevelName;
		int32 Iq = 0, Ir = 0;
		ParseCell(Cell, LevelName, Iq, Ir);
		FVector Cam = FVector::ZeroVector;
		if (APlayerController* PC = W->GetFirstPlayerController()) if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
		static UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
		for (const TPair<FString, FBuilding>& KV : Buildings)
		{
			const FBuilding& B = KV.Value;
			if (B.Hash.IsEmpty() || B.Level != LevelName) continue;
			const TObjectPtr<UTexture2D>* Tx = (B.bWrecked ? TexGrey : Tex).Find(B.Hash);
			if (!Tx || !Tx->Get() || !Unlit) continue;
			float Top = 0.f;
			FVector At;
			if (!St->BoardHexTop(B.Q, B.R, true, Top, At)) continue;
			TMap<FString, TObjectPtr<UMaterialInstanceDynamic>>& Mats = B.bWrecked ? MatGrey : Mat;
			UMaterialInstanceDynamic* M = Mats.FindRef(B.Hash);
			if (!M)
			{
				M = UMaterialInstanceDynamic::Create(Unlit, this);
				M->SetTextureParameterValue(TEXT("SlateUI"), Tx->Get());
				Mats.Add(B.Hash, M);
			}
			UStaticMeshComponent* Card = CardAt(Used++);
			if (Card->GetMaterial(0) != M) Card->SetMaterial(0, M);
			const float Sz = D * SizeOf(B.Kind) * (Level == 0 ? 2.2f : 1.f);
			const FVector Mid = At + FVector(0.f, 0.f, Sz * 0.5f);
			Card->SetWorldLocation(Mid);
			Card->SetWorldRotation(FRotator(0.f, (Cam - Mid).GetSafeNormal2D().Rotation().Yaw + 90.f, 0.f));
			Card->SetWorldScale3D(FVector(Sz, Sz * 0.02f, Sz) / 100.f);
			Card->SetVisibility(true);
		}
	}
	for (int32 c = Used; c < Pool.Num(); ++c) if (Pool[c] && Pool[c]->IsVisible()) Pool[c]->SetVisibility(false);
	if (Used != LastShown)
	{
		LastShown = Used;
		UE_LOG(LogTemp, Display, TEXT("FNHouses: %d house pictures on the board"), Used);
	}
}

bool UFNHouseCardsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNHouseCardsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNHouseCards>(AFNHouseCards::StaticClass(), FTransform::Identity, P);
}
