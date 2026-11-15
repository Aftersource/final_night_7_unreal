#include "FNDuelBox.h"

#include "FNDuelStage.h"
#include "FNStudio.h"
#include "SFNDuelTray.h"
#include "FNDuelRules.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "UObject/UObjectIterator.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace FN7Box
{
	static const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* MeshPath = TEXT("/Game/Duel/Mannequin/SK_DuelMannequin.SK_DuelMannequin");

	// Box cm (the table's units; the actor takes the studio's scale). The stage: S cm per metre of the duel's hexes.
	static constexpr float S = 5.5f;
	static constexpr float W = 124.f, D = 112.f, SlabH = 6.f, RimH = 10.f, RimT = 4.f, Chamfer = 18.f;
	static constexpr float TileH = 1.4f;
	static constexpr float FloorTop = SlabH + TileH;
	static constexpr float LidH = 100.f;
	static constexpr int32 StageRadius = 5;
	static const FVector BoxAt(0.f, 134.f, 92.f);       // studio space: over the board, the lid clear in front of the globe (its face reaches Y ~58)
	static constexpr float RiseFrom = -46.f;           // cm below its place when it starts to rise
	static constexpr float RiseTime = 0.95f, OpenTime = 0.85f, DrawerTime = 0.4f;
	static constexpr float SlamTime = 0.9f;            // the portraits slam in (the HUD) before the box starts to rise
	static constexpr float FighterScale = 1.9f;        // miniatures: a fighter is ~1/5 of the box's height, readable from the wide shot
	static const TCHAR* HexMesh = TEXT("/Game/Earth/SM_HexPrism.SM_HexPrism");   // a real hexagonal prism (r 50, h 100, base at 0)

	// Dark stained wood (walnut): it is lit only by the box's own key and candle now, never the table's spots.
	static const FLinearColor WoodDark(0.03f, 0.012f, 0.005f);
	static const FLinearColor WoodMid(0.062f, 0.028f, 0.011f);
	static const FLinearColor WoodLight(0.11f, 0.055f, 0.022f);
	static const FLinearColor Gilt(0.78f, 0.56f, 0.2f);
	static const FLinearColor GiltDark(0.24f, 0.155f, 0.05f);
	static const FLinearColor LidFace(0.034f, 0.019f, 0.009f);   // the spots hit the lid square on: it needs the darkest wood
	static const FLinearColor TileA(0.16f, 0.145f, 0.12f), TileB(0.115f, 0.105f, 0.088f), TileC(0.2f, 0.18f, 0.145f);   // worn flagstones
	static const FLinearColor Parchment(0.86f, 0.79f, 0.62f);

	static FLinearColor Hex(const TCHAR* H)
	{
		return FLinearColor(FColor::FromHex(H));
	}

	/** duel_lab/lib/materials.js colours. */
	static FLinearColor MaterialColor(const FString& M)
	{
		static const TMap<FString, FString> C = {
			{ TEXT("stone"), TEXT("57534e") }, { TEXT("marble"), TEXT("b9b2a4") }, { TEXT("wood"), TEXT("4a3220") },
			{ TEXT("iron"), TEXT("3a3c40") }, { TEXT("gold"), TEXT("c9a03a") }, { TEXT("cloth"), TEXT("5a1f26") },
			{ TEXT("glass"), TEXT("9fd0d8") }, { TEXT("bone"), TEXT("d8cfb8") }, { TEXT("ice"), TEXT("bfe6f2") },
			{ TEXT("obsidian"), TEXT("17131c") }, { TEXT("clay"), TEXT("8a5238") }, { TEXT("leaf"), TEXT("1f3a26") },
			{ TEXT("rope"), TEXT("8a7350") } };
		const FString* V = C.Find(M.ToLower());
		return Hex(V ? **V : TEXT("57534e"));
	}

	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0)
	{
		double V = Def;
		if (O.IsValid() && O->HasTypedField<EJson::Number>(K)) V = O->GetNumberField(K);
		return V;
	}
	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		FString V;
		if (O.IsValid()) O->TryGetStringField(K, V);
		return V;
	}
	static TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TSharedPtr<FJsonObject>* V = nullptr;
		return (O.IsValid() && O->TryGetObjectField(K, V) && V) ? *V : nullptr;
	}
	static const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
		return (O.IsValid() && O->TryGetArrayField(K, V)) ? V : nullptr;
	}
	static bool Vec(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double* Out, int32 N)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = Arr(O, K);
		if (!A || A->Num() < N) return false;
		for (int32 i = 0; i < N; ++i) Out[i] = (*A)[i].IsValid() ? (*A)[i]->AsNumber() : 0.0;
		return true;
	}

	static float EaseOutBack(float T)
	{
		const float C1 = 1.3f, C3 = C1 + 1.f, U = T - 1.f;
		return 1.f + C3 * U * U * U + C1 * U * U;
	}
	static float EaseInOut(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
}

AFNDuelBox::AFNDuelBox()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Mat.Succeeded()) BaseMat = Mat.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Img(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	if (Img.Succeeded()) ImageMat = Img.Object;
}

AFNDuelBox* AFNDuelBox::Get(UWorld* World, bool bCreate)
{
	if (!World) return nullptr;
	TActorIterator<AFNDuelBox> It(World);
	if (It) return *It;
	if (!bCreate) return nullptr;
	AFNDuelBox* B = World->SpawnActor<AFNDuelBox>();
	if (B) B->SetActorHiddenInGame(true);
	return B;
}

UStaticMeshComponent* AFNDuelBox::Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& At, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetRelativeLocation(At);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(SizeCm / 100.f);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	// The gilt trim is the team colour: blue for a duel of yours, red for one you watch (Chris).
	if (Color.Equals(FN7Box::Gilt) || Color.Equals(FN7Box::GiltDark)) Trim.Add(C);
	return C;
}

UTextRenderComponent* AFNDuelBox::Label(USceneComponent* Parent, const FVector& At, float Size, const FColor& Color, const FString& Text)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(Parent ? Parent : RootComponent.Get());
	T->SetRelativeLocation(At);
	T->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));   // reads from the front (+Y)
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(Size);
	T->SetTextRenderColor(Color);
	T->SetText(FText::FromString(Text));
	T->RegisterComponent();
	return T;
}

FVector AFNDuelBox::HexLocal(double Q, double R) const
{
	// duel_lab: x = 1.732 (q + r/2), z = 1.5 r metres, the audience at +z (our +Y).
	return FVector(1.7320508 * (Q + R * 0.5) * FN7Box::S, 1.5 * R * FN7Box::S, FN7Box::FloorTop);
}

