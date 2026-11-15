#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNEarthPlay.generated.h"

class AFNStudio;
class UFNNetClient;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;

/**
 * The earth simulator on the table (server docs/EARTH_SIM.md §7). Mega powers should visibly act on the table.
 * having any effect on the board"). Per beat it asks game.earth for the board's 22-ring disc at its level (since the
 * beat it drew), decodes the 9-byte records in ring order and:
 *  - lets risen / sunk hexes move as columns over a second, then commits them through the studio's height hook
 *    (SetHexOverride, one RebuildBoard), with the hex's state (trees hide on lava, fire, flood, ash, new land);
 *  - draws what lies on each hex as a cap that blends in over the beat: lava and fire glowing, flood water, ash, snow,
 *    an open rift, a swarm;
 *  - draws the beat's phenomena (storm, blizzard, lightning, volcano, quake, flood, wildfire, meteor, tidal wave,
 *    raise / sink land, forest, swarm) at their places on the board and on the globe, moving with their velocity;
 *  - tints the globe's 4° tiles that carry lava, fire, flood, ash, snow or a rift;
 *  - plays a cast's strike the instant its streak arrives, with the fault tier over it for two seconds.
 */
UCLASS()
class FINALNIGHT_API AFNEarthPlay : public AActor
{
	GENERATED_BODY()

public:
	AFNEarthPlay();
	static AFNEarthPlay* Get(UWorld* World, bool bCreate);

	void Bind(UFNNetClient* InNet) { Net = InNet; }
	/** round.outcome public.earth. */
	void OnOutcome(const TSharedPtr<FJsonObject>& Earth);
	/** A game.earth answer (the board, or the globe's tiles). */
	void OnReply(const TSharedPtr<FJsonObject>& D);
	/** A cast's streak (phase.event public): the strike plays now. */
	void OnStreak(const TSharedPtr<FJsonObject>& Pub);
	// --- time travel (server: game.earth {beat}, game.earth_at {beat}, present.earth) -------------------------------
	/** A game.earth_at answer: the whole field at a beat (phenomena with stable ids, the beat's hash). */
	void OnFieldAt(const TSharedPtr<FJsonObject>& D);
	/** match.snapshot (a join, a reconnect, a fork): the earth as it stands at present.earth.beat. */
	void OnSnapshot(const TSharedPtr<FJsonObject>& PresentEarth);
	/** A new match: everything cleared; its first beat draws in full. */
	void ResetMatch();
	/** Show a past (or any) beat and hold it there while live beats keep arriving (a scrub / timeline). */
	void ScrubTo(int32 Beat);
	/** Back to the live beat after a scrub. */
	void BackToLive();
	int32 GetDrawnBeat() const { return DrawnBeat; }

	virtual void Tick(float DeltaSeconds) override;

	struct FRec { uint8 H16 = 128, Top = 0, Water = 0, Lava = 0, Ash = 0, Fire = 0, Snow = 0, Crack = 0; int8 Heat = 0; };

private:
	TWeakObjectPtr<UFNNetClient> Net;
	TWeakObjectPtr<AFNStudio> Studio;
	TSharedPtr<FJsonObject> PendingBoard;   // a board answer that arrived mid-drag
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Caps;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CapMat;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GlobeCaps;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GlobeMat;

	// The board as drawn: per cell key (the table's level is in the key).
	TMap<FString, FRec> Cur;
	TMap<FString, uint8> Base16;        // each hex's height the first time it was seen (its own baseline)
	TMap<FString, float> DefaultTop;    // the board's own relief there, before any override
	TArray<FString> Tops;               // top material names by index
	FString BoardKey;                   // the centre key the board was fetched at
	FString CapsKey;                    // the board (centre key) the caps were laid on
	double CapsAnchorLat = 0.0, CapsAnchorLon = 0.0;   // a point of that board, and where it stood when the caps were laid:
	FVector CapsAnchorW0 = FVector::ZeroVector;         // the caps ride the land by how far that point has slid since
	FVector CapsBase = FVector::ZeroVector;
	bool bCapsAnchor = false;
	int32 BoardBeat = -1;               // the beat it was drawn at
	int32 LastBeat = -1;
	double AskedAt = 0.0, CentreChangedAt = 0.0;
	FString LastCentre;
	bool bAskedGlobe = false;
	int32 GlobeBeat = -1;
	TMap<int32, FRec> GlobeTiles;
	TArray<int32> GlobeRowCols;

