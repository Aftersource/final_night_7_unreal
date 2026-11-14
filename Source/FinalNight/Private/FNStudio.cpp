// Final Night tabletop (rev 5: sound, typewriter, crisis effects)
#include "FNStudio.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "FNLandMask.h"

const FName AFNStudio::PlayerEasel(TEXT("PlayerEasel"));
const FName AFNStudio::Globe(TEXT("Globe"));
const FName AFNStudio::WillPanel(TEXT("WillPanel"));
const FName AFNStudio::RibbonA(TEXT("RibbonA"));
const FName AFNStudio::RibbonB(TEXT("RibbonB"));
const FName AFNStudio::RibbonC(TEXT("RibbonC"));

namespace FNTable
{
	static const FLinearColor Stone(0.03f, 0.028f, 0.035f);
	static const FLinearColor DarkStone(0.014f, 0.013f, 0.018f);
	static const FLinearColor Wood(0.10f, 0.045f, 0.02f);
	static const FLinearColor Ocean(0.02f, 0.10f, 0.22f);
	static const FLinearColor Land(0.12f, 0.16f, 0.07f);
	static const FLinearColor Opal(0.25f, 0.95f, 1.0f);      // globe guard
	static const FLinearColor Tablet(0.015f, 0.02f, 0.035f);  // visor / tablet screen
	static const FLinearColor Brass(0.55f, 0.38f, 0.12f);
	static const FLinearColor Parch(0.70f, 0.60f, 0.40f);
	static const FLinearColor Wax(0.80f, 0.76f, 0.66f);
	static const FColor Gold(255, 221, 54);     // #ffdd36
	static const FColor Orange(246, 134, 59);   // #f6863b
	static const FColor Cyan(34, 255, 255);     // #22FFFF
	static const FColor Pale(225, 218, 205);
	static const FColor Ink(40, 22, 10);
	// The Globe floats ~3 ft (90 cm) above the table top (z 80), 30% bigger than the old one
	static const FVector GlobeC(0.f, -10.f, 172.f);
	static constexpr float GlobeScale = 1.3f;
	static const FLinearColor ArenaRed(1.f, 0.06f, 0.03f);
}

FLinearColor AFNStudio::PlayerColor(int32 Index)
{
	static const FLinearColor C[4] = {
		FLinearColor(0.95f, 0.55f, 0.10f), FLinearColor(0.15f, 0.75f, 0.95f),
		FLinearColor(0.85f, 0.20f, 0.55f), FLinearColor(0.35f, 0.85f, 0.30f) };
	return C[((Index % 4) + 4) % 4];
}

AFNStudio::AFNStudio()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Mat.Succeeded()) BaseMat = Mat.Object;
	// hard reference so it gets cooked: the 3D-widget material has a texture slot ("SlateUI") for portraits/dioramas
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Img(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	if (Img.Succeeded()) ImageMat = Img.Object;
}

FVector AFNStudio::GetSeatLocation() const { return GetActorLocation() + FVector(0.f, 340.f, 190.f); }
FRotator AFNStudio::GetSeatRotation() const { return FRotator(-11.f, -90.f, 0.f); }

void AFNStudio::Tag(UStaticMeshComponent* C, FName Id)
{
	C->ComponentTags.Add(FName(*(FString(TEXT("FN:")) + Id.ToString())));
	if (!Interactables.Contains(Id)) { Interactables.Add(Id, C); BaseScale.Add(Id, C->GetRelativeScale3D()); }
}

UStaticMeshComponent* AFNStudio::Shape(const TCHAR* MeshPath, const FName& Name, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, USceneComponent* Parent)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	C->SetRelativeLocation(Loc);
	C->SetRelativeScale3D(Scale);
	C->SetCollisionProfileName(TEXT("BlockAll"));
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	return C;
}

UStaticMeshComponent* AFNStudio::Box(const FName& Name, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot)
{
	UStaticMeshComponent* C = Shape(TEXT("/Engine/BasicShapes/Cube.Cube"), Name, Loc, SizeCm / 100.f, Color);
	C->SetRelativeRotation(Rot);
	return C;
}

UTextRenderComponent* AFNStudio::Text(const FName& Name, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color, const FString& Str, USceneComponent* Parent)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, Name);
	T->SetupAttachment(Parent ? Parent : RootComponent.Get());
	T->SetRelativeLocation(Loc);
	T->SetRelativeRotation(Rot);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(Size);
	T->SetTextRenderColor(Color);
	T->SetText(FText::FromString(Str));
	T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	T->RegisterComponent();
	return T;
}

UPointLightComponent* AFNStudio::Light(const FName& Name, const FVector& Loc, float Intensity, const FLinearColor& Color, float Radius)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this, Name);
	L->SetupAttachment(RootComponent);
	L->SetMobility(EComponentMobility::Movable);
	L->SetRelativeLocation(Loc);
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Intensity);
	L->SetLightColor(Color);
	L->SetAttenuationRadius(Radius);
	L->RegisterComponent();
	return L;
}

void AFNStudio::Spot(const FName& Name, const FVector& Loc, const FRotator& Rot, float Intensity, const FLinearColor& Color)
{
	USpotLightComponent* L = NewObject<USpotLightComponent>(this, Name);
	L->SetupAttachment(RootComponent);
	L->SetMobility(EComponentMobility::Movable);
	L->SetRelativeLocation(Loc);
	L->SetRelativeRotation(Rot);
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Intensity);
	L->SetLightColor(Color);
	L->SetAttenuationRadius(1500.f);
	L->SetOuterConeAngle(28.f);
	L->SetInnerConeAngle(12.f);
	L->RegisterComponent();
}

