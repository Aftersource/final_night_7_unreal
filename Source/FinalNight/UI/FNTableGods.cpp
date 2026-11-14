#include "FNTableGods.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FNStudio.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace FN7Gods
{
	static const TCHAR* GodCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* GodCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* GodSph = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* GodCone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* GodPlane = TEXT("/Engine/BasicShapes/Plane.Plane");
	static const TCHAR* GodBaseMatPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const TCHAR* GodImageMatPath = TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque");

	static const FLinearColor GodStone(0.20f, 0.19f, 0.18f);
	static const FLinearColor GodStoneCap(0.26f, 0.25f, 0.23f);
	static const FLinearColor GodRobe(0.018f, 0.018f, 0.026f);
	static const FLinearColor GodGrey(0.22f, 0.22f, 0.23f);
	static const FLinearColor GodIvory(0.78f, 0.72f, 0.60f);
	static const FLinearColor GodGold(0.85f, 0.62f, 0.20f);
	static const FLinearColor GodBone(0.86f, 0.83f, 0.76f);
	static const FLinearColor GodAntler(0.52f, 0.42f, 0.30f);
	static const FLinearColor GodPlinthWood(0.10f, 0.06f, 0.03f);

	static constexpr float GodPedestalTop = 92.f;
	static constexpr int32 GodNumBeads = 14, GodNumCubes = 8;
	static constexpr float GodBodyR = 330.f, GodFigR = 222.f, GodTableTop = 80.f;
	static constexpr float GodPairOffsetCm = 25.f;

	static void GodSetColor(UStaticMeshComponent* C, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* M = C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr) M->SetVectorParameterValue(TEXT("Color"), Color);
	}

	static FVector GodCameraLocation(UWorld* World, const FVector& Fallback)
	{
		if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
			if (PC->PlayerCameraManager) return PC->PlayerCameraManager->GetCameraLocation();
		return Fallback;
	}

	static void GodFaceCamera(USceneComponent* C, const FVector& Cam)
	{
		if (!C) return;
		FVector To = Cam - C->GetComponentLocation();
		To.Z = 0.f;
		if (To.Normalize()) C->SetWorldRotation(To.Rotation());
	}
}

// --- the god on its pedestal -------------------------------------------------------

AFNGodPedestal::AFNGodPedestal()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(FN7Gods::GodBaseMatPath);
	if (Mat.Succeeded()) BaseMat = Mat.Object;
}

