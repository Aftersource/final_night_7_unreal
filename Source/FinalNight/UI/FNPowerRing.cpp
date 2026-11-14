#include "FNPowerRing.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "FNPowerDrag.h"
#include "FNStudio.h"
#include "FNStyle.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SFNPowerTile.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "FNPowerRing"

namespace FN7Ring
{
	static const TCHAR* RingCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* RingCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* RingSph = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* RingCone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* RingPlane = TEXT("/Engine/BasicShapes/Plane.Plane");

	// Sizes in table cm (scaled with the table): a hex of circumradius RingHexR, RingHexH thick, arcs just outside the board rim.
	// Measured on the real camera: the board's near-right edge runs off the screen and the table's WILL bar sits there,
	// so the arc climbs the board's RIGHT side from just right of centre toward the back, two rows deep.
	static constexpr float RingHexR = 10.5f;
	static constexpr float RingHexH = 3.4f;
	static constexpr float RingRimGap = 15.f;      // from the board's rim to the inner row's centre line
	static constexpr float RingGap = 3.5f;         // between neighbouring hexes
	static constexpr float RingArcStart = 12.f;    // degrees from +X (table right) toward +Y (the near side, the camera)
	static constexpr int32 RingPerRow = 6;
	static constexpr float RingTilt = 55.f;        // degrees the hexes stand up from the table (Chris: 50-60)
	static constexpr float RingPouchBeyond = 9.f;  // the pouch sits this many degrees past the inner row's far end

	static const FLinearColor RingWood(0.30f, 0.17f, 0.07f);
	static const FLinearColor RingWoodEdge(0.15f, 0.085f, 0.035f);
	static const FLinearColor RingWoodDim(0.16f, 0.15f, 0.14f);
	static const FLinearColor RingLeather(0.23f, 0.11f, 0.05f);
	static const FLinearColor RingBrass(0.62f, 0.44f, 0.16f);
	// The pouch: crimson velvet, gold trim, a gem.
	static const FLinearColor RingVelvet(0.30f, 0.025f, 0.09f);
	static const FLinearColor RingVelvetDeep(0.16f, 0.01f, 0.06f);
	static const FLinearColor RingGoldTrim(0.95f, 0.66f, 0.18f);
	static const FLinearColor RingGem(0.15f, 0.75f, 1.0f);
	static constexpr int32 RingNumMotes = 12, RingNumSparks = 22;
	static constexpr float RingFlyOut = 0.8f, RingFlyIn = 0.45f;   // seconds per hex

	static float RingEaseOutBack(float T)
	{
		const float C1 = 1.4f, C3 = C1 + 1.f;
		const float U = T - 1.f;
		return 1.f + C3 * U * U * U + C1 * U * U;
	}
}

AFNPowerRing::AFNPowerRing()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Mat.Succeeded()) BaseMat = Mat.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Img(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	if (Img.Succeeded()) ImageMat = Img.Object;
}

AFNPowerRing* AFNPowerRing::Get(AFNStudio* InStudio, bool bCreate)
{
	if (!InStudio || !InStudio->GetWorld()) return nullptr;
	for (TActorIterator<AFNPowerRing> It(InStudio->GetWorld()); It; ++It)
		if (It->Studio.Get() == InStudio) return *It;
	if (!bCreate) return nullptr;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFNPowerRing* R = InStudio->GetWorld()->SpawnActor<AFNPowerRing>(InStudio->GetActorLocation(), InStudio->GetActorRotation(), P);
	if (R)
	{
		R->Studio = InStudio;
		R->BuildPouch();
	}
	return R;
}

void AFNPowerRing::SetColor(UStaticMeshComponent* C, const FLinearColor& Color)
{
	if (UMaterialInstanceDynamic* M = C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr) M->SetVectorParameterValue(TEXT("Color"), Color);
}

UStaticMeshComponent* AFNPowerRing::Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Scale);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	Parts.Add(C);
	return C;
}

bool AFNPowerRing::Rim(FVector& Center, double& Radius, FRotator& TableRot) const
{
	const AFNStudio* St = Studio.Get();
	return St && St->GetBoardRim(Center, Radius, TableRot);
}

float AFNPowerRing::WorldHexR() const
{
	const AFNStudio* St = Studio.Get();
	return FN7Ring::RingHexR * (St ? (float)St->GetActorScale3D().X : 1.f);
}

