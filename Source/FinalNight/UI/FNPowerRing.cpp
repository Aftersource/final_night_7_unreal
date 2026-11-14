#include "FNPowerRing.h"

#include "Camera/PlayerCameraManager.h"
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
	static const TCHAR* RingCube = TEXT("/Engine/BasicShapes/RingCube.RingCube");
	static const TCHAR* RingCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* RingSph = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* RingCone = TEXT("/Engine/BasicShapes/RingCone.RingCone");
	static const TCHAR* RingPlane = TEXT("/Engine/BasicShapes/RingPlane.RingPlane");

	// Sizes in table cm (scaled with the table): a hex of circumradius RingHexR, RingHexH thick, arcs just outside the board rim.
	// Measured on the real camera: the board's near-right edge runs off the screen and the table's WILL bar sits there,
	// so the arc climbs the board's RIGHT side from just right of centre toward the back, two rows deep.
	static constexpr float RingHexR = 10.5f;
	static constexpr float RingHexH = 3.4f;
	static constexpr float RingRimGap = 15.f;      // from the board's rim to the inner row's centre line
	static constexpr float RingGap = 3.5f;         // between neighbouring hexes
	static constexpr float RingArcStart = 12.f;    // degrees from +X (table right) toward +Y (the near side, the camera)
	static constexpr int32 RingPerRow = 6;
	static constexpr float RingPouchBeyond = 9.f;  // the pouch sits this many degrees past the inner row's far end

	static const FLinearColor RingWood(0.30f, 0.17f, 0.07f);
	static const FLinearColor RingWoodEdge(0.15f, 0.085f, 0.035f);
	static const FLinearColor RingWoodDim(0.16f, 0.15f, 0.14f);
	static const FLinearColor RingLeather(0.23f, 0.11f, 0.05f);
	static const FLinearColor RingBrass(0.62f, 0.44f, 0.16f);

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
	const double ArcR = R + (RingRimGap + Row * RingHexR * 1.8f) * S;
	const double StepDeg = FMath::RadiansToDegrees(((RingHexR * 1.7320508f + RingGap) * S) / ArcR);
	const double Deg = RingArcStart - (Col + 0.5 * Row) * StepDeg;
	const double A = FMath::DegreesToRadians(Deg);
	const FVector Local((float)(FMath::Cos(A) * ArcR), (float)(FMath::Sin(A) * ArcR), RingHexH * 0.5f * S);
	OutRot = (TableRot.Quaternion() * FRotator(0.f, (float)Deg, 0.f).Quaternion());
	return C + TableRot.RotateVector(Local);
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
	const double ArcR = R + (RingRimGap + 1.f) * S;
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
	// A small leather pouch: the belly, a gathered neck, a brass tie, a flared mouth.
	PouchParts.Add(Part(Pouch, RingSph, FVector(0, 0, 6.f), FVector(0.17f, 0.15f, 0.13f), FRotator::ZeroRotator, RingLeather));
	PouchParts.Add(Part(Pouch, RingCyl, FVector(0, 0, 12.5f), FVector(0.075f, 0.075f, 0.04f), FRotator::ZeroRotator, RingLeather * 0.85f));
	PouchParts.Add(Part(Pouch, RingCyl, FVector(0, 0, 13.2f), FVector(0.085f, 0.085f, 0.012f), FRotator::ZeroRotator, RingBrass));
	PouchParts.Add(Part(Pouch, RingCone, FVector(0, 0, 16.5f), FVector(0.11f, 0.11f, 0.05f), FRotator(180.f, 0.f, 0.f), RingLeather * 1.1f));
	PouchCount = NewObject<UTextRenderComponent>(this);
	PouchCount->SetupAttachment(Pouch);
	PouchCount->SetRelativeLocation(FVector(0, 0, 24.f));
	PouchCount->SetHorizontalAlignment(EHTA_Center);
	PouchCount->SetVerticalAlignment(EVRTA_TextCenter);
	PouchCount->SetWorldSize(4.2f);
	PouchCount->SetTextRenderColor(FColor(255, 214, 120));
	PouchCount->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PouchCount->RegisterComponent();
	Parts.Add(PouchCount);
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

	// The pouch, its count turned to the camera.
	const FVector PouchPos = PouchWorld();
	FVector Ctr;
	double Rr;
	FRotator TableRot;
	Rim(Ctr, Rr, TableRot);
	if (Pouch)
	{
		const float S = (float)St->GetActorScale3D().X;
		Pouch->SetWorldLocationAndRotation(PouchPos, TableRot);
		Pouch->SetWorldScale3D(FVector(S * (1.f + 0.04f * FMath::Sin(Time * 2.f) * (bOpen ? 0.f : 1.f))));
		if (PouchCount)
		{
			FVector To = Cam - PouchCount->GetComponentLocation();
			To.Z = 0.f;
			if (To.Normalize()) PouchCount->SetWorldRotation(To.Rotation());
		}
	}

	// Out of the pouch (staggered, tumbling along an arc) or back in.
	OpenT = FMath::FInterpConstantTo(OpenT, bOpen ? 1.f : 0.f, Dt, 1.25f);
	const int32 N = Hexes.Num();
	const float S = (float)St->GetActorScale3D().X;
	for (int32 i = 0; i < N; ++i)
	{
		FHex& H = Hexes[i];
		const float Delay = N > 1 ? 0.55f * i / (N - 1) : 0.f;
		const float T = FMath::Clamp((OpenT * 1.55f - Delay) / 1.f, 0.f, 1.f);
		H.Out = T;
		const bool bVisible = T > 0.001f || i == Lifted;
		H.Root->SetVisibility(bVisible, true);
		if (!bVisible) { H.Pos = PouchPos; continue; }

		FQuat SlotRot;
		const FVector Slot = SlotWorld(i, N, SlotRot);
		const float E = RingEaseOutBack(T);
		FVector Target = FMath::Lerp(PouchPos + FVector(0, 0, 10.f * S), Slot, E) + FVector(0, 0, FMath::Sin(PI * FMath::Clamp(T, 0.f, 1.f)) * 16.f * S);
		const FQuat Tumble(H.SpinAxis, (1.f - FMath::Clamp(T, 0.f, 1.f)) * PI * 3.f);
		FQuat TargetRot = Tumble * SlotRot;
		if (i == Hover && Lifted < 0) Target += FVector(0, 0, 2.5f * S);
		float Speed = 16.f;
		if (i == Lifted)
		{
			// In the hand: up off the table, following the cursor, rocking a little.
			Target = bLiftHasTarget ? LiftTarget : Slot + FVector(0, 0, 12.f * S);
			const FVector Vel = (Target - H.Pos) / FMath::Max(Dt, 1e-3f);
			TargetRot = FQuat(FVector::CrossProduct(FVector::UpVector, Vel.GetSafeNormal()).GetSafeNormal(), FMath::Clamp((float)Vel.Size() * 0.0006f, 0.f, 0.35f)) * SlotRot;
			Speed = 18.f;
		}
		const float K = 1.f - FMath::Exp(-Speed * Dt);
		H.Pos = FMath::Lerp(H.Pos, Target, K);
		H.Rot = FQuat::Slerp(H.Rot, TargetRot, K);
		H.Root->SetWorldLocationAndRotation(H.Pos, H.Rot);
	}
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
	const FVector C = Pouch->GetComponentLocation() + FVector(0, 0, 8.f * (float)Pouch->GetComponentScale().X);
	FVector2D P0, P1;
	if (!PC->ProjectWorldLocationToScreen(C, P0) || !PC->ProjectWorldLocationToScreen(C + Right * WorldHexR() * 1.2f, P1)) return false;
	return FVector2D::Distance(P0, Pixel) < FVector2D::Distance(P0, P1) * 1.2;
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
	HoverHex = (R && R->IsShown() && ToPixel(G, E.GetScreenSpacePosition(), Px) && !FFNPowerDragDrop::GetActive().IsValid()) ? R->HexAtPixel(Px) : -1;
	if (R) R->SetHover(HoverHex);
	if (Was != HoverHex) Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Unhandled();
}

void SFNPowerRingInput::OnMouseLeave(const FPointerEvent&)
{
	HoverHex = -1;
	if (AFNPowerRing* R = Ring()) R->SetHover(-1);
}

FCursorReply SFNPowerRingInput::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return HoverHex >= 0 ? FCursorReply::Cursor(EMouseCursor::GrabHand) : FCursorReply::Unhandled();
}

int32 SFNPowerRingInput::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle&, bool) const
{
	// Over a hex: its name and how to cast it, to the left of the hex.
	AFNPowerRing* R = Ring();
	if (!R || HoverHex < 0 || FFNPowerDragDrop::GetActive().IsValid()) return Layer;
	FVector2D Px;
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	if (!VC || !VC->Viewport || !R->HexPixel(HoverHex, Px)) return Layer;
	const FVector2D VS = FVector2D(VC->Viewport->GetSizeXY());
	const FVector2D Local = Px / VS * FVector2D(G.GetLocalSize());

	const FText Name = R->NameOf(HoverHex);
	const FText How = R->IsEnabled() ? LOCTEXT("How", "over the globe: MEGA, 1 bolt   on the board: MINI, 1 spark")
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