UStaticMeshComponent* AFNGodPedestal::Part(const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
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

void AFNGodPedestal::BuildBody()
{
	using namespace FN7Gods;
	const float Z0 = GodPedestalTop;
	// The stone pedestal: a foot, a column, a cap.
	Part(GodCyl, FVector(0, 0, 5), FVector(0.56f, 0.56f, 0.10f), FRotator::ZeroRotator, GodStone * 0.85f);
	Part(GodCyl, FVector(0, 0, 47), FVector(0.40f, 0.40f, 0.74f), FRotator::ZeroRotator, GodStone);
	Part(GodCyl, FVector(0, 0, 88), FVector(0.52f, 0.52f, 0.08f), FRotator::ZeroRotator, GodStoneCap);
	// The god: a faceted robe, a hem in the aura colour, shoulders, a head.
	Robe.Add(Part(GodCone, FVector(0, 0, Z0 + 36.f), FVector(0.46f, 0.46f, 0.72f), FRotator::ZeroRotator, FN7Gods::GodRobe));
	Robe.Add(Part(GodCube, FVector(0, 0, Z0 + 60.f), FVector(0.12f, 0.44f, 0.10f), FRotator::ZeroRotator, FN7Gods::GodRobe));
	Robe.Add(Part(GodSph, FVector(0, 0, Z0 + 76.f), FVector(0.20f), FRotator::ZeroRotator, FN7Gods::GodRobe * 1.6f));
	Hands.Add(Part(GodCyl, FVector(0, 0, Z0 + 1.f), FVector(0.47f, 0.47f, 0.02f), FRotator::ZeroRotator, FLinearColor::White));   // hem
	// Two glowing hands, held out toward the table.
	Hands.Add(Part(GodSph, FVector(9.f, -25.f, Z0 + 40.f), FVector(0.09f), FRotator::ZeroRotator, FLinearColor::White));
	Hands.Add(Part(GodSph, FVector(9.f, 25.f, Z0 + 40.f), FVector(0.09f), FRotator::ZeroRotator, FLinearColor::White));
	for (int32 i = 0; i < GodNumBeads; ++i)
	{
		UStaticMeshComponent* B = Part(GodSph, FVector(0, 0, Z0 + 96.f), FVector(0.04f), FRotator::ZeroRotator, FLinearColor::White);
		B->SetCastShadow(false);
		Beads.Add(B);
	}
	for (int32 i = 0; i < GodNumCubes; ++i)
	{
		UStaticMeshComponent* C = Part(GodCube, FVector(0, 0, Z0 + 70.f), FVector(0.045f), FRotator::ZeroRotator, FLinearColor::White);
		C->SetCastShadow(false);
		Cubes.Add(C);
	}
	Glow = NewObject<UPointLightComponent>(this);
	Glow->SetupAttachment(RootComponent);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetRelativeLocation(FVector(34.f, 0.f, Z0 + 30.f));   // in front of the hands: it lights the rim and the dioramas, not the robe
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(0.f);
	Glow->SetAttenuationRadius(150.f);
	Glow->SetCastShadows(false);
	Glow->RegisterComponent();
	Parts.Add(Glow);
	Plate = NewObject<UTextRenderComponent>(this);
	Plate->SetupAttachment(RootComponent);
	Plate->SetRelativeLocation(FVector(32.f, 0.f, 58.f));
	Plate->SetHorizontalAlignment(EHTA_Center);
	Plate->SetVerticalAlignment(EVRTA_TextCenter);
	Plate->SetWorldSize(10.f);
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->RegisterComponent();
	Parts.Add(Plate);
}

void AFNGodPedestal::BuildFeature(int32 InKind)
{
	using namespace FN7Gods;
	for (UStaticMeshComponent* F : Feature) if (F) { Parts.Remove(F); F->DestroyComponent(); }
	Feature.Reset();
	Kind = InKind;
	const float Z0 = GodPedestalTop;
	switch (Kind)
	{
	case 0:   // hooded
		Feature.Add(Part(GodCone, FVector(-1.f, 0, Z0 + 84.f), FVector(0.30f, 0.30f, 0.34f), FRotator::ZeroRotator, FN7Gods::GodRobe * 1.3f));
		Feature.Add(Part(GodSph, FVector(8.5f, 0, Z0 + 75.f), FVector(0.04f, 0.13f, 0.14f), FRotator::ZeroRotator, FLinearColor(0.f, 0.f, 0.f)));
		break;
	case 1:   // horned
		Feature.Add(Part(GodCone, FVector(0, -10.f, Z0 + 92.f), FVector(0.06f, 0.06f, 0.30f), FRotator(0.f, 0.f, -32.f), GodIvory));
		Feature.Add(Part(GodCone, FVector(0, 10.f, Z0 + 92.f), FVector(0.06f, 0.06f, 0.30f), FRotator(0.f, 0.f, 32.f), GodIvory));
		break;
	case 2:   // crowned
		Feature.Add(Part(GodCyl, FVector(0, 0, Z0 + 85.f), FVector(0.22f, 0.22f, 0.04f), FRotator::ZeroRotator, GodGold));
		for (int32 i = 0; i < 5; ++i)
		{
			const float A = 2.f * PI * i / 5.f;
			Feature.Add(Part(GodCube, FVector(FMath::Cos(A) * 10.f, FMath::Sin(A) * 10.f, Z0 + 90.f), FVector(0.045f, 0.045f, 0.07f), FRotator(0.f, FMath::RadiansToDegrees(A) + 45.f, 0.f), GodGold));
		}
		break;
	case 3:   // winged
		Feature.Add(Part(GodCube, FVector(-12.f, -22.f, Z0 + 58.f), FVector(0.04f, 0.46f, 0.64f), FRotator(0.f, -28.f, -16.f), FN7Gods::GodRobe * 1.4f));
		Feature.Add(Part(GodCube, FVector(-12.f, 22.f, Z0 + 58.f), FVector(0.04f, 0.46f, 0.64f), FRotator(0.f, 28.f, 16.f), FN7Gods::GodRobe * 1.4f));
		break;
	case 4:   // masked
		Feature.Add(Part(GodCube, FVector(10.5f, 0, Z0 + 76.f), FVector(0.03f, 0.18f, 0.22f), FRotator::ZeroRotator, GodBone));
		Feature.Add(Part(GodCube, FVector(12.2f, -4.f, Z0 + 79.f), FVector(0.01f, 0.045f, 0.016f), FRotator::ZeroRotator, FLinearColor(0.f, 0.f, 0.f)));
		Feature.Add(Part(GodCube, FVector(12.2f, 4.f, Z0 + 79.f), FVector(0.01f, 0.045f, 0.016f), FRotator::ZeroRotator, FLinearColor(0.f, 0.f, 0.f)));
		break;
	default:  // antlered
		for (int32 s = -1; s <= 1; s += 2)
		{
			Feature.Add(Part(GodCyl, FVector(0, 8.f * s, Z0 + 96.f), FVector(0.03f, 0.03f, 0.30f), FRotator(0.f, 0.f, 26.f * s), GodAntler));
			Feature.Add(Part(GodCyl, FVector(0, 15.f * s, Z0 + 104.f), FVector(0.025f, 0.025f, 0.16f), FRotator(0.f, 0.f, 62.f * s), GodAntler));
			Feature.Add(Part(GodCyl, FVector(2.f, 12.f * s, Z0 + 108.f), FVector(0.022f, 0.022f, 0.12f), FRotator(0.f, 0.f, 8.f * s), GodAntler));
		}
		break;
	}
}

void AFNGodPedestal::SetGod(const FFNTableGod& God)
{
	using namespace FN7Gods;
	if (Robe.Num() == 0) BuildBody();
	Info = God;
	const int32 WantKind = (int32)(FCrc::StrCrc32(*God.Player.ToLower()) % 6u);
	if (WantKind != Kind) BuildFeature(WantKind);
	Phase = (float)(FCrc::StrCrc32(*God.Player) % 1000u) * 0.01f;
	const FLinearColor Aura = God.bAlive ? God.Aura : GodGrey;
	const float Dim = !God.bAlive ? 0.6f : (God.bConnected ? 1.f : 0.35f);
	for (UStaticMeshComponent* H : Hands) GodSetColor(H, Aura * Dim);
	for (UStaticMeshComponent* B : Beads) GodSetColor(B, Aura * Dim);
	for (UStaticMeshComponent* C : Cubes) GodSetColor(C, Aura * Dim);
	for (UStaticMeshComponent* R : Robe) GodSetColor(R, God.bAlive ? FN7Gods::GodRobe : GodGrey * 0.5f);
	if (Glow) Glow->SetLightColor(Aura);
	if (Plate)
	{
		const TCHAR* Tag = !God.bAlive ? TEXT("OUT") : !God.bConnected ? TEXT("AWAY") : God.bHuman ? TEXT("HUMAN") : TEXT("AI");
		Plate->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *God.Player, Tag)));
		Plate->SetTextRenderColor((God.bAlive ? Aura : GodGrey * 2.f).ToFColor(true));
	}
}