FVector AFNPowerRing::SlotWorld(int32 Index, int32 Count, FQuat& OutRot) const
{
	using namespace FN7Ring;
	FVector C;
	double R;
	FRotator TableRot;
	if (!Rim(C, R, TableRot)) { OutRot = FQuat::Identity; return GetActorLocation(); }
	const AFNStudio* St = Studio.Get();
	const float S = St ? (float)St->GetActorScale3D().X : 1.f;
	const int32 Row = Index / RingPerRow, Col = Index % RingPerRow;
	const double ArcR = R + (RingRimGap + Row * RingHexR * 2.1f) * S;
	const double StepDeg = FMath::RadiansToDegrees(((RingHexR * 1.7320508f + RingGap) * S) / ArcR);
	const double Deg = RingArcStart - (Col + 0.5 * Row) * StepDeg;
	const double A = FMath::DegreesToRadians(Deg);
	const FVector Local((float)(FMath::Cos(A) * ArcR), (float)(FMath::Sin(A) * ArcR), 0.f);
	const FVector Base = C + TableRot.RotateVector(Local);
	// Stood up at RingTilt toward the camera, like a sign on the table's edge: its face (the icon) toward you, the icon's
	// top up and away, so you can read it and pull it onto the board.
	FVector Cam = St ? St->GetActorLocation() + FVector(0.f, 340.f, 190.f) * S : Base + FVector(0, 300, 200);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	FVector ToCam = Cam - Base;
	ToCam.Z = 0.f;
	ToCam = ToCam.GetSafeNormal();
	if (ToCam.IsNearlyZero()) ToCam = TableRot.RotateVector(FVector(0, 1, 0));
	const float Tilt = FMath::DegreesToRadians(RingTilt);
	const FVector Face = FVector::UpVector * FMath::Cos(Tilt) + ToCam * FMath::Sin(Tilt);
	const FVector Top = FVector::UpVector * FMath::Sin(Tilt) - ToCam * FMath::Cos(Tilt);
	OutRot = FRotationMatrix::MakeFromZX(Face, Top).ToQuat();
	// Resting on its lower edge: lift the centre by the half-height of the tilted hex, and the thickness.
	return Base + FVector(0, 0, (RingHexR * 0.9f * FMath::Sin(Tilt) + RingHexH * 0.5f) * S);
}

FVector AFNPowerRing::PouchWorld() const
{
	using namespace FN7Ring;
	FVector C;
	double R;
	FRotator TableRot;
	if (!Rim(C, R, TableRot)) return GetActorLocation();
	const AFNStudio* St = Studio.Get();
	const float S = St ? (float)St->GetActorScale3D().X : 1.f;
	const double ArcR = R + (RingRimGap + 8.f) * S;
	const double StepDeg = FMath::RadiansToDegrees(((RingHexR * 1.7320508f + RingGap) * S) / (R + RingRimGap * S));
	const double A = FMath::DegreesToRadians(RingArcStart - (RingPerRow - 0.5) * StepDeg - RingPouchBeyond);
	return C + TableRot.RotateVector(FVector((float)(FMath::Cos(A) * ArcR), (float)(FMath::Sin(A) * ArcR), 0.f));
}

