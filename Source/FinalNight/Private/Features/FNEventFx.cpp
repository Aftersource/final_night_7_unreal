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
	static constexpr float Dur = 4.f;          // seconds per event (Benito: ~4 s a disaster)
	static constexpr int32 RingSegs = 32, NumBits = 16, MaxLive = 8;
	static constexpr float Stagger = 0.6f;     // a beat's events land one after another
	static const FColor ThemeCol[] = { FColor(255, 214, 120), FColor(255, 110, 30), FColor(70, 160, 255), FColor(225, 225, 235) };

	int32 ThemeOf(const FString& K)
	{
		const FString S = K.ToLower();
		for (const TCHAR* W : { TEXT("fire"), TEXT("lava"), TEXT("volcan"), TEXT("meteor"), TEXT("burn"), TEXT("heat") }) if (S.Contains(W)) return 1;
		for (const TCHAR* W : { TEXT("flood"), TEXT("water"), TEXT("tidal"), TEXT("tsunami"), TEXT("rain"), TEXT("fish"), TEXT("sea") }) if (S.Contains(W)) return 2;
		for (const TCHAR* W : { TEXT("storm"), TEXT("hurricane"), TEXT("wind"), TEXT("quake"), TEXT("battle"), TEXT("war"), TEXT("army"), TEXT("riot"), TEXT("dead") }) if (S.Contains(W)) return 3;
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
		if (Kind == TEXT("cast")) continue;   // the earth playback strikes casts
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
	static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	static UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	FFx F;
	F.Lon = Lon; F.Lat = Lat; F.Mag = FMath::Clamp(Magnitude, 1.f, 10.f); F.Dead = Dead; F.T = -Delay;
	F.Theme = ThemeOf(Kind);
	for (int32 s = 0; s < RingSegs; ++s) F.Ring.Add(Piece(Cube, F.Theme));
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

	for (int32 i = Live.Num() - 1; i >= 0; --i)
	{
		FFx& F = Live[i];
		F.T += DeltaSeconds;
		FVector Foot;
		const bool bOn = F.T >= 0.f && St && St->BoardPointFor(F.Lat, F.Lon, Foot);   // follows the board as it pans and zooms
		const float U = FMath::Clamp(F.T / Dur, 0.f, 1.f);
		for (UStaticMeshComponent* C : F.Ring) C->SetVisibility(bOn);
		for (UStaticMeshComponent* C : F.Bits) C->SetVisibility(bOn);
		if (F.Num) F.Num->SetVisibility(bOn);
		if (bOn)
		{
			const float Size = 0.6f + F.Mag * 0.12f;   // magnitude 1..10 -> 0.7..1.8
			// the shockwave: fast out, then slow, thinning to nothing
			const float RingU = FMath::Clamp(F.T / (Dur * 0.6f), 0.f, 1.f);
			const float R = 30.f * Size * (1.f - FMath::Square(1.f - RingU)) + 1.f;
			const float Thick = FMath::Max(1.6f * Size * (1.f - RingU), 0.01f);
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
			const float G = F.Theme == 1 ? -8.f : 60.f;
			for (int32 b = 0; b < F.Bits.Num(); ++b)
			{
				const FVector V = F.Vel[b] * Size;
				FVector P = Foot + FVector(V.X * F.T, V.Y * F.T, FMath::Max(V.Z * F.T - 0.5f * G * F.T * F.T, 0.f));
				const float Sz = 1.4f * Size * FMath::Max(1.f - U * 1.3f, 0.f);
				F.Bits[b]->SetWorldLocation(P);
				F.Bits[b]->SetWorldRotation(FRotator(F.T * 300.f + b * 40.f, F.T * 200.f + b * 25.f, 0.f));
				F.Bits[b]->SetWorldScale3D(FVector(Sz / 100.f));
			}
			// the toll rises over the spot and turns to the camera
			if (F.Num)
			{
				const FVector P = Foot + FVector(0, 0, 18.f + 14.f * U);
				F.Num->SetWorldLocation(P);
				F.Num->SetWorldRotation((Cam - P).GetSafeNormal2D().Rotation());
				F.Num->SetWorldSize(FMath::Lerp(5.f, 9.f, FMath::Min(U * 3.f, 1.f)) * (U > 0.85f ? (1.f - U) / 0.15f : 1.f));
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