void AFNGodPedestal::Tick(float Dt)
{
	using namespace FN7Gods;
	Super::Tick(Dt);
	Time += Dt;
	const float T = Time + Phase;
	const bool bOn = Info.bAlive;
	const float Away = Info.bConnected ? 1.f : 0.3f;
	const float S = GetActorScale3D().X;
	// Human: a level halo that breathes. AI: cubes orbiting the head on a tilted path, flickering.
	const float Pulse = Info.bHuman ? 0.75f + 0.25f * FMath::Sin(T * 1.6f) : (FMath::Sin(T * 9.f) + FMath::Sin(T * 23.f) > -0.4f ? 1.f : 0.45f);
	if (Glow) Glow->SetIntensity(bOn ? 7.f * Pulse * Away : 0.f);
	for (int32 i = 0; i < Beads.Num(); ++i)
	{
		const bool bShow = bOn && Info.bHuman;
		Beads[i]->SetVisibility(bShow);
		if (!bShow) continue;
		const float A = 2.f * PI * i / Beads.Num() + T * 0.4f;
		Beads[i]->SetRelativeLocation(FVector(FMath::Cos(A) * 18.f, FMath::Sin(A) * 18.f, GodPedestalTop + 96.f + 1.5f * FMath::Sin(T * 1.6f)));
	}
	for (int32 i = 0; i < Cubes.Num(); ++i)
	{
		const bool bShow = bOn && !Info.bHuman && Pulse > 0.5f;
		Cubes[i]->SetVisibility(bShow);
		if (!bShow) continue;
		const float A = 2.f * PI * i / Cubes.Num() + T * 2.2f;
		Cubes[i]->SetRelativeLocation(FVector(FMath::Cos(A) * 28.f, FMath::Sin(A) * 28.f, GodPedestalTop + 70.f + FMath::Sin(A * 2.f) * 12.f));
		Cubes[i]->SetRelativeRotation(FRotator(T * 90.f, T * 120.f, 0.f));
	}
	GodFaceCamera(Plate, GodCameraLocation(GetWorld(), GetActorLocation() + FVector(0, 340.f * S, 190.f * S)));
}

