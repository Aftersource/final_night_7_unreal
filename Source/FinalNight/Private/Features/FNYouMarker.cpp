#include "Features/FNYouMarker.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "../../UI/FNTableGods.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

namespace FNYouMarkerImpl
{
	static constexpr int32 Segs = 24;           // ring pieces
	static constexpr float Breathe = 0.45f;     // breaths per second: slow, never a blink
}

AFNYouMarker::AFNYouMarker()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the board and the pieces moved this frame
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AFNYouMarker::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

void AFNYouMarker::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	if (!D.IsValid()) return;
	// the seat view rides in three message shapes (the HUD reads the same three)
	const TSharedPtr<FJsonObject>* You = nullptr;
	const TSharedPtr<FJsonObject>* V = nullptr;
	if (D->TryGetObjectField(TEXT("you"), You) && You && (*You)->TryGetObjectField(TEXT("private_state"), V) && V) ReadView(*V);
	else if (D->TryGetObjectField(TEXT("private"), V) && V) ReadView(*V);
	else if (D->TryGetObjectField(TEXT("state"), V) && V) ReadView(*V);
}

void AFNYouMarker::ReadView(const TSharedPtr<FJsonObject>& View)
{
	const TArray<TSharedPtr<FJsonValue>>* Hands = nullptr;
	if (!View.IsValid() || !View->TryGetArrayField(TEXT("demigods"), Hands) || !Hands) return;
	Mine.Reset();
	for (const TSharedPtr<FJsonValue>& H : *Hands)
	{
		const TSharedPtr<FJsonObject> G = H.IsValid() ? H->AsObject() : nullptr;
		if (!G.IsValid()) continue;
		FMine M;
		M.Ref = G->GetStringField(TEXT("ref"));
		G->TryGetBoolField(TEXT("alive"), M.bAlive);
		if (!M.Ref.IsEmpty()) Mine.Add(M);
	}
	FString A;
	if (View->TryGetStringField(TEXT("aura"), A) && A.StartsWith(TEXT("#")) && A.Len() >= 7)
	{
		const FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(A));
		if (!C.Equals(Aura)) { Aura = C; Paint(); }
	}
	FString Log;
	for (const FMine& M : Mine) Log += FString::Printf(TEXT(" %s(%s)"), *M.Ref, M.bAlive ? TEXT("alive") : TEXT("fallen"));
	if (Log != LastLog) { LastLog = Log; UE_LOG(LogTemp, Display, TEXT("FNYou: mine%s aura %s"), *Log, *Aura.ToFColor(true).ToHex()); }
}

void AFNYouMarker::Paint()
{
	// one flat colour, unlit (glows against the dark board without adding a light)
	if (!Tex)
	{
		Tex = UTexture2D::CreateTransient(2, 2, PF_B8G8R8A8);
		if (!Tex) return;
		Tex->SRGB = true;
	}
	FColor* Px = static_cast<FColor*>(Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < 4; ++i) Px[i] = Aura.ToFColor(true);
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
}

AFNYouMarker::FMark AFNYouMarker::MakeMark(int32 i)
{
	using namespace FNYouMarkerImpl;
	UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (!Tex) Paint();
	auto Make = [&](UStaticMesh* Mesh, const FString& Name)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, FName(*Name));
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		if (Unlit)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Unlit, C);
			M->SetTextureParameterValue(TEXT("SlateUI"), Tex);
			C->SetMaterial(0, M);
		}
		C->RegisterComponent();
		C->SetVisibility(false);
		Owned.Add(C);
		return C;
	};
	FMark K;
	for (int32 s = 0; s < Segs; ++s) K.Ring.Add(Make(Cube, FString::Printf(TEXT("YouRing%d_%d"), i, s)));
	K.Arrow = Make(Cone, FString::Printf(TEXT("YouArrow%d"), i));
	return K;
}

void AFNYouMarker::Tick(float DeltaSeconds)
{
	using namespace FNYouMarkerImpl;
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNYouMarker::OnMessage); break; }

	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;   // (Mac clang -Werror: a loop that breaks at once is an error)
	while (Marks.Num() < Mine.Num()) Marks.Add(MakeMark(Marks.Num()));
	AFNTableGods* Gods = AFNTableGods::GetAny(W);

	const float B = 0.5f + 0.5f * FMath::Sin(Time * 2.f * PI * Breathe);   // 0..1, slow
	for (int32 i = 0; i < Marks.Num(); ++i)
	{
		FMark& K = Marks[i];
		bool bShow = false;
		FVector Foot, CardC; float Half = 6.f;
		double Lat, Lon;
		if (St && Gods && Mine.IsValidIndex(i) && Mine[i].bAlive && Gods->GetBoardPos(Mine[i].Ref, Lat, Lon) && St->BoardPointFor(Lat, Lon, Foot))
		{
			bShow = true;
			CardC = Foot + FVector(0, 0, 20.f);
			// the board piece is the figure with this ref nearest the spot (the other one waits by your pedestal)
			float Best = 1e9f;
			for (TActorIterator<AFNDemigodFigure> F(W); F; ++F)
			{
				FVector C; float H;
				if (F->GetRef() != Mine[i].Ref || !F->CardBounds(C, H)) continue;
				const float Dd = FVector::Dist2D(C, Foot);
				if (Dd < Best) { Best = Dd; CardC = C; Half = H; }
			}
		}
		for (UStaticMeshComponent* C : K.Ring) C->SetVisibility(bShow);
		K.Arrow->SetVisibility(bShow);
		if (!bShow) continue;
		// a thin ring of light on the ground, slightly wider than the piece, breathing a little
		const float R = FMath::Max(Half * 1.25f, 4.f) * (1.f + 0.08f * B);
		const float Seg = 2.f * PI * R / Segs * 1.08f, Thick = FMath::Max(R * 0.07f, 0.35f);
		for (int32 s = 0; s < Segs; ++s)
		{
			const float A = 2.f * PI * s / Segs;
			UStaticMeshComponent* C = K.Ring[s];
			C->SetWorldLocation(Foot + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.4f));
			C->SetWorldRotation(FRotator(0.f, FMath::RadiansToDegrees(A) + 90.f, 0.f));
			C->SetWorldScale3D(FVector(Seg / 100.f, Thick / 100.f, 0.003f));
		}
		// a small arrow pointing down at the piece, gently bobbing above its card
		const float Ah = FMath::Max(Half * 0.55f, 2.5f);
		K.Arrow->SetWorldScale3D(FVector(Ah * 0.7f / 100.f, Ah * 0.7f / 100.f, Ah / 100.f));
		K.Arrow->SetWorldRotation(FRotator(180.f, 0.f, 0.f));
		K.Arrow->SetWorldLocation(CardC + FVector(0, 0, Half + Ah * (1.2f + 0.25f * B)));
	}
}

bool UFNYouMarkerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNYouMarkerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNYouMarker>(AFNYouMarker::StaticClass(), FTransform::Identity, P);
}
