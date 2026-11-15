#include "FNDuelStage.h"

#include "FNStudio.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"

namespace FN7Duel
{
	static const TCHAR* MeshPath = TEXT("/Game/Duel/Mannequin/SK_DuelMannequin.SK_DuelMannequin");
	static constexpr float ParadeScale = 0.11f;       // 1.7 m tall -> about 19 cm: a table miniature you can read
	static constexpr float ParadeGap = 27.f;          // cm between two figures of the parade
	static const FVector ParadeAt(0.f, 70.f, 90.f);   // studio space: the table's centre of action, above the hexes
	static constexpr float ParadeNear = 55.f;         // cm from there toward the camera: the board's near edge, clear of the cards
}

AFNDuelStage::AFNDuelStage()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AFNDuelStage* AFNDuelStage::Get(UWorld* World, bool bCreate)
{
	if (!World) return nullptr;
	TActorIterator<AFNDuelStage> It(World);
	if (It) return *It;
	return bCreate ? World->SpawnActor<AFNDuelStage>() : nullptr;
}

const TArray<FString>& AFNDuelStage::ClipNames()
{
	// The lab's 35, the 15 strikes and the 14 weapon moves on the same rig (Content/Duel/Mannequin/Anims, 64 clips).
	static const TArray<FString> Names = {
		TEXT("airborne_hit"), TEXT("axe_spin"), TEXT("blade_spin"), TEXT("block"), TEXT("block_cross"), TEXT("bow"),
		TEXT("bow_aim_walk"), TEXT("bow_draw_shoot"), TEXT("bow_quick_shot"), TEXT("combo_triple"), TEXT("command"), TEXT("counter"),
		TEXT("defeat"), TEXT("dodge_roll_back"), TEXT("downed"), TEXT("draw_from_nowhere"), TEXT("elbow_strike"), TEXT("evade_roll"),
		TEXT("evade_spin"), TEXT("evade_stand"), TEXT("flying_kick"), TEXT("grapple_throw_down"), TEXT("ground_slam"), TEXT("gun_quick_draw"),
		TEXT("gun_reload"), TEXT("gun_roll_fire"), TEXT("gun_run_shoot"), TEXT("gun_side_shot"), TEXT("heavy_charged"), TEXT("heavy_hammer"),
		TEXT("high_kick"), TEXT("idle_guard"), TEXT("invoke"), TEXT("jab"), TEXT("jump_attack"), TEXT("kick"),
		TEXT("knee_strike"), TEXT("kneel"), TEXT("launcher_fire"), TEXT("leg_sweep"), TEXT("lunge"), TEXT("mage_cast"),
		TEXT("parry_back"), TEXT("parry_high"), TEXT("punch_combo"), TEXT("rise"), TEXT("roundhouse_kick"), TEXT("run"),
		TEXT("shield_push"), TEXT("spin_rise"), TEXT("stagger_back"), TEXT("stagger_waist"), TEXT("strike_side"), TEXT("strike_slash"),
		TEXT("sword_judgment"), TEXT("taunt"), TEXT("throw_object"), TEXT("throw_shoulder"), TEXT("twohand_chop"), TEXT("twohand_reap"),
		TEXT("twohand_stance"), TEXT("uppercut"), TEXT("victory"), TEXT("walk") };
	return Names;
}

FString AFNDuelStage::ClipFallback(const FString& Clip)
{
	// The server's clip_fallbacks (production_assets/meshy/mannequin/provenance.json, step 6), plus the weapon moves.
	static const TMap<FString, FString> F = {
		{ TEXT("flying_kick"), TEXT("kick") }, { TEXT("roundhouse_kick"), TEXT("kick") }, { TEXT("high_kick"), TEXT("kick") },
		{ TEXT("leg_sweep"), TEXT("kick") }, { TEXT("knee_strike"), TEXT("kick") }, { TEXT("elbow_strike"), TEXT("strike_side") },
		{ TEXT("uppercut"), TEXT("strike_side") }, { TEXT("jab"), TEXT("strike_side") }, { TEXT("punch_combo"), TEXT("combo_triple") },
		{ TEXT("blade_spin"), TEXT("strike_slash") }, { TEXT("axe_spin"), TEXT("heavy_hammer") }, { TEXT("spin_rise"), TEXT("rise") },
		{ TEXT("mage_cast"), TEXT("invoke") }, { TEXT("dodge_roll_back"), TEXT("evade_roll") }, { TEXT("sword_judgment"), TEXT("heavy_charged") },
		{ TEXT("bow_draw_shoot"), TEXT("invoke") }, { TEXT("bow_quick_shot"), TEXT("invoke") }, { TEXT("bow_aim_walk"), TEXT("walk") },
		{ TEXT("gun_side_shot"), TEXT("strike_side") }, { TEXT("gun_run_shoot"), TEXT("run") }, { TEXT("gun_quick_draw"), TEXT("command") },
		{ TEXT("gun_roll_fire"), TEXT("evade_roll") }, { TEXT("gun_reload"), TEXT("idle_guard") }, { TEXT("launcher_fire"), TEXT("heavy_charged") },
		{ TEXT("twohand_chop"), TEXT("heavy_hammer") }, { TEXT("twohand_reap"), TEXT("strike_slash") }, { TEXT("twohand_stance"), TEXT("idle_guard") },
		{ TEXT("throw_object"), TEXT("throw_shoulder") }, { TEXT("draw_from_nowhere"), TEXT("command") } };
	const FString* V = F.Find(Clip);
	return V ? *V : FString(TEXT("idle_guard"));
}