void AFNStudio::BeginPlay()
{
	Super::BeginPlay();
	const FRotator FaceSeat(0.f, 90.f, 0.f); // text faces +Y, toward the seat

	// A dark stone chamber
	Box(TEXT("Floor"), FVector(0, 0, -10), FVector(1800, 1800, 20), FNTable::DarkStone);
	Box(TEXT("WallN"), FVector(0, -900, 400), FVector(1800, 40, 800), FNTable::Stone);
	Box(TEXT("WallS"), FVector(0, 900, 400), FVector(1800, 40, 800), FNTable::Stone);
	Box(TEXT("WallE"), FVector(900, 0, 400), FVector(40, 1800, 800), FNTable::Stone);
	Box(TEXT("WallW"), FVector(-900, 0, 400), FVector(40, 1800, 800), FNTable::Stone);
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector P((i % 2 ? 1 : -1) * 600.f, (i < 2 ? -1 : 1) * 600.f, 400.f);
		Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("Pillar%d"), i)), P, FVector(1.2f, 1.2f, 8.f), FNTable::Stone);
	}

	// The table
	Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("TableTop"), FVector(0, 0, 76), FVector(4.8f, 4.8f, 0.08f), FNTable::Wood);
	Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("TableRim"), FVector(0, 0, 72), FVector(4.95f, 4.95f, 0.06f), FNTable::Brass);
	Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("TableLeg"), FVector(0, 0, 36), FVector(1.0f, 1.0f, 0.75f), FNTable::Wood);

	// Centrex: the Globe of Destiny, a little tile world (think Polytopia, on a globe) on a brass stand
	// a low brass plinth and a thin shaft of light holding the Globe up in the air
	Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("CentrexStand"), FVector(0, -10, 82), FVector(0.6f, 0.6f, 0.04f), FNTable::Brass);
	Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("GlobeShaft"), FVector(0, -10, 92), FVector(0.03f, 0.03f, 0.2f), FNTable::Opal)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Light(TEXT("ShaftGlow"), FVector(0, -10, 95), 6.f, FLinearColor(0.4f, 0.9f, 1.f), 160.f);
	GlobePivot = NewObject<USceneComponent>(this, TEXT("GlobePivot"));
	GlobePivot->SetupAttachment(RootComponent);
	GlobePivot->SetRelativeLocation(FNTable::GlobeC);
	GlobePivot->SetRelativeScale3D(FVector(FNTable::GlobeScale));
	GlobePivot->RegisterComponent();
	FxCenter = FNTable::GlobeC;
	BoardRoot = NewObject<USceneComponent>(this, TEXT("BoardRoot"));
	BoardRoot->SetupAttachment(GlobePivot);
	BoardRoot->SetMobility(EComponentMobility::Movable);
	BoardRoot->RegisterComponent();
	GlobeMesh = Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("Globe"), FVector::ZeroVector, FVector((BoardR - 0.5f) / 50.f), FLinearColor(0.01f, 0.03f, 0.06f), BoardRoot);
	Tag(GlobeMesh, Globe);
	BuildBoard();
	GlobeLight = Light(TEXT("GlobeLight"), FVector(-60, 190, 270), 14.f, FLinearColor(0.75f, 0.9f, 1.f), 420.f);
	Spot(TEXT("BoardKey"), FVector(-60, 160, 330), FRotator(-52, -70, 0), 9.f, FLinearColor(1.f, 0.88f, 0.7f));

	// Globe guard: a shell of opal shards that blitzes until you join, and seals when your WILL is locked
	GuardPivot = NewObject<USceneComponent>(this, TEXT("GuardPivot"));
	GuardPivot->SetupAttachment(RootComponent);
	GuardPivot->SetRelativeLocation(FNTable::GlobeC);
	GuardPivot->SetRelativeScale3D(FVector(FNTable::GlobeScale));
	GuardPivot->RegisterComponent();
	for (int32 i = 0; i < 24; ++i)
	{
		const float Lat = FMath::DegreesToRadians(-60.f + 120.f * ((i / 8) / 2.f));
		const float Lon = FMath::DegreesToRadians(45.f * (i % 8) + 22.5f * (i / 8));
		const FVector D(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
		UStaticMeshComponent* S = Shape(TEXT("/Engine/BasicShapes/Cube.Cube"), FName(*FString::Printf(TEXT("Guard%d"), i)), D * (BoardR + 16.f), FVector(0.14f, 0.14f, 0.02f), FNTable::Opal, GuardPivot);
		S->SetRelativeRotation(D.ToOrientationRotator() + FRotator(90.f, 0.f, 0.f));
		S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S->SetVisibility(false);
		GuardShards.Add(S);
	}

	// Table easel (the tablet): welcome message and round primers
	// raised 60 cm so the floating Globe doesn't hide the writing
	Box(TEXT("TabletFrame"), FVector(0, -250, 310), FVector(316, 8, 186), FNTable::Brass);
	Box(TEXT("Tablet"), FVector(0, -246, 310), FVector(302, 4, 172), FNTable::Tablet);
	Box(TEXT("TabletLegL"), FVector(-130, -255, 110), FVector(8, 8, 220), FNTable::Brass);
	Box(TEXT("TabletLegR"), FVector(130, -255, 110), FVector(8, 8, 220), FNTable::Brass);
	EaselHeader = Text(TEXT("EaselHeader"), FVector(0, -243, 382), FaceSeat, 12.f, FNTable::Gold, FString());
	EaselBody = Text(TEXT("EaselBody"), FVector(0, -243, 330), FaceSeat, 7.5f, FNTable::Pale, FString());
	ShowEaselText(FString(), TEXT("Connect to Network and click the Globe to Begin"));
	EaselBody->SetTextRenderColor(FNTable::Cyan);

	// The player easel: click it to begin
	PlayerEaselPivot = NewObject<USceneComponent>(this, TEXT("PlayerEaselPivot"));
	PlayerEaselPivot->SetupAttachment(RootComponent);
	PlayerEaselPivot->SetRelativeLocation(FVector(-118, 160, 70));
	PlayerEaselPivot->SetRelativeRotation(FRotator(0, 25, 0));
	PlayerEaselPivot->RegisterComponent();
	UStaticMeshComponent* PE = Shape(TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("PlayerEasel"), FVector(0, 0, 55), FVector(0.9f, 0.05f, 0.75f), FNTable::Parch, PlayerEaselPivot);
	PE->SetRelativeRotation(FRotator(0, 0, -12));
	Tag(PE, PlayerEasel);
	Text(TEXT("PlayerEaselText"), FVector(0, 4, 58), FRotator(0, 90, -12), 5.5f, FNTable::Ink, TEXT("FINAL NIGHT\n\nclick to begin"), PlayerEaselPivot);
	PlayerEaselPivot->SetVisibility(false, true);   // it read as a black slab; a click anywhere on the table starts the game

	// WILL panel: a brass plaque on the table in front of you
	// front-right of the table, inside your view (it used to sit just below the bottom of the screen)
	UStaticMeshComponent* WP = Box(TEXT("WillPanel"), FVector(100, 150, 90), FVector(64, 32, 8), FNTable::Brass, FRotator(0, 30, -14));
	Tag(WP, WillPanel);
	WillLabel = Text(TEXT("WillLabel"), FVector(98, 153, 102), FRotator(25, 120, 0), 7.f, FNTable::Gold, TEXT("WILL"));

	// Episode ribbons circle the globe when the round's outcomes arrive
	RibbonPivot = NewObject<USceneComponent>(this, TEXT("RibbonPivot"));
	RibbonPivot->SetupAttachment(RootComponent);
	RibbonPivot->SetRelativeLocation(FVector::ZeroVector);
	RibbonPivot->RegisterComponent();
	// Three fate cards stand on the table in front of the Globe, below your line of sight to it
	const FVector CardPos[3] = { FVector(-118.f, 30.f, 112.f), FVector(0.f, 95.f, 86.f), FVector(118.f, 30.f, 112.f) };
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector ToSeat = (FVector(0.f, 340.f, 190.f) - CardPos[i]).GetSafeNormal2D();
		const float YawToSeat = ToSeat.Rotation().Yaw;
		UStaticMeshComponent* R = Shape(TEXT("/Engine/BasicShapes/Cube.Cube"), FName(*FString::Printf(TEXT("Ribbon%d"), i)), CardPos[i], FVector(0.72f, 0.03f, 0.42f), FLinearColor(0.40f, 0.30f, 0.12f), RibbonPivot);
		R->SetRelativeRotation(FRotator(0, YawToSeat - 90.f, 0));
		Tag(R, i == 0 ? RibbonA : (i == 1 ? RibbonB : RibbonC));
		R->SetVisibility(false);
		R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ribbons.Add(R);
		UTextRenderComponent* T = Text(FName(*FString::Printf(TEXT("RibbonText%d"), i)), CardPos[i] + ToSeat * 2.5f, FRotator(0, YawToSeat, 0), 4.2f, FNTable::Pale, FString(), RibbonPivot);
		T->SetVisibility(false);
		RibbonTexts.Add(T);
	}

	// Candles on the rim, cold light from above
	for (int32 i = 0; i < 5; ++i)
	{
		const float A = FMath::DegreesToRadians(200.f + 35.f * i);
		const FVector P(215.f * FMath::Cos(A), 215.f * FMath::Sin(A) - 10.f, 0.f);
		Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("Candle%d"), i)), P + FVector(0, 0, 92), FVector(0.06f, 0.06f, 0.3f), FNTable::Wax);
		Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FName(*FString::Printf(TEXT("Flame%d"), i)), P + FVector(0, 0, 110), FVector(0.03f, 0.03f, 0.05f), FLinearColor(1.f, 0.6f, 0.15f))->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Candles.Add(Light(FName(*FString::Printf(TEXT("CandleLight%d"), i)), P + FVector(0, 0, 118), 3.f, FLinearColor(1.f, 0.62f, 0.3f), 420.f));
	}
	Light(TEXT("Moon"), FVector(-200, 200, 650), 4.f, FLinearColor(0.45f, 0.5f, 0.9f), 1600.f);
	Spot(TEXT("TabletLight"), FVector(0, 100, 420), FRotator(-35, -90, 0), 10.f, FLinearColor(0.8f, 0.9f, 1.f));
	Spot(TEXT("BeamOnGlobe"), FVector(0, -10, 700), FRotator(-90, 0, 0), 9.f, FLinearColor(0.6f, 0.85f, 1.f));

	// Crisis effects: a pool of small pieces (meteors, locusts, dust) and a flash light for the room
	for (int32 i = 0; i < 40; ++i)
	{
		UStaticMeshComponent* B = Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FName(*FString::Printf(TEXT("Fx%d"), i)), FVector(0, -10, 138), FVector(0.05f), FLinearColor(1.f, 0.4f, 0.1f));
		B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		B->SetCastShadow(false);
		B->SetVisibility(false);
		FxBits.Add(B);
		FxMats.Add(Cast<UMaterialInstanceDynamic>(B->GetMaterial(0)));
	}
	FxPos.SetNumZeroed(40); FxVel.SetNumZeroed(40); FxParam.SetNumZeroed(40);
	FlashLight = Light(TEXT("CrisisFlash"), FVector(0, 80, 280), 0.f, FLinearColor(1.f, 0.4f, 0.1f), 1600.f);

	// The Dungeon Master's d20: a red gem that tumbles onto the table each day
	DicePivot = NewObject<USceneComponent>(this, TEXT("DicePivot"));
	DicePivot->SetupAttachment(RootComponent);
	DicePivot->SetMobility(EComponentMobility::Movable);
	DicePivot->SetRelativeLocation(FVector(78.f, 118.f, 88.f));
	DicePivot->RegisterComponent();
	{
		UStaticMeshComponent* Top = Shape(TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("DieTop"), FVector(0, 0, 3.5f), FVector(0.12f, 0.12f, 0.07f), FLinearColor(0.55f, 0.03f, 0.05f), DicePivot);
		UStaticMeshComponent* Bot = Shape(TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("DieBottom"), FVector(0, 0, -3.5f), FVector(0.12f, 0.12f, 0.07f), FLinearColor(0.45f, 0.02f, 0.04f), DicePivot);
		Bot->SetRelativeRotation(FRotator(180.f, 0.f, 0.f));
		for (UStaticMeshComponent* Pc : { Top, Bot }) Pc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	DicePivot->SetVisibility(false, true);
	DiceNumber = Text(TEXT("DiceNumber"), FVector(78.f, 118.f, 128.f), FRotator::ZeroRotator, 22.f, FNTable::Gold, FString());
	DiceLine = Text(TEXT("DiceLine"), FVector(78.f, 118.f, 110.f), FRotator::ZeroRotator, 5.f, FNTable::Pale, FString());

	// Sound: one procedural voice that we keep fed from Tick
	Rng.Initialize(0xF17A1);
	BuildSounds();
	AudioWave = NewObject<USoundWaveProcedural>(this, TEXT("FNAudioWave"));
	AudioWave->SetSampleRate(44100);
	AudioWave->NumChannels = 1;
	AudioWave->Duration = INDEFINITELY_LOOPING_DURATION;
	AudioWave->SoundGroup = SOUNDGROUP_Default;
	AudioWave->bLooping = false;
	AudioComp = NewObject<UAudioComponent>(this, TEXT("FNAudio"));
	AudioComp->SetupAttachment(RootComponent);
	AudioComp->bAutoActivate = false;
	AudioComp->bAllowSpatialization = false;
	AudioComp->SetSound(AudioWave);
	AudioComp->RegisterComponent();
	PumpAudio();
	AudioComp->Play();

	// the circle of action starts on the Alps until you pick a spot (or the match tells us where you are)
	PickSpot(PickLat, PickLon, false);
}

