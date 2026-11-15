#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNStoryDecorable.h"
#include "FNTableGods.generated.h"

class AFNStudio;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;

/** One demigod as the table shows it: its diorama (full-fidelity image) and where it stands on the board. */
struct FFNTableDemigod
{
	FString Ref;
	FString Name;
	FString Side;                 // "left" / "right"
	bool bAlive = true;
	bool bHasPos = false;
	double Lat = 0.0, Lon = 0.0;
	UTexture2D* Diorama = nullptr;      // the generated diorama_image
	UTexture2D* DioramaGrey = nullptr;  // the same, desaturated, for the fallen
	FString Skin;                       // the server's mannequin skin (wood, obsidian, gold, dragonscale, butterfly)
};

/** One player (a god) at the table: its seat, name, aura and its demigods. */
struct FFNTableGod
{
	int32 Seat = -1;
	FString Player;
	FLinearColor Aura = FLinearColor::White;
	bool bHuman = false;
	bool bConnected = true;
	bool bAlive = true;
	bool bMine = false;
	TArray<FFNTableDemigod> Demigods;
};

/**
 * A player's god on a stone pedestal past the table's rim: a faceted figure with a SHAPE of its own (hooded, horned,
 * crowned, winged, masked or antlered, picked stably from the name), two glowing hands, and an aura in the player's
 * colour: a steady breathing halo for a human, a flickering ring of orbiting cubes for the AI, dimmed while away,
 * grey once out. A name plate turns to the camera.
 */
UCLASS()
class FINALNIGHT_API AFNGodPedestal : public AActor
{
	GENERATED_BODY()
public:
	AFNGodPedestal();
	virtual void Tick(float DeltaSeconds) override;
	void SetGod(const FFNTableGod& God);
	/** The player's own god (FN7GodStatue): its accepted picture standing on the pedestal, then its runtime statue. */
	void ShowGodPicture(UTexture2D* Tex, const FString& Player);
	void ShowRuntimeStatue(UStaticMesh* Mesh, const FLinearColor& Stone, const FString& Player);

private:
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Parts;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Picture;   // an account god's picture, while its statue loads or when it's too heavy
	UPROPERTY() TObjectPtr<UTexture2D> PictureTex;
	UPROPERTY() TObjectPtr<UStaticMesh> RuntimeMesh;
	bool bAccountStatue = false;
	void PlaceStatue(UStaticMesh* Mesh, float Yaw);
	void ShowFigure(bool bFigure);
	TArray<UStaticMeshComponent*> Robe;        // tinted dark with the aura
	TArray<UStaticMeshComponent*> Feature;     // the shape's parts
	TArray<UStaticMeshComponent*> Hands;
	TArray<UStaticMeshComponent*> Beads;       // human halo
	TArray<UStaticMeshComponent*> Cubes;       // AI ring
	UPointLightComponent* Glow = nullptr;
	UTextRenderComponent* Plate = nullptr;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PlateBack;   // a small dark stone plate behind the name (Benito's audit 14: readable)
	/** The player's marble god statue (Content/Figures/<name>_god/SM_<name>_god, from figures.json) on the pedestal in
	 *  place of the faceted figure; none: the faceted figure, in marble. */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Statue;
	UPROPERTY() TObjectPtr<class USpotLightComponent> StatueKey;        // a museum key light from above
	UPROPERTY() TObjectPtr<class ULocalFogVolumeComponent> StatueFog;   // glowing mist at its feet, the player's colour
	FString StatueFor;
	bool bHasStatue = false;
	void UpdateStatue(const FString& Player);
	FFNTableGod Info;
	int32 Kind = -1;
	float Time = 0.f;
	float Phase = 0.f;
	UStaticMeshComponent* Part(const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color);
	void BuildBody();
	void BuildFeature(int32 InKind);
};

/**
 * A demigod's DIORAMA BOX: a small hex plinth with an upright, framed card showing its generated diorama at full
 * fidelity, turned to the camera, framed in the player's aura colour, with an L/R notch on the plinth. Fallen: the
 * card tips over onto the plinth and greys. Until the image is ready: a dim silhouette card. The same box, smaller,
 * stands on the board where the demigod is. Benito's piece-story feature dresses it through IFNStoryDecorable.
 */