void AFNDuelBox::Build()
{
	using namespace FN7Box;
	bBuilt = true;
	Lift = NewObject<USceneComponent>(this);
	Lift->SetupAttachment(RootComponent);
	Lift->RegisterComponent();

	// The base: an octagonal carved tray (two crossed slabs), a rim of walls with chamfered corners, gilt edging.
	Part(Lift, Cube, FVector(0, 0, SlabH * 0.5f), FVector(W, D - 2.f * Chamfer, SlabH), WoodMid);
	Part(Lift, Cube, FVector(0, 0, SlabH * 0.5f), FVector(W - 2.f * Chamfer, D, SlabH), WoodMid);
	Part(Lift, Cube, FVector(0, 0, -1.f), FVector(W + 4.f, D - 2.f * Chamfer + 4.f, 2.f), WoodDark);
	Part(Lift, Cube, FVector(0, 0, -1.f), FVector(W - 2.f * Chamfer + 4.f, D + 4.f, 2.f), WoodDark);
	const float Zr = SlabH + RimH * 0.5f;
	const float Lx = W - 2.f * Chamfer, Ly = D - 2.f * Chamfer, Ld = Chamfer * 1.41421356f;
	for (int32 s = -1; s <= 1; s += 2)
	{
		Part(Lift, Cube, FVector(0, s * (D * 0.5f - RimT * 0.5f), Zr), FVector(Lx, RimT, RimH), WoodDark);
		Part(Lift, Cube, FVector(s * (W * 0.5f - RimT * 0.5f), 0, Zr), FVector(RimT, Ly, RimH), WoodDark);
		Part(Lift, Cube, FVector(0, s * (D * 0.5f - RimT * 0.5f), SlabH + RimH + 0.4f), FVector(Lx, RimT + 0.6f, 0.8f), GiltDark);
		Part(Lift, Cube, FVector(s * (W * 0.5f - RimT * 0.5f), 0, SlabH + RimH + 0.4f), FVector(RimT + 0.6f, Ly, 0.8f), GiltDark);
		for (int32 t = -1; t <= 1; t += 2)
		{
			const FVector C(s * (W * 0.5f - Chamfer * 0.5f), t * (D * 0.5f - Chamfer * 0.5f), Zr);
			const float Yaw = (s * t > 0) ? -45.f : 45.f;
			Part(Lift, Cube, C, FVector(Ld, RimT, RimH), WoodDark, FRotator(0.f, Yaw, 0.f));
			Part(Lift, Cube, C + FVector(0, 0, RimH * 0.5f + 0.4f), FVector(Ld, RimT + 0.6f, 0.8f), GiltDark, FRotator(0.f, Yaw, 0.f));
		}
	}

	// The hex stage: every hex within the radius, one hexagonal flagstone each (instanced), three tones.
	{
		UInstancedStaticMeshComponent* Tiles[3];
		UStaticMesh* Hex = LoadObject<UStaticMesh>(nullptr, HexMesh);
		for (int32 k = 0; k < 3; ++k)
		{
			UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
			I->SetupAttachment(Lift);
			I->SetStaticMesh(Hex ? Hex : LoadObject<UStaticMesh>(nullptr, Cyl));
			I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (BaseMat)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, I);
				M->SetVectorParameterValue(TEXT("Color"), k == 0 ? TileA : k == 1 ? TileB : TileC);
				I->SetMaterial(0, M);
			}
			I->RegisterComponent();
			Tiles[k] = I;
		}
		const float R = S * 0.93f;   // hex circumradius in cm, a grout line between stones
		for (int32 q = -StageRadius; q <= StageRadius; ++q)
			for (int32 r = -StageRadius; r <= StageRadius; ++r)
			{
				if (FMath::Abs(q + r) > StageRadius) continue;
				FVector C = HexLocal(q, r);
				if (FMath::Abs(C.X) > W * 0.5f - RimT - S * 0.9f || FMath::Abs(C.Y) > D * 0.5f - RimT - S * 0.8f) continue;
				C.Z = SlabH;
				const int32 Tone = ((q - r) % 3 + 3) % 3;
				const float Wear = 0.92f + 0.16f * FMath::Frac(FMath::Sin((float)(q * 12.9898 + r * 78.233)) * 43758.5453f);   // stones not quite level
				if (Hex) Tiles[Tone]->AddInstance(FTransform(FRotator::ZeroRotator, C, FVector(R / 50.f, R / 50.f, TileH * Wear / 100.f)));
				else Tiles[Tone]->AddInstance(FTransform(FRotator::ZeroRotator, C + FVector(0, 0, TileH * 0.5f), FVector(R * 2.f, R * 2.f, TileH) / 100.f));
			}
	}

	SceneRoot = NewObject<USceneComponent>(this);
	SceneRoot->SetupAttachment(Lift);
	SceneRoot->RegisterComponent();

	// The lid, hinged at the back edge of the rim: the backdrop when open.
	LidHinge = NewObject<USceneComponent>(this);
	LidHinge->SetupAttachment(Lift);
	LidHinge->SetRelativeLocation(FVector(0.f, -D * 0.5f + 3.f, SlabH + RimH));
	LidHinge->RegisterComponent();
	Part(LidHinge, Cube, FVector(0, -2.5f, LidH * 0.5f), FVector(W, 5.f, LidH), LidFace);
	Part(LidHinge, Cube, FVector(0, -5.6f, LidH * 0.5f), FVector(W - 8.f, 1.2f, LidH - 8.f), WoodDark);   // the back
	// frame
	Part(LidHinge, Cube, FVector(0, 0.6f, LidH - 2.f), FVector(W, 1.6f, 4.f), WoodDark);
	Part(LidHinge, Cube, FVector(0, 0.6f, 2.f), FVector(W, 1.6f, 4.f), WoodDark);
	for (int32 s = -1; s <= 1; s += 2) Part(LidHinge, Cube, FVector(s * (W * 0.5f - 2.f), 0.6f, LidH * 0.5f), FVector(4.f, 1.6f, LidH), WoodDark);
	Part(LidHinge, Cube, FVector(0, 1.5f, LidH - 4.4f), FVector(W - 6.f, 0.6f, 0.8f), Gilt);
	Part(LidHinge, Cube, FVector(0, 1.5f, 4.4f), FVector(W - 6.f, 0.6f, 0.8f), Gilt);
	// crest
	Part(LidHinge, Cube, FVector(0, 0.f, LidH + 4.f), FVector(34.f, 4.f, 8.f), WoodMid);
	Part(LidHinge, Cube, FVector(0, 1.6f, LidH + 4.f), FVector(28.f, 1.f, 5.f), Gilt);
	Part(LidHinge, Sphere, FVector(0, 1.6f, LidH + 9.f), FVector(6.f, 3.f, 6.f), Gilt);
	// the CLASH plaque
	Part(LidHinge, Cube, FVector(0, 1.2f, 84.f), FVector(70.f, 1.6f, 19.f), GiltDark);
	Part(LidHinge, Cube, FVector(0, 1.9f, 84.f), FVector(65.f, 1.f, 15.f), WoodDark);
	Label(LidHinge, FVector(0, 2.6f, 84.f), 15.f, FColor(230, 178, 74), TEXT("CLASH"));
	// the sky between the plaques (bands for a gradient; stars at night)
	for (int32 i = 0; i < 4; ++i) SkyBands.Add(Part(LidHinge, Cube, FVector(0, 0.9f, 18.f + 14.5f * i + 7.f), FVector(58.f, 0.6f, 14.6f), FLinearColor::Black));
	FRandomStream Rng(7);
	for (int32 i = 0; i < 14; ++i)
	{
		UStaticMeshComponent* St = Part(LidHinge, Cube, FVector(Rng.FRandRange(-27.f, 27.f), 1.3f, Rng.FRandRange(40.f, 74.f)), FVector(0.7f, 0.3f, 0.7f), FLinearColor(1.f, 0.95f, 0.8f));
		St->SetVisibility(false);
		Stars.Add(St);
	}
	// the portrait plaques: an arch frame, the portrait, the name, the health bar
	for (int32 s = -1; s <= 1; s += 2)
	{
		const float X = s * 44.f;
		Part(LidHinge, Cube, FVector(X, 1.0f, 50.f), FVector(28.f, 1.6f, 38.f), GiltDark);
		Part(LidHinge, Cyl, FVector(X, 1.0f, 69.f), FVector(28.f, 28.f, 1.6f), GiltDark, FRotator(0.f, 0.f, 90.f));   // the arch
		UStaticMeshComponent* Pic = Part(LidHinge, Cube, FVector(X, 1.9f, 51.f), FVector(23.f, 0.4f, 31.f), WoodLight);
		UTextRenderComponent* Name = Label(LidHinge, FVector(X, 2.4f, 27.5f), 3.4f, FColor(240, 226, 196), FString());
		Part(LidHinge, Cube, FVector(X, 1.4f, 22.f), FVector(26.f, 1.f, 3.4f), FLinearColor(0.03f, 0.02f, 0.02f));
		UStaticMeshComponent* Fill = Part(LidHinge, Cube, FVector(X, 2.f, 22.f), FVector(24.f, 0.6f, 2.2f), s < 0 ? FLinearColor(0.95f, 0.42f, 0.2f) : FLinearColor(0.3f, 0.62f, 1.f));
		if (s < 0) { PortraitA = Pic; NameA = Name; HpFillA = Fill; }
		else { PortraitB = Pic; NameB = Name; HpFillB = Fill; }
	}
	// The fighters' cards: a gilt frame and the portrait, placed over each mannequin every frame (hidden until a portrait comes).
	for (int32 s = 0; s < 2; ++s)
	{
		UStaticMeshComponent* Frame = Part(Lift, Cube, FVector::ZeroVector, FVector(11.2f, 0.5f, 15.4f), GiltDark);
		UStaticMeshComponent* Card = Part(Lift, Cube, FVector::ZeroVector, FVector(10.f, 0.7f, 14.f), WoodLight);
		Frame->SetVisibility(false);
		Card->SetVisibility(false);
		(s == 0 ? CardFrameA : CardFrameB) = Frame;
		(s == 0 ? CardA : CardB) = Card;
	}

	// The drawer at the front: the stars of the lines being typed.
	Drawer = NewObject<USceneComponent>(this);
	Drawer->SetupAttachment(Lift);
	Drawer->SetRelativeLocation(FVector(0.f, D * 0.5f - 8.f, 3.2f));
	Drawer->RegisterComponent();
	Part(Drawer, Cube, FVector(0, 0, 0), FVector(46.f, 16.f, 6.f), WoodMid);
	Part(Drawer, Cube, FVector(0, 8.1f, 0), FVector(46.f, 0.6f, 6.f), WoodDark);
	Part(Drawer, Cube, FVector(0, 8.5f, 0.3f), FVector(38.f, 0.4f, 3.8f), Parchment);
	DrawerText = Label(Drawer, FVector(0, 8.8f, 0.3f), 3.2f, FColor(48, 30, 18), FString());

	// The burst word (BAM, SHHK...) and a warm light over the stage.
	Burst = Label(Lift, FVector::ZeroVector, 9.f, FColor(255, 236, 160), FString());
	Burst->SetVisibility(false);
	RuleText = Label(Lift, FVector::ZeroVector, 1.9f, FColor(240, 236, 220), FString());
	RuleText->SetVisibility(false);
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(Lift);
	L->SetRelativeLocation(FVector(-26.f, 52.f, 74.f));   // a warm key from the front left, above the audience
	L->SetIntensity(18.f);   // the box's key: the table's spots dim to a quarter while it holds the camera (DimTable)
	L->SetAttenuationRadius(240.f);
	L->SetLightColor(FLinearColor(1.f, 0.78f, 0.52f));
	L->SetCastShadows(false);
	L->RegisterComponent();
	KeyLight = L;
	// A candle on the front-right corner of the rim, flickering: the box's second light.
	{
		const FVector At(W * 0.5f - 9.f, D * 0.5f - 9.f, SlabH + RimH);
		Part(Lift, Cyl, At + FVector(0, 0, 4.f), FVector(3.2f, 3.2f, 8.f), FLinearColor(0.75f, 0.68f, 0.52f));
		CandleFlame = Part(Lift, Sphere, At + FVector(0, 0, 9.6f), FVector(1.3f, 1.3f, 2.4f), FLinearColor(1.f, 0.6f, 0.15f) * 6.f);
		CandleLight = NewObject<UPointLightComponent>(this);
		CandleLight->SetupAttachment(Lift);
		CandleLight->SetRelativeLocation(At + FVector(0, 0, 13.f));
		CandleLight->SetIntensity(9.f);
		CandleLight->SetAttenuationRadius(120.f);
		CandleLight->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
		CandleLight->SetCastShadows(false);
		CandleLight->RegisterComponent();
	}
	TeamLight = NewObject<UPointLightComponent>(this);
	TeamLight->SetupAttachment(Lift);
	TeamLight->SetRelativeLocation(FVector(0.f, 30.f, LidH * 0.75f));
	TeamLight->SetIntensity(10.f);
	TeamLight->SetAttenuationRadius(240.f);
	TeamLight->SetCastShadows(false);
	TeamLight->RegisterComponent();

	// The mannequins and their clips (DUEL_UNREAL.md step A assets).
	for (const FString& C : AFNDuelStage::ClipNames())
	{
		const FString P = FString::Printf(TEXT("/Game/Duel/Mannequin/Anims/A_%s.A_%s"), *C, *C);
		Clips.Add(LoadObject<UAnimSequence>(nullptr, *P));
	}
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath))
	{
		const FReferenceSkeleton& Rs = Mesh->GetRefSkeleton();
		const int32 Pelvis = Rs.FindBoneIndex(TEXT("pelvis"));
		FTransform T = FTransform::Identity;
		for (int32 b = Pelvis; b != INDEX_NONE; b = Rs.GetParentIndex(b)) T = T * Rs.GetRefBonePose()[b];
		RefPelvis = T.GetLocation();
		bHaveRefPelvis = Pelvis != INDEX_NONE;
	}
	FighterA = Fighter(TEXT("wood"));
	FighterB = Fighter(TEXT("obsidian"));

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	if (Camera && Camera->GetCameraComponent())
	{
		Camera->GetCameraComponent()->SetFieldOfView(50.f);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	}
}

void AFNDuelBox::DimTable(float Alpha)
{
	// House lights down: while the box holds the camera, the table's spotlights fall to a quarter (they washed the box's
	// tops pale) and the box's own warm key and candle carry it; they come back as it closes. (Lighting channels would
	// shield it more neatly but light nothing on this renderer: channel-1 primitives stayed black.)
	if (Alpha <= 0.f && DimmedSpots.Num() == 0) return;
	if (DimmedSpots.Num() == 0)
		for (TObjectIterator<USpotLightComponent> It; It; ++It)
			if (It->GetWorld() == GetWorld() && It->GetOwner() != this && It->IsVisible()) DimmedSpots.Add(*It, It->Intensity);
	for (const TPair<TWeakObjectPtr<USpotLightComponent>, float>& P : DimmedSpots)
		if (USpotLightComponent* L = P.Key.Get()) L->SetIntensity(P.Value * FMath::Lerp(1.f, 0.25f, FMath::Clamp(Alpha, 0.f, 1.f)));
	if (Alpha <= 0.f) DimmedSpots.Reset();
}

USkeletalMeshComponent* AFNDuelBox::Fighter(const FString& Skin)
{
	USkeletalMeshComponent* M = NewObject<USkeletalMeshComponent>(this);
	M->SetupAttachment(Lift);
	M->RegisterComponent();
	M->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, FN7Box::MeshPath));
	M->SetRelativeScale3D(FVector(FN7Box::S * FN7Box::FighterScale / 100.f));   // a miniature: bigger than the stage's scale, readable
	M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return M;
}

void AFNDuelBox::SetSky(const FString& TimeOfDay)
{
	// Bottom band first.
	TArray<FLinearColor> C;
	if (TimeOfDay == TEXT("dawn")) C = { FLinearColor(1.f, 0.72f, 0.46f), FLinearColor(0.93f, 0.52f, 0.42f), FLinearColor(0.52f, 0.33f, 0.5f), FLinearColor(0.2f, 0.17f, 0.38f) };
	else if (TimeOfDay == TEXT("dusk")) C = { FLinearColor(0.98f, 0.5f, 0.22f), FLinearColor(0.68f, 0.27f, 0.32f), FLinearColor(0.32f, 0.14f, 0.32f), FLinearColor(0.1f, 0.07f, 0.2f) };
	else if (TimeOfDay == TEXT("night")) C = { FLinearColor(0.07f, 0.09f, 0.2f), FLinearColor(0.04f, 0.06f, 0.15f), FLinearColor(0.025f, 0.035f, 0.1f), FLinearColor(0.01f, 0.015f, 0.05f) };
	else C = { FLinearColor(0.75f, 0.87f, 1.f), FLinearColor(0.52f, 0.72f, 0.96f), FLinearColor(0.32f, 0.55f, 0.9f), FLinearColor(0.18f, 0.38f, 0.8f) };
	for (int32 i = 0; i < SkyBands.Num() && i < C.Num(); ++i)
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(SkyBands[i]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), C[i]);
	for (UStaticMeshComponent* St : Stars) if (St) St->SetVisibility(TimeOfDay == TEXT("night"));
}

