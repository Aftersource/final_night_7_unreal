#include "FNStarBackdrop.h"

#include "FNStudio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace FN7Stars
{
	static const TCHAR* MatPath = TEXT("/Game/Stars/M_StarSky.M_StarSky");
	static const TCHAR* SphereMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	// How much of each globe turn a layer follows: the dust barely moves, the near sparkles most (kept modest: Benito
	// asked for the shift to be "a little", and the lead for the near layers to stay calm).
	static const float Parallax[AFNStarBackdrop::NumLayers] = { 0.12f, 0.24f, 0.38f, 0.55f };
	// Discreet: it should not draw too much attention, but look pretty up close.
	static const float Brightness[AFNStarBackdrop::NumLayers] = { 1.0f, 0.75f, 0.8f, 0.9f };
	static constexpr float SkyRadiusCm = 200000.f;   // 2 km, around everything; the camera stays at its centre
}

AFNStarBackdrop::AFNStarBackdrop()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera and the globe have moved this frame
	Sky = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sky"));
	RootComponent = Sky;
	Sky->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sky->SetCastShadow(false);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->bVisibleInRayTracing = false;
	for (FQuat& Q : Layer) Q = FQuat::Identity;
}

AFNStarBackdrop* AFNStarBackdrop::Ensure(UWorld* World)
{
	if (!World) return nullptr;
	if (TActorIterator<AFNStarBackdrop> It(World); It) return *It;
	TActorIterator<AFNStudio> St(World);
	if (!St) return nullptr;   // the studio builds the room; the sky comes after it
	return World->SpawnActor<AFNStarBackdrop>();
}

void AFNStarBackdrop::SetUp()
{
	using namespace FN7Stars;
	bSetUp = true;
	Sky->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, SphereMesh));
	Sky->SetWorldScale3D(FVector(SkyRadiusCm / 50.f));
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MatPath))
	{
		Mid = UMaterialInstanceDynamic::Create(M, this);
		for (int32 k = 0; k < NumLayers; ++k) Mid->SetScalarParameterValue(*FString::Printf(TEXT("B%d"), k), Brightness[k]);
		Sky->SetMaterial(0, Mid);
	}
	// Let the sky show: hide the four stone walls and the tablet behind the globe (its dark face, brass frame and legs:
	// with the face alone gone, the spots lit the solid brass back into two gold arches).
	int32 Hidden = 0;
	if (TActorIterator<AFNStudio> St(GetWorld()); St)
	{
		TInlineComponentArray<USceneComponent*> Comps(*St);
		for (USceneComponent* C : Comps)
		{
			if (!C) continue;
			const FString N = C->GetName();
			static const TSet<FString> Hide = { TEXT("WallN"), TEXT("WallS"), TEXT("WallE"), TEXT("WallW"), TEXT("Tablet"), TEXT("TabletFrame"), TEXT("TabletLegL"), TEXT("TabletLegR") };
			if (Hide.Contains(N) && Cast<UPrimitiveComponent>(C))   // meshes only: the tablet's spotlight stays on (it also lights the globe)
			{
				C->SetVisibility(false, false);
				++Hidden;
			}
			if (N == TEXT("BoardRoot")) GlobeRoot = C;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 stars: %d layers (parallax %.2f/%.2f/%.2f/%.2f), material %s, %d room parts hidden, globe %s"),
		NumLayers, Parallax[0], Parallax[1], Parallax[2], Parallax[3], Mid ? TEXT("ok") : TEXT("MISSING"), Hidden, GlobeRoot.IsValid() ? TEXT("found") : TEXT("not found"));
	PushLayers();
}

void AFNStarBackdrop::PushLayers()
{
	if (!Mid) return;
	for (int32 k = 0; k < NumLayers; ++k)
	{
		// The shader reads a layer's direction l = R^-1 d as (d.(R x), d.(R y), d.(R z)).
		const FQuat& Q = Layer[k];
		const FVector X = Q.RotateVector(FVector::XAxisVector), Y = Q.RotateVector(FVector::YAxisVector), Z = Q.RotateVector(FVector::ZAxisVector);
		Mid->SetVectorParameterValue(*FString::Printf(TEXT("X%d"), k), FLinearColor(X.X, X.Y, X.Z, 0.f));
		Mid->SetVectorParameterValue(*FString::Printf(TEXT("Y%d"), k), FLinearColor(Y.X, Y.Y, Y.Z, 0.f));
		Mid->SetVectorParameterValue(*FString::Printf(TEXT("Z%d"), k), FLinearColor(Z.X, Z.Y, Z.Z, 0.f));
	}
}

void AFNStarBackdrop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSetUp) SetUp();
	// Centred on the camera: the stars are at infinity (the shader reads only the view direction).
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager) SetActorLocation(PC->PlayerCameraManager->GetCameraLocation());
	// Each layer follows this frame's globe turn by its factor (small steps: a long spin never takes the short way back).
	if (USceneComponent* G = GlobeRoot.Get())
	{
		const FQuat Now = G->GetComponentQuat();
		if (bHaveGlobe)
		{
			FQuat D = Now * LastGlobe.Inverse();
			D.Normalize();
			if (D.W < 0.f) D = FQuat(-D.X, -D.Y, -D.Z, -D.W);
			if (!D.Equals(FQuat::Identity, 1e-7f))
			{
				for (int32 k = 0; k < NumLayers; ++k)
				{
					Layer[k] = FQuat::Slerp(FQuat::Identity, D, FN7Stars::Parallax[k]) * Layer[k];
					Layer[k].Normalize();
				}
				PushLayers();
			}
		}
		LastGlobe = Now;
		if ((LastReport += DeltaSeconds) > 20.f)
		{
			LastReport = 0.f;
			UE_LOG(LogTemp, Display, TEXT("FN7 stars: layers turned %.1f / %.1f / %.1f / %.1f deg so far (parallax with the globe)"),
				FMath::RadiansToDegrees(Layer[0].GetAngle()), FMath::RadiansToDegrees(Layer[1].GetAngle()), FMath::RadiansToDegrees(Layer[2].GetAngle()), FMath::RadiansToDegrees(Layer[3].GetAngle()));
		}
		bHaveGlobe = true;
	}
}
