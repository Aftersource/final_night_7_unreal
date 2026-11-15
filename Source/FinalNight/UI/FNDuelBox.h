#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNDuelBox.generated.h"

struct FFNDuelSession;
class AFNStudio;
class ACameraActor;
class UAnimSequence;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;
class FJsonObject;

/**
 * The challenge box (DUEL_UNREAL.md section 1, steps B and C; concept docs/design/challenge_box_concept_v1.png).
 * A carved wooden box rises onto the table in front of the globe and opens like a clamshell: the lid is the backdrop
 * (a sky for the place and time of day, a gilded CLASH plaque, the two portraits as plaques with health bars), the
 * base a hex stage with the duel's scene (duel.scene parts) and the two mannequins in their skins playing the panels
 * (duel.panel segs on the server's clock, contacts with burst words and shakes). A front drawer shows the stars. Its
 * own camera frames both fighters by the panel's shot; the player's view glides onto it and back.
 * Everything on stage is a function of the server time, so a reconnect lands mid-panel.
 */
UCLASS()
class FINALNIGHT_API AFNDuelBox : public AActor
{
	GENERATED_BODY()

public:
	AFNDuelBox();
	static AFNDuelBox* Get(UWorld* World, bool bCreate);

	void Open(const TSharedPtr<FFNDuelSession>& Session, AFNStudio* Studio, bool bTakeCamera, TFunction<double()> ServerNowMs);
	void SetPortraits(UTexture2D* A, UTexture2D* B);
	void Close();
	bool IsShowing() const { return Phase != EPhase::Hidden; }
	const FString& CurrentCaption() const { return Caption; }
	const FString& DuelId() const { return ShownDuel; }
	/** The white flash of an initiative flip (1 at the flip, 0 after 0.3 s): the HUD draws it. */
	float FlashAlpha() const { return FMath::Clamp(1.f - (float)(FPlatformTime::Seconds() - FlashAt) / 0.3f, 0.f, 1.f); }

	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EPhase : uint8 { Hidden, Rising, Open, Closing };
	EPhase Phase = EPhase::Hidden;
	float PhaseT = 0.f;
	bool bBuilt = false;
	bool bTookCamera = false;
	FString ShownDuel;
	const void* ShownScene = nullptr;
	TWeakPtr<FFNDuelSession> Session;
	TFunction<double()> NowMs;
	TWeakObjectPtr<AActor> OldViewTarget;
	TWeakObjectPtr<AFNStudio> StudioRef;

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> ImageMat;
	UPROPERTY() TObjectPtr<USceneComponent> Lift;
	UPROPERTY() TObjectPtr<USceneComponent> LidHinge;
	UPROPERTY() TObjectPtr<USceneComponent> Drawer;
	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SceneParts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SkyBands;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Trim;   // gilt edging, frames, plaques: blue (yours) / red (watched)
	UPROPERTY() TObjectPtr<class UPointLightComponent> TeamLight;
	UPROPERTY() TObjectPtr<class UPointLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<class UPointLightComponent> CandleLight;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CandleFlame;
	double FlashAt = -10.0;
	bool bFlipSide = false;
	bool bDimsTable = false;   // this opening took the camera: the table's spots dim while it's up
	TMap<TWeakObjectPtr<class USpotLightComponent>, float> DimmedSpots;
	void DimTable(float Alpha);
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Stars;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PortraitA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PortraitB;
	// Each fighter's portrait as a big card hovering over its mannequin (Benito's option (a), 2026-11-15), facing the camera.
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CardA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CardB;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CardFrameA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CardFrameB;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HpFillA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HpFillB;
	UPROPERTY() TObjectPtr<UTextRenderComponent> NameA;
	UPROPERTY() TObjectPtr<UTextRenderComponent> NameB;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DrawerText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Burst;
	UPROPERTY() TObjectPtr<UTextRenderComponent> RuleText;   // a blow's rules floating over its striker (~1.2 s)
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> FighterA;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> FighterB;
	UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Clips;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