void AFNStudio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;

	FatesAmt = FMath::FInterpTo(FatesAmt, FatesTarget, DeltaSeconds, 1.5f);
	// The board turns your piece toward you; while the fates decide it spins
	if (BoardRoot && GlobePivot)
	{
		const FVector GCw = GlobePivot->GetComponentLocation();
		FVector Cam = GCw + FVector(0.f, 350.f, 65.f);
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
		const FVector F = (Cam - GCw).GetSafeNormal();
		if (Time < ManualUntil) {}   // you spun it by hand: leave it where you put it for a while
		else if (bHasFocus && FatesAmt < 0.5f)
		{
			const FVector A = FocusDir;
			FVector B = FVector(0, 0, 1) - A * A.Z; if (B.SizeSquared() < 1e-4) B = FVector(1, 0, 0); B.Normalize();
			const FVector C = FVector::CrossProduct(A, B);
			FVector B2 = FVector(0, 0, 1) - F * F.Z; B2.Normalize();
			const FVector C2 = FVector::CrossProduct(F, B2);
			const FMatrix Lm(A, B, C, FVector::ZeroVector), Wm(F, B2, C2, FVector::ZeroVector);
			const FQuat Target(Lm.GetTransposed() * Wm);
			BoardRoot->SetWorldRotation(FQuat::Slerp(BoardRoot->GetComponentQuat(), Target, FMath::Min(1.f, DeltaSeconds * 1.8f)));
		}
		else BoardRoot->AddWorldRotation(FRotator(0.f, (6.f + 80.f * FatesAmt) * DeltaSeconds, 0.f));
	}
	UpdateLabels();
	UpdateTable(DeltaSeconds);
	UpdateSeats(DeltaSeconds);
	// lightning strike: a flickering white bolt standing out of the Globe plus a burst of light
	if (BoltT >= 0.f && BoltMesh)
	{
		BoltT += DeltaSeconds;
		const float A = FMath::Max(0.f, 1.f - BoltT / 0.5f);
		const bool bLit = A > 0.f && FMath::Sin(Time * 95.f) > -0.4f;
		const FVector Jit = FVector(FMath::PerlinNoise1D(Time * 40.f), FMath::PerlinNoise1D(Time * 37.f + 3.f), 0.f) * 0.15f;
		const FVector D = (BoltDir + Jit - BoltDir * FVector::DotProduct(Jit, BoltDir)).GetSafeNormal();
		BoltMesh->SetRelativeLocation(D * (BoardR + 8.f));
		BoltMesh->SetRelativeRotation(FRotationMatrix::MakeFromZ(D).Rotator());
		BoltMesh->SetVisibility(bLit);
		BoltLight->SetRelativeLocation(BoltDir * (BoardR + 6.f));
		BoltLight->SetIntensity(bLit ? 90.f * A : 0.f);
		if (A <= 0.f) BoltT = -1.f;
	}
	if (GlobeLight) GlobeLight->SetIntensity(12.f + 4.f * FMath::Sin(Time * 1.7f) + FatesAmt * (22.f + 10.f * FMath::Sin(Time * 11.f)));
	if (GuardPivot) GuardPivot->AddRelativeRotation(FRotator(0.f, -14.f * DeltaSeconds, 0.f));
	for (int32 i = 0; i < Candles.Num(); ++i)
		Candles[i]->SetIntensity(3.f * (0.82f + (0.18f + 0.5f * FatesAmt) * FMath::Sin(Time * (9.f + i + 14.f * FatesAmt) + i * 1.3f) * FMath::Sin(Time * 3.1f + i)));

	// The tablet writes itself, one letter at a time, with a quill scratch now and then
	if (EaselBody && TypeShown < TypeFull.Len())
	{
		TypeShown = FMath::Min<float>(TypeFull.Len(), TypeShown + DeltaSeconds * FMath::Max(70.f, TypeFull.Len() / 5.f));
		const int32 N = (int32)TypeShown;
		EaselBody->SetText(FText::FromString(TypeFull.Left(N)));
		if (N - TypeLastQuill >= 5) { TypeLastQuill = N; PlaySfx(EFNSfx::Quill, 0.6f + 0.4f * Rng.FRand()); }
	}

	UpdateFx(DeltaSeconds);
	UpdateDice(DeltaSeconds);

	// The Final Cut: the Globe comes to you and replays the night, one day at a time
	if (CutT >= 0.f)
	{
		CutT += DeltaSeconds;
		CutZoom = FMath::FInterpTo(CutZoom, 1.f, DeltaSeconds, 1.5f);
		const int32 Idx = FMath::FloorToInt(CutT / 5.5f);
		if (Idx != CutIdx)
		{
			CutIdx = Idx;
			if (CutLines.IsValidIndex(Idx))
			{
				if (CutSpots.IsValidIndex(Idx)) { FocusDir = BoardDir(CutSpots[Idx].X, CutSpots[Idx].Y).GetSafeNormal(); bHasFocus = true; }
				ShowEaselText(FString::Printf(TEXT("THE FINAL CUT  -  DAY %d"), Idx + 1), CutLines[Idx]);
				PlaySfx(EFNSfx::Chime, 0.55f);
			}
			else if (Idx == CutLines.Num())
			{
				ShowEaselText(TEXT("THE FINAL CUT"), CutClosing);
				PlaySfx(EFNSfx::Boom, 0.8f); PlaySfx(EFNSfx::Chime, 0.9f, 0.5f);
			}
		}
		if (CutT > CutLines.Num() * 5.5f + 30.f) CutT = -1.f;
	}
	else CutZoom = FMath::FInterpTo(CutZoom, 0.f, DeltaSeconds, 1.f);
	if (GlobePivot && FxKind != 3)
	{
		// a slow bob so it reads as floating
		GlobePivot->SetRelativeLocation(FNTable::GlobeC + FVector(0.f, 0.f, 6.f * CutZoom + 1.5f * FMath::Sin(Time * 1.1f)));
		GlobePivot->SetRelativeScale3D(FVector(FNTable::GlobeScale * (1.f + 0.06f * CutZoom)));
		if (GlobeLight && CutZoom > 0.01f) GlobeLight->SetIntensity(GlobeLight->Intensity + 6.f * CutZoom);
	}
	PumpAudio();

	// Globe guard blitz: every 3.72 s it flashes in and shrinks back (BlitzGlobeGuard in the Unity client)
	float GuardScale = 0.f;
	if (bGuardLocked) GuardScale = 1.f;
	else if (bBlitz)
	{
		if (Time >= NextBlitz) { BlitzT = 0.f; NextBlitz = Time + 3.72f + 0.8f; }
		if (BlitzT >= 0.f)
		{
			BlitzT += DeltaSeconds;
			const float T = BlitzT / 0.8f;
			GuardScale = T < 1.f ? FMath::Sin(T * PI) : 0.f;
			if (T >= 1.f) BlitzT = -1.f;
		}
	}
	for (UStaticMeshComponent* S : GuardShards)
	{
		S->SetVisibility(GuardScale > 0.02f);
		S->SetRelativeScale3D(FVector(0.09f * GuardScale, 0.09f * GuardScale, 0.015f));   // smaller since the Globe grew 1.3x
	}

	// Player easel lowers out of view once the experience starts
	if (EaselDownT >= 0.f && PlayerEaselPivot)
	{
		EaselDownT = FMath::Min(1.f, EaselDownT + DeltaSeconds);
		PlayerEaselPivot->SetRelativeLocation(FVector(-118, 160, FMath::Lerp(70.f, -70.f, EaselDownT * EaselDownT)));
	}

	// Hover feedback + the WILL panel breathes when it wants a click
	for (const TPair<FName, TObjectPtr<UStaticMeshComponent>>& P : Interactables)
	{
		const float Bump = (P.Key == Hovered && P.Key != Globe) ? 1.07f : 1.f;
		const float Breathe = (P.Key == WillPanel && bWillActive) ? (1.f + 0.04f * FMath::Sin(Time * 4.f)) : 1.f;
		if (const FVector* S = BaseScale.Find(P.Key)) P.Value->SetRelativeScale3D(*S * Bump * Breathe);
	}
	if (GlobeLight && Hovered == Globe) GlobeLight->SetIntensity(GlobeLight->Intensity + 5.f);   // a gentle lift, not a white glare
}

void AFNStudio::SetHovered(FName Id) { Hovered = Id; }
void AFNStudio::PlayerEaselDown() { if (EaselDownT < 0.f) EaselDownT = 0.f; if (TObjectPtr<UStaticMeshComponent>* E = Interactables.Find(PlayerEasel)) (*E)->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
void AFNStudio::SetGuardBlitz(bool b) { bBlitz = b; }
void AFNStudio::SetGuardLocked(bool b) { bGuardLocked = b; }

void AFNStudio::SetWillPanelState(const FString& Label, bool bActive)
{
	bWillActive = bActive;
	if (WillLabel)
	{
		WillLabel->SetText(FText::FromString(Label));
		WillLabel->SetTextRenderColor(bActive ? FNTable::Gold : FColor(110, 90, 50));
	}
}

FString AFNStudio::Wrap(const FString& S, int32 Width, int32 MaxLines)
{
	// Keep the text's own line breaks; wrap each paragraph to Width characters.
	TArray<FString> Paras;
	S.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Paras, TEXT("\n"), false);
	TArray<FString> Lines;
	for (const FString& Para : Paras)
	{
		TArray<FString> Words;
		Para.ParseIntoArrayWS(Words);
		if (Words.Num() == 0) { Lines.Add(FString()); continue; }
		FString Cur;
		for (const FString& W : Words)
		{
			if (Cur.Len() + W.Len() + 1 > Width && !Cur.IsEmpty()) { Lines.Add(Cur); Cur.Reset(); }
			Cur += (Cur.IsEmpty() ? TEXT("") : TEXT(" ")) + W;
		}
		if (!Cur.IsEmpty()) Lines.Add(Cur);
	}
	if (Lines.Num() > MaxLines) { Lines.SetNum(MaxLines); Lines.Last() += TEXT(" ..."); }
	return FString::Join(Lines, TEXT("\n"));
}

void AFNStudio::ShowEaselText(const FString& Header, const FString& Body)
{
	if (EaselHeader) EaselHeader->SetText(FText::FromString(Header));
	if (EaselBody)
	{
		EaselBody->SetTextRenderColor(FNTable::Pale);
		TypeFull = Wrap(Body, 92, 20);
		TypeShown = 0.f;
		TypeLastQuill = 0;
		EaselBody->SetText(FText::GetEmpty());
	}
}

FVector AFNStudio::LonLatToGlobe(double Lon, double Lat, float R)
{
	// Unreal is left-handed: Y is flipped so east is on the right when you look at the globe
	const double La = FMath::DegreesToRadians(Lat), Lo = FMath::DegreesToRadians(Lon);
	return FVector(R * FMath::Cos(La) * FMath::Cos(Lo), -R * FMath::Cos(La) * FMath::Sin(Lo), R * FMath::Sin(La));
}