const TArray<FString>& AFNDuelStage::SkinNames()
{
	static const TArray<FString> Names = { TEXT("wood"), TEXT("obsidian"), TEXT("gold"), TEXT("dragonscale"), TEXT("butterfly") };
	return Names;
}

int32 AFNDuelStage::SkinForName(const FString& DemigodName)
{
	// As the lab's fnv (duel_lab/lib/m.js) over TextEncoder bytes: offset basis 2166136261, prime 16777619. Lower-casing
	// is ASCII-only, as the server's (A-Z only; other characters as written), so every client picks the same skin.
	FString Key = DemigodName;
	for (TCHAR& Ch : Key) if (Ch >= TEXT('A') && Ch <= TEXT('Z')) Ch = Ch - TEXT('A') + TEXT('a');
	const FTCHARToUTF8 Utf8(*Key);
	uint32 H = 2166136261u;
	for (int32 i = 0; i < Utf8.Length(); ++i)
	{
		H ^= (uint8)Utf8.Get()[i];
		H *= 16777619u;
	}
	return (int32)(H % (uint32)SkinNames().Num());
}

void AFNDuelStage::StartParade(AFNStudio* Studio)
{
	StopParade();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, FN7Duel::MeshPath);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("FN7 duel: no mannequin at %s"), FN7Duel::MeshPath);
		return;
	}
	Clips.Reset();
	int32 Missing = 0;
	for (const FString& C : ClipNames())
	{
		const FString P = FString::Printf(TEXT("/Game/Duel/Mannequin/Anims/A_%s.A_%s"), *C, *C);
		UAnimSequence* A = LoadObject<UAnimSequence>(nullptr, *P);
		Clips.Add(A);   // the slot stays (null when missing) so indices follow the vocabulary
		if (!A) { ++Missing; UE_LOG(LogTemp, Warning, TEXT("FN7 duel: missing clip %s"), *P); }
	}

	// A row over the centre of action, every figure turned to the camera.
	const FTransform StudioT = Studio ? Studio->GetActorTransform() : FTransform::Identity;
	const FVector Action = StudioT.TransformPosition(FN7Duel::ParadeAt);
	FVector Cam = StudioT.TransformPosition(FVector(0.f, 400.f, 160.f));
	if (APlayerCameraManager* Pcm = UGameplayStatics::GetPlayerCameraManager(this, 0)) Cam = Pcm->GetCameraLocation();
	const FVector ToCam = (Cam - Action).GetSafeNormal2D();
	const FVector Centre = Action + ToCam * FN7Duel::ParadeNear;
	const FVector Along = FVector::CrossProduct(FVector::UpVector, ToCam).GetSafeNormal();
	const float CamYaw = ToCam.Rotation().Yaw;
	SetActorLocation(Centre);

	const TArray<FString>& Skins = SkinNames();
	int32 MissingSkins = 0;
	for (int32 i = 0; i < Skins.Num(); ++i)
	{
		const FVector At = Centre + Along * ((float)i - (Skins.Num() - 1) * 0.5f) * FN7Duel::ParadeGap;
		USkeletalMeshComponent* M = NewObject<USkeletalMeshComponent>(this);
		M->SetupAttachment(RootComponent);
		M->RegisterComponent();
		M->SetSkeletalMesh(Mesh);
		const FString MiPath = FString::Printf(TEXT("/Game/Duel/Skins/MI_Skin_%s.MI_Skin_%s"), *Skins[i], *Skins[i]);
		if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, *MiPath))
			for (int32 s = 0; s < M->GetNumMaterials(); ++s) M->SetMaterial(s, Mi);
		else
			++MissingSkins;
		M->SetWorldLocation(At);
		BaseAt.Add(At);
		M->SetWorldRotation(FRotator(0.f, CamYaw - 90.f, 0.f));   // the mesh faces +Y (arms along X): +Y to the camera
		M->SetWorldScale3D(FVector(FN7Duel::ParadeScale));
		M->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Meshes.Add(M);

		UTextRenderComponent* L = NewObject<UTextRenderComponent>(this);
		L->SetupAttachment(RootComponent);
		L->RegisterComponent();
		L->SetWorldLocation(At + FVector(0.f, 0.f, 170.f * FN7Duel::ParadeScale + 5.f));
		L->SetWorldRotation(FRotator(0.f, CamYaw, 0.f));
		L->SetHorizontalAlignment(EHTA_Center);
		L->SetWorldSize(3.6f);
		L->SetTextRenderColor(FColor(255, 232, 176));
		Labels.Add(L);

		ClipOf.Add(0);
		ClipT.Add(0.f);
		PlayClip(i, i * 7);   // staggered, so the five show different clips; each still walks the whole list
	}
	// The pelvis in the reference pose, composed up the bone chain (component space).
	{
		const FReferenceSkeleton& Rs = Mesh->GetRefSkeleton();
		const int32 Pelvis = Rs.FindBoneIndex(TEXT("pelvis"));
		FTransform T = FTransform::Identity;
		for (int32 b = Pelvis; b != INDEX_NONE; b = Rs.GetParentIndex(b)) T = T * Rs.GetRefBonePose()[b];
		RefPelvis = T.GetLocation();
		bHaveRefPelvis = Pelvis != INDEX_NONE;
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 duel parade: %d skins x %d clips (%d clips missing, %d skins missing), pelvis ref %s"),
		Skins.Num(), Clips.Num(), Missing, MissingSkins, *RefPelvis.ToString());
}