	// The panel player's state
	int32 LastPanel = -1;
	double LastPanelT = 0.0;
	int32 ClipA = -1, ClipB = -1;
	float ShownHpA = 10.f, ShownHpB = 10.f;
	float BurstLeft = 0.f, ShakeLeft = 0.f, ShakeAmp = 0.f;
	FVector BurstAt = FVector::ZeroVector;
	float RuleLeft = 0.f;
	FVector RuleAt = FVector::ZeroVector;
	bool bActionPanel = false;   // the panel fires or draws: "wide" frames the stage, not the lid
	FString Caption;
	FVector CamPos = FVector::ZeroVector, CamLook = FVector::ZeroVector;
	bool bCamInit = false;
	FVector RefPelvis = FVector::ZeroVector;
	bool bHaveRefPelvis = false;

	// Weapons, hammer-space and projectiles (server schema 14), the ground underfoot (schema 17). Toy-like, like the box.
	struct FPropPart { TObjectPtr<UStaticMeshComponent> C; FVector Off = FVector::ZeroVector; FRotator Rot = FRotator::ZeroRotator; FVector Size = FVector::OneVector; };
	struct FHeld { FString Key, Class; TArray<FPropPart> Parts; double DrawnAt = -10.0; TSharedPtr<FJsonObject> Prop; };
	FHeld HeldA, HeldB;
	TSet<FString> DrawsSeen;                         // "<panel>:<side>:<seg>" draws whose flash played
	struct FFx { TArray<TObjectPtr<UStaticMeshComponent>> Parts; TArray<FVector> Dirs; TObjectPtr<class UPointLightComponent> Light; FVector At = FVector::ZeroVector; double Start = 0.0; float Dur = 0.5f, Size = 3.f, Lux = 0.f; };
	TArray<FFx> Fxs;
	TMap<FString, TArray<TObjectPtr<UStaticMeshComponent>>> ShotParts;   // projectile id -> its pieces
	TSet<FString> ShotsBurst;                        // projectile ids whose hit (or deflection, "<id>:d") played
	TMap<FString, TArray<int32>> ObjectParts;        // scene object id -> indices into SceneParts
	TArray<FTransform> SceneBase;                    // each scene part as built
	struct FKnock { int32 Panel = 0; double T1 = 0.0; float Impulse = 0.f; FVector Dir = FVector::ZeroVector; };
	TMap<FString, FKnock> Knocks;
	TArray<TObjectPtr<UStaticMeshComponent>> AoeTiles;
	double AoeAt = -10.0;
	TArray<TObjectPtr<UStaticMeshComponent>> GroundPatches;
	TObjectPtr<class UPointLightComponent> GroundLight;
	FString GroundFloor;
	double NextTremor = 0.0;
	void UpdateHeld(FHeld& H, USkeletalMeshComponent* M, double Now);
	void PlayShots(const TSharedPtr<FJsonObject>& Panel, int32 PanelN, double T, bool bLive);
	void Flash(const FVector& At, const FLinearColor& Color, float Size, float Dur, float Lux);
	void TickFx(double Now);
	void ApplyKnocks(int32 PanelN, double T);
	void BuildGround(const TSharedPtr<FJsonObject>& Ground);
	void ClearWeapons();
	FVector BoneLocal(USkeletalMeshComponent* M, const TCHAR* Bone, const FVector& Fallback) const;

	void Build();
	void BuildScene(const TSharedPtr<FJsonObject>& Scene);
	void SetSky(const FString& TimeOfDay);
	UStaticMeshComponent* Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& At, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator);
	UTextRenderComponent* Label(USceneComponent* Parent, const FVector& At, float Size, const FColor& Color, const FString& Text);
	USkeletalMeshComponent* Fighter(const FString& Skin);
	FVector HexLocal(double Q, double R) const;
	void PlayStage(double NowMs, float Dt);
	void PoseFighter(USkeletalMeshComponent* M, int32& ShownClip, const FString& ClipName, float ClipT, bool bLoop, const FVector& At, const FVector& FaceTo);
	void UpdateCamera(const FString& Shot, float Dt);
	void SetHpBars(float A, float B);
};
