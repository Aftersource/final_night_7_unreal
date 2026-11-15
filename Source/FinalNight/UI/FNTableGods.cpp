#include "FNTableGods.h"
#include "FNGodStatue.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/LocalFogVolumeComponent.h"
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
	// The marble gods (stone/marble statues like The Thinker or Michelangelo's David): heroic, base on the cap.
	static constexpr float GodStatueHeight = 128.f;   // the faceted figure stands ~96: half again
	static constexpr float GodStatueFootprint = 100.f; // cm across at most
	static constexpr float GodStatueYaw = -90.f;      // Meshy's glTF front -> the pedestal's +X (toward the table)
	static const FLinearColor GodMarble(0.62f, 0.60f, 0.57f);
	static const FLinearColor GodMarbleDark(0.40f, 0.385f, 0.36f);

	/** "Chris" -> chris_god, "Bot Kestrel" -> bot_kestrel_god: the figures.json slug of a player's god. */
	static FString GodSlug(const FString& Player)
	{
		FString Out;
		for (TCHAR Ch : Player.ToLower()) Out.AppendChar(FChar::IsAlnum(Ch) ? Ch : TEXT('_'));
		while (Out.Contains(TEXT("__"))) Out = Out.Replace(TEXT("__"), TEXT("_"));
		Out.TrimCharInline(TEXT('_'), nullptr);
		return Out + TEXT("_god");
	}
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
	Plate->SetRelativeLocation(FVector(34.f, 0.f, 72.f));   // the column's top, under the cap, toward the table (58 hid behind the table edge)
	Plate->SetHorizontalAlignment(EHTA_Center);
	Plate->SetVerticalAlignment(EVRTA_TextCenter);
	Plate->SetWorldSize(13.f);
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->RegisterComponent();
	Parts.Add(Plate);
	// The stone plate behind the name, turning with it (it is the text's child): aura-coloured letters on dark slate read
	// from the chair, where bare letters on the marble column did not (Benito's audit 14).
	PlateBack = NewObject<UStaticMeshComponent>(this);
	PlateBack->SetupAttachment(Plate);
	PlateBack->SetMobility(EComponentMobility::Movable);
	PlateBack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlateBack->SetCastShadow(false);
	PlateBack->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, PlateBack);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.035f, 0.035f, 0.045f));
		PlateBack->SetMaterial(0, M);
	}
	PlateBack->RegisterComponent();
	Parts.Add(PlateBack);
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

void AFNGodPedestal::UpdateStatue(const FString& Player)
{
	using namespace FN7Gods;
	if (Player == StatueFor) return;
	StatueFor = Player;
	const FString Slug = GodSlug(Player);
	const FString Path = FString::Printf(TEXT("/Game/Figures/%s/SM_%s.SM_%s"), *Slug, *Slug, *Slug);
	UStaticMesh* Mesh = Player.IsEmpty() ? nullptr : Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	bHasStatue = Mesh != nullptr;
	if (bHasStatue) PlaceStatue(Mesh, GodStatueYaw);
	else if (Statue)
	{
		Statue->SetVisibility(false);
		if (StatueKey) StatueKey->SetVisibility(false);
		if (StatueFog) StatueFog->SetVisibility(false);
	}
	// The faceted figure only where there's no statue; the hem ring and the glow stay: the player's colour reads.
	ShowFigure(!bHasStatue);
	UE_LOG(LogTemp, Display, TEXT("FN7 god: %s -> %s"), *Player, bHasStatue ? *Path : TEXT("the faceted figure, in marble"));
	// The player's own god, made in the god screen (an account god), replaces the house statue when the server has one.
	bAccountStatue = false;
	if (Picture) Picture->SetVisibility(false);
	if (!Player.IsEmpty()) FN7GodStatue::Ask(this, Player);
}

void AFNGodPedestal::ShowFigure(bool bFigure)
{
	// The faceted figure only where there's no statue; the hem ring and the glow stay: the player's colour reads.
	for (UStaticMeshComponent* R : Robe) R->SetVisibility(bFigure);
	for (UStaticMeshComponent* F : Feature) F->SetVisibility(bFigure);
	for (int32 i = 1; i < Hands.Num(); ++i) Hands[i]->SetVisibility(bFigure);
}

void AFNGodPedestal::PlaceStatue(UStaticMesh* Mesh, float Yaw)
{
	using namespace FN7Gods;
	const float GodStatueYawUsed = Yaw;
	if (!Statue)
	{
		Statue = NewObject<UStaticMeshComponent>(this);
		Statue->SetupAttachment(RootComponent);
		Statue->SetMobility(EComponentMobility::Movable);
		Statue->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Statue->RegisterComponent();
	}
	Statue->SetStaticMesh(Mesh);
	Statue->EmptyOverrideMaterials();   // a packaged statue keeps its own stone; a runtime one gets its colour after this
	const FBoxSphereBounds B = Mesh->GetBounds();
	// Heroic height, but no wider than a common footprint (a reclining god on a couch would tower otherwise).
	const float Scale = FMath::Min(GodStatueHeight / FMath::Max(1.f, (float)B.BoxExtent.Z * 2.f),
		GodStatueFootprint / FMath::Max(1.f, 2.f * (float)FMath::Max(B.BoxExtent.X, B.BoxExtent.Y)));
	Statue->SetRelativeScale3D(FVector(Scale));
	Statue->SetRelativeRotation(FRotator(0.f, GodStatueYawUsed, 0.f));
	// Its own stone base sits on the pedestal's cap; centred over the column.
	const FVector C = FRotator(0.f, GodStatueYawUsed, 0.f).RotateVector(FVector(B.Origin.X, B.Origin.Y, 0.0)) * Scale;
	Statue->SetRelativeLocation(FVector(-C.X, -C.Y, GodPedestalTop + ((float)B.BoxExtent.Z - (float)B.Origin.Z) * Scale));
	Statue->SetVisibility(true);
	// Lit like a statue in a museum: a warm key from above and in front, the aura as a rim light behind it, and a
	// low glowing mist at its feet in the player's colour (a cheap but nice glowing fog, like Greek statues).
	if (!StatueKey)
	{
		StatueKey = NewObject<USpotLightComponent>(this);
		StatueKey->SetupAttachment(RootComponent);
		StatueKey->SetMobility(EComponentMobility::Movable);
		StatueKey->SetIntensityUnits(ELightUnits::Candelas);
		StatueKey->SetIntensity(16.f);
		StatueKey->SetLightColor(FLinearColor(1.f, 0.93f, 0.82f));
		StatueKey->SetInnerConeAngle(14.f);
		StatueKey->SetOuterConeAngle(26.f);
		StatueKey->SetAttenuationRadius(520.f);
		StatueKey->SetCastShadows(false);   // cheap
		const FVector From(95.f, 0.f, GodPedestalTop + GodStatueHeight + 90.f), To(0.f, 0.f, GodPedestalTop + GodStatueHeight * 0.55f);
		StatueKey->SetRelativeLocationAndRotation(From, (To - From).Rotation());
		StatueKey->RegisterComponent();
	}
	if (!StatueFog)
	{
		// Local fog volumes start 10 m from the camera by default; the table is closer than that.
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.LocalFogVolume.GlobalStartDistance")))
			if (V->GetFloat() > 20.f) V->Set(10.f, ECVF_SetByCode);
		StatueFog = NewObject<ULocalFogVolumeComponent>(this);
		StatueFog->SetupAttachment(RootComponent);
		StatueFog->SetRelativeLocation(FVector(0.f, 0.f, GodPedestalTop + 18.f));
		StatueFog->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.2f));   // the volume is a 500 cm sphere at scale 1
		StatueFog->SetRadialFogExtinction(0.5f);   // a little body; the height mist is dense at the feet, thinning upward
		StatueFog->SetHeightFogExtinction(2.f);
		StatueFog->SetHeightFogFalloff(1400.f);
		StatueFog->SetHeightFogOffset(0.f);
		StatueFog->RegisterComponent();
	}
	StatueKey->SetVisibility(true);
	StatueFog->SetVisibility(true);
	if (Glow) Glow->SetRelativeLocation(FVector(-34.f, 0.f, GodPedestalTop + GodStatueHeight * 0.6f));   // rim, from behind
}