	// Rising / sinking hexes, committed to the studio after the animation.
	struct FMove { FString Key; int32 Q = 0, R = 0; float From = 0.f, To = 0.f; uint8 State = 0; };
	TArray<FMove> Moves;
	double MoveStart = -10.0;
	bool bMovePending = false;
	double CapStart = -10.0;

	// Phenomena, by stable id.
	struct FPhen
	{
		int32 Id = 0;
		FString Kind;
		double Lon = 0.0, Lat = 0.0, ToLon = 0.0, ToLat = 0.0;
		float RadiusKm = 0.f, Intensity = 0.f, FrontKm = -1.f;
		double SeenAt = 0.0;
		bool bEnding = false;
		TArray<TObjectPtr<UStaticMeshComponent>> Parts;
		TObjectPtr<UPointLightComponent> Light;
		TObjectPtr<UStaticMeshComponent> GlobeDot;
	};
	TMap<int32, FPhen> Phens;
	double BeatStart = 0.0;
	float BeatLen = 6.f;

	// Strikes (a cast's moment) and fault labels.
	struct FFlash { TObjectPtr<UStaticMeshComponent> Beam; TObjectPtr<UStaticMeshComponent> Ring; TObjectPtr<UPointLightComponent> Light; TObjectPtr<UTextRenderComponent> Label; double At = 0.0; FVector Where = FVector::ZeroVector; FString Kind; bool bMega = false; };
	TArray<FFlash> Flashes;
	// Time travel: the beat on the table and its hash; a beat that isn't the next one is a jump (full, no since).
	int32 DrawnBeat = -1;
	FString DrawnHash;
	int32 JumpBeat = -1, JumpFrom = -1;
	bool bJumpBoard = false, bJumpTiles = false, bJumpField = false, bHashRetried = false;
	bool bHeld = false;                         // a scrub holds the shown beat; live outcomes only record their hash
	int32 FirstBeat = 1;                        // the match's first beat: every place's heights are measured from it
	TMap<int32, FString> OutcomeHash;           // round.outcome public.earth.hash per beat
	TMap<FString, TSharedPtr<FJsonObject>> BoardCache;   // "<centre>@<beat>" -> the full board answer (last 32)
	TArray<FString> CacheOrder;
	TSet<FString> BaseKnown, BaseAsked;         // board centres whose first-beat baseline is in / asked for
	float CapBlend = 1.f;                       // seconds the caps take to blend in (0.3 for a jump)
	void JumpTo(int32 Beat);
	void ApplyJumpBoard(const TSharedPtr<FJsonObject>& D);
	void ApplyBaseline(const TSharedPtr<FJsonObject>& Board);
	void HeightsNow();
	void ClearPhenomena();
	// The Populous-style earth-heave: the earth itself should move, not the camera. A quake's hexes jolt up and down in
	// a wave out from the epicentre and settle; raise / sink land jolt smaller. The real tiles move (SetHexOverride, one
	// RebuildBoard a frame while it runs); the view stays still, and nothing heaves under a held hand or a zoom.
	struct FHeave { double Lat = 0.0, Lon = 0.0, Start = 0.0; float Amp = 0.f, RadiusKm = 0.f, Dur = 0.f; };
	struct FHeaveHex { FString Key; double Lat = 0.0, Lon = 0.0; float Def = 0.f; };
	TArray<FHeave> Heaves;
	TMap<FString, FHeaveHex> HeaveHexes;
	FString HeaveBoard;
	TMap<FString, float> Committed;          // the earth's committed height per key (cm above the default top; NaN = state only)
	TMap<FString, uint8> CommittedState;
	void StartHeave(double Lat, double Lon, float AmpCm, float RadiusKm, float Dur);
	void TickHeave(double Now, AFNStudio* St);
	void EndHeave(AFNStudio* St, bool bRebuild);

	AFNStudio* FindStudio();
	void AskBoard(bool bFull);
	void AskGlobe();
	void ApplyBoard(const TSharedPtr<FJsonObject>& Board);
	void ApplyTiles(const TSharedPtr<FJsonObject>& Tiles);
	void RebuildCaps(float StartT);
	void CommitMoves();
	void SyncPhenomena(const TArray<TSharedPtr<FJsonValue>>& List, const TArray<TSharedPtr<FJsonValue>>* Ended);
	void BuildPhen(FPhen& P);
	void PlacePhen(FPhen& P, double Now);
	bool OnBoard(double Lat, double Lon, FVector& OutWorld) const;
	UStaticMeshComponent* Shape(const TCHAR* Mesh, const FLinearColor& Color, USceneComponent* Parent = nullptr);
	FString TopName(uint8 Top) const { return Tops.IsValidIndex(Top) ? Tops[Top] : FString(); }
};