void AFNDuelBox::BuildScene(const TSharedPtr<FJsonObject>& Scene)
{
	using namespace FN7Box;
	for (UStaticMeshComponent* P : SceneParts) if (P) P->DestroyComponent();
	SceneParts.Reset();
	ClearWeapons();
	SetSky(Str(Scene, TEXT("time_of_day")));
	BuildGround(Obj(Scene, TEXT("ground")));
	const TArray<TSharedPtr<FJsonValue>>* Objects = Arr(Scene, TEXT("objects"));
	if (!Objects) return;
	int32 Built = 0;
	for (const TSharedPtr<FJsonValue>& Ov : *Objects)
	{
		const TSharedPtr<FJsonObject> O = Ov.IsValid() ? Ov->AsObject() : nullptr;
		double At[2] = { 0.0, 0.0 };
		if (!O.IsValid() || !Vec(O, TEXT("at"), At, 2)) continue;
		const FVector Origin = HexLocal(At[0], At[1]);
		const float Yaw = -(float)Num(O, TEXT("yaw"));   // three.js y-up right-handed -> Unreal z-up left-handed
		const FRotator ObjRot(0.f, Yaw, 0.f);
		const TArray<TSharedPtr<FJsonValue>>* Parts = Arr(O, TEXT("parts"));
		if (!Parts) continue;
		const FString ObjId = Str(O, TEXT("id"));
		for (const TSharedPtr<FJsonValue>& Pv : *Parts)
		{
			const TSharedPtr<FJsonObject> P = Pv.IsValid() ? Pv->AsObject() : nullptr;
			if (!P.IsValid()) continue;
			const FString Shape = Str(P, TEXT("shape"));
			double Sz[3] = { 0.5, 0.5, 0.5 }, Off[3] = { 0.0, 0.0, 0.0 }, Rot[3] = { 0.0, 0.0, 0.0 };
			const TArray<TSharedPtr<FJsonValue>>* SzA = Arr(P, TEXT("size"));
			if (SzA) for (int32 i = 0; i < SzA->Num() && i < 3; ++i) Sz[i] = (*SzA)[i]->AsNumber();
			Vec(P, TEXT("offset"), Off, 3);
			Vec(P, TEXT("rot"), Rot, 3);
			const TCHAR* Mesh = Cube;
			FVector Size;
			if (Shape == TEXT("box")) Size = FVector(Sz[0], Sz[2], Sz[1]);
			else if (Shape == TEXT("cylinder")) { Mesh = Cyl; Size = FVector(2.0 * Sz[0], 2.0 * Sz[0], Sz[1]); }
			else if (Shape == TEXT("cone") || Shape == TEXT("tetra")) { Mesh = Cone; Size = (Shape == TEXT("cone")) ? FVector(2.0 * Sz[0], 2.0 * Sz[0], Sz[1]) : FVector(2.0 * Sz[0]); }
			else { Mesh = Sphere; Size = FVector(2.0 * Sz[0]); }   // sphere, octa, ico, dodeca
			FLinearColor Col = MaterialColor(Str(P, TEXT("material")));
			const FString Over = Str(P, TEXT("color"));
			if (Over.StartsWith(TEXT("#"))) Col = Hex(*Over.Mid(1));
			const float Glow = (float)Num(P, TEXT("glow"));
			if (Glow > 0.f) Col = Col * (1.f + 3.f * Glow);
			const FVector LocalOff = ObjRot.RotateVector(FVector(Off[0], Off[2], Off[1]) * S);
			const FRotator PartRot = (ObjRot.Quaternion() * FRotator((float)Rot[2], -(float)Rot[1], -(float)Rot[0]).Quaternion()).Rotator();
			UStaticMeshComponent* Pc = Part(SceneRoot, Mesh, Origin - FVector(0, 0, FloorTop - SlabH - TileH) + LocalOff, Size * S, Col, PartRot);
			if (!ObjId.IsEmpty()) ObjectParts.FindOrAdd(ObjId).Add(SceneParts.Num());
			SceneBase.Add(Pc->GetRelativeTransform());
			SceneParts.Add(Pc);
			++Built;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 duel box: scene %s, %s, %d objects, %d parts"), *Str(Scene, TEXT("place")), *Str(Scene, TEXT("time_of_day")), Objects->Num(), Built);
}

void AFNDuelBox::Open(const TSharedPtr<FFNDuelSession>& InSession, AFNStudio* Studio, bool bTakeCamera, TFunction<double()> ServerNowMs)
{
	if (!InSession.IsValid()) return;
	if (!bBuilt) Build();
	Session = InSession;
	NowMs = MoveTemp(ServerNowMs);
	StudioRef = Studio;
	const bool bNew = ShownDuel != InSession->DuelId;
	ShownDuel = InSession->DuelId;

	// Face the player's seat: the box's front (+Y) toward the camera, level.
	const FTransform St = Studio ? Studio->GetActorTransform() : FTransform::Identity;
	const FVector At = St.TransformPosition(FN7Box::BoxAt);
	FVector Cam = St.TransformPosition(FVector(0.f, 420.f, 170.f));
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (PC->PlayerCameraManager && !bTookCamera) Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector ToCam = (Cam - At).GetSafeNormal2D();
	SetActorTransform(FTransform(FRotator(0.f, ToCam.Rotation().Yaw - 90.f, 0.f), At, St.GetScale3D()));

	if (bNew)
	{
		auto Skin = [](USkeletalMeshComponent* M, const FString& Name)
		{
			const FString MiPath = FString::Printf(TEXT("/Game/Duel/Skins/MI_Skin_%s.MI_Skin_%s"), *Name, *Name);
			if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, *MiPath))
				for (int32 s = 0; M && s < M->GetNumMaterials(); ++s) M->SetMaterial(s, Mi);
		};
		Skin(FighterA, InSession->ASkin);
		Skin(FighterB, InSession->BSkin);
		NameA->SetText(FText::FromString(InSession->AName));
		NameB->SetText(FText::FromString(InSession->BName));
		LastPanel = -1;
		LastPanelT = 0.0;
		ClipA = ClipB = -1;
		ShownHpA = InSession->HpA;
		ShownHpB = InSession->HpB;
		bCamInit = false;
	}
	// Blue when it's your duel, red when you're watching: unmistakable in the wide shot (Chris).
	{
		const bool bYours = !InSession->MySide.IsEmpty();
		const FLinearColor Team = bYours ? FLinearColor(0.12f, 0.38f, 1.f) : FLinearColor(1.f, 0.1f, 0.06f);
		for (UStaticMeshComponent* T : Trim)
			if (UMaterialInstanceDynamic* M = T ? Cast<UMaterialInstanceDynamic>(T->GetMaterial(0)) : nullptr) M->SetVectorParameterValue(TEXT("Color"), Team);
		if (TeamLight) TeamLight->SetLightColor(Team);
	}
	if (bNew || InSession->Scene.Get() != ShownScene)
	{
		ShownScene = InSession->Scene.Get();
		BuildScene(InSession->Scene);
	}
	SetActorHiddenInGame(false);
	if (Phase == EPhase::Hidden) { Phase = EPhase::Rising; PhaseT = bTakeCamera ? -FN7Box::SlamTime : 0.f; }   // taking the camera: the portraits slam in first
	else if (Phase == EPhase::Closing) { Phase = EPhase::Rising; PhaseT = 0.f; }

	if (bTakeCamera && !bTookCamera && Camera)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			OldViewTarget = PC->GetViewTarget();
			bCamInit = false;
			UpdateCamera(TEXT("wide"), 1.f);
			PC->SetViewTargetWithBlend(Camera, 1.1f, VTBlend_EaseInOut, 2.f);
			bTookCamera = true;
			bDimsTable = true;
		}
	UE_LOG(LogTemp, Display, TEXT("FN7 duel box: open %s%s"), *ShownDuel, bTookCamera ? TEXT(" (camera on the box)") : TEXT(""));
}

void AFNDuelBox::SetPortraits(UTexture2D* A, UTexture2D* B)
{
	if (!ImageMat) return;
	auto Put = [this](UStaticMeshComponent* C, UTexture2D* T)
	{
		if (!C || !T) return;
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ImageMat, C);
		M->SetTextureParameterValue(TEXT("SlateUI"), T);
		C->SetMaterial(0, M);
	};
	Put(PortraitA, A);
	Put(PortraitB, B);
	Put(CardA, A);
	Put(CardB, B);
	if (CardA) { CardA->SetVisibility(A != nullptr); CardFrameA->SetVisibility(A != nullptr); }
	if (CardB) { CardB->SetVisibility(B != nullptr); CardFrameB->SetVisibility(B != nullptr); }
}

void AFNDuelBox::Close()
{
	if (Phase == EPhase::Hidden || Phase == EPhase::Closing) return;
	Phase = EPhase::Closing;
	PhaseT = 0.f;
	if (bTookCamera)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			AActor* Back = OldViewTarget.IsValid() ? OldViewTarget.Get() : PC->GetPawn();
			if (Back) PC->SetViewTargetWithBlend(Back, 1.0f, VTBlend_EaseInOut, 2.f);
		}
	bTookCamera = false;
	ClearWeapons();
	UE_LOG(LogTemp, Display, TEXT("FN7 duel box: close %s"), *ShownDuel);
}

void AFNDuelBox::SetHpBars(float A, float B)
{
	const TSharedPtr<FFNDuelSession> S = Session.Pin();
	const float MaxA = S.IsValid() ? FMath::Max(1.f, S->MaxA) : 10.f, MaxB = S.IsValid() ? FMath::Max(1.f, S->MaxB) : 10.f;
	auto Bar = [](UStaticMeshComponent* Fill, float Frac, float X)
	{
		if (!Fill) return;
		Frac = FMath::Clamp(Frac, 0.001f, 1.f);
		Fill->SetRelativeScale3D(FVector(24.f * Frac, 0.6f, 2.2f) / 100.f);
		Fill->SetRelativeLocation(FVector(X - 12.f + 12.f * Frac, 2.f, 22.f));
	};
	Bar(HpFillA, A / MaxA, -44.f);
	Bar(HpFillB, B / MaxB, 44.f);
}

void AFNDuelBox::PoseFighter(USkeletalMeshComponent* M, int32& ShownClip, const FString& ClipName, float ClipT, bool bLoop, const FVector& At, const FVector& FaceTo)
{
	if (!M) return;
	int32 Idx = AFNDuelStage::ClipNames().IndexOfByKey(ClipName);
	if (!Clips.IsValidIndex(Idx) || !Clips[Idx]) Idx = AFNDuelStage::ClipNames().IndexOfByKey(AFNDuelStage::ClipFallback(ClipName));
	if (!Clips.IsValidIndex(Idx) || !Clips[Idx]) Idx = AFNDuelStage::ClipNames().IndexOfByKey(FString(TEXT("idle_guard")));
	UAnimSequence* A = Clips.IsValidIndex(Idx) ? Clips[Idx].Get() : nullptr;
	if (Idx != ShownClip && A)
	{
		M->PlayAnimation(A, bLoop);
		M->Stop();   // posed by time below: a function of the server clock, never free-running
		ShownClip = Idx;
	}
	if (A)
	{
		const float Len = FMath::Max(0.05f, A->GetPlayLength());
		M->SetPosition(bLoop ? FMath::Fmod(FMath::Max(0.f, ClipT), Len) : FMath::Clamp(ClipT, 0.f, Len - 0.01f), false);
	}
	// Facing (local): the mesh looks along its +Y.
	FVector Dir = FaceTo - At;
	Dir.Z = 0.f;
	if (!Dir.Normalize()) Dir = FVector(1, 0, 0);
	M->SetRelativeRotation(FRotator(0.f, Dir.Rotation().Yaw - 90.f, 0.f));
	M->SetRelativeLocation(At);
	if (bHaveRefPelvis)
	{
		// The clips walk the pelvis: strip its X/Y travel so the figure stays on its hex (as the lab does).
		FVector Drift = M->GetBoneLocation(TEXT("pelvis"), EBoneSpaces::ComponentSpace) - RefPelvis;
		Drift.Z = 0.f;
		M->SetRelativeLocation(At - M->GetRelativeTransform().TransformVector(Drift));
	}
}