UCLASS()
class FINALNIGHT_API AFNDemigodFigure : public AActor, public IFNStoryDecorable
{
	GENERATED_BODY()
public:
	AFNDemigodFigure();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void SetDemigod(const FFNTableDemigod& D, const FLinearColor& Aura, bool bOwnerAlive);
	const FString& GetRef() const { return Ref; }
	virtual void ApplyStory(const TSharedPtr<FJsonObject>& Story) override;
	/** Held (summon drag): lifted, a little bigger, its frame glowing. */
	void SetHeld(bool bInHeld) { bHeld = bInHeld; }
	/** The card's centre and half-size in world units, for hit tests. */
	bool CardBounds(FVector& OutCenter, float& OutHalf) const;
	/** A piece on the board: its card floats above the board's towers, trees and peaks, so nothing hides it. */
	void SetOnBoard(bool bInOnBoard) { bOnBoard = bInOnBoard; }

private:
	bool bOnBoard = false;
	bool bHeld = false;
	float HeldT = 0.f;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Parts;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> ImageMat;
	UPROPERTY() TObjectPtr<UTexture2D> ShownTex;
	USceneComponent* Card = nullptr;
	UStaticMeshComponent* Image = nullptr;
	UStaticMeshComponent* Matte = nullptr;
	UStaticMeshComponent* Back = nullptr;
	UStaticMeshComponent* Notch = nullptr;
	UTextRenderComponent* NotchText = nullptr;
	TArray<UStaticMeshComponent*> Plinth;
	TArray<UStaticMeshComponent*> StoryBits;
	FString Ref;
	FString Side;
	FLinearColor Aura = FLinearColor::White;
	bool bAlive = true;
	float Tip = 0.f;
	float Time = 0.f;
	// Its statue on the plinth (Content/Figures/<name slug>/SM_<slug>), else a duel mannequin in its skin (the AI
	// players need to have mannequins please actually do it properly"); the diorama card floats above its head.
	UPROPERTY() TObjectPtr<UStaticMesh> StatueMesh;
	FPrimitiveInstanceId StatueId;
	FTransform StatueRel = FTransform::Identity;   // its place on the plinth (actor space, without rotation)
	FTransform StatueShown;                        // the world transform last sent to the clone set
	TWeakObjectPtr<AFNTableGods> StatueOwner;
	UPROPERTY() TObjectPtr<class USkeletalMeshComponent> Mini;
	int32 MiniFrames = 0;                          // the mannequin poses for a few frames, then stops ticking
	FString FigureFor;
	float FigureH = 0.f;
	float FigureYaw = 0.f;
	void UpdateFigure(const FString& Name, const FString& Skin, const FString& InSide);
	UStaticMeshComponent* Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color);
};

/**
 * The table's gods and dioramas: spawns a pedestal per rival player at the seat slots around the far rim
 * (slot angles 90/200/235/305: slot 0 is you at the camera, the others (seat - yours) mod 4, clockwise), two diorama
 * boxes per player between its pedestal and the rim (yours at the near rim), and a small diorama box on the board at
 * each demigod's position. While it is up, the classic seat silhouettes and minifigs are hidden.
 */
UCLASS()
class FINALNIGHT_API AFNTableGods : public AActor
{
	GENERATED_BODY()
public:
	/** The demigods' statues as clones: one instanced mesh per statue, an instance per figure (stable ids, moved only
	 *  when the figure moves, removed when it goes) instead of a component each (use proper mesh clones and
	 *  cleanup to deal with the many objects"). World-space transforms. */
	FPrimitiveInstanceId AddStatue(UStaticMesh* Mesh, const FTransform& World);
	void MoveStatue(UStaticMesh* Mesh, FPrimitiveInstanceId Id, const FTransform& World);
	void RemoveStatue(UStaticMesh* Mesh, FPrimitiveInstanceId Id);
	AFNTableGods();
	virtual void Tick(float DeltaSeconds) override;
	static AFNTableGods* Get(AFNStudio* Studio, bool bCreate);
	void SetGods(const TArray<FFNTableGod>& Gods, int32 MySeat);
	void SetShown(bool bShow);
	/** The figure for a demigod ref (the one by its god's pedestal), for the piece-story feature. */
	AFNDemigodFigure* FindFigure(const FString& Ref) const;

	/** For the summon feature: the demigod ref whose diorama box (by its pedestal) or board piece is under a VIEWPORT
	 *  pixel (as APlayerController::GetMousePosition gives), else empty. The nearest card wins. */
	static FString FigureAtScreen(UWorld* World, const FVector2D& ViewportPixel, TFunction<bool(const FString&)> Prefer = nullptr, double ZoneScale = 1.0, double* OutDistPx = nullptr);
	/** The demigod whose card (board piece or box) is NEAREST to a viewport pixel within MaxPx (the wheel's "near").
	 *  Prefer: among the ones in range, those it says yes to win (overlapping cards: the enemy first). */
	static FString FigureNear(UWorld* World, const FVector2D& ViewportPixel, double MaxPx, TFunction<bool(const FString&)> Prefer = nullptr);
	/** Two demigods' board pieces touching or overlapping on the table right now (contact for a challenge). */
	bool PiecesTouch(const FString& A, const FString& B) const;
	/** Lift and glow (or settle) every figure of that demigod (its diorama box and its board piece) while it is held. */
	void SetFigureHeld(const FString& Ref, bool bHeld);
	/** One ring of beads pulsing out under a piece (a demigod double-click: gold on yours, red on a far rival). */
	void PulseRing(const FString& Ref, const FLinearColor& Color);
	/** The table's gods in this world, if any. */
	static AFNTableGods* GetAny(UWorld* World);
	/** Show a demigod's board piece at a new point right away (a summon sent, before the server's next state). */
	void SetLocalPos(const FString& Ref, double Lat, double Lon);
	/** Where a demigod's board piece stands now (false when it has none). */
	bool GetBoardPos(const FString& Ref, double& OutLat, double& OutLon) const;