void AFNGodPedestal::ShowGodPicture(UTexture2D* Tex, const FString& Player)
{
	using namespace FN7Gods;
	if (!Tex || Player != StatueFor || bAccountStatue) return;   // a newer player, or the statue is already up
	PictureTex = Tex;
	if (!Picture)
	{
		Picture = NewObject<UStaticMeshComponent>(this);
		Picture->SetupAttachment(RootComponent);
		Picture->SetMobility(EComponentMobility::Movable);
		Picture->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Picture->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Picture->RegisterComponent();
	}
	if (UMaterialInterface* Im = LoadObject<UMaterialInterface>(nullptr, GodImageMatPath))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Im, Picture);
		M->SetTextureParameterValue(TEXT("SlateUI"), Tex);
		Picture->SetMaterial(0, M);
	}
	// Standing on the cap, facing the table (+X), in the picture's own shape, statue height at most.
	const float Aspect = (float)Tex->GetSizeX() / FMath::Max(1.f, (float)Tex->GetSizeY());
	const float H = GodStatueHeight * 0.9f, W = FMath::Min(H * Aspect, GodStatueFootprint * 0.9f);
	Picture->SetRelativeScale3D(FVector(0.02f, W / 100.f, H / 100.f));
	Picture->SetRelativeLocation(FVector(0.f, 0.f, GodPedestalTop + H * 0.5f + 2.f));
	Picture->SetVisibility(true);
	if (Statue) Statue->SetVisibility(false);
	ShowFigure(false);
	UE_LOG(LogTemp, Display, TEXT("FN7 god: %s -> their own god's picture on the pedestal"), *Player);
}

void AFNGodPedestal::ShowRuntimeStatue(UStaticMesh* Mesh, const FLinearColor& Stone, const FString& Player)
{
	if (!Mesh || Player != StatueFor) return;
	RuntimeMesh = Mesh;
	bAccountStatue = true;
	bHasStatue = true;
	PlaceStatue(Mesh, 0.f);   // built facing +X (the table) already
	if (BaseMat && Statue)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, Statue);
		M->SetVectorParameterValue(TEXT("Color"), Stone);
		Statue->SetMaterial(0, M);
	}
	if (Picture) Picture->SetVisibility(false);
	ShowFigure(false);
	UE_LOG(LogTemp, Display, TEXT("FN7 god: %s -> their own god's statue on the pedestal"), *Player);
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
	// One family at the table: the faceted figures are marble too, like stone/marble statues.
	for (int32 i = 0; i < Robe.Num(); ++i) GodSetColor(Robe[i], God.bAlive ? (i == Robe.Num() - 1 ? GodMarble : GodMarbleDark) : GodGrey * 0.5f);
	UpdateStatue(God.Player);
	if (StatueFog)
	{
		StatueFog->SetFogAlbedo(Aura * 0.4f);
		StatueFog->SetFogEmissive(Aura * (0.3f * Dim));
	}
	if (Glow) Glow->SetLightColor(Aura);
	if (Plate)
	{
		const TCHAR* PlateTag = !God.bAlive ? TEXT("OUT") : !God.bConnected ? TEXT("AWAY") : God.bHuman ? TEXT("HUMAN") : TEXT("AI");
		Plate->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *God.Player, PlateTag)));
		Plate->SetTextRenderColor((God.bAlive ? Aura : GodGrey * 2.f).ToFColor(true));
		if (PlateBack)
		{
			// sized to the longer line (about 0.6 of the size a letter), two lines tall, just behind the letters
			const float Sz = Plate->WorldSize, Chars = (float)FMath::Max(God.Player.Len(), (int32)FCString::Strlen(PlateTag));
			const float W = Chars * Sz * 0.62f + Sz * 0.9f, Ht = Sz * 2.5f;
			PlateBack->SetRelativeLocation(FVector(-1.2f, 0.f, 0.f));
			PlateBack->SetRelativeScale3D(FVector(0.015f, W / 100.f, Ht / 100.f));
		}
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
		const bool bShow = bOn && Info.bHuman && !bHasStatue;   // a statue gets light and mist, not orbiting blocks
		Beads[i]->SetVisibility(bShow);
		if (!bShow) continue;
		const float A = 2.f * PI * i / Beads.Num() + T * 0.4f;
		Beads[i]->SetRelativeLocation(FVector(FMath::Cos(A) * 18.f, FMath::Sin(A) * 18.f, (GodPedestalTop + (bHasStatue ? GodStatueHeight + 8.f : 96.f)) + 1.5f * FMath::Sin(T * 1.6f)));
	}
	for (int32 i = 0; i < Cubes.Num(); ++i)
	{
		const bool bShow = bOn && !Info.bHuman && Pulse > 0.5f && !bHasStatue;
		Cubes[i]->SetVisibility(bShow);
		if (!bShow) continue;
		const float A = 2.f * PI * i / Cubes.Num() + T * 2.2f;
		Cubes[i]->SetRelativeLocation(FVector(FMath::Cos(A) * 28.f, FMath::Sin(A) * 28.f, (GodPedestalTop + (bHasStatue ? GodStatueHeight - 6.f : 70.f)) + FMath::Sin(A * 2.f) * 12.f));
		Cubes[i]->SetRelativeRotation(FRotator(T * 90.f, T * 120.f, 0.f));
	}
	GodFaceCamera(Plate, GodCameraLocation(GetWorld(), GetActorLocation() + FVector(0, 340.f * S, 190.f * S)));
	if (bHasStatue && Statue)
	{
		// A statue that seems to breathe: a slow look around its seat, and the mist glows with the aura's pulse.
		Statue->SetRelativeRotation(FRotator(0.f, GodStatueYaw + 9.f * FMath::Sin(T * 0.33f), 0.f));
		if (StatueFog) StatueFog->SetFogEmissive((Info.bAlive ? Info.Aura : GodGrey) * (0.22f + 0.12f * Pulse) * Away);
	}
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
	UpdateFigure(D.Name, D.Skin, D.Side);
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