double AFNDuelBox::InstantStepMs(const TSharedPtr<FJsonObject>& Step)
{
	double Ms = 0.0;
	if (Step.IsValid() && Step->TryGetNumberField(TEXT("ms"), Ms) && Ms > 0.0) return FMath::Clamp(Ms, 150.0, 4000.0);
	FString Path;
	if (Step.IsValid()) Step->TryGetStringField(TEXT("path"), Path);
	return Path.Contains(TEXT("circle")) ? 1800.0 : 900.0;
}

void AFNDuelBox::PlayInstant(const FString& Side, const TSharedPtr<FJsonObject>& Blow)
{
	using namespace FN7Box;
	if (!Blow.IsValid() || (Side != TEXT("a") && Side != TEXT("b"))) return;
	FInstant& In = Side == TEXT("a") ? InstA : InstB;
	In.Steps.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Stage = Arr(Blow, TEXT("stage")))
		for (const TSharedPtr<FJsonValue>& V : *Stage)
			if (const TSharedPtr<FJsonObject> Sp = V.IsValid() ? V->AsObject() : nullptr)
				if (In.Steps.Num() < 4) In.Steps.Add(Sp);
	if (In.Steps.Num() == 0)
	{
		// Older servers: the blow's `anim` cue (a clip name, or an object with one).
		FString Clip = Str(Blow, TEXT("anim"));
		if (Clip.IsEmpty()) Clip = Str(Obj(Blow, TEXT("anim")), TEXT("clip"));
		if (Clip.IsEmpty()) return;
		TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
		One->SetStringField(TEXT("clip"), Clip);
		In.Steps.Add(One);
	}
	In.From = Side == TEXT("a") ? ShownAtA : ShownAtB;
	In.StartS = FPlatformTime::Seconds();
	In.BlowN = (int32)Num(Blow, TEXT("n"), (double)In.BlowN + 1.0);
	// A whiff ("you have no pistol", "no tree here"): the words float over the fighter in grey.
	FString Whiff;
	for (const TSharedPtr<FJsonObject>& Sp : In.Steps)
	{
		bool bW = false;
		if (Sp->TryGetStringField(TEXT("whiff"), Whiff) && !Whiff.IsEmpty()) break;
		if (Sp->TryGetBoolField(TEXT("whiff"), bW) && bW) { Whiff = TEXT("WHIFF"); break; }
	}
	if (Whiff.IsEmpty()) Blow->TryGetStringField(TEXT("whiff"), Whiff);
	if (!Whiff.IsEmpty() && RuleText)
	{
		RuleText->SetText(FText::FromString(Whiff));
		RuleText->SetTextRenderColor(FColor(170, 170, 170));
		RuleAt = In.From + FVector(0, 0, 29.f);
		RuleText->SetVisibility(true);
		RuleLeft = 1.6f;
	}
	const FString First = Str(In.Steps[0], TEXT("clip")), FirstPath = Str(In.Steps[0], TEXT("path"));
	UE_LOG(LogTemp, Display, TEXT("FN7 duel instant: %s blow %d, %d step(s), first %s path '%s' whiff '%s'"), *Side, In.BlowN, In.Steps.Num(), *First, *FirstPath, *Whiff);
}