	/** The summon, as the contract shows it: accepted = the piece lifts and dissolves upward in a column of light at its
	 *  origin; resolved = a bright arc from origin to target, an arrival burst, the piece dropping onto its hex with a
	 *  bounce, and a red "-N" when it lost health. */
	void PlaySummoning(const FString& Ref, double FromLat, double FromLon, const FLinearColor& Aura);
	/** In sequence: the arc from origin to target (0.8 s), the demigod's DIORAMA popping up big over the target (a card
	 *  zooming in, held, then shrinking into its piece, 1.3 s), the piece dropping onto its hex, a red "-N" and the d20
	 *  tumbling beside it (Roll 0 = no die). */
	void PlaySummoned(const FString& Ref, double FromLat, double FromLon, double ToLat, double ToLon, float HealthLost, int32 Roll, const FLinearColor& Aura, float Delay = 0.f);
	/** A new match: forget every summon in flight and every local place (refs are reused across matches). */
	void ResetSummons();
	/** The warp-in played at once on the server's accept (before the resolve): arc + big diorama card after the column
	 *  has risen (Delay s), no damage yet. */
	void PlayWarpIn(const FString& Ref, double FromLat, double FromLon, double ToLat, double ToLon, const FLinearColor& Aura, float Delay, float HealthLost = 0.f, int32 Roll = 0);
	/** The resolve after a warp-in: settle on the real landing (glide if the server nudged it), the red "-N", the d20. */
	void PlaySummonResult(const FString& Ref, double ToLat, double ToLon, float HealthLost, int32 Roll);
	/** A refused summon: the piece comes back where it was, with a red shake. */
	void CancelSummon(const FString& Ref);
	bool IsSummoning(const FString& Ref) const { return Dissolved.Contains(Ref); }
	/** Seconds the whole PlaySummoned sequence takes until the piece stands on its hex. */
	static constexpr float SummonLandDelay = 2.1f;

private:
	UPROPERTY() TMap<TObjectPtr<UStaticMesh>, TObjectPtr<class UInstancedStaticMeshComponent>> StatueSets;
	TWeakObjectPtr<AFNStudio> Studio;
	UPROPERTY() TMap<int32, TObjectPtr<AFNGodPedestal>> Pedestals;   // by slot
	UPROPERTY() TMap<FString, TObjectPtr<AFNDemigodFigure>> Figures;  // by ref
	UPROPERTY() TMap<FString, TObjectPtr<AFNDemigodFigure>> OnBoard;  // by ref
	TMap<FString, FFNTableDemigod> BoardInfo;
	TMap<FString, FLinearColor> BoardAura;

	// Summon effects (component pools on this actor).
	struct FFx
	{
		int32 Kind = 0;          // 0 column, 1 arc, 2 burst, 3 damage text
		float T = 0.f, Dur = 1.f;
		FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		FString Ref;
		float Value = 0.f;
		int32 Roll = 0;
		TWeakObjectPtr<UTexture2D> Tex;
		TArray<USceneComponent*> Comps;
		TArray<FVector> Vel;
	};
	TArray<FFx> Fx;
	TMap<FString, float> Dissolved;      // pieces hidden between `summoning` and landing (value: seconds hidden)
	TMap<FString, FVector> LocalPos;     // a landed summon's place (X lat, Y lon, Z age s) until the commit agrees
	TMap<FString, float> Shake;          // a refused summon's red shake (seconds left)
	TMap<FString, TWeakObjectPtr<UTexture2D>> DioramaOf;   // every demigod's diorama, for the summon card
	TMap<FString, FVector> PieceLL;      // each board piece's lat/lon as last placed (Z: seconds gliding, -1 settled)
	TMap<FString, float> PulseOut;       // teleporting away: the piece pulses out (seconds so far)
	TMap<FString, float> PulseIn;        // teleporting in: the piece pulses in (seconds so far)
	struct FRingPulse { FString Ref; float T = 0.f; FVector At = FVector::ZeroVector; float R = 20.f; TArray<UStaticMeshComponent*> Beads; };
	TArray<FRingPulse> RingPulses;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RingBeadsKeep;
	void TickRingPulses(float Dt);
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> FxParts;
	UPROPERTY() TObjectPtr<UMaterialInterface> FxMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> CardMat;
	UStaticMeshComponent* FxShape(const TCHAR* Mesh, const FVector& At, const FVector& Scale, const FLinearColor& Color);
	void SpawnLanding(const FVector& At, float HealthLost, int32 Roll, TArray<FFx>& Out);
	UStaticMeshComponent* FxChild(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color);
	USceneComponent* FxRoot(const FVector& At);
	void TickFx(float Dt);
	bool BoardWorld(double Lat, double Lon, FVector& Out) const;
	bool bShown = true;
	bool bHasGods = false;
	static float SlotAngle(int32 Slot);
	FVector Local(float Deg, float Radius, float Z) const;
};
