#include "Features/FNEventFx.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Regex.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

namespace FNEventFxImpl
{
	static constexpr float Dur = 7.f;          // seconds per event (Benito: ~4 s a disaster; 4 s was too quick to catch)
	static constexpr int32 RingSegs = 32, NumBits = 16, MaxLive = 8;
	static constexpr float Stagger = 0.6f;     // a beat's events land one after another
	static const FColor ThemeCol[] = { FColor(255, 214, 120), FColor(255, 110, 30), FColor(70, 160, 255), FColor(225, 225, 235) };

	int32 ThemeOf(const FString& K)
	{
		// the server's kind word comes first in K, so it wins over words in the story text ("riot ... shops burned")
		const FString S = K.ToLower();
		const FString First = S.Left(S.Find(TEXT(" ")) < 0 ? S.Len() : S.Find(TEXT(" ")));
		for (const FString& T : { First, S })
		{
			for (const TCHAR* W : { TEXT("flood"), TEXT("water"), TEXT("tidal"), TEXT("tsunami"), TEXT("rain"), TEXT("fish") }) if (T.Contains(W)) return 2;
			for (const TCHAR* W : { TEXT("storm"), TEXT("hurricane"), TEXT("wind"), TEXT("quake"), TEXT("battle"), TEXT("army"), TEXT("riot"), TEXT("loot"), TEXT("duel") }) if (T.Contains(W)) return 3;
			for (const TCHAR* W : { TEXT("fire"), TEXT("lava"), TEXT("volcan"), TEXT("meteor"), TEXT("burn") }) if (T.Contains(W)) return 1;
		}
		return 0;
	}

	/** "loses 1498 dead", "1,401 died": the people killed, else 0. */
	int32 DeadIn(const FString& Text)
	{
		const FString S = Text.Replace(TEXT(","), TEXT(""));
		FRegexMatcher M(FRegexPattern(TEXT("(\\d{2,})\\s*(dead|died|killed|deaths|lives)")), S);
		return M.FindNext() ? FCString::Atoi(*M.GetCaptureGroup(1)) : 0;
	}
}

AFNEventFx::AFNEventFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AFNEventFx::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

void AFNEventFx::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	if (Type != TEXT("round.outcome") || !D.IsValid()) return;
	const TSharedPtr<FJsonObject>* Pub = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
	if (!D->TryGetObjectField(TEXT("public"), Pub) || !Pub || !(*Pub)->TryGetArrayField(TEXT("events"), Ev) || !Ev) return;
	int32 n = 0;
	for (const TSharedPtr<FJsonValue>& V : *Ev)
	{
		const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
		if (!E.IsValid() || !E->TryGetArrayField(TEXT("at"), At) || !At || At->Num() < 2) continue;
		const FString Kind = E->GetStringField(TEXT("kind"));
		const FString Text = E->GetStringField(TEXT("text"));
		double Mag = 5.0; E->TryGetNumberField(TEXT("magnitude"), Mag);
		Play((*At)[0]->AsNumber(), (*At)[1]->AsNumber(), Kind + TEXT(" ") + Text, (float)Mag, FNEventFxImpl::DeadIn(Text), n++ * FNEventFxImpl::Stagger);
	}
}

UStaticMeshComponent* AFNEventFx::Piece(UStaticMesh* Mesh, int32 Theme)
{
	using namespace FNEventFxImpl;
	static UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	if (Colors.Num() == 0)
		for (const FColor& C : ThemeCol)
		{
			UTexture2D* T = UTexture2D::CreateTransient(2, 2, PF_B8G8R8A8);
			FColor* Px = static_cast<FColor*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
			for (int32 i = 0; i < 4; ++i) Px[i] = C;
			T->GetPlatformData()->Mips[0].BulkData.Unlock();
			T->SRGB = true;
			T->UpdateResource();
			Colors.Add(T);
		}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, FName(*FString::Printf(TEXT("EvFx%d"), Seq++)));
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	if (Unlit)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Unlit, C);
		M->SetTextureParameterValue(TEXT("SlateUI"), Colors[Theme]);
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	C->SetVisibility(false);
	return C;
}