namespace FN7Figures
{
	static constexpr float Height = 24.f, Footprint = 18.f, PlinthTop = 4.2f;   // cm, on the 26 cm hex plinth
	static FString DemigodSlug(const FString& Name)
	{
		// "Chris's Left Hand" -> chris_left_hand, "Bot Kestrel's Left Hand" -> kestrel_left_hand (production_assets/meshy/figures).
		FString N = Name.ToLower().Replace(TEXT("'s "), TEXT(" ")).Replace(TEXT("\u2019s "), TEXT(" "));
		N.RemoveFromStart(TEXT("bot "));
		FString Out;
		for (TCHAR Ch : N) Out.AppendChar(FChar::IsAlnum(Ch) ? Ch : TEXT('_'));
		while (Out.ReplaceInline(TEXT("__"), TEXT("_")) > 0) {}
		Out.RemoveFromStart(TEXT("_"));
		Out.RemoveFromEnd(TEXT("_"));
		return Out;
	}
}

void AFNDemigodFigure::EndPlay(const EEndPlayReason::Type Reason)
{
	// Cleanup: its clone leaves the statue set with it.
	if (StatueMesh && StatueOwner.IsValid() && StatueId.IsValid()) StatueOwner->RemoveStatue(StatueMesh, StatueId);
	StatueId = FPrimitiveInstanceId();
	Super::EndPlay(Reason);
}

FPrimitiveInstanceId AFNTableGods::AddStatue(UStaticMesh* Mesh, const FTransform& World)
{
	if (!Mesh) return FPrimitiveInstanceId();
	TObjectPtr<UInstancedStaticMeshComponent>& Set = StatueSets.FindOrAdd(Mesh);
	if (!Set)
	{
		Set = NewObject<UInstancedStaticMeshComponent>(this);
		Set->SetupAttachment(RootComponent);
		Set->SetUsingAbsoluteLocation(true);
		Set->SetUsingAbsoluteRotation(true);
		Set->SetUsingAbsoluteScale(true);
		Set->SetMobility(EComponentMobility::Movable);
		Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Set->SetCastShadow(false);
		Set->SetStaticMesh(Mesh);
		Set->RegisterComponent();
		UE_LOG(LogTemp, Display, TEXT("FN7 figure: clone set for %s"), *Mesh->GetName());
	}
	return Set->AddInstanceById(World, true);
}

void AFNTableGods::MoveStatue(UStaticMesh* Mesh, FPrimitiveInstanceId Id, const FTransform& World)
{
	if (const TObjectPtr<UInstancedStaticMeshComponent>* Set = StatueSets.Find(Mesh))
		if (*Set) (*Set)->UpdateInstanceTransformById(Id, World, true, true);
}

void AFNTableGods::RemoveStatue(UStaticMesh* Mesh, FPrimitiveInstanceId Id)
{
	if (const TObjectPtr<UInstancedStaticMeshComponent>* Set = StatueSets.Find(Mesh))
		if (*Set) (*Set)->RemoveInstanceById(Id);
}

void AFNDemigodFigure::UpdateFigure(const FString& Name, const FString& Skin, const FString& InSide)
{
	using namespace FN7Figures;
	const FString Key = Name + TEXT("|") + Skin;
	if (Key == FigureFor) return;
	FigureFor = Key;
	const FString Slug = DemigodSlug(Name);
	const FString Path = FString::Printf(TEXT("/Game/Figures/%s/SM_%s.SM_%s"), *Slug, *Slug, *Slug);
	UStaticMesh* Mesh = Slug.IsEmpty() ? nullptr : Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	if (Mesh)
	{
		const FBoxSphereBounds B = Mesh->GetBounds();
		const float Scale = FMath::Min(Height / FMath::Max(1.f, (float)B.BoxExtent.Z * 2.f), Footprint / FMath::Max(1.f, 2.f * (float)FMath::Max(B.BoxExtent.X, B.BoxExtent.Y)));
		AFNTableGods* FigOwner = AFNTableGods::GetAny(GetWorld());   // not "Owner": that is AActor::Owner (MSVC C4458 on Win64)
		if (StatueMesh && StatueOwner.IsValid() && StatueId.IsValid()) StatueOwner->RemoveStatue(StatueMesh, StatueId);
		StatueMesh = Mesh;
		StatueOwner = FigOwner;
		StatueRel = FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PlinthTop + ((float)B.BoxExtent.Z - (float)B.Origin.Z) * Scale), FVector(Scale));
		StatueShown = FTransform(FRotator::ZeroRotator, GetActorTransform().TransformPosition(StatueRel.GetLocation()), StatueRel.GetScale3D() * GetActorScale3D());
		StatueId = FigOwner ? FigOwner->AddStatue(Mesh, StatueShown) : FPrimitiveInstanceId();
		FigureH = (float)B.BoxExtent.Z * 2.f * Scale;
		if (Mini) { Mini->DestroyComponent(); Mini = nullptr; }
	}
	else
	{
		// No statue (the AI Right Hands): the duel mannequin in its skin, standing guard.
		if (!Mini)
		{
			Mini = NewObject<USkeletalMeshComponent>(this);
			Mini->SetupAttachment(RootComponent);
			Mini->SetUsingAbsoluteRotation(true);
			Mini->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mini->RegisterComponent();
			Mini->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Duel/Mannequin/SK_DuelMannequin.SK_DuelMannequin")));
			Mini->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Duel/Mannequin/Anims/A_idle_guard.A_idle_guard"))) Mini->PlayAnimation(Idle, true);
		}
		const FString UseSkin = !Skin.IsEmpty() ? Skin : (InSide == TEXT("right") ? FString(TEXT("obsidian")) : FString(TEXT("wood")));
		const FString Mi = FString::Printf(TEXT("/Game/Duel/Skins/MI_Skin_%s.MI_Skin_%s"), *UseSkin, *UseSkin);
		if (UMaterialInterface* M = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *Mi, nullptr, LOAD_NoWarn | LOAD_Quiet)))
			for (int32 i = 0; i < Mini->GetNumMaterials(); ++i) Mini->SetMaterial(i, M);
		const float Scale = Height / 180.f;   // the mannequin stands ~1.8 m
		Mini->SetRelativeScale3D(FVector(Scale));
		Mini->SetRelativeLocation(FVector(0.f, 0.f, PlinthTop));
		Mini->SetVisibility(true);
		Mini->SetComponentTickEnabled(true);
		MiniFrames = 0;
		FigureH = Height;
		if (StatueMesh && StatueOwner.IsValid() && StatueId.IsValid()) StatueOwner->RemoveStatue(StatueMesh, StatueId);
		StatueMesh = nullptr;
		StatueId = FPrimitiveInstanceId();
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 figure: %s -> %s"), *Name, Mesh ? *FString::Printf(TEXT("statue %s"), *Slug) : *FString::Printf(TEXT("mannequin (%s)"), Skin.IsEmpty() ? TEXT("side default") : *Skin));
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
	// On the board the card floats ~16 cm of table above its hex (the props top out around 14 cm), so towers, trees and
	// peaks in front of it never cover it (portraits should not be in front of buildings). Nothing stands in front of it:
	// no pole. The poles should not be in front.
	const float Raise = bOnBoard ? 40.f : 0.f;
	// With a figure on the plinth the card floats over its head, smaller (the statue is the piece; the card its portrait).
	const float Over = FigureH > 0.f ? FigureH + 6.f : 0.f;
	const float CardS = FigureH > 0.f ? 0.62f : 1.f;
	Card->SetRelativeLocation(FVector(0.f, 0.f, FMath::Lerp(4.2f + 17.f * CardS + Over + Raise, 4.2f + 0.8f, Tip) + 9.f * HeldT + 1.2f * HeldT * FMath::Sin(Time * 6.f)));
	Card->SetRelativeScale3D(FVector(CardS * (1.f + 0.18f * HeldT)));
	// The figure faces the camera (turning gently), and lies down when the demigod falls.
	{
		const float WantYaw = ToH.IsNearlyZero() ? FigureYaw : ToH.Rotation().Yaw - 90.f;
		FigureYaw = FMath::RadiansToDegrees(FMath::UnwindRadians(FMath::DegreesToRadians(FigureYaw)));
		FigureYaw += FMath::FindDeltaAngleDegrees(FigureYaw, WantYaw) * FMath::Clamp(Dt * 4.f, 0.f, 1.f);
		const FRotator R(0.f, FigureYaw, -85.f * Tip);
		if (StatueMesh && StatueOwner.IsValid() && StatueId.IsValid())
		{
			// The clone moves only when the figure does (turning to the camera, summoned, ridden by the board's pan).
			const FTransform W = IsHidden() ? FTransform(R, GetActorLocation(), FVector::ZeroVector)
				: FTransform(R, GetActorTransform().TransformPosition(StatueRel.GetLocation()), StatueRel.GetScale3D() * GetActorScale3D());
			if (!W.Equals(StatueShown, 0.01))
			{
				StatueShown = W;
				StatueOwner->MoveStatue(StatueMesh, StatueId, W);
			}
		}
		if (Mini && Mini->IsVisible())
		{
			Mini->SetWorldRotation(R);
			// Posed (idle guard), then frozen: a few frames of animation to settle the pose, no ticking after that.
			if (MiniFrames < 4 && ++MiniFrames == 4) { Mini->bPauseAnims = true; Mini->SetComponentTickEnabled(false); }
		}
	}
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(FN7Gods::GodBaseMatPath);
	if (Mat.Succeeded()) FxMat = Mat.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Img(FN7Gods::GodImageMatPath);
	if (Img.Succeeded()) CardMat = Img.Object;
}

