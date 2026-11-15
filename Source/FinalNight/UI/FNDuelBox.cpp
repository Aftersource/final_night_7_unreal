#include "FNDuelBox.h"

#include "FNDuelStage.h"
#include "FNStudio.h"
#include "SFNDuelTray.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
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

	static const FLinearColor WoodDark(0.05f, 0.028f, 0.014f);
	static const FLinearColor WoodMid(0.1f, 0.058f, 0.027f);
	static const FLinearColor WoodLight(0.17f, 0.1f, 0.045f);
	static const FLinearColor Gilt(0.78f, 0.56f, 0.2f);
	static const FLinearColor GiltDark(0.24f, 0.155f, 0.05f);
	static const FLinearColor LidFace(0.034f, 0.019f, 0.009f);   // the spots hit the lid square on: it needs the darkest wood
	static const FLinearColor TileA(0.13f, 0.115f, 0.075f), TileB(0.095f, 0.082f, 0.052f);   // the table's spots wash flat tops pale
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

	// The hex stage: every hex within the radius, two tones, three crossed boxes per hex (instanced).
	{
		UInstancedStaticMeshComponent* Tiles[2];
		for (int32 k = 0; k < 2; ++k)
		{
			UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
			I->SetupAttachment(Lift);
			I->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Cube));
			I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (BaseMat)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, I);
				M->SetVectorParameterValue(TEXT("Color"), k ? TileB : TileA);
				I->SetMaterial(0, M);
			}
			I->RegisterComponent();
			Tiles[k] = I;
		}
		const float R = S * 0.95f;   // hex circumradius in cm, a hair of gap
		for (int32 q = -StageRadius; q <= StageRadius; ++q)
			for (int32 r = -StageRadius; r <= StageRadius; ++r)
			{
				if (FMath::Abs(q + r) > StageRadius) continue;
				FVector C = HexLocal(q, r);
				if (FMath::Abs(C.X) > W * 0.5f - RimT - S * 0.9f || FMath::Abs(C.Y) > D * 0.5f - RimT - S * 0.8f) continue;
				C.Z = SlabH + TileH * 0.5f;
				const int32 Tone = ((q - r) % 3 + 3) % 3 == 0 ? 1 : 0;
				for (int32 k = 0; k < 3; ++k)
					Tiles[Tone]->AddInstance(FTransform(FRotator(0.f, 30.f + 60.f * k, 0.f), C, FVector(R * 1.7320508f, R, TileH) / 100.f));
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
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(Lift);
	L->SetRelativeLocation(FVector(0.f, 40.f, 70.f));
	L->SetIntensity(14.f);   // the studio's scale of light (its spots run 9-10): the wood stays dark, the skins read
	L->SetAttenuationRadius(220.f);
	L->SetLightColor(FLinearColor(1.f, 0.85f, 0.65f));
	L->SetCastShadows(false);
	L->RegisterComponent();
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

USkeletalMeshComponent* AFNDuelBox::Fighter(const FString& Skin)
{
	USkeletalMeshComponent* M = NewObject<USkeletalMeshComponent>(this);
	M->SetupAttachment(Lift);
	M->RegisterComponent();
	M->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, FN7Box::MeshPath));
	M->SetRelativeScale3D(FVector(FN7Box::S / 100.f));   // 1.7 m of mannequin = 1.7 m of stage
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
	SetSky(Str(Scene, TEXT("time_of_day")));
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
			SceneParts.Add(Part(SceneRoot, Mesh, Origin - FVector(0, 0, FloorTop - SlabH - TileH) + LocalOff, Size * S, Col, PartRot));
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
	if (Phase == EPhase::Hidden || Phase == EPhase::Closing) { Phase = EPhase::Rising; PhaseT = 0.f; }

	if (bTakeCamera && !bTookCamera && Camera)
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			OldViewTarget = PC->GetViewTarget();
			bCamInit = false;
			UpdateCamera(TEXT("wide"), 1.f);
			PC->SetViewTargetWithBlend(Camera, 1.1f, VTBlend_EaseInOut, 2.f);
			bTookCamera = true;
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
	struct FPose { FVector At; FString Clip; float ClipT = 0.f; bool bLoop = true; };
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
				return Out;
			}
			From = Dest;
			Acc += Ms;
		}
		Out.At = From;
		return Out;
	};
	const FPose Pa = Track(TEXT("a"), StartA), Pb = Track(TEXT("b"), StartB);
	PoseFighter(FighterA, ClipA, Pa.Clip, Pa.ClipT, Pa.bLoop, Pa.At, Pb.At);
	PoseFighter(FighterB, ClipB, Pb.Clip, Pb.ClipT, Pb.bLoop, Pb.At, Pa.At);

	// Contacts: the burst word, a shake; the bars move to the panel's hp after its last contact.
	if (Panel.IsValid())
	{
		const bool bFresh = Cur != LastPanel;
		const double From = bFresh ? -1.0 : LastPanelT;
		const bool bLive = !bFresh || T < 800.0;   // a reconnect into an old panel doesn't replay its bursts
		double LastContact = -1.0;
		if (const TArray<TSharedPtr<FJsonValue>>* Cs = Arr(Panel, TEXT("contacts")))
			for (const TSharedPtr<FJsonValue>& Cv : *Cs)
			{
				const TSharedPtr<FJsonObject> C = Cv.IsValid() ? Cv->AsObject() : nullptr;
				const double Tm = Num(C, TEXT("t_ms"), -1.0);
				LastContact = FMath::Max(LastContact, Tm);
				if (!bLive || Tm <= From || Tm > T) continue;
				const FString On = Str(C, TEXT("on"));
				BurstAt = (On == TEXT("a") ? Pa.At : On == TEXT("b") ? Pb.At : (Pa.At + Pb.At) * 0.5f) + FVector(0, 0, 12.f);
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
		Caption = Cap;
		LastPanel = Cur;
		LastPanelT = T;
	}
	SetHpBars(ShownHpA, ShownHpB);

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

	UpdateCamera(Panel.IsValid() ? Str(Panel, TEXT("shot")) : FString(TEXT("wide")), Dt);
}