void AFNPowerRing::BuildPouch()
{
	using namespace FN7Ring;
	if (Pouch) return;
	Pouch = NewObject<USceneComponent>(this);
	Pouch->SetupAttachment(RootComponent);
	Pouch->SetMobility(EComponentMobility::Movable);
	Pouch->RegisterComponent();
	Parts.Add(Pouch);
	PouchBody = NewObject<USceneComponent>(this);
	PouchBody->SetupAttachment(Pouch);
	PouchBody->SetMobility(EComponentMobility::Movable);
	PouchBody->RegisterComponent();
	Parts.Add(PouchBody);
	// A flamboyant pouch, about 34 cm tall: a fat crimson velvet belly with a gold band and gold studs, a gem on the
	// front with a gold bolt over it, a gathered neck tied with a gold drawstring, a flared ruffled mouth, two tassels.
	PouchParts.Add(Part(PouchBody, RingSph, FVector(0, 0, 14.f), FVector(0.34f, 0.32f, 0.28f), FRotator::ZeroRotator, RingVelvet));
	PouchParts.Add(Part(PouchBody, RingCyl, FVector(0, 0, 14.f), FVector(0.345f, 0.325f, 0.035f), FRotator::ZeroRotator, RingGoldTrim));
	for (int32 i = 0; i < 10; ++i)
	{
		const float A = 2.f * PI * i / 10.f;
		PouchParts.Add(Part(PouchBody, RingSph, FVector(FMath::Cos(A) * 17.2f, FMath::Sin(A) * 16.2f, 14.f), FVector(0.028f), FRotator::ZeroRotator, RingGoldTrim));
	}
	PouchParts.Add(Part(PouchBody, RingSph, FVector(16.6f, 0, 18.5f), FVector(0.05f, 0.075f, 0.075f), FRotator::ZeroRotator, RingGem));
	PouchParts.Add(Part(PouchBody, RingCube, FVector(16.4f, -1.2f, 23.5f), FVector(0.01f, 0.012f, 0.06f), FRotator(0.f, 0.f, -25.f), RingGoldTrim));
	PouchParts.Add(Part(PouchBody, RingCube, FVector(16.4f, 1.0f, 21.0f), FVector(0.01f, 0.03f, 0.008f), FRotator(0.f, 0.f, 0.f), RingGoldTrim));
	PouchParts.Add(Part(PouchBody, RingCube, FVector(16.4f, 1.6f, 25.0f), FVector(0.01f, 0.012f, 0.05f), FRotator(0.f, 0.f, -25.f), RingGoldTrim));
	PouchParts.Add(Part(PouchBody, RingCyl, FVector(0, 0, 27.5f), FVector(0.15f, 0.15f, 0.07f), FRotator::ZeroRotator, RingVelvetDeep));
	PouchParts.Add(Part(PouchBody, RingCyl, FVector(0, 0, 28.5f), FVector(0.175f, 0.175f, 0.022f), FRotator::ZeroRotator, RingGoldTrim));
	PouchParts.Add(Part(PouchBody, RingCone, FVector(0, 0, 34.f), FVector(0.24f, 0.24f, 0.10f), FRotator(180.f, 0.f, 0.f), RingVelvet * 1.25f));
	for (int32 i = 0; i < 8; ++i)
	{
		const float A = 2.f * PI * i / 8.f;
		PouchParts.Add(Part(PouchBody, RingSph, FVector(FMath::Cos(A) * 11.f, FMath::Sin(A) * 11.f, 38.5f), FVector(0.045f, 0.045f, 0.03f), FRotator::ZeroRotator, RingVelvet * 1.4f));
	}
	for (int32 t = -1; t <= 1; t += 2)
	{
		PouchParts.Add(Part(PouchBody, RingCyl, FVector(8.f, 9.f * t, 22.f), FVector(0.01f, 0.01f, 0.13f), FRotator(0.f, 0.f, 12.f * t), RingGoldTrim));
		PouchParts.Add(Part(PouchBody, RingCone, FVector(8.f, 10.5f * t, 14.5f), FVector(0.04f, 0.04f, 0.06f), FRotator::ZeroRotator, RingGoldTrim));
	}
	// A warm glow, and motes of light circling it.
	PouchGlow = NewObject<UPointLightComponent>(this);
	PouchGlow->SetupAttachment(Pouch);
	PouchGlow->SetMobility(EComponentMobility::Movable);
	PouchGlow->SetRelativeLocation(FVector(20.f, 0.f, 30.f));
	PouchGlow->SetIntensityUnits(ELightUnits::Candelas);
	PouchGlow->SetIntensity(6.f);
	PouchGlow->SetLightColor(FLinearColor(1.f, 0.7f, 0.3f));
	PouchGlow->SetAttenuationRadius(110.f);
	PouchGlow->SetCastShadows(false);
	PouchGlow->RegisterComponent();
	Parts.Add(PouchGlow);
	for (int32 i = 0; i < RingNumMotes; ++i)
	{
		UStaticMeshComponent* M = Part(Pouch, RingCube, FVector::ZeroVector, FVector(0.012f), FRotator::ZeroRotator, i % 3 == 0 ? RingGem * 2.f : RingGoldTrim * 1.6f);
		M->SetCastShadow(false);
		Motes.Add(M);
	}
	for (int32 i = 0; i < RingNumSparks; ++i)
	{
		UStaticMeshComponent* M = Part(Pouch, RingCube, FVector::ZeroVector, FVector(0.015f), FRotator::ZeroRotator, i % 4 == 0 ? RingGem * 2.f : RingGoldTrim * 1.8f);
		M->SetCastShadow(false);
		M->SetVisibility(false);
		Spray.Add(M);
		SprayVel.Add(FVector::ZeroVector);
	}
	PouchCount = NewObject<UTextRenderComponent>(this);
	PouchCount->SetupAttachment(Pouch);
	PouchCount->SetRelativeLocation(FVector(0, 0, 52.f));
	PouchCount->SetHorizontalAlignment(EHTA_Center);
	PouchCount->SetVerticalAlignment(EVRTA_TextCenter);
	PouchCount->SetWorldSize(8.f);
	PouchCount->SetTextRenderColor(FColor(255, 214, 120));
	PouchCount->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PouchCount->RegisterComponent();
	Parts.Add(PouchCount);
}

