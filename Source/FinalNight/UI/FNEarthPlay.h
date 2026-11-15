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

	virtual void Tick(float DeltaSeconds) override;

	struct FRec { uint8 H16 = 128, Top = 0, Water = 0, Lava = 0, Ash = 0, Fire = 0, Snow = 0, Crack = 0; int8 Heat = 0; };

private:
	TWeakObjectPtr<UFNNetClient> Net;
	TWeakObjectPtr<AFNStudio> Studio;
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
	struct FFlash { TObjectPtr<UStaticMeshComponent> Beam; TObjectPtr<UPointLightComponent> Light; TObjectPtr<UTextRenderComponent> Label; double At = 0.0; FVector Where = FVector::ZeroVector; FString Kind; };
	TArray<FFlash> Flashes;

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
