#include "FNCandles.h"

#include "FNStudio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

AFNCandles::AFNCandles()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

namespace FN7CandlesImpl
{
	/** A teardrop flame, 32x64: a white-yellow core low in the middle, orange to the edge, alpha falling off outside. */
	static UTexture2D* MakeFlameTexture()
	{
		constexpr int32 W = 32, H = 64;
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!T) return nullptr;
		T->SRGB = true;
		uint8* Px = static_cast<uint8*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 y = 0; y < H; ++y)
			for (int32 x = 0; x < W; ++x)
			{
				const float V = 1.f - (y + 0.5f) / H;                 // 0 at the bottom, 1 at the tip
				const float U = (x + 0.5f) / W * 2.f - 1.f;           // -1 .. 1 across
				const float Wd = 0.82f * FMath::Pow(FMath::Sin(PI * FMath::Clamp(V * 0.92f + 0.05f, 0.f, 1.f)), 0.8f) * (1.f - 0.55f * V);
				const float D = Wd > 0.01f ? FMath::Abs(U) / Wd : 2.f;
				const float A = FMath::Clamp(1.f - D, 0.f, 1.f);
				const float Core = FMath::Clamp((1.f - D) * 1.6f - V * 0.9f, 0.f, 1.f);
				const FLinearColor C = FMath::Lerp(FLinearColor(1.f, 0.42f, 0.06f), FLinearColor(1.f, 0.96f, 0.78f), Core);
				const FColor S = FLinearColor(C.R, C.G, C.B, FMath::Pow(A, 0.65f) * FMath::Clamp(1.2f - V * 0.4f, 0.f, 1.f)).ToFColor(true);
				uint8* P = Px + (y * W + x) * 4;
				P[0] = S.B; P[1] = S.G; P[2] = S.R; P[3] = S.A;
			}
		T->GetPlatformData()->Mips[0].BulkData.Unlock();
		T->UpdateResource();
		return T;
	}
}

void AFNCandles::Build()
{
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(GetWorld()); It) St = *It;
	if (!St) return;
	TArray<UStaticMeshComponent*> Meshes;
	St->GetComponents<UStaticMeshComponent>(Meshes);
	TArray<UPointLightComponent*> Lights;
	St->GetComponents<UPointLightComponent>(Lights);
	FlameTex = FN7CandlesImpl::MakeFlameTexture();
	UMaterialInterface* CardMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (UStaticMeshComponent* M : Meshes)
	{
		const FString N = M->GetName();
		if (N.StartsWith(TEXT("Flame"))) { M->SetVisibility(false); continue; }   // the old orange spheres
		if (!N.StartsWith(TEXT("Candle")) || N.StartsWith(TEXT("CandleLight"))) continue;
		FFlame F;
		F.Wax = M;
		// the wick top from THIS candle's wax: the top of its bounds, whatever its height
		const FBoxSphereBounds B = M->Bounds;
		F.Top = B.Origin + FVector(0.f, 0.f, B.BoxExtent.Z);
		F.Height = 2.f * B.BoxExtent.Z;
		F.Seed = FMath::FRand() * 10.f;
		const int32 Idx = FCString::Atoi(*N.RightChop(6));
		for (UPointLightComponent* L : Lights) if (L->GetName() == FString::Printf(TEXT("CandleLight%d"), Idx)) F.Light = L;
		const float R = FMath::Max(1.f, (float)FMath::Min(B.BoxExtent.X, B.BoxExtent.Y));   // the candle's radius: flame and wick scale with it
		F.Wick = NewObject<UStaticMeshComponent>(this);
		F.Wick->SetupAttachment(RootComponent);
		F.Wick->SetStaticMesh(Cyl);
		F.Wick->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F.Wick->SetCastShadow(false);
		if (Basic) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Basic, F.Wick); D->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.015f, 0.01f)); F.Wick->SetMaterial(0, D); }
		F.Wick->RegisterComponent();
		F.Wick->SetWorldScale3D(FVector(R * 0.09f / 50.f, R * 0.09f / 50.f, R * 0.5f / 100.f));
		F.Wick->SetWorldLocation(F.Top + FVector(0.f, 0.f, R * 0.25f));
		F.Card = NewObject<UStaticMeshComponent>(this);
		F.Card->SetupAttachment(RootComponent);
		F.Card->SetStaticMesh(Cube);
		F.Card->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F.Card->SetCastShadow(false);
		if (CardMat && FlameTex) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(CardMat, F.Card); D->SetTextureParameterValue(TEXT("SlateUI"), FlameTex); F.Card->SetMaterial(0, D); }
		F.Card->RegisterComponent();
		Keep.Add(F.Wick); Keep.Add(F.Card);
		Flames.Add(F);
	}
	bBuilt = Flames.Num() > 0;
	UE_LOG(LogTemp, Display, TEXT("FN7 candles: %d flames lit at their wick tops"), Flames.Num());
}

void AFNCandles::Tick(float Dt)
{
	Super::Tick(Dt);
	Time += Dt;
	if (!bBuilt) { Build(); if (!bBuilt) return; }
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const FVector Cam = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector(0, 600, 300);
	for (FFlame& F : Flames)
	{
		if (!F.Wax.IsValid() || !F.Card) continue;
		const FBoxSphereBounds B = F.Wax->Bounds;   // the table can move or scale: follow the wax
		F.Top = B.Origin + FVector(0.f, 0.f, B.BoxExtent.Z);
		const float R = FMath::Max(1.f, (float)FMath::Min(B.BoxExtent.X, B.BoxExtent.Y));
		const float T = Time + F.Seed;
		const float Flick = 0.85f + 0.1f * FMath::Sin(T * 13.f) + 0.07f * FMath::Sin(T * 31.f + 1.3f);
		const float Hgt = R * 3.4f * Flick, Wd = R * 1.5f * (0.95f + 0.05f * FMath::Sin(T * 17.f));
		const FVector Sway(R * 0.12f * FMath::Sin(T * 2.3f), R * 0.12f * FMath::Sin(T * 1.7f + 0.6f), 0.f);
		const FVector Base = F.Top + FVector(0.f, 0.f, R * 0.35f);
		F.Wick->SetWorldLocation(F.Top + FVector(0.f, 0.f, R * 0.25f));
		FVector ToCam = Cam - Base;
		ToCam.Z = 0.f;
		const FRotator Face = ToCam.IsNearlyZero() ? FRotator::ZeroRotator : ToCam.Rotation();
		F.Card->SetWorldRotation(Face);
		F.Card->SetWorldScale3D(FVector(0.001f, Wd / 100.f, Hgt / 100.f));
		F.Card->SetWorldLocation(Base + Sway * 0.5f + FVector(0.f, 0.f, Hgt * 0.5f));
		if (UPointLightComponent* L = F.Light.Get()) L->SetWorldLocation(Base + FVector(0.f, 0.f, Hgt * 0.45f));
	}
}

bool UFNCandlesSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNCandlesSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNCandles>(AFNCandles::StaticClass(), FTransform::Identity, P);
}