void AFNPowerRing::TogglePouch()
{
	bOpen = !bOpen;
	BurstT = 0.f;
	const int32 N = Hexes.Num();
	for (int32 i = 0; i < N; ++i) Hexes[i].Wait = bOpen ? 0.12f + 0.045f * i : 0.03f * (N - 1 - i);
	if (bOpen)
		for (int32 i = 0; i < SprayVel.Num(); ++i)
		{
			// A spray from the mouth: up and outward, every direction.
			const float A = 2.f * PI * i / SprayVel.Num() + FMath::FRandRange(-0.2f, 0.2f);
			SprayVel[i] = FVector(FMath::Cos(A) * FMath::FRandRange(40.f, 95.f), FMath::Sin(A) * FMath::FRandRange(40.f, 95.f), FMath::FRandRange(90.f, 170.f));
			Spray[i]->SetRelativeLocation(FVector(0, 0, 38.f));
			Spray[i]->SetVisibility(true);
		}
}

void AFNPowerRing::ClearHexes()
{
	for (FHex& H : Hexes)
	{
		for (UStaticMeshComponent* W : H.Wood) if (W) { Parts.Remove(W); W->DestroyComponent(); }
		if (H.Plaque) { Parts.Remove(H.Plaque); H.Plaque->DestroyComponent(); }
		if (H.Root) { Parts.Remove(H.Root); H.Root->DestroyComponent(); }
	}
	Hexes.Reset();
}

void AFNPowerRing::SetPowers(const TArray<FString>& Slugs, const TArray<FText>& Names)
{
	using namespace FN7Ring;
	const FString Sig = FString::Join(Slugs, TEXT("|"));
	if (Sig == PowersSig) return;
	PowersSig = Sig;
	ClearHexes();
	const float S = Studio.IsValid() ? (float)Studio->GetActorScale3D().X : 1.f;
	for (int32 i = 0; i < Slugs.Num(); ++i)
	{
		FHex H;
		H.Slug = Slugs[i];
		H.Name = Names.IsValidIndex(i) ? Names[i] : FText::FromString(Slugs[i]);
		H.Root = NewObject<USceneComponent>(this);
		H.Root->SetupAttachment(RootComponent);
		H.Root->SetMobility(EComponentMobility::Movable);
		H.Root->SetUsingAbsoluteLocation(true);
		H.Root->SetUsingAbsoluteRotation(true);
		H.Root->SetUsingAbsoluteScale(true);
		H.Root->SetWorldScale3D(FVector(S));
		H.Root->RegisterComponent();
		Parts.Add(H.Root);
		// The carved hex: three boxes at 0/60/120 degrees make the hexagon; a darker, slightly wider underside reads as a bevel.
		for (int32 k = 0; k < 3; ++k)
		{
			H.Wood.Add(Part(H.Root, RingCube, FVector(0, 0, 0.35f), FVector(RingHexR * 1.7320508f / 100.f, RingHexR / 100.f, (RingHexH - 0.7f) / 100.f), FRotator(0.f, 60.f * k, 0.f), RingWood));
			H.Wood.Add(Part(H.Root, RingCube, FVector(0, 0, -RingHexH * 0.5f + 0.35f), FVector((RingHexR + 0.6f) * 1.7320508f / 100.f, (RingHexR + 0.6f) / 100.f, 0.7f / 100.f), FRotator(0.f, 60.f * k, 0.f), RingWoodEdge));
		}
		// The inked plaque on top (the power's icon), image top away from the camera.
		H.Plaque = Part(H.Root, RingPlane, FVector(0, 0, RingHexH * 0.5f + 0.06f), FVector(0.125f, 0.125f, 1.f), FRotator(0.f, 90.f, 0.f), FLinearColor::White);
		if (ImageMat)
			if (UTexture2D* Icon = IconTexture(H.Slug))
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ImageMat, H.Plaque);
				M->SetTextureParameterValue(TEXT("SlateUI"), Icon);
				H.Plaque->SetMaterial(0, M);
			}
		H.SpinAxis = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(0.2f, 1.f)).GetSafeNormal();
		H.Pos = PouchWorld();
		H.Root->SetVisibility(false, true);
		Hexes.Add(H);
	}
	SetPurse(Bolts, Sparks);
}

void AFNPowerRing::SetPurse(int32 InBolts, int32 InSparks)
{
	Bolts = InBolts;
	Sparks = InSparks;
	const bool bOn = IsEnabled();
	for (FHex& H : Hexes)
		for (int32 k = 0; k < H.Wood.Num(); ++k)
			SetColor(H.Wood[k], bOn ? (k % 2 == 0 ? FN7Ring::RingWood : FN7Ring::RingWoodEdge) : FN7Ring::RingWoodDim * (k % 2 == 0 ? 1.f : 0.6f));
	if (PouchCount) PouchCount->SetText(FText::FromString(Bolts == 1 ? TEXT("1 BOLT") : FString::Printf(TEXT("%d BOLTS"), FMath::Max(0, Bolts))));
}