UStaticMeshComponent* AFNTableGods::FxShape(const TCHAR* Mesh, const FVector& At, const FVector& Scale, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetUsingAbsoluteLocation(true);
	C->SetUsingAbsoluteRotation(true);
	C->SetUsingAbsoluteScale(true);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	if (FxMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(FxMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->SetWorldLocation(At);
	C->SetWorldScale3D(Scale);
	C->RegisterComponent();
	FxParts.Add(C);
	return C;
}

USceneComponent* AFNTableGods::FxRoot(const FVector& At)
{
	USceneComponent* C = NewObject<USceneComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetUsingAbsoluteLocation(true);
	C->SetUsingAbsoluteRotation(true);
	C->SetUsingAbsoluteScale(true);
	C->SetWorldLocation(At);
	C->RegisterComponent();
	FxParts.Add(C);
	return C;
}

UStaticMeshComponent* AFNTableGods::FxChild(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Scale);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	if (FxMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(FxMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	FxParts.Add(C);
	return C;
}

bool AFNTableGods::BoardWorld(double Lat, double Lon, FVector& Out) const
{
	const AFNStudio* St = Studio.Get();
	return St && St->BoardPointFor(Lat, Lon, Out);
}

bool AFNTableGods::GetBoardPos(const FString& Ref, double& OutLat, double& OutLon) const
{
	const FFNTableDemigod* D = BoardInfo.Find(Ref);
	if (!D || !D->bHasPos) return false;
	OutLat = D->Lat;
	OutLon = D->Lon;
	return true;
}

void AFNTableGods::PlaySummoning(const FString& Ref, double FromLat, double FromLon, const FLinearColor& Aura)
{
	using namespace FN7Gods;
	if (Dissolved.Contains(Ref) || PulseOut.Contains(Ref)) return;   // already going (your drop, then the server's push)
	const float S = Studio.IsValid() ? (float)Studio->GetActorScale3D().X : 1.f;
	FVector At;
	if (!BoardWorld(FromLat, FromLon, At)) { Dissolved.Add(Ref, 0.f); return; }
	// The red aura should gently pulse in and out, and beams must not obscure it. A red aura disc breathes under
	// the piece while it pulses out.
	FFx F;
	F.Kind = 6;
	F.Dur = 0.9f;
	F.A = At;
	F.Ref = Ref;
	F.Comps.Add(FxShape(GodCyl, At + FVector(0, 0, 0.4f * S), FVector(0.001f), FLinearColor(1.f, 0.07f, 0.04f) * 3.f));
	Fx.Add(F);
	PulseOut.Add(Ref, 0.f);
	PulseIn.Remove(Ref);
}

void AFNTableGods::ResetSummons()
{
	for (FFx& F : Fx) for (USceneComponent* C : F.Comps) if (C) { FxParts.Remove(C); C->DestroyComponent(); }
	Fx.Reset();
	Dissolved.Reset();
	LocalPos.Reset();
	PulseOut.Reset();
	PulseIn.Reset();
	Shake.Reset();
	PieceLL.Reset();
}

void AFNTableGods::CancelSummon(const FString& Ref)
{
	Dissolved.Remove(Ref);
	LocalPos.Remove(Ref);
	PulseOut.Remove(Ref);
	PulseIn.Remove(Ref);
	Shake.Add(Ref, 0.7f);
	for (int32 i = Fx.Num() - 1; i >= 0; --i)
		if (Fx[i].Ref == Ref)
		{
			for (USceneComponent* C : Fx[i].Comps) if (C) { FxParts.Remove(C); C->DestroyComponent(); }
			Fx.RemoveAt(i);
		}
}

void AFNTableGods::PlaySummoned(const FString& Ref, double FromLat, double FromLon, double ToLat, double ToLon, float HealthLost, int32 Roll, const FLinearColor& Aura, float Delay)
{
	using namespace FN7Gods;
	const float S = Studio.IsValid() ? (float)Studio->GetActorScale3D().X : 1.f;
	// Its new place holds until the commit agrees (every HUD refresh rebuilds the board from the last commit).
	SetLocalPos(Ref, ToLat, ToLon);
	LocalPos.Add(Ref, FVector(ToLat, ToLon, 0.f));
	FVector B;
	if (!BoardWorld(ToLat, ToLon, B)) { Dissolved.Remove(Ref); PulseOut.Remove(Ref); return; }   // off the board: it simply stands there
	if (!PulseOut.Contains(Ref)) Dissolved.Add(Ref, 0.f);
	// At the target: a red aura breathing on the hex, the diorama card shown briefly over it (no beams), then the piece
	// pulses in. Delay lets the origin's pulse-out finish first.
	FFx Ring;
	Ring.Kind = 6;
	Ring.Dur = 2.6f;
	Ring.T = -Delay;
	Ring.A = B;
	Ring.Ref = Ref;
	Ring.Comps.Add(FxShape(GodCyl, B + FVector(0, 0, 0.4f * S), FVector(0.001f), FLinearColor(1.f, 0.07f, 0.04f) * 3.f));
	Fx.Add(Ring);
	FFx Card;
	Card.Kind = 4;
	Card.Dur = 1.1f;
	Card.T = -Delay;
	Card.A = B;
	Card.Color = FLinearColor(1.f, 0.12f, 0.08f);
	Card.Ref = Ref;
	Card.Value = HealthLost;
	Card.Roll = Roll;
	if (const TWeakObjectPtr<UTexture2D>* T = DioramaOf.Find(Ref)) Card.Tex = *T;
	USceneComponent* Root = FxRoot(B);
	Card.Comps.Add(Root);
	Card.Comps.Add(FxChild(Root, GodPlane, FVector(0, 0, -0.8f), FVector(1.08f, 1.08f, 1.f), FRotator::ZeroRotator, FLinearColor(0.01f, 0.01f, 0.015f)));
	Card.Comps.Add(FxChild(Root, GodPlane, FVector(0, 0, -0.4f), FVector(1.03f, 1.03f, 1.f), FRotator::ZeroRotator, Card.Color * 1.6f));
	UStaticMeshComponent* Img = FxChild(Root, GodPlane, FVector::ZeroVector, FVector(0.95f, 0.95f, 1.f), FRotator::ZeroRotator, Aura * 0.25f);
	if (UTexture2D* T = Card.Tex.Get())
		if (CardMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(CardMat, Img);
			M->SetTextureParameterValue(TEXT("SlateUI"), T);
			Img->SetMaterial(0, M);
		}
	Card.Comps.Add(Img);
	Root->SetWorldScale3D(FVector(0.001f));
	Fx.Add(Card);
}

void AFNTableGods::SpawnLanding(const FVector& At, float HealthLost, int32 Roll, TArray<FFx>& Out)
{
	using namespace FN7Gods;
	const float S = Studio.IsValid() ? (float)Studio->GetActorScale3D().X : 1.f;
	const FVector Cam = GodCameraLocation(GetWorld(), GetActorLocation() + FVector(0, 340.f * S, 190.f * S));
				if (HealthLost > 0.f)
				{
					FFx Dmg;
					Dmg.Kind = 3;
					Dmg.Dur = 2.4f;
					Dmg.A = At + FVector(0, 0, 25.f * S);
					UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
					T->SetupAttachment(RootComponent);
					T->SetUsingAbsoluteLocation(true);
					T->SetUsingAbsoluteRotation(true);
					T->SetUsingAbsoluteScale(true);
					T->SetHorizontalAlignment(EHTA_Center);
					T->SetVerticalAlignment(EVRTA_TextCenter);
					T->SetWorldSize(11.f * S);
					T->SetTextRenderColor(FColor(255, 70, 50));
					T->SetText(FText::FromString(FString::Printf(TEXT("-%g"), FMath::RoundToFloat(HealthLost * 10.f) / 10.f)));
					T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					T->SetWorldLocation(Dmg.A);
					T->RegisterComponent();
					FxParts.Add(T);
					Dmg.Comps.Add(T);
					Out.Add(Dmg);
				}
				if (Roll > 0)
				{
					// The d20: an ivory gem thrown out beside the piece, tumbling, settling with its number over it.
					FFx Die;
					Die.Kind = 5;
					Die.Dur = 3.6f;
					FVector Side = FVector::CrossProduct(FVector::UpVector, (Cam - At).GetSafeNormal2D()).GetSafeNormal();
					if (Side.IsNearlyZero()) Side = FVector(1, 0, 0);
					Die.A = At + FVector(0, 0, 10.f * S);
					Die.B = At + Side * 16.f * S + FVector(0, 0, 3.f * S);
					Die.Roll = Roll;
					USceneComponent* Gem = FxRoot(Die.A);
					Gem->SetWorldScale3D(FVector(S));
					Die.Comps.Add(Gem);
					Die.Comps.Add(FxChild(Gem, GodCone, FVector(0, 0, 4.f), FVector(0.1f, 0.1f, 0.08f), FRotator::ZeroRotator, GodIvory * 1.5f));
					Die.Comps.Add(FxChild(Gem, GodCone, FVector(0, 0, -4.f), FVector(0.1f, 0.1f, 0.08f), FRotator(180.f, 0.f, 0.f), GodIvory * 1.2f));
					UTextRenderComponent* N = NewObject<UTextRenderComponent>(this);
					N->SetupAttachment(RootComponent);
					N->SetUsingAbsoluteLocation(true);
					N->SetUsingAbsoluteRotation(true);
					N->SetUsingAbsoluteScale(true);
					N->SetHorizontalAlignment(EHTA_Center);
					N->SetVerticalAlignment(EVRTA_TextCenter);
					N->SetWorldSize(10.f * S);
					N->SetTextRenderColor(Roll == 1 ? FColor(255, 70, 50) : (Roll == 20 ? FColor(120, 255, 140) : FColor(255, 214, 120)));
					N->SetText(FText::AsNumber(Roll));
					N->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					N->SetVisibility(false);
					N->SetWorldLocation(Die.B + FVector(0, 0, 12.f * S));
					N->RegisterComponent();
					FxParts.Add(N);
					Die.Comps.Add(N);
					Out.Add(Die);
				}
}

void AFNTableGods::PlayWarpIn(const FString& Ref, double FromLat, double FromLon, double ToLat, double ToLon, const FLinearColor& Aura, float Delay, float HealthLost, int32 Roll)
{
	PlaySummoned(Ref, FromLat, FromLon, ToLat, ToLon, HealthLost, Roll, Aura, Delay);
}

void AFNTableGods::PlaySummonResult(const FString& Ref, double ToLat, double ToLon, float HealthLost, int32 Roll)
{
	SetLocalPos(Ref, ToLat, ToLon);
	LocalPos.Add(Ref, FVector(ToLat, ToLon, 0.f));
	FVector B;
	if (!BoardWorld(ToLat, ToLon, B)) return;
	TArray<FFx> Out;
	SpawnLanding(B, HealthLost, Roll, Out);
	Fx.Append(Out);
}

void AFNTableGods::TickFx(float Dt)
{
	using namespace FN7Gods;
	const float S = Studio.IsValid() ? (float)Studio->GetActorScale3D().X : 1.f;
	// Safety: a piece never stays dissolved (no answer from the server) and a local place never outlives the commits.
	for (auto It = Dissolved.CreateIterator(); It; ++It) if ((It.Value() += Dt) > 12.f) It.RemoveCurrent();
	for (auto It = LocalPos.CreateIterator(); It; ++It) if ((It.Value().Z += Dt) > 20.f) It.RemoveCurrent();
	for (auto It = Shake.CreateIterator(); It; ++It) if ((It.Value() -= Dt) <= 0.f) It.RemoveCurrent();
	const FVector Cam = GodCameraLocation(GetWorld(), GetActorLocation() + FVector(0, 340.f * S, 190.f * S));
	TArray<FFx> Spawned;
	for (int32 i = Fx.Num() - 1; i >= 0; --i)
	{
		FFx& F = Fx[i];
		F.T += Dt;
		const float U = FMath::Clamp(F.T / F.Dur, 0.f, 1.f);
		if (F.Kind == 0)   // the column: rises fast, then thins away
		{
			const float H = FMath::Min(U / 0.33f, 1.f) * 90.f * S;
			const float Thin = U < 0.33f ? 1.f : 1.f - (U - 0.33f) / 0.67f;
			for (int32 k = 0; k < F.Comps.Num(); ++k)
			{
				const float W = (k == 0 ? 0.05f : 0.12f) * S * FMath::Max(Thin, 0.02f);
				F.Comps[k]->SetWorldLocation(F.A + FVector(0, 0, H * 0.5f));
				F.Comps[k]->SetWorldScale3D(FVector(W, W, H / 100.f + 0.001f));
			}
		}
		else if (F.Kind == 1)   // the arc: a parabola, the trail a few steps behind
		{
			const float Height = FMath::Max(40.f * S, (float)FVector::Dist(F.A, F.B) * 0.35f);
			for (int32 k = 0; k < F.Comps.Num(); ++k)
			{
				const float V = FMath::Clamp(U - 0.035f * k, 0.f, 1.f);
				F.Comps[k]->SetWorldLocation(FMath::Lerp(F.A, F.B, V) + FVector(0, 0, 4.f * V * (1.f - V) * Height));
			}
			if (U >= 1.f)
			{
				// Arrival: a burst, and the demigod's diorama pops up big over its hex.
				FFx Burst;
				Burst.Kind = 2;
				Burst.Dur = 0.9f;
				Burst.A = F.B;
				for (int32 k = 0; k < 16; ++k)
				{
					const float Ang = 2.f * PI * k / 16.f;
					Burst.Comps.Add(FxShape(GodCube, F.B, FVector(0.018f * S), (k % 3 == 0 ? FLinearColor::White : F.Color) * 2.5f));
					Burst.Vel.Add(FVector(FMath::Cos(Ang) * 70.f, FMath::Sin(Ang) * 70.f, FMath::FRandRange(60.f, 130.f)) * S);
				}
				Spawned.Add(Burst);
				FFx Card;
				Card.Kind = 4;
				Card.Dur = 1.3f;
				Card.A = F.B;
				Card.Color = F.Color;
				Card.Ref = F.Ref;
				Card.Value = F.Value;
				Card.Roll = F.Roll;
				Card.Tex = F.Tex;
				USceneComponent* Root = FxRoot(F.B);
				Card.Comps.Add(Root);
				Card.Comps.Add(FxChild(Root, GodPlane, FVector(0, 0, -0.8f), FVector(1.08f, 1.08f, 1.f), FRotator::ZeroRotator, FLinearColor(0.01f, 0.01f, 0.015f)));
				Card.Comps.Add(FxChild(Root, GodPlane, FVector(0, 0, -0.4f), FVector(1.03f, 1.03f, 1.f), FRotator::ZeroRotator, F.Color * 1.6f));
				UStaticMeshComponent* Img = FxChild(Root, GodPlane, FVector::ZeroVector, FVector(0.95f, 0.95f, 1.f), FRotator::ZeroRotator, F.Color * 0.25f);
				if (UTexture2D* T = F.Tex.Get())
					if (CardMat)
					{
						UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(CardMat, Img);
						M->SetTextureParameterValue(TEXT("SlateUI"), T);
						Img->SetMaterial(0, M);
					}
				Card.Comps.Add(Img);
				Root->SetWorldScale3D(FVector(0.001f));
				Spawned.Add(Card);
			}
		}
		else if (F.Kind == 2)   // the burst: sparks fly up and out and fall
		{
			for (int32 k = 0; k < F.Comps.Num(); ++k)
			{
				F.Vel[k] += FVector(0, 0, -260.f * S) * Dt;
				F.Comps[k]->SetWorldLocation(F.Comps[k]->GetComponentLocation() + F.Vel[k] * Dt);
				F.Comps[k]->SetWorldScale3D(FVector(0.018f * S * (1.f - U)));
			}
		}
		else if (F.Kind == 3)   // the red -N: rises and turns to the camera
		{
			if (UTextRenderComponent* T = Cast<UTextRenderComponent>(F.Comps[0]))
			{
				T->SetWorldLocation(F.A + FVector(0, 0, 30.f * S * U));
				GodFaceCamera(T, Cam);
				T->SetVisibility(U < 0.95f);
			}
		}
		else if (F.Kind == 4)   // the diorama card: zooms in big over the hex, holds, shrinks down into the piece
		{
			USceneComponent* Root = F.Comps.Num() > 0 ? F.Comps[0] : nullptr;
			if (Root)
			{
				const float Big = 0.42f * S;     // ~42 cm of table at the table's scale (it ends where the piece's raised card stands)
				float Sc, Up;
				if (U < 0.3f)
				{
					const float E = U / 0.3f;
					const float Back = 1.f + 2.2f * FMath::Pow(E - 1.f, 3.f) + 1.2f * FMath::Pow(E - 1.f, 2.f);   // ease out, a little overshoot
					Sc = Big * Back;
					Up = 52.f * S * E;
				}
				else if (U < 0.75f)
				{
					Sc = Big * (1.f + 0.02f * FMath::Sin((U - 0.3f) * 18.f));
					Up = 52.f * S + 2.f * S * FMath::Sin((U - 0.3f) * 9.f);
				}
				else
				{
					const float E = (U - 0.75f) / 0.25f;
					const float Ease = E * E * (3.f - 2.f * E);
					Sc = FMath::Lerp(Big, 0.11f * S, Ease);
					Up = FMath::Lerp(52.f * S, 23.f * S, Ease);
				}
				const FVector P = F.A + FVector(0, 0, Up);
				Root->SetWorldLocation(P);
				Root->SetWorldScale3D(FVector(FMath::Max(Sc, 0.001f), FMath::Max(Sc, 0.001f), FMath::Max(Sc, 0.001f)));
				const FVector To = (Cam - P).GetSafeNormal();
				Root->SetWorldRotation(FRotationMatrix::MakeFromZY(To, -FVector::UpVector).ToQuat());
			}
			if (U >= 1.f)
			{
				// It settles into its piece: the piece drops onto its hex, the damage rises, the d20 tumbles beside it.
				Dissolved.Remove(F.Ref);
				PulseIn.Add(F.Ref, 0.f);
				if (TObjectPtr<AFNDemigodFigure>* Fig = OnBoard.Find(F.Ref)) if (Fig->Get()) (*Fig)->SetActorLocation(F.A);
				if (const FFNTableDemigod* D = BoardInfo.Find(F.Ref)) PieceLL.Add(F.Ref, FVector(D->Lat, D->Lon, -1.f));   // stands there: no slide
				SpawnLanding(F.A, F.Value, F.Roll, Spawned);
			}
		}
		else if (F.Kind == 6)   // the red aura: a disc on the hex that breathes gently, swelling in and fading out
		{
			if (F.T >= 0.f && F.Comps.Num() > 0)
			{
				const float In = FMath::Clamp(F.T / 0.25f, 0.f, 1.f), Out = FMath::Clamp((F.Dur - F.T) / 0.4f, 0.f, 1.f);
				const float R = 0.2f * S * In * Out * (1.f + 0.22f * FMath::Sin(F.T * 7.f));
				F.Comps[0]->SetWorldScale3D(FVector(FMath::Max(R, 0.001f), FMath::Max(R, 0.001f), 0.004f));
			}
		}
		else if (F.Kind == 5)   // the d20: thrown in an arc, spinning, rests; then its number shows
		{
			if (USceneComponent* Gem = F.Comps.Num() > 0 ? F.Comps[0] : nullptr)
			{
				const float Throw = FMath::Clamp(F.T / 0.7f, 0.f, 1.f);
				const float Bounce = Throw < 1.f ? 4.f * Throw * (1.f - Throw) * 14.f * S : 0.f;
				Gem->SetWorldLocation(FMath::Lerp(F.A, F.B, Throw) + FVector(0, 0, Bounce));
				const float Spin = (1.f - Throw) * (1.f - Throw);
				Gem->SetWorldRotation(FRotator(F.T * 720.f * Spin + 20.f, F.T * 540.f * Spin, F.T * 300.f * Spin));
			}
			if (F.Comps.Num() > 3)
				if (UTextRenderComponent* N = Cast<UTextRenderComponent>(F.Comps[3]))
				{
					N->SetVisibility(F.T > 0.75f && U < 0.97f);
					GodFaceCamera(N, Cam);
				}
		}
		if (U >= 1.f)
		{
			for (int32 k = F.Comps.Num() - 1; k >= 0; --k) if (USceneComponent* C = F.Comps[k]) { FxParts.Remove(C); C->DestroyComponent(); }
			Fx.RemoveAt(i);
		}
	}
	Fx.Append(Spawned);
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

FString AFNTableGods::FigureAtScreen(UWorld* World, const FVector2D& ViewportPixel, TFunction<bool(const FString&)> Prefer, double ZoneScale, double* OutDistPx)
{
	AFNTableGods* T = GetAny(World);
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!T || !PC || !PC->PlayerCameraManager || !T->bShown) return FString();
	const FVector Right = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);
	FString Best, BestPref;
	double BestD = TNumericLimits<double>::Max(), BestPrefD = TNumericLimits<double>::Max();
	auto Test = [&](const TMap<FString, TObjectPtr<AFNDemigodFigure>>& Map)
	{
		for (const auto& KV : Map)
		{
			const AFNDemigodFigure* F = KV.Value.Get();
			FVector C;
			float Half;
			if (!F || !F->CardBounds(C, Half)) continue;
			FVector2D P0, P1, PF;
			if (!PC->ProjectWorldLocationToScreen(C, P0) || !PC->ProjectWorldLocationToScreen(C + Right * Half, P1)) continue;
			// Easy to click (Chris): well past the card's edge, at least 22 px, and the foot of its pole too.
			const double R = FMath::Max(FVector2D::Distance(P0, P1) * 1.7, 22.0) * ZoneScale;
			double D = FVector2D::Distance(P0, ViewportPixel);
			if (PC->ProjectWorldLocationToScreen(F->GetActorLocation(), PF)) D = FMath::Min(D, FVector2D::Distance(PF, ViewportPixel) * 1.4);
			const FString R0 = F->GetRef().IsEmpty() ? KV.Key : F->GetRef();
			if (D < R && D < BestD) { BestD = D; Best = R0; }
			if (D < R && Prefer && Prefer(R0) && D < BestPrefD) { BestPrefD = D; BestPref = R0; }
		}
	};
	Test(T->OnBoard);
	Test(T->Figures);
	if (OutDistPx) *OutDistPx = BestPref.IsEmpty() ? BestD : BestPrefD;
	return BestPref.IsEmpty() ? Best : BestPref;
}

void AFNTableGods::SetFigureHeld(const FString& Ref, bool bHeld)
{
	if (const TObjectPtr<AFNDemigodFigure>* F = Figures.Find(Ref)) if (F->Get()) (*F)->SetHeld(bHeld);
	if (const TObjectPtr<AFNDemigodFigure>* F = OnBoard.Find(Ref)) if (F->Get()) (*F)->SetHeld(bHeld);
}

void AFNTableGods::SetLocalPos(const FString& Ref, double Lat, double Lon)
{
	FFNTableDemigod* D = BoardInfo.Find(Ref);
	if (!D)
	{
		// Not on the board yet (no pos from the server): use its pedestal-side figure's data.
		FFNTableDemigod New;
		New.Ref = Ref;
		New.bAlive = true;
		D = &BoardInfo.Add(Ref, New);
		if (const TObjectPtr<AFNDemigodFigure>* Side = Figures.Find(Ref))
			if (Side->Get()) BoardAura.Add(Ref, FLinearColor(1.f, 0.85f, 0.4f));
	}
	D->Lat = Lat;
	D->Lon = Lon;
	D->bHasPos = true;
	if (const TWeakObjectPtr<UTexture2D>* T = DioramaOf.Find(Ref)) if (!D->Diorama) D->Diorama = T->Get();
	TObjectPtr<AFNDemigodFigure>& Fig = OnBoard.FindOrAdd(Ref);
	if (!Fig && GetWorld())
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Fig = GetWorld()->SpawnActor<AFNDemigodFigure>(GetActorLocation(), FRotator::ZeroRotator, P);
		if (Fig) { Fig->SetDemigod(*D, BoardAura.FindRef(Ref), true); Fig->SetOnBoard(true); }
	}
}

FString AFNTableGods::FigureNear(UWorld* World, const FVector2D& ViewportPixel, double MaxPx, TFunction<bool(const FString&)> Prefer)
{
	AFNTableGods* T = GetAny(World);
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!T || !PC || !T->bShown) return FString();
	FString Best, BestPref;
	double BestD = MaxPx, BestPrefD = MaxPx;
	auto Test = [&](const TMap<FString, TObjectPtr<AFNDemigodFigure>>& Map)
	{
		for (const auto& KV : Map)
		{
			const AFNDemigodFigure* F = KV.Value.Get();
			FVector C;
			float Half;
			if (!F || !F->CardBounds(C, Half)) continue;
			FVector2D P0, PF;
			double D = TNumericLimits<double>::Max();
			if (PC->ProjectWorldLocationToScreen(C, P0)) D = FVector2D::Distance(P0, ViewportPixel);
			if (PC->ProjectWorldLocationToScreen(F->GetActorLocation(), PF)) D = FMath::Min(D, FVector2D::Distance(PF, ViewportPixel));
			const FString R0 = F->GetRef().IsEmpty() ? KV.Key : F->GetRef();
			if (D < BestD) { BestD = D; Best = R0; }
			if (Prefer && Prefer(R0) && D < BestPrefD) { BestPrefD = D; BestPref = R0; }
		}
	};
	Test(T->OnBoard);
	Test(T->Figures);
	return BestPref.IsEmpty() ? Best : BestPref;
}

