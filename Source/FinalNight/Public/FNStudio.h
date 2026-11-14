#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNTypes.h"
#include "FNStudio.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
class UPointLightComponent;
class UAudioComponent;
class USoundWaveProcedural;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;

/** Something standing on the globe board. */
struct FFNPiece
{
	FString Name;
	double Lon = 0.0, Lat = 0.0;
	/** 0 you, 1 another player, 2 villager (AI), 3 fallen, 4 drop spot (day 1) */
	int32 Kind = 1;
	int32 Color = 1;
};

/** Sounds the table can make. All of them are synthesised in code (no audio files). */
enum class EFNSfx : uint8 { Click, Boom, Chime, Snuff, Quill, Impact, Swarm, Roar, Whoosh };

/**
 * The Final Night tabletop, rebuilt in Unreal from the Unity client
 * (elationate/finalnight_unity, scene "FinalNight - Client"):
 *  - Player easel        click to begin (StartExperience -> connect)
 *  - Centrex / Globe of Destiny + globe guard (blitzes until you join)  click to join the lobby
 *  - WILL panel          click to join the game, then to write / lock your WILL
 *  - Table easel         welcome message, then "[primer] round: #N"
 *  - 4 player stands     names + colours, markers on the globe
 *  - Episode ribbons     circle the globe; click one to choose it (selection is new: the 2024 build had none)
 * Built from engine basic shapes; it only DISPLAYS state. SFNRootWidget owns the logic.
 * Clickable parts carry the component tag "FN:<Id>".
 */
UCLASS()
class FINALNIGHT_API AFNStudio : public AActor
{
	GENERATED_BODY()

public:
	AFNStudio();
	virtual void Tick(float DeltaSeconds) override;

	static const FName PlayerEasel;
	static const FName Globe;
	static const FName WillPanel;
	static const FName RibbonA;
	static const FName RibbonB;
	static const FName RibbonC;

	FVector GetSeatLocation() const;
	FRotator GetSeatRotation() const;
	void SetHovered(FName Id);

	// Scene animator (mirrors SceneAnimator in the Unity client)
	void PlayerEaselDown();
	void SetGuardBlitz(bool bBlitz);
	void SetGuardLocked(bool bLocked);
	void SetWillPanelState(const FString& Label, bool bActive);

	void ShowEaselText(const FString& Header, const FString& Body);
	/** Stands show who is near you (name + distance/direction). */
	void ShowNearby(const TArray<FFNPlayer>& Nearby);
	/** Globe: where you are, the safe zone ring, and (day 1) the battle bus drop cities. */
	void ShowSelfOnGlobe(double Lon, double Lat, bool bAlive);
	void ShowZoneOnGlobe(double Lon, double Lat, double RadiusKm);
	void ShowDropSpots(const TArray<FFNPiece>& Spots);
	/** The board: every piece we know about (you, players, villagers, the fallen). */
	void ShowWorld(const TArray<FFNPiece>& Pieces);
	/** Your little settlement grows with the things you build, find and keep. */
	void ShowBuilt(const TArray<FString>& Things);
	void ShowRibbons(const TArray<FFNEpisode>& Choices);
	void ChooseRibbon(int32 Index);
	void ClearRibbons();

	static FLinearColor PlayerColor(int32 Index);

	// Atmosphere: sound, the day's crisis played out on the Globe, and the build-up while the day is written
	void PlaySfx(EFNSfx Sfx, float Gain = 1.f, float DelaySeconds = 0.f);
	void PlayHazard(const FString& Hazard);
	void SetFatesDeciding(bool bOn);
	FRotator GetShake() const;
	/** The DM's d20 tumbles onto the table and lands on D20; the line under it shows the bonus and verdict. */
	void RollDice(int32 D20, int32 Mod, const FString& Verdict);
	/** Duel dice: any number of sides (the odds are 1 in N with no ceiling); you need Need or higher. */
	void RollDuelDice(int32 Roll, int32 Sides, int32 Need, bool bSuccess, const FString& Label);
	/** End of match: the Globe flies toward you and replays each day where it happened, one line at a time. */
	void PlayFinalCut(const TArray<FString>& Lines, const TArray<FVector2D>& Spots, const FString& Closing);
	/** 0 = quiet table, 1 = war drums (rises over the days). */
	void SetMusicLevel(float Level) { MusicLevel = FMath::Clamp(Level, 0.f, 1.f); }

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* Box(const FName& Name, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator);
	UStaticMeshComponent* Shape(const TCHAR* MeshPath, const FName& Name, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, USceneComponent* Parent = nullptr);
	UTextRenderComponent* Text(const FName& Name, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color, const FString& Str, USceneComponent* Parent = nullptr);
	UPointLightComponent* Light(const FName& Name, const FVector& Loc, float Intensity, const FLinearColor& Color, float Radius);
	void Spot(const FName& Name, const FVector& Loc, const FRotator& Rot, float Intensity, const FLinearColor& Color);
	void Tag(UStaticMeshComponent* C, FName Id);
	static FString Wrap(const FString& S, int32 Width, int32 MaxLines);
	static FVector LonLatToGlobe(double Lon, double Lat, float R);

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<USceneComponent> GlobePivot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> GlobeMesh;
	UPROPERTY() TObjectPtr<USceneComponent> GuardPivot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GuardShards;
	UPROPERTY() TObjectPtr<UPointLightComponent> GlobeLight;
	UPROPERTY() TObjectPtr<USceneComponent> PlayerEaselPivot;
	UPROPERTY() TObjectPtr<UTextRenderComponent> EaselHeader;
	UPROPERTY() TObjectPtr<UTextRenderComponent> EaselBody;
	UPROPERTY() TObjectPtr<UTextRenderComponent> WillLabel;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> StandTexts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ZoneMarkers;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SpotMarkers;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> SelfMarker;
	UPROPERTY() TObjectPtr<USceneComponent> RibbonPivot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Ribbons;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> RibbonTexts;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Candles;
	UPROPERTY() TMap<FName, TObjectPtr<UStaticMeshComponent>> Interactables;