FVector AFNStudio::BoardToActor(const FVector& LocalDir, float Radius) const
{
	if (!BoardRoot) return FNTable::GlobeC;
	return GetActorTransform().InverseTransformPosition(BoardRoot->GetComponentTransform().TransformPosition(LocalDir * Radius));
}

namespace FNBoard
{
	enum : uint8 { BioOcean, BioShallow, BioGrass, BioForest, BioDesert, BioSnow, BioMountain, NumBiomes };
	static const FLinearColor Colors[NumBiomes] = {
		FLinearColor(0.012f, 0.040f, 0.090f), FLinearColor(0.030f, 0.110f, 0.160f), FLinearColor(0.150f, 0.230f, 0.070f),
		FLinearColor(0.060f, 0.150f, 0.050f), FLinearColor(0.450f, 0.330f, 0.150f), FLinearColor(0.780f, 0.800f, 0.840f),
		FLinearColor(0.240f, 0.220f, 0.210f) };
	static const float Heights[NumBiomes] = { 1.0f, 1.3f, 2.3f, 2.5f, 2.1f, 2.7f, 3.4f };
	static bool LandAt(double Lon, double Lat)
	{
		int32 N = 0;
		const double Dx[5] = { 0, 1.3, -1.3, 0, 0 }, Dy[5] = { 0, 0, 0, 1.3, -1.3 };
		for (int32 i = 0; i < 5; ++i) N += FNLand::IsLand(Lon + Dx[i], FMath::Clamp(Lat + Dy[i], -89.9, 89.9)) ? 1 : 0;
		return N >= 2;
	}
}

void AFNStudio::BuildBoard()
{
	using namespace FNBoard;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	auto MakeSet = [this](const FName& Name, UStaticMesh* Mesh, const FLinearColor& Color) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
		C->SetupAttachment(BoardRoot);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
			M->SetVectorParameterValue(TEXT("Color"), Color);
			C->SetMaterial(0, M);
		}
		C->RegisterComponent();
		return C;
	};
	// One set per biome x checker shade x inside/outside the safe zone (outside = blighted)
	for (int32 B = 0; B < NumBiomes; ++B)
		for (int32 Sh = 0; Sh < 2; ++Sh)
			for (int32 Out = 0; Out < 2; ++Out)
			{
				FLinearColor C = Colors[B] * (Sh ? 0.80f : 1.f);
				if (Out) C = FLinearColor(C.R * 0.38f + 0.012f, C.G * 0.30f, C.B * 0.34f);
				TileSets.Add(MakeSet(FName(*FString::Printf(TEXT("Tiles_%d_%d_%d"), B, Sh, Out)), Cube, C));
			}
	Trees = MakeSet(TEXT("Trees"), Cone, FLinearColor(0.04f, 0.11f, 0.035f));
	Peaks = MakeSet(TEXT("Peaks"), Cone, FLinearColor(0.32f, 0.30f, 0.29f));

	const float Step = 4.f;
	for (float Lat = -88.f; Lat <= 88.f; Lat += Step)
	{
		const double LaR = FMath::DegreesToRadians(Lat);
		const int32 N = FMath::Max(4, FMath::RoundToInt(360.f / Step * FMath::Cos(LaR)));
		for (int32 j = 0; j < N; ++j)
		{
			const double Lon = -180.0 + (j + 0.5) * 360.0 / N;
			const double LoR = FMath::DegreesToRadians(Lon);
			FTile T;
			T.Dir = BoardDir(Lon, Lat).GetSafeNormal();
			T.East = FVector(-FMath::Sin(LoR), -FMath::Cos(LoR), 0.f);
			T.W = (float)(FMath::DegreesToRadians(360.0 / N) * FMath::Cos(LaR) * BoardR * 0.9);
			T.D = FMath::DegreesToRadians(Step) * BoardR * 0.9f;
			T.Shade = (uint8)((j + FMath::RoundToInt((Lat + 88.f) / Step)) % 2);
			const bool bLand = LandAt(Lon, Lat);
			const float N1 = FMath::PerlinNoise3D(T.Dir * 4.f), N2 = FMath::PerlinNoise3D(T.Dir * 3.f + FVector(7.f)), N3 = FMath::PerlinNoise3D(T.Dir * 5.f + FVector(13.f));
			if (!bLand)
			{
				const double Dl = 4.5 / FMath::Max(0.2, FMath::Cos(LaR));
				const bool bCoast = LandAt(Lon + Dl, Lat) || LandAt(Lon - Dl, Lat) || LandAt(Lon, FMath::Min(89.0, Lat + 4.5)) || LandAt(Lon, FMath::Max(-89.0, Lat - 4.5));
				T.Biome = FMath::Abs(Lat) > 70.f ? BioSnow : (bCoast ? BioShallow : BioOcean);
			}
			else if (FMath::Abs(Lat) > 62.f) T.Biome = BioSnow;
			else if (N1 > 0.28f) T.Biome = BioMountain;
			else if (FMath::Abs(Lat) > 12.f && FMath::Abs(Lat) < 38.f && N2 > -0.08f) T.Biome = BioDesert;
			else if (N3 > 0.02f) T.Biome = BioForest;
			else T.Biome = BioGrass;
			T.H = Heights[T.Biome] + (bLand ? 0.6f * N2 : 0.f);
			Tiles.Add(T);

			const FRotator Rot = FRotationMatrix::MakeFromZX(T.Dir, T.East).Rotator();
			if (T.Biome == BioForest)
				Trees->AddInstance(FTransform(Rot, T.Dir * (BoardR - 1.f + T.H + 1.3f), FVector(0.013f, 0.013f, 0.028f)));
			if (T.Biome == BioMountain)
				Peaks->AddInstance(FTransform(Rot, T.Dir * (BoardR - 1.f + T.H + 1.6f), FVector(0.03f, 0.03f, 0.034f)));
		}
	}
	PaintTiles();
}

void AFNStudio::PaintTiles()
{
	for (UInstancedStaticMeshComponent* C : TileSets) C->ClearInstances();
	const double CosZone = FMath::Cos(ZoneAng);
	for (const FTile& T : Tiles)
	{
		const bool bOut = !ZoneDir.IsZero() && FVector::DotProduct(T.Dir, ZoneDir) < CosZone;
		const int32 Set = (T.Biome * 2 + T.Shade) * 2 + (bOut ? 1 : 0);
		const FRotator Rot = FRotationMatrix::MakeFromZX(T.Dir, T.East).Rotator();
		TileSets[Set]->AddInstance(FTransform(Rot, T.Dir * (BoardR - 1.f + T.H * 0.5f), FVector(T.W / 100.f, T.D / 100.f, T.H / 100.f)));
	}
}

void AFNStudio::ShowSelfOnGlobe(double Lon, double Lat, bool bAlive)
{
	FocusDir = BoardDir(Lon, Lat).GetSafeNormal();
	bHasFocus = true;
	// until you pick a spot yourself, the table shows the land around your demigod
	// rebuild only when the demigod has really moved (~1/20 of the circle), not on every small drift
	if (!bUserPicked && FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(FocusDir, PickDir), -1.f, 1.f))) * 12742.0 * PI / 180.0 * 1000.0 > PickRadiusM * 0.05) PickSpot(Lat, Lon, false);
}

void AFNStudio::ShowZoneOnGlobe(double Lon, double Lat, double RadiusKm)
{
	if (!BoardRoot) return;
	if (ZoneMarkers.Num() == 0)
	{
		for (int32 i = 0; i < 64; ++i)
		{
			UStaticMeshComponent* M = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("Zone%d"), i)), FVector::ZeroVector, FVector(0.014f, 0.014f, 0.08f), FNTable::ArenaRed, BoardRoot);
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->SetCastShadow(false);
			ZoneMarkers.Add(M);
		}
		ZoneLabel = Text(TEXT("ZoneLabel"), FVector::ZeroVector, FRotator::ZeroRotator, 3.6f, FColor(255, 60, 40), FString());
	}
	// A red fence at angular radius (km / earth radius) around the arena centre; past 90 deg it wraps the far side
	ZoneAng = FMath::Min(RadiusKm / 6371.0, PI);
	const FVector C = BoardDir(Lon, Lat).GetSafeNormal();
	ZoneDir = RadiusKm < 20000.0 ? C : FVector::ZeroVector;
	const FVector Up = FMath::Abs(C.Z) > 0.95f ? FVector(1, 0, 0) : FVector(0, 0, 1);
	const FVector E1 = FVector::CrossProduct(Up, C).GetSafeNormal();
	const FVector E2 = FVector::CrossProduct(C, E1);
	for (int32 i = 0; i < ZoneMarkers.Num(); ++i)
	{
		const double T = 2.0 * PI * i / ZoneMarkers.Num();
		const FVector D = (C * FMath::Cos(ZoneAng) + (E1 * FMath::Cos(T) + E2 * FMath::Sin(T)) * FMath::Sin(ZoneAng)).GetSafeNormal();
		ZoneMarkers[i]->SetRelativeLocation(D * (BoardR + 4.5f));
		ZoneMarkers[i]->SetRelativeRotation(FRotationMatrix::MakeFromZ(D).Rotator());
		ZoneMarkers[i]->SetVisibility(RadiusKm < 20000.0);
	}
	if (ZoneLabel) ZoneLabel->SetText(FText::FromString(RadiusKm < 20000.0 ? FString::Printf(TEXT("ARENA  %d km"), (int32)RadiusKm) : FString()));
	PaintTiles();
}

void AFNStudio::FlashAt(double Lon, double Lat)
{
	if (!BoardRoot) return;
	if (!BoltMesh)
	{
		BoltMesh = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("Bolt"), FVector::ZeroVector, FVector(0.02f, 0.02f, 0.16f), FLinearColor(0.9f, 0.95f, 1.f), BoardRoot);
		BoltMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoltMesh->SetCastShadow(false);
		BoltLight = Light(TEXT("BoltLight"), FVector::ZeroVector, 0.f, FLinearColor(0.75f, 0.85f, 1.f), 260.f);
		BoltLight->AttachToComponent(BoardRoot, FAttachmentTransformRules::KeepRelativeTransform);
	}
	// shortcut: one bolt reused, so a second strike within 0.5 s moves it; add a pool if casts overlap a lot
	BoltDir = BoardDir(Lon, Lat).GetSafeNormal();
	BoltT = 0.f;
	PlaySfx(EFNSfx::Impact, 0.5f);
}