bool AFNTableGods::PiecesTouch(const FString& A, const FString& B) const
{
	const TObjectPtr<AFNDemigodFigure>* FA = OnBoard.Find(A);
	const TObjectPtr<AFNDemigodFigure>* FB = OnBoard.Find(B);
	if (!FA || !FB || !FA->Get() || !FB->Get() || (*FA)->IsHidden() || (*FB)->IsHidden()) return false;
	FVector CA, CB;
	float HA, HB;
	if (!(*FA)->CardBounds(CA, HA) || !(*FB)->CardBounds(CB, HB)) return false;
	// Cards touching: their centres closer than the two half-widths (plus a little grace).
	return FVector::Dist2D(CA, CB) <= (HA + HB) * 1.25f;
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
			if (Dg.Diorama) DioramaOf.Add(Key, Dg.Diorama);
			if (G.bMine)
			{
				if (Dg.bHasPos && Dg.bAlive) { BoardInfo.Add(Key, Dg); BoardAura.Add(Key, G.Aura); }
				continue;
			}
			float Deg = G.Demigods.Num() == 1 ? Angle : Angle + (Dg.Side == TEXT("right") ? -Off : Off);
			// The god at the right of the globe (slot 3, 305 deg): both boxes 2 deg further from the globe, which hid them a
			// bit (shift its first one 2 degrees and its second one 2 degrees too).
			if (Slot == 3) Deg += 2.f;
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
	// A summon that landed this beat holds its new place until a commit agrees (within 25 km: the server's nudge).
	for (auto It = LocalPos.CreateIterator(); It; ++It)
	{
		FFNTableDemigod* D = BoardInfo.Find(It.Key());
		if (!D) continue;
		const double La1 = FMath::DegreesToRadians(D->Lat), La2 = FMath::DegreesToRadians(It.Value().X);
		const double DLo = FMath::DegreesToRadians(It.Value().Y - D->Lon);
		const double Cos = FMath::Clamp(FMath::Sin(La1) * FMath::Sin(La2) + FMath::Cos(La1) * FMath::Cos(La2) * FMath::Cos(DLo), -1.0, 1.0);
		if (FMath::Acos(Cos) * 6371.0 < 25.0) { It.RemoveCurrent(); continue; }
		D->Lat = It.Value().X;
		D->Lon = It.Value().Y;
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
		if (Fig) { Fig->SetDemigod(KV.Value, BoardAura.FindRef(KV.Key), true); Fig->SetOnBoard(true); }
	}
}