void AFNEventFx::Play(double Lon, double Lat, const FString& Kind, float Magnitude, int32 Dead, float Delay)
{
	using namespace FNEventFxImpl;
	if (Live.Num() >= MaxLive) return;
	// only what lands on the board in front of you (the rest would use up the slots unseen)
	AFNStudio* St = nullptr;
	if (UWorld* W = GetWorld()) { if (TActorIterator<AFNStudio> It(W); It) St = *It; }
	FVector Spot;
	if (!St || !St->BoardPointFor(Lat, Lon, Spot)) return;
	static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	static UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	FFx F;
	F.Lon = Lon; F.Lat = Lat; F.Mag = FMath::Clamp(Magnitude, 1.f, 10.f); F.Dead = Dead; F.T = -Delay;
	F.Theme = ThemeOf(Kind);
	for (int32 s = 0; s < RingSegs; ++s) F.Ring.Add(Piece(Cube, F.Theme));
	F.Beam = Piece(Cube, F.Theme);   // a column of light over the spot, so it reads from the chair
	FRandomStream Rng(Seq);
	for (int32 b = 0; b < NumBits; ++b)
	{
		F.Bits.Add(Piece(F.Theme == 2 ? Ball : Cube, F.Theme));
		const float A = Rng.FRandRange(0.f, 2.f * PI), Out = Rng.FRandRange(10.f, 28.f);
		// fire rises, water arcs high and falls, the rest flies low
		const float Up = F.Theme == 1 ? Rng.FRandRange(20.f, 45.f) : F.Theme == 2 ? Rng.FRandRange(35.f, 55.f) : Rng.FRandRange(12.f, 30.f);
		F.Vel.Add(FVector(FMath::Cos(A) * Out, FMath::Sin(A) * Out, Up));
	}
	if (Dead > 0)
	{
		F.Num = NewObject<UTextRenderComponent>(this, FName(*FString::Printf(TEXT("EvNum%d"), Seq++)));
		F.Num->SetupAttachment(RootComponent);
		F.Num->SetHorizontalAlignment(EHTA_Center);
		F.Num->SetVerticalAlignment(EVRTA_TextCenter);
		F.Num->SetTextRenderColor(FColor(255, 70, 60));
		F.Num->SetText(FText::FromString(TEXT("-") + FText::AsNumber(Dead).ToString()));
		F.Num->SetCastShadow(false);
		F.Num->RegisterComponent();
		F.Num->SetVisibility(false);
	}
	UE_LOG(LogTemp, Display, TEXT("FNEventFx: %s at %.2f,%.2f mag %.1f dead %d theme %d"), *Kind.Left(60), Lat, Lon, Magnitude, Dead, F.Theme);
	Live.Add(MoveTemp(F));
}

void AFNEventFx::Free(FFx& F)
{
	for (UStaticMeshComponent* C : F.Ring) C->DestroyComponent();
	for (UStaticMeshComponent* C : F.Bits) C->DestroyComponent();
	if (F.Beam) F.Beam->DestroyComponent();
	if (F.Num) F.Num->DestroyComponent();
}