void AFNDuelBox::PlayStage(double Now, float Dt)
{
	using namespace FN7Box;
	const TSharedPtr<FFNDuelSession> Ses = Session.Pin();
	if (!Ses.IsValid()) return;
	double StartA[2] = { -3.0, 0.0 }, StartB[2] = { 2.0, 0.0 };
	Vec(Ses->Scene, TEXT("a_start"), StartA, 2);
	Vec(Ses->Scene, TEXT("b_start"), StartB, 2);

	int32 Cur = -1;
	for (const TPair<int32, TSharedPtr<FJsonObject>>& P : Ses->Panels)
		if (Num(P.Value, TEXT("starts_at_ms")) <= Now && P.Key > Cur) Cur = P.Key;
	const TSharedPtr<FJsonObject> Panel = Cur >= 0 ? Ses->Panels.FindRef(Cur) : nullptr;
	const double T = Panel.IsValid() ? FMath::Clamp(Now - Num(Panel, TEXT("starts_at_ms")), 0.0, Num(Panel, TEXT("play_ms"), 2400.0)) : 0.0;

	// Where each fighter starts this panel: where it ended the last panel we have, else its start hex.
	auto StartOf = [&](const TCHAR* Side, const double* Def) -> FVector
	{
		for (int32 m = Cur - 1; m >= 0; --m)
			if (const TSharedPtr<FJsonObject> Prev = Ses->Panels.FindRef(m))
			{
				double H[2];
				if (Vec(Obj(Prev, Side), TEXT("at"), H, 2)) return HexLocal(H[0], H[1]);
			}
		return HexLocal(Def[0], Def[1]);
	};
	struct FPose { FVector At; FString Clip; float ClipT = 0.f; bool bLoop = true; TSharedPtr<FJsonObject> Seg; int32 SegIdx = -1; double SegT = 0.0; };
	auto Track = [&](const TCHAR* Side, const double* Def) -> FPose
	{
		FPose Out;
		Out.At = StartOf(Side, Def);
		Out.Clip = TEXT("idle_guard");
		Out.ClipT = (float)FMath::Fmod(FPlatformTime::Seconds(), 100.0);
		if (!Panel.IsValid()) return Out;
		const TArray<TSharedPtr<FJsonValue>>* Segs = Arr(Obj(Panel, Side), TEXT("segs"));
		if (!Segs) return Out;
		double Acc = 0.0;
		FVector From = Out.At;
		for (int32 i = 0; i < Segs->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Sg = (*Segs)[i].IsValid() ? (*Segs)[i]->AsObject() : nullptr;
			if (!Sg.IsValid()) continue;
			const double Ms = FMath::Max(1.0, Num(Sg, TEXT("ms"), 500.0));
			double To[2];
			const bool bMove = Vec(Sg, TEXT("to"), To, 2);
			const FVector Dest = bMove ? HexLocal(To[0], To[1]) : From;
			const bool bLast = i == Segs->Num() - 1;
			if (T < Acc + Ms || bLast)
			{
				const float F = (float)FMath::Clamp((T - Acc) / Ms, 0.0, 1.0);
				const FString Path = Str(Sg, TEXT("path"));
				float Fe = EaseInOut(F);
				FVector P = FMath::Lerp(From, Dest, Fe);
				if (Path == TEXT("leap") || Path == TEXT("vault") || Path == TEXT("wallkick") || Path == TEXT("flip"))
					P.Z += FMath::Sin(F * PI) * (float)Num(Sg, TEXT("h"), Path == TEXT("vault") ? 0.9 : 1.2) * S;
				else if (Path == TEXT("knock")) P = FMath::Lerp(From, Dest, 1.f - FMath::Square(1.f - F));
				Out.At = P;
				Out.Clip = Str(Sg, TEXT("clip"));
				bool bLoopSeg = false;
				Sg->TryGetBoolField(TEXT("loop"), bLoopSeg);
				Out.bLoop = bLoopSeg;
				Out.ClipT = (float)((T - Acc) / 1000.0 * Num(Sg, TEXT("speed"), 1.0));
				Out.Seg = Sg;
				Out.SegIdx = i;
				Out.SegT = T - Acc;
				return Out;
			}
			From = Dest;
			Acc += Ms;
		}
		Out.At = From;
		return Out;
	};
	FPose Pa = Track(TEXT("a"), StartA), Pb = Track(TEXT("b"), StartB);
	// An instant move per Enter (PlayInstant) overrides the panel pose while it plays: its clip from the fighter's
	// current spot, moved by its path (left/right a hex sideways, in/out toward/away from the foe, circle around the
	// foe, jump/leap a hop). When it ends the panels take over again.
	{
		const double NowS = FPlatformTime::Seconds();
		const float Hex = (HexLocal(1.0, 0.0) - HexLocal(0.0, 0.0)).Size();
		auto Instant = [&](FInstant& In, FPose& P, const FVector& FoeAt, int32 SideIdx)
		{
			if (In.StartS < 0.0 || In.Steps.Num() == 0) return;
			double Acc = 0.0;
			const double El = (NowS - In.StartS) * 1000.0;
			FVector From = In.From;
			for (int32 i = 0; i < In.Steps.Num(); ++i)
			{
				const TSharedPtr<FJsonObject>& Sp = In.Steps[i];
				const double Ms = InstantStepMs(Sp);
				FString Path = Str(Sp, TEXT("path")).ToLower();
				FVector ToFoe = (FoeAt - From).GetSafeNormal2D();
				if (ToFoe.IsNearlyZero()) ToFoe = FVector(SideIdx == 0 ? 1.f : -1.f, 0.f, 0.f);
				const FVector Sideways = FVector::CrossProduct(FVector::UpVector, ToFoe);
				FVector Dest = From;
				if (Path.Contains(TEXT("left"))) Dest = From + Sideways * Hex;
				else if (Path.Contains(TEXT("right"))) Dest = From - Sideways * Hex;
				else if (Path == TEXT("in") || Path == TEXT("dash") || Path == TEXT("charge") || Path == TEXT("forward") || Path == TEXT("lunge"))
					Dest = From + ToFoe * FMath::Max(0.f, FMath::Min(Hex, (FoeAt - From).Size2D() - Hex * 0.8f));
				else if (Path == TEXT("out") || Path == TEXT("back") || Path == TEXT("retreat")) Dest = From - ToFoe * Hex;
				if (El < Acc + Ms)
				{
					const float F = (float)FMath::Clamp((El - Acc) / Ms, 0.0, 1.0);
					FVector At = FMath::Lerp(From, Dest, FMath::InterpEaseInOut(0.f, 1.f, F, 2.f));
					if (Path.Contains(TEXT("circle")))
					{
						const FVector Rel = From - FoeAt;
						At = FoeAt + Rel.RotateAngleAxis(F * 360.f, FVector::UpVector);
					}
					if (Path.Contains(TEXT("jump")) || Path.Contains(TEXT("leap")) || Path == TEXT("vault") || Path == TEXT("flip"))
						At.Z += FMath::Sin(F * PI) * 1.2f * FN7Box::S;
					P.At = At;
					P.Clip = Str(Sp, TEXT("clip"));
					if (P.Clip.IsEmpty()) P.Clip = TEXT("idle_guard");
					P.bLoop = Path.Contains(TEXT("circle")) || P.Clip == TEXT("run") || P.Clip == TEXT("walk");
					P.ClipT = (float)((El - Acc) / 1000.0);
					P.Seg = Sp;
					P.SegIdx = 100000 + In.BlowN * 8 + i;   // unique per instant step: its draw flash plays once
					P.SegT = El - Acc;
					return;
				}
				From = Dest;
				Acc += Ms;
			}
			In.StartS = -1.0;   // done: the panels take over
		};
		const FVector FoeOfA = Pb.At, FoeOfB = Pa.At;
		Instant(InstA, Pa, FoeOfA, 0);
		Instant(InstB, Pb, FoeOfB, 1);
	}
	ShownAtA = Pa.At;
	ShownAtB = Pb.At;
	PoseFighter(FighterA, ClipA, Pa.Clip, Pa.ClipT, Pa.bLoop, Pa.At, Pb.At);
	PoseFighter(FighterB, ClipB, Pb.Clip, Pb.ClipT, Pb.bLoop, Pb.At, Pa.At);
	// The portrait cards ride beside the mannequins at head height, on the far side from the foe, turned to the camera
	// (over the heads they sat under the HUD's banners, and the space over the heads is the floating rules').
	{
		const FVector CamLocal = Camera ? Lift->GetComponentTransform().InverseTransformPosition(Camera->GetActorLocation()) : FVector(0.f, -300.f, 100.f);
		auto Place = [&CamLocal](UStaticMeshComponent* Card, UStaticMeshComponent* Frame, const FVector& Over, const FVector& Foe)
		{
			if (!Card || !Card->IsVisible()) return;
			FVector Out = (Over - Foe).GetSafeNormal2D();
			if (Out.IsNearlyZero()) Out = FVector(1.f, 0.f, 0.f);
			const FVector P = Over + Out * 11.f + FVector(0.f, 0.f, 22.f);
			const FVector To = CamLocal - P;
			const FRotator R(0.f, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)) - 90.f, 0.f);
			Card->SetRelativeLocationAndRotation(P, R);
			Frame->SetRelativeLocationAndRotation(P - R.RotateVector(FVector(0.f, 0.25f, 0.f)), R);
		};
		Place(CardA, CardFrameA, Pa.At, Pb.At);
		Place(CardB, CardFrameB, Pb.At, Pa.At);
	}

	// Weapons in hand: each seg of an armed fighter carries its prop (none = empty hands). A hammer-space draw
	// (a draw_from_nowhere seg) flashes at the hand and the new weapon grows into it.
	{
		const double NowS = FPlatformTime::Seconds();
		auto Arm = [&](FHeld& Hd, USkeletalMeshComponent* M, const FPose& P, const TCHAR* Side)
		{
			if (Panel.IsValid() || P.SegIdx >= 100000) Hd.Prop = P.Seg.IsValid() ? Obj(P.Seg, TEXT("prop")) : TSharedPtr<FJsonObject>();
			if (P.Seg.IsValid() && Str(P.Seg, TEXT("clip")) == TEXT("draw_from_nowhere"))
			{
				const FString Key = FString::Printf(TEXT("%d:%s:%d"), Cur, Side, P.SegIdx);
				if (!DrawsSeen.Contains(Key))
				{
					DrawsSeen.Add(Key);
					if (P.SegT < 700.0)
					{
						Flash(BoneLocal(M, TEXT("hand_r"), P.At + FVector(0, 0, 11.f)), FLinearColor(1.f, 0.85f, 0.45f), 4.5f, 0.45f, 40.f);
						Hd.DrawnAt = NowS + 0.15;
						UE_LOG(LogTemp, Display, TEXT("FN7 duel box: %s draws %s from nowhere"), Side, *Str(Hd.Prop, TEXT("name")));
					}
				}
			}
			UpdateHeld(Hd, M, NowS);
		};
		Arm(HeldA, FighterA, Pa, TEXT("a"));
		Arm(HeldB, FighterB, Pb, TEXT("b"));
	}

	// Contacts: the burst word, a shake; the bars move to the panel's hp after its last contact.
	if (Panel.IsValid())
	{
		const bool bFresh = Cur != LastPanel;
		const double From = bFresh ? -1.0 : LastPanelT;
		const bool bLive = !bFresh || T < 800.0;   // a reconnect into an old panel doesn't replay its bursts
		double LastContact = -1.0;
		// The panel's strikes in order (each blow, then its counter), by side: the k-th contact by a side is its k-th strike.
		TArray<TPair<FString, TSharedPtr<FJsonObject>>> Strikes;
		if (const TArray<TSharedPtr<FJsonValue>>* Bn = Arr(Panel, TEXT("blows")))
			for (const TSharedPtr<FJsonValue>& V : *Bn)
				if (const TSharedPtr<FJsonObject> Bl = Ses->Blows.FindRef((int32)V->AsNumber()))
				{
					Strikes.Add({ Str(Bl, TEXT("by")) == Ses->A ? TEXT("a") : TEXT("b"), Bl });
					if (const TSharedPtr<FJsonObject> Co = Obj(Bl, TEXT("counter"))) Strikes.Add({ Str(Co, TEXT("by")) == Ses->A ? TEXT("a") : TEXT("b"), Co });
				}
		TMap<FString, int32> SeenBySide;
		if (const TArray<TSharedPtr<FJsonValue>>* Cs = Arr(Panel, TEXT("contacts")))
			for (const TSharedPtr<FJsonValue>& Cv : *Cs)
			{
				const TSharedPtr<FJsonObject> C = Cv.IsValid() ? Cv->AsObject() : nullptr;
				const double Tm = Num(C, TEXT("t_ms"), -1.0);
				LastContact = FMath::Max(LastContact, Tm);
				const FString By = Str(C, TEXT("by"));
				const int32 K = SeenBySide.FindOrAdd(By)++;
				if (!bLive || Tm <= From || Tm > T) continue;
				for (int32 s = 0, Seen = 0; s < Strikes.Num(); ++s)
					if (Strikes[s].Key == By && Seen++ == K)
					{
						// Two lines: what the words and the moment earned, then the die and the verdict.
						FString Full = FN7DuelRules::Breakdown(Strikes[s].Value), Head, Tail;
						if (Full.Split(TEXT("  |  d20"), &Head, &Tail, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) Full = Head + TEXT("\nd20") + Tail;
						else if (Full.Split(TEXT("  |  no roll"), &Head, &Tail, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) Full = Head + TEXT("\nno roll") + Tail;
						RuleText->SetText(FText::FromString(Full));
						RuleText->SetTextRenderColor(FN7DuelRules::ToneColor(FN7DuelRules::Tone(Strikes[s].Value)).ToFColor(true));
						RuleAt = (By == TEXT("a") ? Pa.At : Pb.At) + FVector(0, 0, 29.f);   // over the striker, under the burst word
						RuleText->SetVisibility(true);
						RuleLeft = 1.4f;
						break;
					}
				const FString On = Str(C, TEXT("on"));
				BurstAt = (On == TEXT("a") ? Pa.At : On == TEXT("b") ? Pb.At : (Pa.At + Pb.At) * 0.5f) + FVector(0, 0, 26.f);   // over the miniatures' heads
				Burst->SetText(FText::FromString(Str(C, TEXT("text"))));
				Burst->SetVisibility(true);
				BurstLeft = 0.7f;
				bool bMiss = false;
				C->TryGetBoolField(TEXT("miss"), bMiss);
				if (!bMiss)
				{
					ShakeLeft = 0.3f;
					ShakeAmp = 0.6f + 2.4f * (float)Num(C, TEXT("impulse"), 0.3);
				}
			}
		if (T >= LastContact)
			if (const TSharedPtr<FJsonObject> Hp = Obj(Panel, TEXT("hp")))
			{
				ShownHpA = (float)Num(Hp, TEXT("a"), ShownHpA);
				ShownHpB = (float)Num(Hp, TEXT("b"), ShownHpB);
			}
		FString Cap = Str(Panel, TEXT("caption"));
		bool bFlip = false;
		Panel->TryGetBoolField(TEXT("flip"), bFlip);
		if (bFlip) Cap = TEXT("INITIATIVE FLIPS!  ") + Cap;
		if (bFlip && bFresh && bLive)
		{
			FlashAt = FPlatformTime::Seconds();   // the HUD flashes white (FlashAlpha)
			bCamInit = false;                     // and the camera cuts, no glide
			bFlipSide = !bFlipSide;               // to the other side of the stage
		}
		Caption = Cap;
		PlayShots(Panel, Cur, T, bLive);
		LastPanel = Cur;
		LastPanelT = T;
	}
	ApplyKnocks(Cur, T);
	TickFx(FPlatformTime::Seconds());
	if (GroundFloor == TEXT("shaken") && FPlatformTime::Seconds() > NextTremor)
	{
		// The ground shakes underfoot: a short tremor every few seconds.
		NextTremor = FPlatformTime::Seconds() + 2.2 + FMath::FRand() * 1.5;
		ShakeLeft = FMath::Max(ShakeLeft, 0.35f);
		ShakeAmp = FMath::Max(ShakeAmp, 0.7f);
	}
	if (GroundLight)
	{
		const float Tm = (float)FPlatformTime::Seconds();
		const float Base = GroundFloor == TEXT("lava") ? 28.f : GroundFloor == TEXT("burning") ? 18.f : 0.f;
		GroundLight->SetIntensity(Base * (0.8f + 0.2f * FMath::PerlinNoise1D(Tm * 5.f)));
	}
	SetHpBars(ShownHpA, ShownHpB);

	if (RuleLeft > 0.f)
	{
		RuleLeft -= Dt;
		RuleText->SetRelativeLocation(RuleAt + FVector(0, 0, (1.4f - RuleLeft) * 3.f));
		if (RuleLeft <= 0.f) RuleText->SetVisibility(false);
	}
	if (BurstLeft > 0.f)
	{
		BurstLeft -= Dt;
		Burst->SetRelativeLocation(BurstAt + FVector(0, 0, (0.7f - BurstLeft) * 10.f));
		Burst->SetWorldSize(9.f + 6.f * FMath::Clamp(BurstLeft / 0.7f, 0.f, 1.f));
		if (BurstLeft <= 0.f) Burst->SetVisibility(false);
	}

	// The drawer: both lines' stars.
	auto StarStr = [](int32 N) { return FString::ChrN(FMath::Clamp(N, 0, 24), TEXT('*')); };
	DrawerText->SetText(FText::FromString(FString::Printf(TEXT("%s   |   %s"), *StarStr(Ses->LineA.Len), *StarStr(Ses->LineB.Len))));

	// A panel that fires (projectiles) or pulls a weapon out of nowhere: "wide" frames the stage so the fire is seen.
	bActionPanel = false;
	if (Panel.IsValid())
	{
		if (const TArray<TSharedPtr<FJsonValue>>* Ps = Arr(Panel, TEXT("projectiles"))) bActionPanel = Ps->Num() > 0;
		for (const TCHAR* Side : { TEXT("a"), TEXT("b") })
			if (const TArray<TSharedPtr<FJsonValue>>* Sg = Arr(Obj(Panel, Side), TEXT("segs")))
				for (const TSharedPtr<FJsonValue>& V : *Sg) if (V.IsValid() && V->AsObject().IsValid() && Str(V->AsObject(), TEXT("clip")) == TEXT("draw_from_nowhere")) bActionPanel = true;
	}
	UpdateCamera(Panel.IsValid() ? Str(Panel, TEXT("shot")) : FString(TEXT("wide")), Dt);
}

void AFNDuelBox::UpdateCamera(const FString& Shot, float Dt)
{
	if (!Camera || !FighterA || !FighterB) return;
	const FTransform B = GetActorTransform();
	const FVector A = FighterA->GetRelativeLocation(), Bp = FighterB->GetRelativeLocation();
	FVector Mid = (A + Bp) * 0.5f + FVector(0, 0, 10.f);   // the miniatures' chests
	const float Sep = FVector::Dist2D(A, Bp);
	float Dist = FMath::Max(115.f, Sep * 1.45f + 75.f);
	FVector Dir(0.f, 1.f, 0.5f);
	if ((Shot.IsEmpty() || Shot == TEXT("wide")) && bActionPanel)
	{
		// Fire or a draw: the stage, both fighters and the flight between them in view (the lid out of frame).
		Mid = (A + Bp) * 0.5f + FVector(0.f, 0.f, 9.f);
		Dir = FVector(0.f, 1.f, 0.52f);
		Dist = FMath::Max(150.f, Sep * 1.5f + 85.f);
	}
	else if (Shot.IsEmpty() || Shot == TEXT("wide"))
	{
		// The whole box, closer than before: the lid's CLASH and plaques at the top of the frame, the fighters readable.
		Mid = FVector(0.f, -8.f, 58.f);
		Dir = FVector(0.f, 1.f, 0.36f);
		Dist = 222.f;
	}
	else if (Shot == TEXT("low")) Dir = FVector(0.f, 1.f, 0.27f);   // low, but over the 10 cm rim's horizon: the frame stays on the stage, not the table beyond
	else if (Shot == TEXT("side")) Dir = FVector(0.75f, 0.65f, 0.35f);
	else if (Shot == TEXT("over_a") || Shot == TEXT("over_b"))
	{
		FVector Away = (Shot == TEXT("over_a") ? A - Bp : Bp - A);
		Away.Z = 0.f;
		Away = Away.GetSafeNormal();
		Dir = FVector(Away.X * 0.8f, FMath::Max(0.45f, Away.Y * 0.8f + 0.5f), 0.4f);
	}
	else if (Shot == TEXT("close_a") || Shot == TEXT("close_b")) { Dir = FVector((Shot == TEXT("close_a") ? -0.25f : 0.25f), 1.f, 0.32f); Dist *= 0.78f; }
	if (bFlipSide && !(Shot.IsEmpty() || Shot == TEXT("wide"))) Dir.X = FMath::Abs(Dir.X) < 0.2f ? -0.3f : -Dir.X;   // after a flip: the mirror angle (never wider than the shot's own, so never outside the box)
	Dir.Y = FMath::Max(Dir.Y, 0.35f);   // always from the front: never behind the lid
	const FVector WantLook = Mid;
	const FVector WantPos = Mid + Dir.GetSafeNormal() * Dist;
	if (!bCamInit) { CamPos = WantPos; CamLook = WantLook; bCamInit = true; }
	const float K = 1.f - FMath::Exp(-2.6f * Dt);
	CamPos = FMath::Lerp(CamPos, WantPos, K);
	CamLook = FMath::Lerp(CamLook, WantLook, K);
	FVector Shake = FVector::ZeroVector;
	if (ShakeLeft > 0.f)
	{
		ShakeLeft -= Dt;
		const float Tm = (float)FPlatformTime::Seconds();
		Shake = FVector(FMath::PerlinNoise1D(Tm * 41.f), FMath::PerlinNoise1D(Tm * 37.f + 3.f), FMath::PerlinNoise1D(Tm * 43.f + 7.f)) * ShakeAmp * FMath::Clamp(ShakeLeft / 0.3f, 0.f, 1.f);
	}
	const FVector Pw = B.TransformPosition(CamPos + Shake), Lw = B.TransformPosition(CamLook);
	Camera->SetActorLocationAndRotation(Pw, (Lw - Pw).Rotation());
}

void AFNDuelBox::Tick(float DeltaSeconds)
{
	using namespace FN7Box;
	Super::Tick(DeltaSeconds);
	if (Phase == EPhase::Hidden || !bBuilt) return;
	PhaseT += DeltaSeconds;
	SetActorHiddenInGame(Phase == EPhase::Rising && PhaseT < 0.f);   // waiting under the table while the portraits slam in
	if (CandleLight)
	{
		const float Tm = (float)FPlatformTime::Seconds();
		const float F = 0.82f + 0.12f * FMath::PerlinNoise1D(Tm * 7.3f) + 0.06f * FMath::PerlinNoise1D(Tm * 23.f + 5.f);
		CandleLight->SetIntensity(9.f * F);
		if (CandleFlame) CandleFlame->SetRelativeScale3D(FVector(0.013f, 0.013f, 0.024f * (0.85f + 0.3f * F)));
	}
	float Rise = 1.f, Lid = 1.f, Draw = 1.f;
	if (Phase == EPhase::Rising)
	{
		Rise = EaseOutBack(FMath::Clamp(PhaseT / RiseTime, 0.f, 1.f));
		Lid = EaseInOut((PhaseT - RiseTime * 0.85f) / OpenTime);
		Draw = EaseInOut((PhaseT - RiseTime - OpenTime * 0.7f) / DrawerTime);
		if (PhaseT > RiseTime + OpenTime + DrawerTime) Phase = EPhase::Open;
	}
	else if (Phase == EPhase::Closing)
	{
		Draw = 1.f - EaseInOut(PhaseT / 0.3f);
		Lid = 1.f - EaseInOut((PhaseT - 0.2f) / 0.7f);
		Rise = 1.f - EaseInOut((PhaseT - 0.9f) / 0.7f);
		if (PhaseT > 1.7f) { Phase = EPhase::Hidden; SetActorHiddenInGame(true); ShownDuel.Reset(); ShownScene = nullptr; DimTable(0.f); bDimsTable = false; return; }
	}
	Lift->SetRelativeLocation(FVector(0.f, 0.f, RiseFrom * (1.f - Rise)));
	Lift->SetRelativeScale3D(FVector(FMath::Lerp(0.72f, 1.f, FMath::Clamp(Rise, 0.f, 1.2f))));
	// Closed, the lid lies over the stage (its face down); open, it stands, leaning back 8 degrees.
	LidHinge->SetRelativeRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(FMath::Lerp(-90.f, 8.f, FMath::Clamp(Lid, 0.f, 1.f)))));
	Drawer->SetRelativeLocation(FVector(0.f, D * 0.5f - 8.f + 10.f * FMath::Clamp(Draw, 0.f, 1.f), 3.2f));
	SceneRoot->SetVisibility(Lid > 0.5f, true);
	if (bDimsTable) DimTable(FMath::Clamp(Phase == EPhase::Closing ? Lid : FMath::Max(Rise, Lid), 0.f, 1.f));
	FighterA->SetVisibility(Lid > 0.5f);
	FighterB->SetVisibility(Lid > 0.5f);
	PlayStage(NowMs ? NowMs() : 0.0, DeltaSeconds);
}