void AFNPowerRing::SetShown(bool bShow)
{
	if (bShown == bShow) return;
	bShown = bShow;
	SetActorHiddenInGame(!bShow);
}

void AFNPowerRing::BeginLift(int32 Index)
{
	Lifted = Hexes.IsValidIndex(Index) ? Index : -1;
	bLiftHasTarget = false;
}

void AFNPowerRing::MoveLifted(const FVector& WorldPoint)
{
	LiftTarget = WorldPoint;
	bLiftHasTarget = true;
}

void AFNPowerRing::EndLift()
{
	Lifted = -1;
	bLiftHasTarget = false;
}

void AFNPowerRing::Tick(float Dt)
{
	using namespace FN7Ring;
	Super::Tick(Dt);
	Time += Dt;
	AFNStudio* St = Studio.Get();
	if (!St || !bShown) return;

	FVector Cam = St->GetActorLocation() + FVector(0.f, 340.f, 190.f);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();

	// The pouch: breathing, swaying, glowing; a puff on every click; motes circling; the burst's spray falling.
	const FVector PouchPos = PouchWorld();
	FVector Ctr;
	double Rr;
	FRotator TableRot;
	Rim(Ctr, Rr, TableRot);
	const float S = (float)St->GetActorScale3D().X;
	if (BurstT >= 0.f) BurstT += Dt;
	if (Pouch)
	{
		Pouch->SetWorldLocationAndRotation(PouchPos, TableRot);
		Pouch->SetWorldScale3D(FVector(S));
		float Sx = 1.f + 0.035f * FMath::Sin(Time * 2.2f), Sz = 1.f - 0.03f * FMath::Sin(Time * 2.2f);
		if (bPouchHover) { Sx *= 1.06f; Sz *= 1.06f; }
		if (BurstT >= 0.f && BurstT < 0.6f)
		{
			// Squash down, then pop up tall, then settle.
			const float B = BurstT / 0.6f;
			const float Pop = FMath::Sin(B * PI * 2.f) * FMath::Exp(-3.f * B);
			Sz *= 1.f + 0.28f * Pop;
			Sx *= 1.f - 0.16f * Pop;
		}
		if (PouchBody)
		{
			PouchBody->SetRelativeScale3D(FVector(Sx, Sx, Sz));
			PouchBody->SetRelativeRotation(FRotator(2.5f * FMath::Sin(Time * 1.3f), 0.f, 2.5f * FMath::Sin(Time * 1.7f)));
		}
		if (PouchGlow)
		{
			const float Flash = (BurstT >= 0.f && BurstT < 0.7f) ? 60.f * FMath::Exp(-6.f * BurstT) : 0.f;
			PouchGlow->SetIntensity((5.f + 2.5f * FMath::Sin(Time * 3.f) + (bPouchHover ? 4.f : 0.f) + Flash) * (IsEnabled() ? 1.f : 0.4f));
		}
		for (int32 i = 0; i < Motes.Num(); ++i)
		{
			const float A = 2.f * PI * i / Motes.Num() + Time * (0.6f + 0.05f * (i % 4));
			const float R = 24.f + 4.f * FMath::Sin(Time * 1.1f + i);
			Motes[i]->SetRelativeLocation(FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 20.f + 14.f * FMath::Sin(Time * 0.9f + i * 1.7f)));
			const float Tw = 0.5f + 0.5f * FMath::Sin(Time * 5.f + i * 2.3f);
			Motes[i]->SetRelativeScale3D(FVector(0.006f + 0.012f * Tw));
			Motes[i]->SetRelativeRotation(FRotator(Time * 80.f + i * 30.f, Time * 60.f, 0.f));
		}
		for (int32 i = 0; i < Spray.Num(); ++i)
		{
			if (!Spray[i]->IsVisible()) continue;
			if (BurstT < 0.f || BurstT > 1.2f) { Spray[i]->SetVisibility(false); continue; }
			SprayVel[i] += FVector(0, 0, -260.f) * Dt;
			Spray[i]->SetRelativeLocation(Spray[i]->GetRelativeLocation() + SprayVel[i] * Dt);
			Spray[i]->SetRelativeScale3D(FVector(0.02f * FMath::Clamp(1.f - BurstT / 1.2f, 0.f, 1.f)));
			Spray[i]->SetRelativeRotation(FRotator(BurstT * 600.f + i * 20.f, BurstT * 400.f, 0.f));
		}
		if (PouchCount)
		{
			FVector To = Cam - PouchCount->GetComponentLocation();
			To.Z = 0.f;
			if (To.Normalize()) PouchCount->SetWorldRotation(To.Rotation());
		}
	}

	// The hexes: in the pouch, bursting out (up and over, spinning, landing with a bounce), or flying back in.
	const FVector Mouth = PouchPos + FVector(0, 0, 38.f * S);
	const int32 N = Hexes.Num();
	for (int32 i = 0; i < N; ++i)
	{
		FHex& H = Hexes[i];
		if (H.Wait > 0.f) H.Wait -= Dt;
		else H.Out = FMath::Clamp(H.Out + (bOpen ? Dt / RingFlyOut : -Dt / RingFlyIn), 0.f, 1.f);
		const float T = H.Out;
		const bool bVisible = T > 0.001f || i == Lifted;
		H.Root->SetVisibility(bVisible, true);
		if (!bVisible) { H.Pos = Mouth; H.Rot = FQuat::Identity; continue; }

		FQuat SlotRot;
		const FVector Slot = SlotWorld(i, N, SlotRot);
		const float E = bOpen ? RingEaseOutBack(T) : T * T * (3.f - 2.f * T);
		FVector Target = FMath::Lerp(Mouth, Slot, E) + FVector(0, 0, 4.f * T * (1.f - T) * (bOpen ? 46.f : 22.f) * S);
		const FQuat Tumble(H.SpinAxis, (1.f - T) * PI * 4.f);
		FQuat TargetRot = Tumble * SlotRot;
		if (i == Hover && Lifted < 0) Target += FVector(0, 0, 2.5f * S);
		bool bDirect = T < 1.f && i != Lifted;   // in flight: follow the arc exactly
		if (i == Lifted)
		{
			// In the hand: up off the table, following the cursor, rocking a little.
			Target = bLiftHasTarget ? LiftTarget : Slot + FVector(0, 0, 12.f * S);
			const FVector Vel = (Target - H.Pos) / FMath::Max(Dt, 1e-3f);
			TargetRot = FQuat(FVector::CrossProduct(FVector::UpVector, Vel.GetSafeNormal()).GetSafeNormal(), FMath::Clamp((float)Vel.Size() * 0.0006f, 0.f, 0.35f)) * SlotRot;
		}
		if (bDirect)
		{
			H.Pos = Target;
			H.Rot = TargetRot;
		}
		else
		{
			const float K = 1.f - FMath::Exp(-(i == Lifted ? 18.f : 16.f) * Dt);
			H.Pos = FMath::Lerp(H.Pos, Target, K);
			H.Rot = FQuat::Slerp(H.Rot, TargetRot, K);
		}
		H.Root->SetWorldLocationAndRotation(H.Pos, H.Rot);
	}
	St->HideWillBlock();
}