// --- the diorama box -------------------------------------------------------------

AFNDemigodFigure::AFNDemigodFigure()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(FN7Gods::GodBaseMatPath);
	if (Mat.Succeeded()) BaseMat = Mat.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Img(FN7Gods::GodImageMatPath);
	if (Img.Succeeded()) ImageMat = Img.Object;
}

UStaticMeshComponent* AFNDemigodFigure::Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color)
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

void AFNDemigodFigure::SetDemigod(const FFNTableDemigod& D, const FLinearColor& InAura, bool bOwnerAlive)
{
	using namespace FN7Gods;
	if (!Card)
	{
		// The hex plinth (three boxes make the hexagon) with a darker skirt.
		const float R = 13.f;
		for (int32 k = 0; k < 3; ++k)
		{
			Plinth.Add(Part(nullptr, GodCube, FVector(0, 0, 2.6f), FVector(R * 1.7320508f / 100.f, R / 100.f, 3.2f / 100.f), FRotator(0.f, 60.f * k, 0.f), GodPlinthWood));
			Plinth.Add(Part(nullptr, GodCube, FVector(0, 0, 0.5f), FVector((R + 0.8f) * 1.7320508f / 100.f, (R + 0.8f) / 100.f, 1.f / 100.f), FRotator(0.f, 60.f * k, 0.f), GodPlinthWood * 0.5f));
		}
		Notch = Part(nullptr, GodCube, FVector(0, 0, 4.6f), FVector(0.035f, 0.035f, 0.02f), FRotator::ZeroRotator, FLinearColor::White);
		NotchText = NewObject<UTextRenderComponent>(this);
		NotchText->SetupAttachment(RootComponent);
		NotchText->SetHorizontalAlignment(EHTA_Center);
		NotchText->SetVerticalAlignment(EVRTA_TextCenter);
		NotchText->SetWorldSize(5.f);
		NotchText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		NotchText->RegisterComponent();
		Parts.Add(NotchText);
		// The card: the diorama in a matte of the aura colour on a dark back board. Card space +Z faces the camera.
		Card = NewObject<USceneComponent>(this);
		Card->SetupAttachment(RootComponent);
		Card->SetMobility(EComponentMobility::Movable);
		Card->SetUsingAbsoluteRotation(true);
		Card->RegisterComponent();
		Parts.Add(Card);
		Back = Part(Card, GodPlane, FVector(0, 0, -0.7f), FVector(0.345f, 0.345f, 1.f), FRotator::ZeroRotator, FLinearColor(0.01f, 0.01f, 0.015f));
		Matte = Part(Card, GodPlane, FVector(0, 0, -0.35f), FVector(0.33f, 0.33f, 1.f), FRotator::ZeroRotator, FLinearColor::White);
		Image = Part(Card, GodPlane, FVector::ZeroVector, FVector(0.30f, 0.30f, 1.f), FRotator::ZeroRotator, FLinearColor(0.03f, 0.03f, 0.05f));
	}
	Ref = D.Ref;
	Side = D.Side;
	Aura = InAura;
	bAlive = D.bAlive && bOwnerAlive;
	const FLinearColor Frame = bAlive ? Aura : GodGrey;
	GodSetColor(Matte, Frame);
	GodSetColor(Notch, Frame);
	const bool bRight = Side == TEXT("right");
	Notch->SetRelativeLocation(FVector(7.f, bRight ? 8.f : -8.f, 4.6f));
	NotchText->SetText(FText::FromString(Side.IsEmpty() ? FString() : (bRight ? TEXT("R") : TEXT("L"))));
	NotchText->SetTextRenderColor(Frame.ToFColor(true));
	NotchText->SetRelativeLocation(FVector(14.f, bRight ? 8.f : -8.f, 4.f));

	// The diorama at full fidelity (greyed for the fallen), else a dim silhouette card.
	UTexture2D* Tex = bAlive ? D.Diorama : (D.DioramaGrey ? D.DioramaGrey : D.Diorama);
	if (Tex && ImageMat)
	{
		if (Tex != ShownTex)
		{
			UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Image->GetMaterial(0));
			if (!M || M->Parent != ImageMat) { M = UMaterialInstanceDynamic::Create(ImageMat, Image); Image->SetMaterial(0, M); }
			M->SetTextureParameterValue(TEXT("SlateUI"), Tex);
			ShownTex = Tex;
		}
	}
	else if (ShownTex || !Tex)
	{
		ShownTex = nullptr;
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, Image);
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.03f, 0.03f, 0.05f));
			Image->SetMaterial(0, M);
		}
	}
}