void AFNStudio::ShowDropSpots(const TArray<FFNPiece>& Spots)
{
	DropPieces = Spots;
	for (FFNPiece& P : DropPieces) P.Kind = 4;
	ShowWorld(WorldPieces);
}

void AFNStudio::ShowWorld(const TArray<FFNPiece>& Pieces)
{
	WorldPieces = Pieces;
	TArray<FFNPiece> All = DropPieces;
	All.Append(WorldPieces);
	All.SetNum(FMath::Min(All.Num(), 40));
	// Grow the pool: a pawn is a base, a body and a head (a toppled pawn = fallen)
	while (PieceRoots.Num() < All.Num())
	{
		const int32 i = PieceRoots.Num();
		USceneComponent* R = NewObject<USceneComponent>(this, FName(*FString::Printf(TEXT("Piece%d"), i)));
		R->SetupAttachment(BoardRoot);
		R->SetMobility(EComponentMobility::Movable);
		R->RegisterComponent();
		PieceRoots.Add(R);
		UStaticMeshComponent* P0 = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("PieceBase%d"), i)), FVector(0, 0, 0.4f), FVector(0.03f, 0.03f, 0.008f), FNTable::Brass, R);
		UStaticMeshComponent* P1 = Shape(TEXT("/Engine/BasicShapes/Cone.Cone"), FName(*FString::Printf(TEXT("PieceBody%d"), i)), FVector(0, 0, 3.0f), FVector(0.022f, 0.022f, 0.045f), FNTable::Brass, R);
		UStaticMeshComponent* P2 = Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FName(*FString::Printf(TEXT("PieceHead%d"), i)), FVector(0, 0, 5.6f), FVector(0.02f), FNTable::Brass, R);
		for (UStaticMeshComponent* P : { P0, P1, P2 }) { P->SetCollisionEnabled(ECollisionEnabled::NoCollision); PieceParts.Add(P); }
		// white halo disc under the piece: only shown for your own demigod
		UStaticMeshComponent* H = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("PieceHalo%d"), i)), FVector(0, 0, 0.1f), FVector(0.075f, 0.075f, 0.004f), FLinearColor(1.f, 1.f, 1.f), R);
		H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->SetCastShadow(false);
		Halos.Add(H);
		PieceLabels.Add(Text(FName(*FString::Printf(TEXT("PieceLabel%d"), i)), FVector::ZeroVector, FRotator::ZeroRotator, 3.f, FNTable::Pale, FString()));
	}
	PieceDirs.SetNum(PieceRoots.Num());
	PieceKinds.SetNum(PieceRoots.Num());
	for (int32 i = 0; i < PieceRoots.Num(); ++i)
	{
		const bool bOn = All.IsValidIndex(i);
		PieceRoots[i]->SetVisibility(bOn, true);
		PieceLabels[i]->SetVisibility(bOn);
		PieceKinds[i] = bOn ? All[i].Kind : -1;
		if (bOn) PlacePiece(i, All[i]);
		Halos[i]->SetVisibility(bOn && All[i].Kind == 0);
	}
	if (All.Num() > 0 && PieceRoots.Num() > 0)
		UE_LOG(LogTemp, Display, TEXT("FNBoard: %d pieces; first '%s' kind %d at %s (visible %d)"), All.Num(), *All[0].Name, All[0].Kind, *PieceRoots[0]->GetComponentLocation().ToString(), PieceRoots[0]->IsVisible() ? 1 : 0);
	PlaceTableHeroes();
}

void AFNStudio::PlacePiece(int32 i, const FFNPiece& P)
{
	const FVector D = BoardDir(P.Lon, P.Lat).GetSafeNormal();
	PieceDirs[i] = D;
	const double LoR = FMath::DegreesToRadians(P.Lon);
	const FVector East(-FMath::Sin(LoR), -FMath::Cos(LoR), 0.f);
	FRotator Rot = FRotationMatrix::MakeFromZX(D, East).Rotator();
	float Scale = 2.2f; FLinearColor Col = PlayerColor(P.Color); FColor LabelCol = PlayerColor(P.Color).ToFColor(true);
	FString Label = P.Name; float LabelSize = 4.f;
	switch (P.Kind)
	{
	case 0: Scale = 3.0f; Col = FLinearColor(1.f, 0.72f, 0.12f); LabelCol = FNTable::Gold; Label = P.Name.IsEmpty() ? FString(TEXT("YOU")) : P.Name; LabelSize = 6.f; break;
	case 2: Scale = 1.6f; Col = FLinearColor(0.50f, 0.47f, 0.42f); LabelCol = FNTable::Pale; LabelSize = 3.f; break;   // AI demigod: named, smaller
	case 3: Scale = 1.8f; Col = FLinearColor(0.10f, 0.08f, 0.08f); LabelCol = FColor(150, 40, 30); Label = TEXT("fallen: ") + P.Name;
		Rot = (FRotationMatrix::MakeFromZX(D, East).ToQuat() * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(80.f))).Rotator(); break;
	case 4: Scale = 1.8f; Col = FLinearColor(0.9f, 0.75f, 0.25f); LabelCol = FColor(255, 221, 54); LabelSize = 2.4f; break;
	default: break;
	}
	PieceRoots[i]->SetRelativeLocation(D * (BoardR + 2.4f));
	PieceRoots[i]->SetRelativeRotation(Rot);
	PieceRoots[i]->SetRelativeScale3D(FVector(Scale));
	for (int32 k = 0; k < 3; ++k)
	{
		UStaticMeshComponent* Part = PieceParts[i * 3 + k];
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), k == 0 && P.Kind != 3 ? Col * 0.6f : Col);
		// drop spots are banners: a thin pole with a flag
		if (k == 1) Part->SetRelativeScale3D(P.Kind == 4 ? FVector(0.006f, 0.006f, 0.09f) : FVector(0.022f, 0.022f, 0.045f));
		if (k == 1) Part->SetRelativeLocation(FVector(0, 0, P.Kind == 4 ? 5.2f : 3.0f));
		if (k == 2) Part->SetRelativeScale3D(P.Kind == 4 ? FVector(0.035f, 0.004f, 0.022f) : FVector(0.02f));
		if (k == 2) Part->SetRelativeLocation(P.Kind == 4 ? FVector(1.8f, 0, 8.6f) : FVector(0, 0, 5.6f));
		if (k == 2 && P.Kind == 4) Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		if (k == 2 && P.Kind != 4) Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	}
	PieceLabels[i]->SetText(FText::FromString(Label));
	PieceLabels[i]->SetTextRenderColor(LabelCol);
	PieceLabels[i]->SetWorldSize(LabelSize);
}

void AFNStudio::ShowBuilt(const TArray<FString>& Things)
{
	// Little huts, towers and crates grow around your piece, one per thing you made or kept
	while (HutRoots.Num() < FMath::Min(Things.Num(), 6))
	{
		const int32 i = HutRoots.Num();
		USceneComponent* R = NewObject<USceneComponent>(this, FName(*FString::Printf(TEXT("Hut%d"), i)));
		R->SetupAttachment(BoardRoot);
		R->SetMobility(EComponentMobility::Movable);
		R->RegisterComponent();
		const bool bTower = i % 3 == 1;
		Shape(TEXT("/Engine/BasicShapes/Cube.Cube"), FName(*FString::Printf(TEXT("HutWall%d"), i)), FVector(0, 0, bTower ? 2.2f : 1.1f), bTower ? FVector(0.016f, 0.016f, 0.044f) : FVector(0.026f, 0.022f, 0.022f), FNTable::Wood, R)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shape(TEXT("/Engine/BasicShapes/Cone.Cone"), FName(*FString::Printf(TEXT("HutRoof%d"), i)), FVector(0, 0, bTower ? 5.4f : 3.1f), FVector(0.024f, 0.024f, 0.02f), FLinearColor(0.35f, 0.08f, 0.05f), R)->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HutRoots.Add(R);
		HutLabels.Add(Text(FName(*FString::Printf(TEXT("HutLabel%d"), i)), FVector::ZeroVector, FRotator::ZeroRotator, 1.9f, FColor(200, 170, 120), FString()));
	}
	if (!bHasFocus) return;
	const FVector Up = FMath::Abs(FocusDir.Z) > 0.95f ? FVector(1, 0, 0) : FVector(0, 0, 1);
	const FVector E1 = FVector::CrossProduct(Up, FocusDir).GetSafeNormal(), E2 = FVector::CrossProduct(FocusDir, E1);
	for (int32 i = 0; i < HutRoots.Num(); ++i)
	{
		const bool bOn = Things.IsValidIndex(i);
		HutRoots[i]->SetVisibility(bOn, true);
		HutLabels[i]->SetVisibility(bOn);
		if (!bOn) continue;
		const float A = 2.f * PI * i / 6.f + 0.5f;
		const FVector D = (FocusDir + (E1 * FMath::Cos(A) + E2 * FMath::Sin(A)) * 0.17f).GetSafeNormal();
		HutRoots[i]->SetRelativeLocation(D * (BoardR + 2.f));
		HutRoots[i]->SetRelativeRotation(FRotationMatrix::MakeFromZ(D).Rotator());
		HutRoots[i]->SetRelativeScale3D(FVector(1.8f));
		HutLabels[i]->SetText(FText::FromString(Things[i].Left(24)));
	}
}