void AFNEventFx::Tick(float DeltaSeconds)
{
	using namespace FNEventFxImpl;
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNEventFx::OnMessage); break; }
	if (Live.Num() == 0) return;
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;   // (Mac clang -Werror: a loop that breaks at once is an error)
	FVector Cam = FVector::ZeroVector;
	if (APlayerController* PC = W->GetFirstPlayerController()) if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();

	// the board's raised hexagons stand above its map points: play on top of the tiles, not under them
	int32 Q0 = 0, R0 = 0; float Top = 0.f; FVector C0 = FVector::ZeroVector;
	FVector C1 = FVector::ZeroVector; float Top1 = 0.f;
	const bool bTop = St && St->BoardCentreHex(Q0, R0) && St->BoardHexTop(Q0, R0, true, Top, C0) && St->BoardHexTop(Q0 + 1, R0, true, Top1, C1);
	// everything is sized in hexagons, so it reads the same at every zoom (one hexagon = D cm; 10 cm was the old fixed size)
	const float D = bTop ? FMath::Max(FVector::Dist2D(C0, C1), 1.f) : 10.f, K = D / 10.f;
	for (int32 i = Live.Num() - 1; i >= 0; --i)
	{
		FFx& F = Live[i];
		F.T += DeltaSeconds;
		FVector Foot = FVector::ZeroVector;
		const bool bOn = F.T >= 0.f && St && St->BoardPointFor(F.Lat, F.Lon, Foot);   // follows the board as it pans and zooms
		if (bOn && bTop) Foot.Z = FMath::Max(Foot.Z, C0.Z + D * 0.35f);   // above the trees on the tiles
		if (bOn && !F.bLogged) { F.bLogged = true; UE_LOG(LogTemp, Display, TEXT("FNEventFx: shown on the board at %.0f,%.0f,%.0f"), Foot.X, Foot.Y, Foot.Z); }
		const float U = FMath::Clamp(F.T / Dur, 0.f, 1.f);
		for (UStaticMeshComponent* C : F.Ring) C->SetVisibility(bOn);
		for (UStaticMeshComponent* C : F.Bits) C->SetVisibility(bOn);
		if (F.Num) F.Num->SetVisibility(bOn);
		if (F.Beam)
		{
			F.Beam->SetVisibility(bOn);
			if (bOn)
			{
				const float Bs = 0.6f + F.Mag * 0.12f, Fade = U < 0.7f ? 1.f : (1.f - U) / 0.3f, Hb = 70.f * K * Bs * FMath::Min(F.T * 3.f, 1.f);
				F.Beam->SetWorldLocation(Foot + FVector(0, 0, Hb * 0.5f));
				F.Beam->SetWorldRotation(FRotator::ZeroRotator);
				F.Beam->SetWorldScale3D(FVector(4.f * K * Bs * Fade, 4.f * K * Bs * Fade, Hb) / 100.f);
			}
		}
		if (bOn)
		{
			const float Size = 0.6f + F.Mag * 0.12f;   // magnitude 1..10 -> 0.7..1.8
			// the shockwave: fast out, then slow, thinning to nothing
			const float RingU = FMath::Clamp(F.T / (Dur * 0.6f), 0.f, 1.f);
			const float R = (30.f * Size * (1.f - FMath::Square(1.f - RingU)) + 1.f) * K;
			const float Thick = FMath::Max(3.f * Size * (1.f - RingU), 0.01f) * K;
			for (int32 s = 0; s < RingSegs; ++s)
			{
				const float A = 2.f * PI * s / RingSegs;
				UStaticMeshComponent* C = F.Ring[s];
				C->SetWorldLocation(Foot + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 1.f));
				C->SetWorldRotation(FRotator(0.f, FMath::RadiansToDegrees(A) + 90.f, 0.f));
				C->SetWorldScale3D(FVector(2.f * PI * R / RingSegs * 1.1f / 100.f, Thick / 100.f, Thick * 0.5f / 100.f));
				C->SetVisibility(RingU < 1.f);
			}
			// the burst: thrown out with gravity (fire floats up instead), shrinking away
			const float G = (F.Theme == 1 ? -8.f : 60.f) * K;
			for (int32 b = 0; b < F.Bits.Num(); ++b)
			{
				const FVector V = F.Vel[b] * Size * K;
				FVector P = Foot + FVector(V.X * F.T, V.Y * F.T, FMath::Max(V.Z * F.T - 0.5f * G * F.T * F.T, 0.f));
				const float Sz = 3.f * K * Size * FMath::Max(1.f - U * 1.3f, 0.f);
				F.Bits[b]->SetWorldLocation(P);
				F.Bits[b]->SetWorldRotation(FRotator(F.T * 300.f + b * 40.f, F.T * 200.f + b * 25.f, 0.f));
				F.Bits[b]->SetWorldScale3D(FVector(Sz / 100.f));
			}
			// the toll rises over the spot and turns to the camera
			if (F.Num)
			{
				const FVector P = Foot + FVector(0, 0, (18.f + 14.f * U) * K);
				F.Num->SetWorldLocation(P);
				F.Num->SetWorldRotation((Cam - P).GetSafeNormal2D().Rotation());
				F.Num->SetWorldSize(K * FMath::Lerp(10.f, 16.f, FMath::Min(U * 3.f, 1.f)) * (U > 0.85f ? (1.f - U) / 0.15f : 1.f));
			}
		}
		if (F.T >= Dur) { Free(F); Live.RemoveAt(i); }
	}
}

bool UFNEventFxSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNEventFxSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNEventFx>(AFNEventFx::StaticClass(), FTransform::Identity, P);
}