void AFNDemigodFigure::ApplyStory(const TSharedPtr<FJsonObject>& Story)
{
	using namespace FN7Gods;
	for (UStaticMeshComponent* B : StoryBits) if (B) { Parts.Remove(B); B->DestroyComponent(); }
	StoryBits.Reset();
	if (!Story.IsValid() || !Card) return;
	auto Count = [&Story](const TCHAR* Key) -> int32
	{
		double N = 0.0;
		if (Story->TryGetNumberField(Key, N)) return (int32)N;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return Story->TryGetArrayField(Key, A) && A ? A->Num() : 0;
	};
	// Followers: small figures around the plinth.
	const int32 Followers = FMath::Min(Count(TEXT("followers")), 10);
	for (int32 i = 0; i < Followers; ++i)
	{
		const float A = 2.f * PI * i / FMath::Max(1, Followers) + 0.3f;
		StoryBits.Add(Part(nullptr, GodCyl, FVector(FMath::Cos(A) * 16.f, FMath::Sin(A) * 16.f, 2.6f), FVector(0.022f, 0.022f, 0.05f), FRotator::ZeroRotator, Aura * 0.6f));
	}
	// Held items: tiny gold pieces at the front edge.
	const int32 Items = FMath::Min(Count(TEXT("items")), 4);
	for (int32 i = 0; i < Items; ++i)
		StoryBits.Add(Part(nullptr, GodCube, FVector(11.f, -6.f + 4.f * i, 5.2f), FVector(0.025f), FRotator(0.f, 45.f, 0.f), GodGold));
	// Scars: dark notches cut into the bottom of the matte.
	const int32 Scars = FMath::Min(Count(TEXT("scars")), 6);
	for (int32 i = 0; i < Scars; ++i)
		StoryBits.Add(Part(Card, GodCube, FVector(-15.5f, -12.f + 4.5f * i, -0.2f), FVector(0.012f, 0.006f, 0.004f), FRotator(0.f, 0.f, 20.f), FLinearColor(0.01f, 0.f, 0.f)));
	// The wardrobe stage brightens the frame.
	double Stage = 0.0;
	if (Story->TryGetNumberField(TEXT("wardrobe_stage"), Stage) && bAlive) GodSetColor(Matte, Aura * (1.f + 0.18f * (float)Stage));
}