void AFNStudio::UpdateLabels()
{
	if (!BoardRoot || !GlobePivot) return;
	FVector Cam = GlobePivot->GetComponentLocation() + FVector(0.f, 350.f, 65.f);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FTransform BT = BoardRoot->GetComponentTransform();
	const FVector GCw = GlobePivot->GetComponentLocation();
	auto Place = [&](UTextRenderComponent* L, const FVector& LocalDir, float Height)
	{
		if (!L) return;
		// float the label straight up above the piece, so it never sits between you and the piece
		const FVector W = BT.TransformPosition(LocalDir * (BoardR + 5.f)) + FVector(0.f, 0.f, Height);
		const bool bFacing = FVector::DotProduct((W - GCw).GetSafeNormal(), (Cam - GCw).GetSafeNormal()) > 0.25f;
		L->SetHiddenInGame(!bFacing);
		L->SetWorldLocation(W);
		L->SetWorldRotation((Cam - W).Rotation());
	};
	for (int32 i = 0; i < PieceLabels.Num(); ++i)
		if (PieceDirs.IsValidIndex(i) && PieceKinds[i] >= 0) Place(PieceLabels[i], PieceDirs[i], PieceKinds[i] == 0 ? 24.f : 17.f);
	for (int32 i = 0; i < HutLabels.Num(); ++i)
		if (HutRoots[i]->IsVisible()) Place(HutLabels[i], BT.InverseTransformVector(HutRoots[i]->GetComponentLocation() - BT.GetLocation()).GetSafeNormal(), 13.f);
	if (ZoneLabel && !ZoneDir.IsZero())
	{
		// put the label on the fence, on the side facing you
		const FVector CamLocal = BT.InverseTransformVector(FVector(0.f, 0.f, 1.f)).GetSafeNormal();   // the top of the fence
		FVector T = CamLocal - ZoneDir * FVector::DotProduct(CamLocal, ZoneDir);
		if (T.SizeSquared() < 1e-4) T = FVector::CrossProduct(ZoneDir, FVector(0, 0, 1));
		T.Normalize();
		Place(ZoneLabel, (ZoneDir * FMath::Cos(ZoneAng) + T * FMath::Sin(ZoneAng)).GetSafeNormal(), 9.f);
	}
}

void AFNStudio::ShowNearby(const TArray<FFNPlayer>& Nearby)
{
	// The Globe board shows nearby players now; the old stands only cluttered the table
#if 0

	// Four stands around the table edge, two each side so the globe stays clear
	static const float Angles[4] = { 25.f, 335.f, 155.f, 205.f };
	if (StandTexts.Num() == 0)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const float A = FMath::DegreesToRadians(Angles[i]);
			const FVector P(175.f * FMath::Cos(A), 175.f * FMath::Sin(A), 0.f);
			Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("StandBase%d"), i)), P + FVector(0, 0, 82), FVector(0.3f, 0.3f, 0.05f), FNTable::Brass);
			Box(FName(*FString::Printf(TEXT("StandCard%d"), i)), P + FVector(0, 0, 112), FVector(40, 4, 52), PlayerColor(i), FRotator(0, 0, 0));
			StandTexts.Add(Text(FName(*FString::Printf(TEXT("StandText%d"), i)), P + FVector(0, 3, 160), FRotator(0, 90, 0), 5.f, FNTable::Pale, FString()));
		}
	}
	for (int32 i = 0; i < StandTexts.Num(); ++i)
	{
		if (!Nearby.IsValidIndex(i)) { StandTexts[i]->SetText(FText::FromString(i == 0 && Nearby.Num() == 0 ? TEXT("no one near") : TEXT(""))); continue; }
		StandTexts[i]->SetTextRenderColor(PlayerColor(i).ToFColor(true));
		StandTexts[i]->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *Nearby[i].Name, *Nearby[i].Location)));
	}
#endif
}

void AFNStudio::ShowRibbons(const TArray<FFNEpisode>& Choices)
{
	for (int32 i = 0; i < Ribbons.Num(); ++i)
	{
		const bool bShow = Choices.IsValidIndex(i);
		Ribbons[i]->SetVisibility(bShow);
		Ribbons[i]->SetCollisionEnabled(bShow ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		RibbonTexts[i]->SetVisibility(bShow);
		if (!bShow) continue;
		const FFNEpisode& C = Choices[i];
		const FLinearColor Col = C.RoundOutcome == TEXT("up") ? FLinearColor(0.85f, 0.62f, 0.18f) : (C.RoundOutcome == TEXT("down") ? FLinearColor(0.65f, 0.12f, 0.12f) : FLinearColor(0.30f, 0.40f, 0.65f));
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Ribbons[i]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), Col);
		RibbonTexts[i]->SetText(FText::FromString(Wrap(C.LocalExperience, 26, 6)));
		RibbonTexts[i]->SetTextRenderColor(C.RoundOutcome == TEXT("down") ? FColor(255, 235, 200) : FColor(30, 16, 6));
	}
}

void AFNStudio::ChooseRibbon(int32 Index)
{
	for (int32 i = 0; i < Ribbons.Num(); ++i)
	{
		Ribbons[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (i != Index) { Ribbons[i]->SetVisibility(false); RibbonTexts[i]->SetVisibility(false); }
	}
}

void AFNStudio::ClearRibbons()
{
	for (int32 i = 0; i < Ribbons.Num(); ++i)
	{
		Ribbons[i]->SetVisibility(false);
		Ribbons[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RibbonTexts[i]->SetVisibility(false);
	}
}

// =====================================================================================
//  Atmosphere: a tiny software synth, the crisis played out on the Globe, the fates build-up
// =====================================================================================

namespace FNSynth
{
	static constexpr int32 Rate = 44100;
	static constexpr float TwoPi = 6.28318530718f;
	static TArray<float> Make(float Seconds) { TArray<float> A; A.SetNumZeroed((int32)(Seconds * Rate)); return A; }
}

void AFNStudio::BuildSounds()
{
	using namespace FNSynth;
	FRandomStream R(1234);

	{ // Click: a short wooden tick
		TArray<float> A = Make(0.08f); float Ph = 0.f;
		for (int32 i = 0; i < A.Num(); ++i) { const float t = (float)i / Rate; Ph += TwoPi * (1300.f - 700.f * t / 0.08f) / Rate; A[i] = 0.28f * FMath::Sin(Ph) * FMath::Exp(-t * 55.f) + 0.08f * R.FRandRange(-1.f, 1.f) * FMath::Exp(-t * 300.f); }
		SfxBank.Add((uint8)EFNSfx::Click, MoveTemp(A));
	}
	{ // Boom: a deep falling thunder
		TArray<float> A = Make(2.6f); float Ph = 0.f, L1 = 0.f, L2 = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate;
			Ph += TwoPi * (32.f + 75.f * FMath::Exp(-t * 3.f)) / Rate;
			L1 += 0.03f * (R.FRandRange(-1.f, 1.f) - L1); L2 += 0.03f * (L1 - L2);
			A[i] = 0.75f * FMath::Sin(Ph) * FMath::Exp(-t * 1.5f) + 2.2f * L2 * FMath::Exp(-t * 2.2f) + 0.25f * R.FRandRange(-1.f, 1.f) * FMath::Exp(-t * 40.f);
		}
		SfxBank.Add((uint8)EFNSfx::Boom, MoveTemp(A));
	}
	{ // Chime: a cold bell, two notes
		TArray<float> A = Make(3.2f);
		const float P[4] = { 1.f, 2.f, 2.76f, 5.4f }, Amp[4] = { 0.22f, 0.09f, 0.07f, 0.035f }, Dec[4] = { 1.1f, 1.7f, 2.4f, 4.f };
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate; float v = 0.f;
			for (int32 k = 0; k < 4; ++k)
			{
				v += Amp[k] * FMath::Sin(TwoPi * 392.f * P[k] * t) * FMath::Exp(-t * Dec[k]);
				if (t > 0.14f) v += 0.8f * Amp[k] * FMath::Sin(TwoPi * 587.3f * P[k] * (t - 0.14f)) * FMath::Exp(-(t - 0.14f) * Dec[k]);
			}
			A[i] = v;
		}
		SfxBank.Add((uint8)EFNSfx::Chime, MoveTemp(A));
	}
	{ // Snuff: a candle blown out, then a low thud
		TArray<float> A = Make(0.7f); float L = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate;
			L += 0.12f * (R.FRandRange(-1.f, 1.f) - L);
			A[i] = 0.5f * L * FMath::Sin(PI * FMath::Min(1.f, t / 0.35f)) * (t < 0.35f ? 1.f : FMath::Exp(-(t - 0.35f) * 20.f))
				+ (t > 0.3f ? 0.45f * FMath::Sin(TwoPi * 58.f * (t - 0.3f)) * FMath::Exp(-(t - 0.3f) * 9.f) : 0.f);
		}
		SfxBank.Add((uint8)EFNSfx::Snuff, MoveTemp(A));
	}
	{ // Quill: a tiny scratch
		TArray<float> A = Make(0.03f); float Prev = 0.f;
		for (int32 i = 0; i < A.Num(); ++i) { const float t = (float)i / Rate; const float n = R.FRandRange(-1.f, 1.f); A[i] = 0.07f * (n - Prev) * FMath::Exp(-t * 150.f); Prev = n; }
		SfxBank.Add((uint8)EFNSfx::Quill, MoveTemp(A));
	}
	{ // Impact: a meteor hitting the world
		TArray<float> A = Make(1.1f); float Ph = 0.f, L = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate;
			Ph += TwoPi * (45.f + 90.f * FMath::Exp(-t * 8.f)) / Rate;
			L += 0.08f * (R.FRandRange(-1.f, 1.f) - L);
			A[i] = 0.5f * FMath::Sin(Ph) * FMath::Exp(-t * 4.f) + 0.9f * L * FMath::Exp(-t * 5.f);
		}
		SfxBank.Add((uint8)EFNSfx::Impact, MoveTemp(A));
	}
	{ // Swarm: thousands of wings
		TArray<float> A = Make(5.f); float Ph[6] = { 0 }; float Base[6];
		for (int32 k = 0; k < 6; ++k) Base[k] = R.FRandRange(150.f, 240.f);
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate; float v = 0.f;
			for (int32 k = 0; k < 6; ++k)
			{
				Ph[k] += TwoPi * Base[k] * (1.f + 0.03f * FMath::Sin(TwoPi * (3.f + k) * t + k)) / Rate;
				v += FMath::Sin(Ph[k]) + 0.5f * FMath::Sin(2.f * Ph[k]) + 0.33f * FMath::Sin(3.f * Ph[k]);
			}
			const float Env = FMath::Min(1.f, t / 0.8f) * FMath::Min(1.f, (5.f - t) / 1.5f);
			A[i] = 0.035f * v * Env * (0.75f + 0.25f * FMath::Sin(TwoPi * 23.f * t));
		}
		SfxBank.Add((uint8)EFNSfx::Swarm, MoveTemp(A));
	}
	{ // Roar: something enormous and very much alive
		TArray<float> A = Make(2.8f); float Ph = 0.f, L = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate;
			const float F = 70.f + 30.f * FMath::Sin(TwoPi * 0.4f * t) + 6.f * FMath::Sin(TwoPi * 9.f * t);
			Ph += TwoPi * F / Rate;
			L += 0.2f * (R.FRandRange(-1.f, 1.f) - L);
			const float Env = FMath::Min(1.f, t / 0.35f) * FMath::Exp(-FMath::Max(0.f, t - 1.2f) * 2.2f);
			const float Saw = FMath::Sin(Ph) + 0.5f * FMath::Sin(2.f * Ph) + 0.33f * FMath::Sin(3.f * Ph) + 0.25f * FMath::Sin(4.f * Ph);
			A[i] = Env * (0.22f * Saw + 0.45f * L);
		}
		SfxBank.Add((uint8)EFNSfx::Roar, MoveTemp(A));
	}
	{ // Whoosh: fire falling from the sky
		TArray<float> A = Make(1.4f); float L1 = 0.f, L2 = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float t = (float)i / Rate;
			const float n = R.FRandRange(-1.f, 1.f);
			const float a = 0.02f + 0.25f * (t / 1.4f);
			L1 += a * (n - L1); L2 += 0.5f * a * (L1 - L2);
			A[i] = 0.6f * (L1 - L2) * FMath::Sin(PI * t / 1.4f);
		}
		SfxBank.Add((uint8)EFNSfx::Whoosh, MoveTemp(A));
	}
}