// --- weapons, hammer-space, projectiles, the ground (server CLIENT_API.md "Duel weapons" + schema 17) ----------------

FVector AFNDuelBox::BoneLocal(USkeletalMeshComponent* M, const TCHAR* Bone, const FVector& Fallback) const
{
	if (!M || !Lift || M->GetBoneIndex(FName(Bone)) == INDEX_NONE) return Fallback;
	return Lift->GetComponentTransform().InverseTransformPosition(M->GetBoneLocation(FName(Bone), EBoneSpaces::WorldSpace));
}

void AFNDuelBox::UpdateHeld(FHeld& H, USkeletalMeshComponent* M, double Now)
{
	using namespace FN7Box;
	const FString Class = H.Prop.IsValid() ? Str(H.Prop, TEXT("weapon_class")) : FString();
	const FString Key = Class.IsEmpty() || Class == TEXT("unarmed") ? FString() : Class + TEXT("|") + Str(H.Prop, TEXT("name")) + TEXT("|") + Str(H.Prop, TEXT("material"));
	if (Key != H.Key)
	{
		for (FPropPart& P : H.Parts) if (P.C) P.C->DestroyComponent();
		H.Parts.Reset();
		H.Key = Key;
		H.Class = Class;
		if (Now - H.DrawnAt > 0.6) H.DrawnAt = Now - 1.0;   // taken in hand without a draw: there at once
		if (!Key.IsEmpty())
		{
			const FString Mat = Str(H.Prop, TEXT("material"));
			FLinearColor Metal = Mat.IsEmpty() ? FLinearColor(0.55f, 0.57f, 0.6f) : MaterialColor(Mat);
			if (Str(H.Prop, TEXT("tier")) == TEXT("artifact")) Metal = Metal * 1.6f + FLinearColor(0.08f, 0.06f, 0.02f);   // an artifact gleams
			const FLinearColor Haft = WoodMid * 2.5f, Dark(0.05f, 0.05f, 0.055f);
			auto Add = [&](const TCHAR* Mesh, const FVector& Off, const FVector& Size, const FLinearColor& Col, const FRotator& Rot = FRotator::ZeroRotator)
			{
				FPropPart P;
				P.C = Part(Lift, Mesh, FVector::ZeroVector, Size, Col, Rot);
				P.Off = Off; P.Rot = Rot; P.Size = Size;
				H.Parts.Add(P);
			};
			if (Class == TEXT("one_handed")) { Add(Cube, FVector(4.6f, 0, 0), FVector(7.5f, 0.8f, 0.25f), Metal); Add(Cube, FVector(0.9f, 0, 0), FVector(0.45f, 2.4f, 0.5f), GiltDark * 2.f); Add(Cube, FVector(-0.1f, 0, 0), FVector(1.6f, 0.45f, 0.45f), Haft); }
			else if (Class == TEXT("two_handed")) { Add(Cube, FVector(4.f, 0, 0), FVector(11.f, 0.5f, 0.5f), Haft); Add(Cube, FVector(9.6f, 0, 0), FVector(1.9f, 3.3f, 1.4f), Metal); }
			else if (Class == TEXT("staff")) { Add(Cube, FVector(3.f, 0, 0), FVector(14.f, 0.45f, 0.45f), Haft); Add(Sphere, FVector(10.4f, 0, 0), FVector(1.7f), Metal * 2.5f + FLinearColor(0.2f, 0.1f, 0.35f)); }
			else if (Class == TEXT("bow"))
			{
				// Upright (X), the curve bulging toward the foe (Y), a string behind.
				for (int32 k = -2; k <= 2; ++k)
				{
					const float A = k / 2.f, Bulge = 1.1f * (1.f - A * A);
					Add(Cube, FVector(k * 1.55f, Bulge, 0), FVector(1.7f, 0.4f, 0.45f), Haft, FRotator(FMath::RadiansToDegrees(FMath::Atan(2.2f * A * 1.1f / 3.1f)), 0.f, 0.f));
				}
				Add(Cube, FVector(0, -0.05f, 0), FVector(6.3f, 0.08f, 0.08f), FLinearColor(0.8f, 0.78f, 0.7f));
			}
			else if (Class == TEXT("rifle")) { Add(Cube, FVector(1.6f, 0, 0.3f), FVector(5.5f, 0.8f, 1.0f), Dark); Add(Cube, FVector(5.9f, 0, 0.5f), FVector(4.f, 0.38f, 0.38f), Metal); Add(Cube, FVector(-1.7f, 0, 0), FVector(2.f, 0.8f, 1.3f), Haft); }
			else if (Class == TEXT("launcher")) { Add(Cube, FVector(1.5f, 0, 0), FVector(10.f, 1.7f, 1.7f), Mat.IsEmpty() ? FLinearColor(0.2f, 0.24f, 0.11f) : Metal); Add(Cube, FVector(6.7f, 0, 0), FVector(0.6f, 2.1f, 2.1f), Dark); Add(Cube, FVector(0.4f, 0, -1.4f), FVector(0.7f, 0.5f, 1.4f), Dark); }
			else if (Class == TEXT("thrown")) Add(Sphere, FVector::ZeroVector, FVector(1.4f), MaterialColor(TEXT("stone")) * 1.4f);
			else if (Class == TEXT("shield")) { Add(Cyl, FVector::ZeroVector, FVector(6.2f, 6.2f, 0.5f), Metal, FRotator(90.f, 0.f, 0.f)); Add(Sphere, FVector(0.4f, 0, 0), FVector(1.3f), GiltDark * 2.f); }
			else { Add(Cube, FVector(3.f, 0, 0), FVector(6.f, 0.7f, 0.3f), Metal); }   // anything else: a plain blade
		}
	}
	if (H.Parts.Num() == 0 || !M) return;
	// Where it sits: in the right hand along the forearm (a blade, a gun, a hammer); a staff upright; a bow upright in the
	// left hand; a launcher on the right shoulder; a shield on the left arm, facing the foe.
	const FVector Feet = M->GetRelativeLocation();
	const FVector Fwd = M->GetRelativeRotation().RotateVector(FVector(0, 1, 0)).GetSafeNormal2D();
	const FVector Up(0, 0, 1);
	const float Hm = S * FighterScale;   // cm per metre of the miniature
	const FVector HandR = BoneLocal(M, TEXT("hand_r"), Feet + Up * 1.1f * Hm + Fwd * 0.3f * Hm);
	const FVector HandL = BoneLocal(M, TEXT("hand_l"), Feet + Up * 1.1f * Hm + Fwd * 0.3f * Hm);
	const FVector Elbow = BoneLocal(M, TEXT("lowerarm_r"), HandR - Fwd * 0.3f * Hm);
	FVector Axis = (HandR - Elbow).GetSafeNormal();
	if (Axis.IsNearlyZero()) Axis = Fwd;
	FVector Origin = HandR;
	FRotator R = FRotationMatrix::MakeFromXZ(Axis, Up).Rotator();
	if (H.Class == TEXT("staff")) R = FRotationMatrix::MakeFromXZ((Up + 0.25f * Fwd).GetSafeNormal(), Fwd).Rotator();
	else if (H.Class == TEXT("bow")) { Origin = HandL; R = FRotationMatrix::MakeFromXY(Up, Fwd).Rotator(); }
	else if (H.Class == TEXT("launcher")) { Origin = BoneLocal(M, TEXT("clavicle_r"), Feet + Up * 1.45f * Hm) + Up * 0.12f * Hm; R = FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator(); }
	else if (H.Class == TEXT("shield")) { Origin = HandL + Fwd * 0.1f * Hm; R = FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator(); }
	const float G = FMath::Clamp((float)((Now - H.DrawnAt) / 0.25), 0.f, 1.f);
	const float Grow = G < 1.f ? G * (1.f + 0.25f * FMath::Sin(G * PI)) : 1.f;   // pops in a little past full
	const bool bShow = M->IsVisible();
	for (FPropPart& P : H.Parts)
	{
		if (!P.C) continue;
		P.C->SetRelativeLocation(Origin + R.RotateVector(P.Off * Grow));
		P.C->SetRelativeRotation((R.Quaternion() * P.Rot.Quaternion()).Rotator());
		P.C->SetRelativeScale3D(P.Size / 100.f * FMath::Max(Grow, 0.001f));
		P.C->SetVisibility(bShow && Grow > 0.01f);
	}
}