int32 AFNPowerRing::HexAtPixel(const FVector2D& Pixel) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !bShown || !PC->PlayerCameraManager) return -1;
	const FVector Right = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);
	int32 Best = -1;
	double BestD = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Hexes.Num(); ++i)
	{
		const FHex& H = Hexes[i];
		if (H.Out < 0.6f) continue;
		FVector2D P0, P1;
		if (!PC->ProjectWorldLocationToScreen(H.Pos, P0) || !PC->ProjectWorldLocationToScreen(H.Pos + Right * WorldHexR(), P1)) continue;
		const double R = FVector2D::Distance(P0, P1) * 1.1;
		const double D = FVector2D::Distance(P0, Pixel);
		if (D < R && D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

bool AFNPowerRing::PouchAtPixel(const FVector2D& Pixel) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !Pouch || !bShown || !PC->PlayerCameraManager) return false;
	const FVector Right = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);
	const float Ps = (float)Pouch->GetComponentScale().X;
	const FVector C = Pouch->GetComponentLocation() + FVector(0, 0, 20.f * Ps);
	FVector2D P0, P1;
	if (!PC->ProjectWorldLocationToScreen(C, P0) || !PC->ProjectWorldLocationToScreen(C + Right * 22.f * Ps, P1)) return false;
	return FVector2D::Distance(P0, Pixel) < FVector2D::Distance(P0, P1) * 1.1;
}

bool AFNPowerRing::PouchPixel(FVector2D& OutPixel) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC && Pouch && PC->ProjectWorldLocationToScreen(Pouch->GetComponentLocation() + FVector(0, 0, 20.f * (float)Pouch->GetComponentScale().X), OutPixel);
}

bool AFNPowerRing::HexPixel(int32 Index, FVector2D& OutPixel) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC && Hexes.IsValidIndex(Index) && PC->ProjectWorldLocationToScreen(Hexes[Index].Pos, OutPixel);
}