void AFNStudio::PlaySfx(EFNSfx Sfx, float Gain, float DelaySeconds)
{
	const TArray<float>* S = SfxBank.Find((uint8)Sfx);
	if (!S || Voices.Num() > 48) return;
	FVoice V; V.Samples = S; V.Gain = Gain; V.Delay = FMath::Max(0, (int32)(DelaySeconds * FNSynth::Rate));
	Voices.Add(V);
}

void AFNStudio::PumpAudio()
{
	using namespace FNSynth;
	if (!AudioWave) return;
	// Keep ~0.15 s of sound queued ahead of the speakers
	const int32 Want = (int32)(Rate * 0.15f) * 2;
	int32 Guard = 0;
	while (AudioWave->GetAvailableAudioByteCount() < Want && Guard++ < 16)
	{
		static constexpr int32 N = 1024;
		int16 Out[N];
		for (int32 i = 0; i < N; ++i)
		{
			const double t = (double)(SampleClock++) / Rate;
			const float tf = (float)FMath::Fmod(t, 1000.0);   // slow LFOs only
			const float n = Rng.FRandRange(-1.f, 1.f);
			// Drone: a low open fifth that slowly breathes
			const float Swell = 0.75f + 0.25f * FMath::Sin(TwoPi * 0.07f * tf);
			const float Drone = (float)(0.05 * FMath::Sin(6.283185307 * 55.0 * t) + 0.035 * FMath::Sin(6.283185307 * 82.41 * t + 0.3 * FMath::Sin(TwoPi * 0.1f * tf)) + 0.022 * FMath::Sin(6.283185307 * 110.6 * t));
			float v = Drone * Swell * (1.f + 0.8f * FatesAmt);
			// Wind through the chamber
			Lp1 += 0.02f * (n - Lp1); Lp2 += 0.02f * (Lp1 - Lp2);
			v += 1.6f * Lp2 * (0.6f + 0.4f * FMath::Sin(TwoPi * 0.05f * tf + 1.f));
			// Candle crackle
			if (Rng.FRand() < 6.f / Rate) CrackleEnv = Rng.FRandRange(0.4f, 1.f);
			v += 0.22f * n * CrackleEnv; CrackleEnv *= 0.993f;
			// Whispers while the fates decide
			if (FatesAmt > 0.01f)
			{
				Lp3 += 0.15f * (n - Lp3); Lp4 += 0.3f * ((n - Lp3) - Lp4);
				const float Hum = (float)(0.025 * FMath::Sin(6.283185307 * 220.5 * t)) * FMath::Sin(TwoPi * 0.5f * tf);
				v += FatesAmt * (0.5f * Lp4 * (0.5f + 0.5f * FMath::Sin(TwoPi * 0.7f * tf) * FMath::Sin(TwoPi * 0.23f * tf)) + Hum);
			}
			// War drums that rise over the days
			if (MusicLevel > 0.01f)
			{
				const double Period = 60.0 / (64.0 + 48.0 * MusicLevel);
				const double Tb = FMath::Fmod(t, Period);
				const float Kick = (float)(FMath::Sin(6.283185307 * (48.0 * Tb + 1.6 * (1.0 - FMath::Exp(-Tb * 28.0)))) * FMath::Exp(-Tb * 9.0));
				v += 0.32f * MusicLevel * Kick;
				if (MusicLevel > 0.55f) v += 0.04f * (MusicLevel - 0.55f) * FMath::Sin(TwoPi * 0.25f * tf) * (float)FMath::Sin(6.283185307 * 164.8 * t);
			}
			Out[i] = 0;
			for (FVoice& Vc : Voices)
			{
				if (Vc.Delay > 0) { --Vc.Delay; continue; }
				if (Vc.Pos < Vc.Samples->Num()) v += Vc.Gain * (*Vc.Samples)[Vc.Pos++];
			}
			v = FMath::Clamp(v, -1.5f, 1.5f);
			v = v - (v * v * v) / 6.75f;   // gentle soft clip
			Out[i] = (int16)FMath::Clamp(v * 30000.f, -32767.f, 32767.f);
		}
		Voices.RemoveAll([](const FVoice& Vc) { return Vc.Delay <= 0 && Vc.Pos >= Vc.Samples->Num(); });
		AudioWave->QueueAudio(reinterpret_cast<const uint8*>(Out), N * sizeof(int16));
	}
}

void AFNStudio::SetFatesDeciding(bool bOn) { FatesTarget = bOn ? 1.f : 0.f; }

FRotator AFNStudio::GetShake() const
{
	if (ShakeAmt < 0.01f) return FRotator::ZeroRotator;
	return FRotator(FMath::PerlinNoise1D(Time * 23.f) * ShakeAmt, FMath::PerlinNoise1D(Time * 19.f + 7.3f) * ShakeAmt, FMath::PerlinNoise1D(Time * 17.f + 3.1f) * ShakeAmt * 0.5f);
}

void AFNStudio::ResetFxBit(int32 i)
{
	const FVector GC = FNTable::GlobeC;
	if (FxKind == 1)
	{
		const FVector N = (FxCenter - GC).GetSafeNormal();
		FVector D = (N * 1.2f + Rng.GetUnitVector() * 0.7f + FVector(0, 0, 0.5f)).GetSafeNormal();
		FxPos[i] = FxCenter + D * Rng.FRandRange(130.f, 230.f);
		const FVector Target = FxCenter + Rng.GetUnitVector() * 6.f;
		FxVel[i] = (Target - FxPos[i]).GetSafeNormal() * Rng.FRandRange(150.f, 240.f);
		FxBits[i]->SetRelativeScale3D(FVector(Rng.FRandRange(0.025f, 0.045f)));
	}
	else if (FxKind == 2)
	{
		FxParam[i] = FVector(Rng.FRandRange(7.f, 22.f), Rng.FRandRange(1.8f, 3.6f) * (Rng.FRand() < 0.5f ? -1.f : 1.f), Rng.FRandRange(0.f, 6.28f));
		FxBits[i]->SetRelativeScale3D(FVector(0.011f));
	}
	else if (FxKind == 3)
	{
		const FVector N = (FxCenter - GC).GetSafeNormal();
		FxPos[i] = FxCenter + Rng.GetUnitVector() * 3.f;
		FxVel[i] = N * Rng.FRandRange(60.f, 120.f) + Rng.GetUnitVector() * Rng.FRandRange(30.f, 70.f);
		FxBits[i]->SetRelativeScale3D(FVector(Rng.FRandRange(0.015f, 0.03f)));
	}
}

void AFNStudio::PlayHazard(const FString& HazardIn)
{
	const FString H = HazardIn.ToLower();
	FxKind = H.Contains(TEXT("meteor")) ? 1 : H.Contains(TEXT("locust")) ? 2 : H.Contains(TEXT("dino")) ? 3 : 4;
	FxT = 0.f; FxDur = FxKind == 1 ? 4.5f : FxKind == 2 ? 5.f : FxKind == 3 ? 3.2f : 1.5f;
	StompsDone = 0; FlashAmt = 1.f; ShakeAmt = 2.5f;
	FxCenter = bHasFocus ? BoardToActor(FocusDir, BoardR + 3.f) : FNTable::GlobeC + FVector(0.f, 0.f, BoardR * FNTable::GlobeScale);
	FLinearColor Col(0.9f, 0.15f, 0.08f), FlashCol(1.f, 0.25f, 0.1f);
	int32 Count = 0;
	switch (FxKind)
	{
	case 1: Col = FLinearColor(1.f, 0.35f, 0.05f); FlashCol = FLinearColor(1.f, 0.45f, 0.12f); Count = 14; PlaySfx(EFNSfx::Whoosh, 0.9f); PlaySfx(EFNSfx::Boom, 0.6f, 0.2f); break;
	case 2: Col = FLinearColor(0.06f, 0.05f, 0.02f); FlashCol = FLinearColor(0.6f, 0.7f, 0.15f); Count = 40; PlaySfx(EFNSfx::Swarm, 1.f); PlaySfx(EFNSfx::Boom, 0.4f); break;
	case 3: Col = FLinearColor(0.25f, 0.16f, 0.08f); FlashCol = FLinearColor(0.8f, 0.4f, 0.15f); Count = 24; PlaySfx(EFNSfx::Roar, 1.f, 0.1f); break;
	default: PlaySfx(EFNSfx::Boom, 0.9f); break;
	}
	if (FlashLight) FlashLight->SetLightColor(FlashCol);
	for (int32 i = 0; i < FxBits.Num(); ++i)
	{
		const bool bOn = i < Count && FxKind != 3;   // dino dust appears with each stomp
		FxBits[i]->SetVisibility(bOn);
		if (FxMats.IsValidIndex(i) && FxMats[i]) FxMats[i]->SetVectorParameterValue(TEXT("Color"), Col);
		if (bOn) ResetFxBit(i);
	}
}