void AFNTableGods::Tick(float Dt)
{
	Super::Tick(Dt);
	TickFx(Dt);
	TickRingPulses(Dt);
	AFNStudio* St = Studio.Get();
	if (St) { St->HideWillBlock(); St->TickViewAnim(Dt); }   // the WILL bar stays hidden; view changes animate
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
		const bool bOn = St->BoardPointFor(D->Lat, D->Lon, Wp) && !Dissolved.Contains(KV.Key);
		Fig->SetActorHiddenInGame(!bOn);
		if (bOn)
		{
			// Its own lat/lon changed (a server move, a summon): glide there for a moment. Only the VIEW changed (a click
			// zoomed or panned the map): stand exactly on its point at once, never wander (they should be at a lat/lon point).
			FVector& LL = PieceLL.FindOrAdd(KV.Key, FVector(D->Lat, D->Lon, -1.f));
			if (FMath::Abs(LL.X - D->Lat) > 1e-7 || FMath::Abs(LL.Y - D->Lon) > 1e-7) LL = FVector(D->Lat, D->Lon, 0.f);
			FVector At = Wp;
			if (LL.Z >= 0.f)
			{
				LL.Z += Dt;
				At = FMath::VInterpTo(Fig->GetActorLocation(), Wp, Dt, 6.f);
				if (LL.Z > 1.2f || FVector::Dist(At, Wp) < 0.05f) LL.Z = -1.f;
			}
			// A refused summon shakes.
			if (const float* Sh = Shake.Find(KV.Key)) At += FVector(FMath::Sin(*Sh * 70.f) * 3.f * S * (*Sh / 0.7f), 0.f, 0.f);
			if (PulseOut.Contains(KV.Key)) At = Fig->GetActorLocation();   // pulsing out where it stood: it never slides
			Fig->SetActorLocation(At);
			// Teleporting: the piece pulses out at the origin, and in at the target (gently, a few beats of a pulse).
			float Sc = 1.f;
			if (float* Po = PulseOut.Find(KV.Key))
			{
				*Po += Dt;
				const float E = FMath::Clamp(*Po / 0.6f, 0.f, 1.f);
				Sc = (1.f - E * E * (3.f - 2.f * E)) * (1.f + 0.14f * FMath::Sin(*Po * 16.f));
				if (*Po >= 0.6f) { PulseOut.Remove(KV.Key); Dissolved.Add(KV.Key, 0.f); }
			}
			else if (float* Pi = PulseIn.Find(KV.Key))
			{
				*Pi += Dt;
				const float E = FMath::Clamp(*Pi / 1.4f, 0.f, 1.f);
				Sc = FMath::Min(1.f, E * 2.f) * (1.f + 0.16f * (1.f - E) * FMath::Sin(*Pi * 9.f));
				if (*Pi >= 1.4f) PulseIn.Remove(KV.Key);
			}
			Fig->SetActorScale3D(FVector(0.36f * S * FMath::Max(Sc, 0.01f)));
		}
	}
}