UTexture2D* AFNPowerRing::IconTexture(const FString& Slug)
{
	// The power's vector icon (the HUD tiles' strokes on a 24 grid), inked on a parchment plaque with a dark rim.
	const int32 Size = 128;
	const TArray<TArray<FVector2f>> Strokes = SFNPowerIcon::Strokes(Slug);
	TArray<TPair<FVector2f, FVector2f>> Segs;
	const float Margin = 18.f, Span = Size - 2.f * Margin;
	for (const TArray<FVector2f>& Stroke : Strokes)
		for (int32 i = 1; i < Stroke.Num(); ++i)
			Segs.Add(TPair<FVector2f, FVector2f>(Stroke[i - 1] / 24.f * Span + FVector2f(Margin), Stroke[i] / 24.f * Span + FVector2f(Margin)));
	const float Half = 4.4f;   // half the ink width, px
	const FLinearColor PlaquePaper = FLinearColor::FromSRGBColor(FColor(236, 222, 186));
	const FLinearColor PlaquePaperEdge = FLinearColor::FromSRGBColor(FColor(205, 182, 135));
	const FLinearColor PlaqueInk = FLinearColor::FromSRGBColor(FColor(42, 26, 13));
	TArray<FColor> Px;
	Px.SetNumUninitialized(Size * Size);
	for (int32 y = 0; y < Size; ++y)
		for (int32 x = 0; x < Size; ++x)
		{
			const FVector2f P(x + 0.5f, y + 0.5f);
			float D = 1e9f;
			for (const TPair<FVector2f, FVector2f>& S : Segs)
			{
				const FVector2f AB = S.Value - S.Key;
				const float L2 = AB.SizeSquared();
				const float T = L2 > 0.f ? FMath::Clamp(FVector2f::DotProduct(P - S.Key, AB) / L2, 0.f, 1.f) : 0.f;
				D = FMath::Min(D, FVector2f::Distance(P, S.Key + AB * T));
			}
			const float Edge = (float)FMath::Min(FMath::Min(x, y), FMath::Min(Size - 1 - x, Size - 1 - y));
			FLinearColor C = FMath::Lerp(PlaquePaperEdge, PlaquePaper, FMath::Clamp(Edge / 22.f, 0.f, 1.f));
			C += FLinearColor(0.02f, 0.015f, 0.01f) * FMath::PerlinNoise2D(FVector2D(x * 0.09f, y * 0.09f));
			if (Edge < 4.f) C = PlaqueInk * 0.8f + C * 0.2f;   // the plaque's rim
			const float A = FMath::Clamp(Half + 0.6f - D, 0.f, 1.f);
			C = FMath::Lerp(C, PlaqueInk, A);
			Px[y * Size + x] = C.ToFColor(true);
		}
	UTexture2D* Tex = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Tex) return nullptr;
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dst, Px.GetData(), Px.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	Icons.Add(Tex);
	return Tex;
}

// --- input -----------------------------------------------------------------------

void SFNPowerRingInput::Construct(const FArguments& InArgs)
{
	Studio = InArgs._Studio;
	OnHexClicked = InArgs._OnHexClicked;
}

AFNPowerRing* SFNPowerRingInput::Ring() const
{
	return AFNPowerRing::Get(Studio.Get(), false);
}

bool SFNPowerRingInput::ToPixel(const FGeometry& G, const FVector2D& Screen, FVector2D& OutPixel) const
{
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	if (!VC || !VC->Viewport) return false;
	const FVector2D Size = FVector2D(VC->Viewport->GetSizeXY());
	const FVector2D LS = FVector2D(G.GetLocalSize());
	if (LS.X < 1.0 || LS.Y < 1.0) return false;
	OutPixel = FVector2D(G.AbsoluteToLocal(Screen)) / LS * Size;
	return true;
}