void AFNDuelBox::Flash(const FVector& At, const FLinearColor& Color, float Size, float Dur, float Lux)
{
	using namespace FN7Box;
	FFx F;
	F.At = At; F.Start = FPlatformTime::Seconds(); F.Dur = Dur; F.Size = Size; F.Lux = Lux;
	F.Parts.Add(Part(Lift, Sphere, At, FVector(0.01f), Color * 2.5f + FLinearColor(0.3f, 0.3f, 0.3f)));
	F.Dirs.Add(FVector::ZeroVector);
	for (int32 i = 0; i < 7; ++i)
	{
		const float A = i * 2.f * PI / 7.f + FMath::FRand() * 0.6f;
		F.Dirs.Add(FVector(FMath::Cos(A), FMath::Sin(A), 0.4f + FMath::FRand() * 0.8f).GetSafeNormal());
		F.Parts.Add(Part(Lift, Sphere, At, FVector(0.01f), Color * 2.f));
	}
	if (Lux > 0.f)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Lift);
		L->SetRelativeLocation(At + FVector(0, 0, 2.f));
		L->SetIntensity(Lux);
		L->SetAttenuationRadius(Size * 14.f);
		L->SetLightColor(Color);
		L->SetCastShadows(false);
		L->RegisterComponent();
		F.Light = L;
	}
	Fxs.Add(F);
}

void AFNDuelBox::TickFx(double Now)
{
	for (int32 i = Fxs.Num() - 1; i >= 0; --i)
	{
		FFx& F = Fxs[i];
		const float U = (float)((Now - F.Start) / FMath::Max(0.05f, F.Dur));
		if (U >= 1.f)
		{
			for (UStaticMeshComponent* C : F.Parts) if (C) C->DestroyComponent();
			if (F.Light) F.Light->DestroyComponent();
			Fxs.RemoveAt(i);
			continue;
		}
		const float Core = F.Size * (U < 0.3f ? U / 0.3f : 1.f - (U - 0.3f) / 0.7f);
		for (int32 k = 0; k < F.Parts.Num(); ++k)
		{
			if (!F.Parts[k]) continue;
			if (k == 0) { F.Parts[k]->SetRelativeScale3D(FVector(FMath::Max(Core, 0.01f) / 100.f)); continue; }
			F.Parts[k]->SetRelativeLocation(F.At + F.Dirs[k] * F.Size * 1.6f * U);
			F.Parts[k]->SetRelativeScale3D(FVector(FMath::Max(F.Size * 0.22f * (1.f - U), 0.01f) / 100.f));
		}
		if (F.Light) F.Light->SetIntensity(F.Lux * (1.f - U));
	}
	// The rocket's blast on its seven hexes: glowing, then gone.
	const float Ua = (float)((Now - AoeAt) / 0.9);
	for (UStaticMeshComponent* T : AoeTiles)
	{
		if (!T) continue;
		T->SetVisibility(Ua >= 0.f && Ua < 1.f);
		if (Ua >= 0.f && Ua < 1.f) T->SetRelativeScale3D(FVector(FN7Box::S * 0.93f / 50.f * (1.f - 0.3f * Ua), FN7Box::S * 0.93f / 50.f * (1.f - 0.3f * Ua), 0.004f * (1.f - Ua) + 0.0005f));
	}
}

void AFNDuelBox::PlayShots(const TSharedPtr<FJsonObject>& Panel, int32 PanelN, double T, bool bLive)
{
	// panel.projectiles[]: each flies from its shooter's hand on an arc (t_fire_ms -> t_hit_ms, arc_h metres high) to the
	// foe, a hex, or a scene object; a deflected one glances off the foe to a hex aside. A function of the panel's
	// time, so a reconnect lands mid-flight. On landing: a burst; a rocket lights its seven hexes and knocks the objects.
	using namespace FN7Box;
	const TArray<TSharedPtr<FJsonValue>>* Ps = Arr(Panel, TEXT("projectiles"));
	if (!Ps) return;
	const float Hm = S * FighterScale;
	const FVector PosA = FighterA ? FighterA->GetRelativeLocation() : FVector::ZeroVector, PosB = FighterB ? FighterB->GetRelativeLocation() : FVector::ZeroVector;
	for (const TSharedPtr<FJsonValue>& Pv : *Ps)
	{
		const TSharedPtr<FJsonObject> P = Pv.IsValid() ? Pv->AsObject() : nullptr;
		if (!P.IsValid()) continue;
		const FString Id = Str(P, TEXT("id")), Kind = Str(P, TEXT("kind")), By = Str(P, TEXT("by"));
		const double T0 = Num(P, TEXT("t_fire_ms")), T1 = FMath::Max(T0 + 1.0, Num(P, TEXT("t_hit_ms")));
		const float ArcH = (float)Num(P, TEXT("arc_h"), 0.3) * Hm;
		USkeletalMeshComponent* Shooter = By == TEXT("b") ? FighterB : FighterA;
		const FVector Feet = By == TEXT("b") ? PosB : PosA;
		FVector From = BoneLocal(Shooter, Kind == TEXT("rocket") ? TEXT("clavicle_r") : TEXT("hand_r"), Feet + FVector(0, 0, 1.2f * Hm));
		double Fh[2];
		if (!Shooter && Vec(P, TEXT("from_hex"), Fh, 2)) From = HexLocal(Fh[0], Fh[1]) + FVector(0, 0, 1.2f * Hm);
		// Where it lands.
		FString ToStr; double Th[2] = { 0, 0 };
		const bool bToHex = Vec(P, TEXT("to_hex"), Th, 2);
		P->TryGetStringField(TEXT("to"), ToStr);
		FVector To = bToHex ? HexLocal(Th[0], Th[1]) + FVector(0, 0, 0.3f * Hm) : From;
		const FVector Foe = (By == TEXT("b") ? PosA : PosB) + FVector(0, 0, 1.25f * Hm);
		if (ToStr == TEXT("a") || ToStr == TEXT("b")) To = (ToStr == TEXT("a") ? PosA : PosB) + FVector(0, 0, 1.25f * Hm);
		else if (!ToStr.IsEmpty())
			if (const TArray<int32>* Op = ObjectParts.Find(ToStr); Op && Op->Num() && SceneParts.IsValidIndex((*Op)[0]) && SceneParts[(*Op)[0]])
				To = SceneParts[(*Op)[0]]->GetRelativeLocation() + FVector(0, 0, 0.2f * Hm);
		FString Defl;
		P->TryGetStringField(TEXT("deflected_by"), Defl);
		auto PathAt = [&](float U) -> FVector
		{
			FVector A = From, B = To;
			float V = U;
			if (!Defl.IsEmpty()) { if (U < 0.55f) { B = Foe; V = U / 0.55f; } else { A = Foe; V = (U - 0.55f) / 0.45f; } }
			FVector X = FMath::Lerp(A, B, V);
			X.Z += 4.f * ArcH * V * (1.f - V) * (Defl.IsEmpty() ? 1.f : 0.5f);
			return X;
		};
		TArray<TObjectPtr<UStaticMeshComponent>>& Parts = ShotParts.FindOrAdd(Id);
		if (T >= T0 && T <= T1)
		{
			if (Parts.Num() == 0)
			{
				// Toy projectiles, readable from the wide shot.
				if (Kind == TEXT("arrow")) { Parts.Add(Part(Lift, Cube, From, FVector(5.f, 0.35f, 0.35f), FLinearColor(0.85f, 0.75f, 0.55f))); Parts.Add(Part(Lift, Cone, From, FVector(0.8f, 0.8f, 1.f), FLinearColor(0.7f, 0.72f, 0.76f))); }
				else if (Kind == TEXT("bullet")) { Parts.Add(Part(Lift, Sphere, From, FVector(0.6f), FLinearColor(1.f, 0.85f, 0.3f) * 3.f)); Parts.Add(Part(Lift, Cube, From, FVector(3.f, 0.12f, 0.12f), FLinearColor(1.f, 0.9f, 0.5f) * 2.f)); }
				else if (Kind == TEXT("rocket")) { Parts.Add(Part(Lift, Cube, From, FVector(2.6f, 0.8f, 0.8f), FLinearColor(0.25f, 0.27f, 0.2f))); Parts.Add(Part(Lift, Sphere, From, FVector(1.1f), FLinearColor(1.f, 0.45f, 0.08f) * 3.f)); }
				else if (Kind == TEXT("orb")) Parts.Add(Part(Lift, Sphere, From, FVector(1.5f), FLinearColor(0.6f, 0.3f, 1.f) * 3.f));
				else if (Kind == TEXT("bolt")) Parts.Add(Part(Lift, Cube, From, FVector(4.f, 0.3f, 0.3f), FLinearColor(0.7f, 0.85f, 1.f) * 4.f));
				else Parts.Add(Part(Lift, Sphere, From, FVector(1.2f), MaterialColor(TEXT("stone")) * 1.5f));   // a rock
			}
			const float U = (float)((T - T0) / (T1 - T0));
			const FVector X = PathAt(U), Ahead = PathAt(FMath::Min(1.f, U + 0.03f));
			const FRotator Rot = (Ahead - X).IsNearlyZero() ? FRotator::ZeroRotator : (Ahead - X).Rotation();
			for (int32 k = 0; k < Parts.Num(); ++k)
			{
				if (!Parts[k]) continue;
				const float Back = (Kind == TEXT("arrow") && k == 1) ? 2.6f : (Kind == TEXT("bullet") && k == 1) ? -1.6f : (Kind == TEXT("rocket") && k == 1) ? -1.7f : 0.f;
				Parts[k]->SetRelativeLocation(X + Rot.Vector() * Back);
				Parts[k]->SetRelativeRotation(Kind == TEXT("arrow") && k == 1 ? (Rot.Quaternion() * FRotator(-90.f, 0.f, 0.f).Quaternion()).Rotator() : Rot);
				Parts[k]->SetVisibility(true);
				if (Kind == TEXT("rocket") && k == 1) Parts[k]->SetRelativeScale3D(FVector(1.1f / 100.f * (0.8f + 0.4f * FMath::FRand())));
			}
			if (!Defl.IsEmpty() && U >= 0.55f && !ShotsBurst.Contains(Id + TEXT(":d")))
			{
				ShotsBurst.Add(Id + TEXT(":d"));
				if (bLive) Flash(Foe, FLinearColor(1.f, 0.9f, 0.5f), 2.2f, 0.3f, 10.f);   // the glance off a parry / shield
			}
		}
		else for (UStaticMeshComponent* C : Parts) if (C) C->SetVisibility(false);
		if (T >= T1 && !ShotsBurst.Contains(Id))
		{
			ShotsBurst.Add(Id);
			const TSharedPtr<FJsonObject> Hit = Obj(P, TEXT("on_hit"));
			bool bMiss = false;
			P->TryGetBoolField(TEXT("miss"), bMiss);
			const FVector At = PathAt(1.f);
			const bool bFresh = bLive && T - T1 < 900.0;
			if (Kind == TEXT("rocket"))
			{
				if (bFresh) Flash(At, FLinearColor(1.f, 0.5f, 0.1f), 9.f, 0.75f, 90.f);
				for (UStaticMeshComponent* Tl : AoeTiles) if (Tl) Tl->DestroyComponent();
				AoeTiles.Reset();
				if (const TArray<TSharedPtr<FJsonValue>>* Aoe = Arr(Hit, TEXT("aoe_hexes")))
					for (const TSharedPtr<FJsonValue>& Hv : *Aoe)
					{
						const TArray<TSharedPtr<FJsonValue>>* Qr = nullptr;
						if (!Hv.IsValid() || !Hv->TryGetArray(Qr) || Qr->Num() < 2) continue;
						UStaticMeshComponent* Tl = Part(Lift, HexMesh, HexLocal((*Qr)[0]->AsNumber(), (*Qr)[1]->AsNumber()) + FVector(0, 0, 0.15f), FVector(S * 0.93f * 2.f, S * 0.93f * 2.f, 0.4f), FLinearColor(1.f, 0.3f, 0.03f) * 1.6f);
						Tl->SetRelativeScale3D(FVector(S * 0.93f / 50.f, S * 0.93f / 50.f, 0.004f));
						AoeTiles.Add(Tl);
					}
				AoeAt = bFresh ? FPlatformTime::Seconds() : -10.0;
				if (const TArray<TSharedPtr<FJsonValue>>* Objs = Arr(Hit, TEXT("objects")))
					for (const TSharedPtr<FJsonValue>& Ov : *Objs)
					{
						const TSharedPtr<FJsonObject> O = Ov.IsValid() ? Ov->AsObject() : nullptr;
						const FString Oid = Str(O, TEXT("id"));
						const TArray<int32>* Op = ObjectParts.Find(Oid);
						if (!Op || !Op->Num() || !SceneBase.IsValidIndex((*Op)[0])) continue;
						FVector Dir = SceneBase[(*Op)[0]].GetLocation() - At;
						Dir.Z = 0.f;
						Knocks.Add(Oid, { PanelN, T1, (float)Num(O, TEXT("impulse"), 0.5), Dir.GetSafeNormal() });
					}
			}
			else if (bFresh)
			{
				const FLinearColor Cc = Kind == TEXT("orb") ? FLinearColor(0.6f, 0.3f, 1.f) : Kind == TEXT("bolt") ? FLinearColor(0.7f, 0.85f, 1.f) : Kind == TEXT("bullet") ? FLinearColor(1.f, 0.85f, 0.4f) : bMiss ? FLinearColor(0.5f, 0.45f, 0.4f) : FLinearColor(1.f, 0.95f, 0.85f);
				Flash(At, Cc, (Kind == TEXT("orb") || Kind == TEXT("bolt")) ? 4.f : 2.4f, 0.4f, (Kind == TEXT("orb") || Kind == TEXT("bolt")) ? 35.f : 0.f);
			}
			// A miss on a scene object knocks it a little.
			if (Kind != TEXT("rocket") && bMiss && !ToStr.IsEmpty() && ToStr != TEXT("a") && ToStr != TEXT("b"))
				if (const TArray<int32>* Op = ObjectParts.Find(ToStr); Op && Op->Num() && SceneBase.IsValidIndex((*Op)[0]))
				{
					FVector Dir = SceneBase[(*Op)[0]].GetLocation() - From;
					Dir.Z = 0.f;
					Knocks.Add(ToStr, { PanelN, T1, FMath::Clamp((float)Num(Hit, TEXT("impulse"), 0.3) * 0.6f, 0.15f, 0.5f), Dir.GetSafeNormal() });
				}
			UE_LOG(LogTemp, Display, TEXT("FN7 duel box: %s %s %s at %.0f ms%s"), *Kind, *Id, bMiss ? TEXT("misses") : !Defl.IsEmpty() ? TEXT("is deflected") : TEXT("hits"), T1, Kind == TEXT("rocket") ? *FString::Printf(TEXT(" (%d hexes, %d objects knocked)"), AoeTiles.Num(), Knocks.Num()) : TEXT(""));
		}
	}
}