void AFNDuelBox::UpdateCamera(const FString& Shot, float Dt)
{
	if (!Camera || !FighterA || !FighterB) return;
	const FTransform B = GetActorTransform();
	const FVector A = FighterA->GetRelativeLocation(), Bp = FighterB->GetRelativeLocation();
	FVector Mid = (A + Bp) * 0.5f + FVector(0, 0, 6.f);
	const float Sep = FVector::Dist2D(A, Bp);
	float Dist = FMath::Max(95.f, Sep * 1.5f + 60.f);
	FVector Dir(0.f, 1.f, 0.5f);
	if (Shot.IsEmpty() || Shot == TEXT("wide"))
	{
		// The whole box, as in the concept: the lid (CLASH, the sky, the two plaques) over the stage.
		Mid = FVector(0.f, -16.f, 70.f);
		Dir = FVector(0.f, 1.f, 0.3f);
		Dist = 265.f;
	}
	else if (Shot == TEXT("low")) Dir = FVector(0.f, 1.f, 0.16f);
	else if (Shot == TEXT("side")) Dir = FVector(0.75f, 0.65f, 0.35f);
	else if (Shot == TEXT("over_a") || Shot == TEXT("over_b"))
	{
		FVector Away = (Shot == TEXT("over_a") ? A - Bp : Bp - A);
		Away.Z = 0.f;
		Away = Away.GetSafeNormal();
		Dir = FVector(Away.X * 0.8f, FMath::Max(0.45f, Away.Y * 0.8f + 0.5f), 0.4f);
	}
	else if (Shot == TEXT("close_a") || Shot == TEXT("close_b")) { Dir = FVector((Shot == TEXT("close_a") ? -0.25f : 0.25f), 1.f, 0.32f); Dist *= 0.78f; }
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
		if (PhaseT > 1.7f) { Phase = EPhase::Hidden; SetActorHiddenInGame(true); ShownDuel.Reset(); ShownScene = nullptr; return; }
	}
	Lift->SetRelativeLocation(FVector(0.f, 0.f, RiseFrom * (1.f - Rise)));
	Lift->SetRelativeScale3D(FVector(FMath::Lerp(0.72f, 1.f, FMath::Clamp(Rise, 0.f, 1.2f))));
	// Closed, the lid lies over the stage (its face down); open, it stands, leaning back 8 degrees.
	LidHinge->SetRelativeRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(FMath::Lerp(-90.f, 8.f, FMath::Clamp(Lid, 0.f, 1.f)))));
	Drawer->SetRelativeLocation(FVector(0.f, D * 0.5f - 8.f + 10.f * FMath::Clamp(Draw, 0.f, 1.f), 3.2f));
	SceneRoot->SetVisibility(Lid > 0.5f, true);
	FighterA->SetVisibility(Lid > 0.5f);
	FighterB->SetVisibility(Lid > 0.5f);
	PlayStage(NowMs ? NowMs() : 0.0, DeltaSeconds);
}