FReply SFNPowerRingInput::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	AFNPowerRing* R = Ring();
	FVector2D Px;
	if (!R || !R->IsShown() || !ToPixel(G, E.GetScreenSpacePosition(), Px)) return FReply::Unhandled();
	if (R->PouchAtPixel(Px))
	{
		R->TogglePouch();
		return FReply::Handled();
	}
	const int32 Hit = R->HexAtPixel(Px);
	if (Hit < 0) return FReply::Unhandled();   // not ours: the game gets the click
	Pressed = Hit;
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SFNPowerRingInput::OnMouseButtonUp(const FGeometry&, const FPointerEvent& E)
{
	if (Pressed < 0 || E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	AFNPowerRing* R = Ring();
	const FString Slug = R ? R->SlugOf(Pressed) : FString();
	Pressed = -1;
	if (!Slug.IsEmpty()) OnHexClicked.ExecuteIfBound(Slug);   // a click: arm it (write a target, SEND)
	return FReply::Handled();
}

FReply SFNPowerRingInput::OnDragDetected(const FGeometry&, const FPointerEvent& E)
{
	AFNPowerRing* R = Ring();
	if (Pressed < 0 || !R) return FReply::Unhandled();
	const TSharedRef<FFNPowerDragDrop> Op = FFNPowerDragDrop::New(R->SlugOf(Pressed), E.GetScreenSpacePosition(), FString());
	Op->bFromRing = true;
	R->BeginLift(Pressed);
	Pressed = -1;
	return FReply::Handled().BeginDragDrop(Op);
}

FReply SFNPowerRingInput::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	AFNPowerRing* R = Ring();
	FVector2D Px;
	const int32 Was = HoverHex;
	const bool bWasPouch = bHoverPouch;
	const bool bCan = R && R->IsShown() && ToPixel(G, E.GetScreenSpacePosition(), Px) && !FFNPowerDragDrop::GetActive().IsValid();
	bHoverPouch = bCan && R->PouchAtPixel(Px);
	HoverHex = (bCan && !bHoverPouch) ? R->HexAtPixel(Px) : -1;
	if (R) { R->SetHover(HoverHex); R->SetPouchHover(bHoverPouch); }
	if (Was != HoverHex || bWasPouch != bHoverPouch) Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Unhandled();
}

void SFNPowerRingInput::OnMouseLeave(const FPointerEvent&)
{
	HoverHex = -1;
	bHoverPouch = false;
	if (AFNPowerRing* R = Ring()) { R->SetHover(-1); R->SetPouchHover(false); }
}

FCursorReply SFNPowerRingInput::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	if (HoverHex >= 0) return FCursorReply::Cursor(EMouseCursor::GrabHand);
	return bHoverPouch ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

int32 SFNPowerRingInput::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle&, bool) const
{
	// Over a hex: its name and how to cast it, to the left of the hex. Over the pouch: what a click does.
	AFNPowerRing* R = Ring();
	if (!R || (HoverHex < 0 && !bHoverPouch) || FFNPowerDragDrop::GetActive().IsValid()) return Layer;
	FVector2D Px;
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	if (!VC || !VC->Viewport) return Layer;
	if (bHoverPouch) { if (!R->PouchPixel(Px)) return Layer; }
	else if (!R->HexPixel(HoverHex, Px)) return Layer;
	const FVector2D VS = FVector2D(VC->Viewport->GetSizeXY());
	const FVector2D Local = Px / VS * FVector2D(G.GetLocalSize());

	const FText Name = bHoverPouch ? LOCTEXT("PouchName", "ENTROPY POWERS") : R->NameOf(HoverHex);
	const FText How = bHoverPouch ? (R->IsOpen() ? LOCTEXT("PouchClose", "click to gather them back in") : LOCTEXT("PouchOpen", "click to pour them out"))
		: R->IsEnabled() ? LOCTEXT("How", "over the globe: MEGA, 1 bolt   on the board: MINI, 1 spark")
		: LOCTEXT("HowNone", "no bolts or sparks left");
	const FSlateFontInfo NameFont = FN7Style::Type::Font(14, TEXT("Bold"));
	const FSlateFontInfo HowFont = FN7Style::Type::Font(10);
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D N = Measure->Measure(Name, NameFont), H = Measure->Measure(How, HowFont);
	const FVector2D Pad(12.f, 8.f);
	const FVector2D Box(FMath::Max(N.X, H.X) + Pad.X * 2.f, N.Y + H.Y + Pad.Y * 2.f + 2.f);
	FVector2D TopLeft = Local + FVector2D(-Box.X - 34.f, -Box.Y * 0.5f);
	TopLeft.X = FMath::Max(8.0, TopLeft.X);
	TopLeft.Y = FMath::Clamp(TopLeft.Y, 8.0, G.GetLocalSize().Y - Box.Y - 8.0);
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Box), FSlateLayoutTransform(FVector2f(TopLeft))), FN7Style::Brush::Panel());
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(N), FSlateLayoutTransform(FVector2f(TopLeft + Pad))), Name, NameFont, ESlateDrawEffect::None, FN7Style::Color::Text());
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(H), FSlateLayoutTransform(FVector2f(TopLeft + Pad + FVector2D(0.f, N.Y + 2.f)))), How, HowFont, ESlateDrawEffect::None, FN7Style::Color::TextDim());
	return Layer + 1;
}

#undef LOCTEXT_NAMESPACE