void AFNTableGods::PulseRing(const FString& Ref, const FLinearColor& Color)
{
	const TObjectPtr<AFNDemigodFigure>* F = OnBoard.Find(Ref);
	if (!F || !*F) return;   // not on the board now: the centre brings it there; nothing to ring
	FRingPulse P;
	P.Ref = Ref;
	const FBox B = (*F)->GetComponentsBoundingBox(true);
	P.At = B.IsValid ? FVector(B.GetCenter().X, B.GetCenter().Y, B.Min.Z + 1.f) : (*F)->GetActorLocation();
	P.R = B.IsValid ? FMath::Max(8.f, (float)FMath::Max(B.GetExtent().X, B.GetExtent().Y)) : 20.f;
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	for (int32 i = 0; i < 18; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetStaticMesh(Sphere);
		if (M)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, C);
			D->SetVectorParameterValue(TEXT("Color"), Color);
			C->SetMaterial(0, D);
		}
		C->RegisterComponent();
		RingBeadsKeep.Add(C);
		P.Beads.Add(C);
	}
	RingPulses.Add(MoveTemp(P));
}

void AFNTableGods::TickRingPulses(float Dt)
{
	for (int32 k = RingPulses.Num() - 1; k >= 0; --k)
	{
		FRingPulse& P = RingPulses[k];
		P.T += Dt;
		// follow the piece while the board glides under it
		if (const TObjectPtr<AFNDemigodFigure>* F = OnBoard.Find(P.Ref))
			if (*F) { const FBox B = (*F)->GetComponentsBoundingBox(true); if (B.IsValid) P.At = FVector(B.GetCenter().X, B.GetCenter().Y, B.Min.Z + 1.f); }
		const float U = FMath::Clamp(P.T / 0.6f, 0.f, 1.f);   // once, ~0.6 s, out along the ground
		const float Rad = P.R * (0.9f + 1.6f * FMath::Sin(U * PI * 0.5f));
		const float Bead = P.R * 0.16f * (1.f - 0.7f * U) / 50.f;
		for (int32 i = 0; i < P.Beads.Num(); ++i)
		{
			const float A = 2.f * PI * i / P.Beads.Num();
			P.Beads[i]->SetWorldLocation(P.At + FVector(FMath::Cos(A) * Rad, FMath::Sin(A) * Rad, 0.f));
			P.Beads[i]->SetWorldScale3D(FVector(FMath::Max(0.0005f, Bead)));
		}
		if (U >= 1.f)
		{
			for (UStaticMeshComponent* C : P.Beads) { RingBeadsKeep.Remove(C); C->DestroyComponent(); }
			RingPulses.RemoveAt(k);
		}
	}
}
