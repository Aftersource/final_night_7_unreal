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
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Stars;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PortraitA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PortraitB;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HpFillA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HpFillB;
	UPROPERTY() TObjectPtr<UTextRenderComponent> NameA;
	UPROPERTY() TObjectPtr<UTextRenderComponent> NameB;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DrawerText;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Burst;
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
	FString Caption;
	FVector CamPos = FVector::ZeroVector, CamLook = FVector::ZeroVector;
	bool bCamInit = false;
	FVector RefPelvis = FVector::ZeroVector;
	bool bHaveRefPelvis = false;

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