void AFNDuelBox::ApplyKnocks(int32 PanelN, double T)
{
	// Knocked objects jump, slide away from the blast and topple; they stay where they land for the rest of the duel.
	using namespace FN7Box;
	for (const TPair<FString, FKnock>& K : Knocks)
	{
		const TArray<int32>* Op = ObjectParts.Find(K.Key);
		if (!Op) continue;
		float P = 1.f;
		if (PanelN == K.Value.Panel) P = (float)((T - K.Value.T1) / 600.0);
		else if (PanelN < K.Value.Panel) P = -1.f;
		if (P < 0.f) continue;
		P = FMath::Clamp(P, 0.f, 1.f);
		const float Imp = FMath::Clamp(K.Value.Impulse, 0.f, 1.f);
		const FVector Slide = K.Value.Dir * Imp * 0.9f * 1.732f * S * EaseInOut(P);
		const float Jump = Imp * 2.2f * S * FMath::Sin(P * PI);
		const float Tilt = Imp * 75.f * EaseInOut(P);
		FVector Pivot = FVector::ZeroVector;
		for (int32 Idx : *Op) if (SceneBase.IsValidIndex(Idx)) Pivot += SceneBase[Idx].GetLocation();
		Pivot /= FMath::Max(1, Op->Num());
		Pivot.Z = FloorTop;
		const FVector Ax = FVector::CrossProduct(FVector::UpVector, K.Value.Dir).GetSafeNormal();
		const FQuat Q = Ax.IsNearlyZero() ? FQuat::Identity : FQuat(Ax, FMath::DegreesToRadians(Tilt));
		for (int32 Idx : *Op)
		{
			if (!SceneBase.IsValidIndex(Idx) || !SceneParts.IsValidIndex(Idx) || !SceneParts[Idx]) continue;
			const FTransform& B = SceneBase[Idx];
			SceneParts[Idx]->SetRelativeLocationAndRotation(Pivot + Q.RotateVector(B.GetLocation() - Pivot) + Slide + FVector(0, 0, Jump), Q * B.GetRotation());
		}
	}
}

void AFNDuelBox::BuildGround(const TSharedPtr<FJsonObject>& Ground)
{
	// The earth underfoot (schema 17): the floor shows through as toy patches between the flagstones (the hazard
	// objects themselves come with the scene's objects); lava and fire glow, a shaken floor trembles.
	using namespace FN7Box;
	for (UStaticMeshComponent* P : GroundPatches) if (P) P->DestroyComponent();
	GroundPatches.Reset();
	GroundFloor = Ground.IsValid() ? Str(Ground, TEXT("floor")) : FString();
	if (GroundLight) GroundLight->SetIntensity(0.f);
	if (GroundFloor.IsEmpty() || GroundFloor == TEXT("shaken")) return;
	static const TMap<FString, FLinearColor> Floor = {
		{ TEXT("lava"), FLinearColor(0.85f, 0.14f, 0.01f) * 1.3f }, { TEXT("burning"), FLinearColor(0.07f, 0.035f, 0.02f) },   // deep orange: brighter washes to yellow
		{ TEXT("frozen"), FLinearColor(0.72f, 0.86f, 0.98f) }, { TEXT("flooded"), FLinearColor(0.08f, 0.25f, 0.6f) },
		{ TEXT("rift"), FLinearColor(0.03f, 0.01f, 0.01f) }, { TEXT("cracked"), FLinearColor(0.06f, 0.055f, 0.05f) },
		{ TEXT("snow"), FLinearColor(0.95f, 0.96f, 1.f) }, { TEXT("ash"), FLinearColor(0.22f, 0.21f, 0.2f) },
		{ TEXT("ruins"), FLinearColor(0.3f, 0.24f, 0.17f) } };
	const FLinearColor* Col = Floor.Find(GroundFloor);
	if (!Col) return;
	const uint32 Seed = GetTypeHash(GroundFloor);
	for (int32 q = -StageRadius; q <= StageRadius; ++q)
		for (int32 r = -StageRadius; r <= StageRadius; ++r)
		{
			if (FMath::Abs(q + r) > StageRadius) continue;
			const float Hh = FMath::Frac(FMath::Sin((float)(q * 12.9898 + r * 78.233 + Seed % 97)) * 43758.5453f);
			if (Hh > 0.33f) continue;
			FVector C = HexLocal(q, r);
			if (FMath::Abs(C.X) > W * 0.5f - RimT - S * 0.9f || FMath::Abs(C.Y) > D * 0.5f - RimT - S * 0.8f) continue;
			C.Z = FloorTop + 0.05f;
			const float Rr = S * 0.93f * (0.55f + 0.4f * Hh / 0.33f);
			UStaticMeshComponent* P = Part(SceneRoot, HexMesh, C, FVector(Rr * 2.f, Rr * 2.f, 0.3f), *Col);
			P->SetRelativeScale3D(FVector(Rr / 50.f, Rr / 50.f, 0.003f));
			GroundPatches.Add(P);
		}
	if ((GroundFloor == TEXT("lava") || GroundFloor == TEXT("burning")) && !GroundLight)
	{
		GroundLight = NewObject<UPointLightComponent>(this);
		GroundLight->SetupAttachment(Lift);
		GroundLight->SetRelativeLocation(FVector(0.f, 0.f, FloorTop + 12.f));
		GroundLight->SetAttenuationRadius(120.f);
		GroundLight->SetLightColor(FLinearColor(1.f, 0.45f, 0.12f));
		GroundLight->SetCastShadows(false);
		GroundLight->RegisterComponent();
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 duel box: the ground is %s (%d patches)"), *GroundFloor, GroundPatches.Num());
}

void AFNDuelBox::ClearWeapons()
{
	for (FHeld* H : { &HeldA, &HeldB })
	{
		for (FPropPart& P : H->Parts) if (P.C) P.C->DestroyComponent();
		H->Parts.Reset(); H->Key.Reset(); H->Class.Reset(); H->Prop.Reset(); H->DrawnAt = -10.0;
	}
	for (TPair<FString, TArray<TObjectPtr<UStaticMeshComponent>>>& Sp : ShotParts) for (UStaticMeshComponent* C : Sp.Value) if (C) C->DestroyComponent();
	ShotParts.Reset();
	ShotsBurst.Reset();
	DrawsSeen.Reset();
	for (FFx& F : Fxs) { for (UStaticMeshComponent* C : F.Parts) if (C) C->DestroyComponent(); if (F.Light) F.Light->DestroyComponent(); }
	Fxs.Reset();
	for (UStaticMeshComponent* T : AoeTiles) if (T) T->DestroyComponent();
	AoeTiles.Reset();
	Knocks.Reset();
	ObjectParts.Reset();
	SceneBase.Reset();
}