	UPROPERTY() TObjectPtr<UTextRenderComponent> PlayerEaselTextComp;
	UPROPERTY() TObjectPtr<USceneComponent> BoardRoot;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> TileSets;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Trees;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Peaks;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> PieceRoots;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PieceParts;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> PieceLabels;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> HutRoots;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> HutLabels;
	UPROPERTY() TObjectPtr<UTextRenderComponent> ZoneLabel;
	UPROPERTY() TObjectPtr<USceneComponent> DicePivot;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DiceNumber;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DiceLine;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> AudioWave;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioComp;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FxBits;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FxMats;
	UPROPERTY() TObjectPtr<UPointLightComponent> FlashLight;

	// --- sound (a tiny software synth feeding one procedural voice) ---
	struct FVoice { const TArray<float>* Samples = nullptr; int32 Pos = 0; int32 Delay = 0; float Gain = 1.f; };
	void BuildSounds();
	void PumpAudio();
	TMap<uint8, TArray<float>> SfxBank;
	TArray<FVoice> Voices;
	FRandomStream Rng;
	int64 SampleClock = 0;
	float Lp1 = 0.f, Lp2 = 0.f, Lp3 = 0.f, Lp4 = 0.f, CrackleEnv = 0.f;
	float FatesAmt = 0.f, FatesTarget = 0.f;

	// --- typewriter on the tablet ---
	FString TypeFull;
	float TypeShown = 0.f;
	int32 TypeLastQuill = 0;

	// --- crisis effects ---
	int32 FxKind = 0;      // 0 none, 1 meteors, 2 locusts, 3 dinosaurs, 4 other
	float FxT = 0.f, FxDur = 0.f;
	TArray<FVector> FxPos, FxVel, FxParam;
	float FlashAmt = 0.f, ShakeAmt = 0.f, LastImpact = -10.f;
	int32 StompsDone = 0;
	void UpdateFx(float Dt);
	void ResetFxBit(int32 i);

	// --- the globe board (a Polytopia-style tile world) ---
	void BuildBoard();
	void PaintTiles();
	void PlacePiece(int32 Index, const FFNPiece& P);
	void UpdateLabels();
	FVector BoardDir(double Lon, double Lat) const { return LonLatToGlobe(Lon, Lat, 1.f); }
	FVector BoardToActor(const FVector& LocalDir, float Radius) const;
	struct FTile { FVector Dir; FVector East; uint8 Biome; uint8 Shade; float H; float W; float D; };
	TArray<FTile> Tiles;
	TArray<FVector> PieceDirs;
	TArray<int32> PieceKinds;
	TArray<FFNPiece> WorldPieces, DropPieces;
	FVector ZoneDir = FVector::ZeroVector;
	double ZoneAng = PI;
	FVector FocusDir = FVector::ZeroVector;
	bool bHasFocus = false;
	FVector FxCenter = FVector(0.f, -10.f, 125.f);
	static constexpr float BoardR = 52.f;

	// dice
	float DiceT = -1.f;
	FVector DiceSpin = FVector::ZeroVector;
	int32 DiceSounds = 0;
	bool bDiceGood = true;
	void UpdateDice(float Dt);
	// the Final Cut replay
	TArray<FString> CutLines;
	TArray<FVector2D> CutSpots;
	FString CutClosing;
	float CutT = -1.f, CutZoom = 0.f;
	int32 CutIdx = -1;
	// music
	float MusicLevel = 0.f;

	TMap<FName, FVector> BaseScale;
	FName Hovered;
	float Time = 0.f;
	float NextBlitz = 3.72f;
	float BlitzT = -1.f;
	bool bBlitz = true;
	bool bGuardLocked = false;
	bool bWillActive = false;
	float EaselDownT = -1.f;
};