void AFNDuelStage::StopParade()
{
	for (USkeletalMeshComponent* M : Meshes) if (M) M->DestroyComponent();
	for (UTextRenderComponent* L : Labels) if (L) L->DestroyComponent();
	Meshes.Reset();
	Labels.Reset();
	ClipOf.Reset();
	ClipT.Reset();
	BaseAt.Reset();
}

void AFNDuelStage::StripPelvisTravel(int32 Figure)
{
	if (!bHaveRefPelvis || !Meshes.IsValidIndex(Figure) || !BaseAt.IsValidIndex(Figure)) return;
	USkeletalMeshComponent* M = Meshes[Figure];
	FVector Drift = M->GetBoneLocation(TEXT("pelvis"), EBoneSpaces::ComponentSpace) - RefPelvis;
	Drift.Z = 0.f;
	M->SetWorldLocation(BaseAt[Figure] - M->GetComponentTransform().TransformVector(Drift));
}

void AFNDuelStage::PlayClip(int32 Figure, int32 Clip)
{
	if (!Meshes.IsValidIndex(Figure) || Clips.Num() == 0) return;
	Clip = ((Clip % Clips.Num()) + Clips.Num()) % Clips.Num();
	for (int32 k = 0; k < Clips.Num() && !Clips[Clip]; ++k) Clip = (Clip + 1) % Clips.Num();   // skip a missing one
	ClipOf[Figure] = Clip;
	ClipT[Figure] = 0.f;
	if (UAnimSequence* A = Clips[Clip]) Meshes[Figure]->PlayAnimation(A, false);
	if (Labels.IsValidIndex(Figure))
		Labels[Figure]->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *SkinNames()[Figure % SkinNames().Num()], *ClipNames()[Clip])));
}

void AFNDuelStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (int32 i = 0; i < Meshes.Num(); ++i)
	{
		ClipT[i] += DeltaSeconds;
		const UAnimSequence* A = Clips.IsValidIndex(ClipOf[i]) ? Clips[ClipOf[i]].Get() : nullptr;
		const float Len = A ? FMath::Max(0.3f, A->GetPlayLength()) : 1.f;
		if (ClipT[i] >= Len + 0.15f) PlayClip(i, ClipOf[i] + 1);
		StripPelvisTravel(i);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFN7DuelDemo(TEXT("FN7.DuelDemo"),
	TEXT("FN7.DuelDemo [off]: the duel mannequin in all five skins on the table, each cycling every clip (DUEL_UNREAL.md step A)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFNDuelStage* Stage = AFNDuelStage::Get(World, true);
		if (!Stage) return;
		if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)) { Stage->StopParade(); return; }
		TActorIterator<AFNStudio> It(World);
		AFNStudio* Studio = It ? *It : nullptr;
		Stage->StartParade(Studio);
	}));