void AFNDemigodFigure::Tick(float Dt)
{
	using namespace FN7Gods;
	Super::Tick(Dt);
	Time += Dt;
	if (!Card) return;
	const float S = GetActorScale3D().X;
	const FVector Cam = GodCameraLocation(GetWorld(), GetActorLocation() + FVector(0, 340.f * S, 190.f * S));
	Tip = FMath::FInterpTo(Tip, bAlive ? 0.f : 1.f, Dt, 3.f);
	FVector To = (Cam - Card->GetComponentLocation()).GetSafeNormal();
	FVector ToH = To;
	ToH.Z = 0.f;
	ToH = ToH.GetSafeNormal();
	// Upright: facing the camera, leaning back a touch. Fallen: lying on the plinth, top away from the camera.
	const FQuat Upright = FRotationMatrix::MakeFromZY((To + ToH).GetSafeNormal(), -FVector::UpVector).ToQuat();
	const FQuat Flat = FRotationMatrix::MakeFromZY(FVector::UpVector, ToH).ToQuat();
	Card->SetWorldRotation(FQuat::Slerp(Upright, Flat, Tip));
	HeldT = FMath::FInterpTo(HeldT, bHeld ? 1.f : 0.f, Dt, 10.f);
	Card->SetRelativeLocation(FVector(0.f, 0.f, FMath::Lerp(4.2f + 17.f, 4.2f + 0.8f, Tip) + 9.f * HeldT + 1.2f * HeldT * FMath::Sin(Time * 6.f)));
	Card->SetRelativeScale3D(FVector(1.f + 0.18f * HeldT));
	if (Matte && HeldT > 0.001f) GodSetColor(Matte, (bAlive ? Aura : GodGrey) * (1.f + 1.5f * HeldT));
	GodFaceCamera(NotchText, Cam);
}

bool AFNDemigodFigure::CardBounds(FVector& OutCenter, float& OutHalf) const
{
	if (!Card || IsHidden()) return false;
	OutCenter = Card->GetComponentLocation();
	OutHalf = 17.f * (float)Card->GetComponentScale().X;
	return true;
}

// --- the manager -------------------------------------------------------------------

AFNTableGods::AFNTableGods()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AFNTableGods* AFNTableGods::Get(AFNStudio* InStudio, bool bCreate)
{
	if (!InStudio || !InStudio->GetWorld()) return nullptr;
	for (TActorIterator<AFNTableGods> It(InStudio->GetWorld()); It; ++It)
		if (It->Studio.Get() == InStudio) return *It;
	if (!bCreate) return nullptr;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFNTableGods* G = InStudio->GetWorld()->SpawnActor<AFNTableGods>(InStudio->GetActorLocation(), InStudio->GetActorRotation(), P);
	if (G) G->Studio = InStudio;
	return G;
}

float AFNTableGods::SlotAngle(int32 Slot)
{
	// FNStudioSeats' slots (0 = you at the camera), then extra chairs for bigger tables.
	static const float A[8] = { 90.f, 200.f, 235.f, 305.f, 160.f, 270.f, 340.f, 20.f };
	return A[FMath::Clamp(Slot, 0, 7)];
}

FVector AFNTableGods::Local(float Deg, float Radius, float Z) const
{
	const AFNStudio* St = Studio.Get();
	const float A = FMath::DegreesToRadians(Deg);
	const FVector L(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, Z);
	return St ? St->GetActorTransform().TransformPosition(L) : L;
}

AFNTableGods* AFNTableGods::GetAny(UWorld* World)
{
	if (!World) return nullptr;
	TActorIterator<AFNTableGods> It(World);
	return It ? *It : nullptr;
}