void AFNStudio::UpdateFx(float Dt)
{
	FlashAmt *= FMath::Exp(-Dt * 2.5f);
	ShakeAmt *= FMath::Exp(-Dt * 3.f);
	if (FlashLight) FlashLight->SetIntensity(30.f * FlashAmt);
	if (FxKind == 0) return;

	FxT += Dt;
	const bool bEnding = FxT > FxDur;
	const FVector GC = FNTable::GlobeC;
	for (int32 i = 0; i < FxBits.Num(); ++i)
	{
		UStaticMeshComponent* B = FxBits[i];
		if (!B->IsVisible()) continue;
		if (FxKind == 1)
		{
			FxPos[i] += FxVel[i] * Dt;
			if (FVector::Dist(FxPos[i], GC) < BoardR * FNTable::GlobeScale + 3.f || FVector::Dist(FxPos[i], FxCenter) < 3.f)
			{
				if (Time - LastImpact > 0.15f) { PlaySfx(EFNSfx::Impact, Rng.FRandRange(0.35f, 0.6f)); LastImpact = Time; }
				FlashAmt = FMath::Max(FlashAmt, 0.7f); ShakeAmt = FMath::Max(ShakeAmt, 1.2f);
				if (FxT > FxDur - 1.2f) { B->SetVisibility(false); continue; }
				ResetFxBit(i);
			}
			B->SetRelativeLocation(FxPos[i]);
		}
		else if (FxKind == 2)
		{
			const FVector P = FxParam[i];
			const float A = P.Y * FxT + P.Z, Bt = 0.6f * FMath::Sin(1.3f * P.Y * FxT + P.Z * 2.f);
			const float Rr = P.X * (bEnding ? 1.f + (FxT - FxDur) * 3.f : 1.f) + 6.f * FMath::Sin(FxT * 9.f + i);
			B->SetRelativeLocation(FxCenter + FVector(FMath::Cos(A) * FMath::Cos(Bt), FMath::Sin(A) * FMath::Cos(Bt), FMath::Sin(Bt)) * Rr);
			if (FxT > FxDur + 1.f) B->SetVisibility(false);
		}
		else if (FxKind == 3)
		{
			FxVel[i] -= (FxCenter - GC).GetSafeNormal() * 400.f * Dt;
			FxPos[i] += FxVel[i] * Dt;
			if (FVector::Dist(FxPos[i], GC) < BoardR) B->SetVisibility(false);
			B->SetRelativeLocation(FxPos[i]);
		}
	}
	if (FxKind == 2) ShakeAmt = FMath::Max(ShakeAmt, 0.25f * (bEnding ? 0.f : 1.f));
	if (FxKind == 3)
	{
		static const float Stomps[4] = { 0.4f, 1.1f, 1.8f, 2.5f };
		while (StompsDone < 4 && FxT >= Stomps[StompsDone])
		{
			++StompsDone;
			PlaySfx(EFNSfx::Impact, 0.9f);
			ShakeAmt = 3.5f; FlashAmt = FMath::Max(FlashAmt, 0.5f);
			for (int32 k = 0; k < 6; ++k)
			{
				const int32 i = ((StompsDone - 1) * 6 + k) % FxBits.Num();
				FxBits[i]->SetVisibility(true);
				ResetFxBit(i);
			}
		}
		if (GlobePivot)
			GlobePivot->SetRelativeLocation(FNTable::GlobeC + FVector(FMath::PerlinNoise1D(Time * 30.f), FMath::PerlinNoise1D(Time * 27.f + 5.f), 0.f) * ShakeAmt * 1.5f);
	}
	if (FxT > FxDur + 1.5f)
	{
		for (UStaticMeshComponent* B : FxBits) B->SetVisibility(false);
		if (GlobePivot) GlobePivot->SetRelativeLocation(FNTable::GlobeC);
		FxKind = 0;
	}
}


void AFNStudio::RollDice(int32 D20, int32 Mod, const FString& Verdict)
{
	if (!DicePivot) return;
	DiceT = 0.f;
	DiceSounds = 0;
	bDiceGood = D20 != 1 && !Verdict.Contains(TEXT("FAIL")) && !Verdict.Contains(TEXT("DISASTER"));
	DiceSpin = FVector(Rng.FRandRange(500.f, 900.f), Rng.FRandRange(400.f, 800.f), Rng.FRandRange(300.f, 700.f));
	DicePivot->SetVisibility(true, true);
	if (DiceNumber)
	{
		DiceNumber->SetText(FText::FromString(FString::FromInt(D20)));
		DiceNumber->SetTextRenderColor(D20 == 20 ? FColor(120, 255, 140) : D20 == 1 ? FColor(255, 60, 40) : FNTable::Gold);
		DiceNumber->SetVisibility(false);
	}
	if (DiceLine)
	{
		DiceLine->SetText(FText::FromString(Mod != 0 ? FString::Printf(TEXT("d20 %d  +%d  =  %d\n%s"), D20, Mod, D20 + Mod, *Verdict) : FString::Printf(TEXT("d20 %d\n%s"), D20, *Verdict)));
		DiceLine->SetVisibility(false);
	}
	PlaySfx(EFNSfx::Whoosh, 0.35f);
}

void AFNStudio::RollDuelDice(int32 Roll, int32 Sides, int32 Need, bool bSuccess, const FString& Label)
{
	if (!DicePivot) return;
	DiceT = 0.f;
	DiceSounds = 0;
	bDiceGood = bSuccess;
	DiceSpin = FVector(Rng.FRandRange(500.f, 900.f), Rng.FRandRange(400.f, 800.f), Rng.FRandRange(300.f, 700.f));
	DicePivot->SetVisibility(true, true);
	if (DiceNumber)
	{
		DiceNumber->SetText(FText::FromString(FString::FromInt(Roll)));
		DiceNumber->SetTextRenderColor(bSuccess ? FColor(120, 255, 140) : FColor(255, 60, 40));
		DiceNumber->SetVisibility(false);
	}
	if (DiceLine)
	{
		DiceLine->SetText(FText::FromString(FString::Printf(TEXT("%s\nd%d  rolled %d  (needed %d+)\n%s"), *Label, Sides, Roll, Need, bSuccess ? TEXT("HIT") : TEXT("MISS"))));
		DiceLine->SetVisibility(false);
	}
	PlaySfx(EFNSfx::Whoosh, 0.35f);
}

void AFNStudio::UpdateDice(float Dt)
{
	if (DiceT < 0.f || !DicePivot) return;
	DiceT += Dt;
	const FVector Rest(78.f, 118.f, 88.f);
	if (DiceT < 1.6f)
	{
		// falls, bounces three times, spins slower each bounce
		const float H = 75.f * FMath::Exp(-2.6f * DiceT) * FMath::Abs(FMath::Cos(7.f * DiceT));
		DicePivot->SetRelativeLocation(Rest + FVector(-20.f * (1.6f - DiceT), 10.f * (1.6f - DiceT), H));
		DicePivot->AddLocalRotation(FRotator(DiceSpin.X * Dt, DiceSpin.Y * Dt, DiceSpin.Z * Dt) * FMath::Exp(-1.5f * DiceT));
		static const float Hits[3] = { 0.224f, 0.673f, 1.122f };
		while (DiceSounds < 3 && DiceT >= Hits[DiceSounds]) { PlaySfx(EFNSfx::Click, 1.f - 0.25f * DiceSounds); ++DiceSounds; }
	}
	else if (DiceSounds < 4)
	{
		DiceSounds = 4;
		DicePivot->SetRelativeLocation(Rest);
		DicePivot->SetRelativeRotation(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f));
		if (DiceNumber) DiceNumber->SetVisibility(true);
		if (DiceLine) DiceLine->SetVisibility(true);
		if (bDiceGood) PlaySfx(EFNSfx::Chime, 0.8f); else { PlaySfx(EFNSfx::Boom, 0.6f); ShakeAmt = FMath::Max(ShakeAmt, 1.2f); }
		FlashAmt = FMath::Max(FlashAmt, 0.4f);
	}
	// the number floats above the die and faces you
	FVector Cam = GetActorLocation() + FVector(0.f, 340.f, 190.f);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	for (UTextRenderComponent* L : { DiceNumber.Get(), DiceLine.Get() })
		if (L) L->SetWorldRotation((Cam - L->GetComponentLocation()).Rotation());
	if (DiceT > 16.f)
	{
		DiceT = -1.f;
		DicePivot->SetVisibility(false, true);
		if (DiceNumber) DiceNumber->SetVisibility(false);
		if (DiceLine) DiceLine->SetVisibility(false);
	}
}

void AFNStudio::PlayFinalCut(const TArray<FString>& Lines, const TArray<FVector2D>& Spots, const FString& Closing)
{
	CutLines = Lines;
	CutSpots = Spots;
	CutClosing = Closing;
	CutT = 0.f;
	CutIdx = -1;
	MusicLevel = 1.f;
	ClearRibbons();
	// the whole world lights up again for the replay
	ZoneDir = FVector::ZeroVector;
	for (UStaticMeshComponent* M : ZoneMarkers) if (M) M->SetVisibility(false);
	if (ZoneLabel) ZoneLabel->SetText(FText::GetEmpty());
	PaintTiles();
}