FString AFNTableGods::FigureAtScreen(UWorld* World, const FVector2D& ViewportPixel)
{
	AFNTableGods* T = GetAny(World);
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!T || !PC || !PC->PlayerCameraManager || !T->bShown) return FString();
	const FVector Right = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);
	FString Best;
	double BestD = TNumericLimits<double>::Max();
	auto Test = [&](const TMap<FString, TObjectPtr<AFNDemigodFigure>>& Map)
	{
		for (const auto& KV : Map)
		{
			const AFNDemigodFigure* F = KV.Value.Get();
			FVector C;
			float Half;
			if (!F || !F->CardBounds(C, Half)) continue;
			FVector2D P0, P1;
			if (!PC->ProjectWorldLocationToScreen(C, P0) || !PC->ProjectWorldLocationToScreen(C + Right * Half, P1)) continue;
			const double R = FVector2D::Distance(P0, P1) * 1.15;
			const double D = FVector2D::Distance(P0, ViewportPixel);
			if (D < R && D < BestD) { BestD = D; Best = F->GetRef().IsEmpty() ? KV.Key : F->GetRef(); }
		}
	};
	Test(T->OnBoard);
	Test(T->Figures);
	return Best;
}

void AFNTableGods::SetFigureHeld(const FString& Ref, bool bHeld)
{
	if (const TObjectPtr<AFNDemigodFigure>* F = Figures.Find(Ref)) if (F->Get()) (*F)->SetHeld(bHeld);
	if (const TObjectPtr<AFNDemigodFigure>* F = OnBoard.Find(Ref)) if (F->Get()) (*F)->SetHeld(bHeld);
}

AFNDemigodFigure* AFNTableGods::FindFigure(const FString& Ref) const
{
	const TObjectPtr<AFNDemigodFigure>* F = Figures.Find(Ref);
	return F ? F->Get() : nullptr;
}

void AFNTableGods::SetShown(bool bShow)
{
	bShown = bShow;
	for (auto& KV : Pedestals) if (KV.Value) KV.Value->SetActorHiddenInGame(!bShow);
	for (auto& KV : Figures) if (KV.Value) KV.Value->SetActorHiddenInGame(!bShow);
	for (auto& KV : OnBoard) if (KV.Value) KV.Value->SetActorHiddenInGame(!bShow);
}

void AFNTableGods::SetGods(const TArray<FFNTableGod>& Gods, int32 MySeat)
{
	using namespace FN7Gods;
	AFNStudio* St = Studio.Get();
	UWorld* W = GetWorld();
	if (!St || !W) return;
	bHasGods = Gods.Num() > 0;
	const float S = (float)St->GetActorScale3D().X;
	const float StudioYaw = St->GetActorRotation().Yaw;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Seats to slots: you at 0, the others by (seat - yours) mod 4, then the next free chair.
	TArray<int32> SlotOf;
	SlotOf.Init(-1, Gods.Num());
	TArray<bool> Taken;
	Taken.Init(false, 8);
	for (int32 i = 0; i < Gods.Num(); ++i)
	{
		const FFNTableGod& G = Gods[i];
		int32 Slot = G.bMine ? 0 : (G.Seat >= 0 && MySeat >= 0 ? ((G.Seat - MySeat) % 4 + 4) % 4 : -1);
		if (!G.bMine && Slot == 0) Slot = -1;
		if (Slot >= 0 && !Taken[Slot]) { Taken[Slot] = true; SlotOf[i] = Slot; }
	}
	for (int32 i = 0; i < Gods.Num(); ++i)
	{
		if (SlotOf[i] >= 0) continue;
		for (int32 k = 1; k < 8; ++k) if (!Taken[k]) { Taken[k] = true; SlotOf[i] = k; break; }
	}

	TSet<int32> UsedSlots;
	TSet<FString> UsedRefs;
	BoardInfo.Reset();
	BoardAura.Reset();
	for (int32 i = 0; i < Gods.Num(); ++i)
	{
		const FFNTableGod& G = Gods[i];
		const int32 Slot = SlotOf[i];
		if (Slot < 0) continue;
		const float Angle = Slot == 0 ? 110.f : SlotAngle(Slot);
		if (Slot != 0)   // you are the camera: no pedestal at your chair
		{
			TObjectPtr<AFNGodPedestal>& Ped = Pedestals.FindOrAdd(Slot);
			if (!Ped) Ped = W->SpawnActor<AFNGodPedestal>(Local(Angle, GodBodyR, 0.f), FRotator(0.f, StudioYaw + Angle + 180.f, 0.f), P);
			if (Ped)
			{
				Ped->SetActorLocationAndRotation(Local(Angle, GodBodyR, 0.f), FRotator(0.f, StudioYaw + Angle + 180.f, 0.f));
				Ped->SetActorScale3D(FVector(S));
				Ped->SetGod(G);
				Ped->SetActorHiddenInGame(!bShown);
				UsedSlots.Add(Slot);
			}
		}
		// The diorama boxes between the pedestal and the rim: the god's left hand on its left. Yours are in the HUD
		// (the near rim is under the camera), so only your board pieces stand on the table.
		const float Off = FMath::RadiansToDegrees(GodPairOffsetCm / GodFigR);
		for (int32 d = 0; d < G.Demigods.Num() && d < 2; ++d)
		{
			const FFNTableDemigod& Dg = G.Demigods[d];
			const FString Key = Dg.Ref.IsEmpty() ? FString::Printf(TEXT("%s#%d"), *G.Player, d) : Dg.Ref;
			if (G.bMine)
			{
				if (Dg.bHasPos && Dg.bAlive) { BoardInfo.Add(Key, Dg); BoardAura.Add(Key, G.Aura); }
				continue;
			}
			const float Deg = G.Demigods.Num() == 1 ? Angle : Angle + (Dg.Side == TEXT("right") ? -Off : Off);
			TObjectPtr<AFNDemigodFigure>& Fig = Figures.FindOrAdd(Key);
			if (!Fig) Fig = W->SpawnActor<AFNDemigodFigure>(Local(Deg, GodFigR, GodTableTop), FRotator(0.f, StudioYaw + Deg + 180.f, 0.f), P);
			if (Fig)
			{
				Fig->SetActorLocationAndRotation(Local(Deg, GodFigR, GodTableTop), FRotator(0.f, StudioYaw + Deg + 180.f, 0.f));
				Fig->SetActorScale3D(FVector(S));
				Fig->SetDemigod(Dg, G.Aura, G.bAlive);
				Fig->SetActorHiddenInGame(!bShown);
				UsedRefs.Add(Key);
			}
			if (Dg.bHasPos && Dg.bAlive)
			{
				BoardInfo.Add(Key, Dg);
				BoardAura.Add(Key, G.Aura);
			}
		}
	}
	// Gone from the table: remove.
	for (auto It = Pedestals.CreateIterator(); It; ++It)
		if (!UsedSlots.Contains(It.Key())) { if (It.Value()) It.Value()->Destroy(); It.RemoveCurrent(); }
	for (auto It = Figures.CreateIterator(); It; ++It)
		if (!UsedRefs.Contains(It.Key())) { if (It.Value()) It.Value()->Destroy(); It.RemoveCurrent(); }
	for (auto It = OnBoard.CreateIterator(); It; ++It)
		if (!BoardInfo.Contains(It.Key())) { if (It.Value()) It.Value()->Destroy(); It.RemoveCurrent(); }
	for (const auto& KV : BoardInfo)
	{
		TObjectPtr<AFNDemigodFigure>& Fig = OnBoard.FindOrAdd(KV.Key);
		if (!Fig) Fig = W->SpawnActor<AFNDemigodFigure>(GetActorLocation(), FRotator::ZeroRotator, P);
		if (Fig) Fig->SetDemigod(KV.Value, BoardAura.FindRef(KV.Key), true);
	}
}

void AFNTableGods::Tick(float Dt)
{
	Super::Tick(Dt);
	AFNStudio* St = Studio.Get();
	if (!St || !bHasGods || !bShown) return;
	St->HideClassicSeats();
	// The small boxes on the board follow the map as it zooms and pans.
	const float S = (float)St->GetActorScale3D().X;
	for (auto& KV : OnBoard)
	{
		AFNDemigodFigure* Fig = KV.Value.Get();
		const FFNTableDemigod* D = BoardInfo.Find(KV.Key);
		if (!Fig || !D) continue;
		FVector Wp;
		const bool bOn = St->BoardPointFor(D->Lat, D->Lon, Wp);
		Fig->SetActorHiddenInGame(!bOn);
		if (bOn)
		{
			Fig->SetActorLocation(Wp);
			Fig->SetActorScale3D(FVector(0.36f * S));
		}
	}
}
