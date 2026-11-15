#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNEarthField.generated.h"

class AFNStudio;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/** One cell's state at a beat (the server's earth field; names follow the earth-sim contract as announced). */
struct FFNEarthState
{
	float Height = 1.f;          // relief, in the board's units (cm on the table at scale 1)
	FString Material;            // top material: water sand grass forest rock snow lava ash ...
	float Water = 0.f;           // 0..1: darkens and glosses the top
	float Heat = 0.f;            // 0..1: lava / fire glow
};

/** A cell of the field and its two states (the beat before, the beat after). */
struct FFNEarthCell
{
	FString Key;                 // "face:level:q:r" when the server sends it
	double Lat = 0.0, Lon = 0.0;
	FFNEarthState A, B;
	FVector World = FVector::ZeroVector;   // its board point (AFNStudio::BoardPointFor), filled by SetFrame
	bool bOnBoard = false;
};

/** The time cursor: LIVE follows the beats on the server clock; SCRUB is set by hand (a replay / time-travel UI);
 *  RECONNECT reads the present once. Every mode ends in one number: T in 0..1 between beat A and beat B. */
struct FFNEarthTime
{
	enum class EMode : uint8 { Live, Scrub, Reconnect };
	EMode Mode = EMode::Live;
	int32 BeatA = 0, BeatB = 0;
	double StartMs = 0.0, EndMs = 0.0;   // LIVE: the server-clock span of the beat being animated
	float ScrubT = 0.f;                   // SCRUB: the position between A and B
	bool bLoop = false;                   // the stub: play A -> B over and over
};

/**
 * The earth simulator's field on the board: a low-resolution voxel field covering weather, landmass movement, earthquakes and volcanoes.
 * simulator on backend, timetravel"). One instanced hex column per cell (SM_HexPrism, a unit column), placed with the
 * studio's own BoardPointFor (no second hex formula). Per-instance custom data carries both beats' top colour,
 * wetness and heat; the material (M_EarthColumn) blends them on the GPU by BeatT, and the heights blend here, so a
 * whole beat animates smoothly. Driven by the time cursor; a stub file plays it for development (FN7.EarthStub).
 */
UCLASS()
class FINALNIGHT_API AFNEarthField : public AActor
{
	GENERATED_BODY()

public:
	AFNEarthField();
	static AFNEarthField* Get(UWorld* World, bool bCreate);

	/** A new frame: the cells and their two states. Places each on the board (cells off the board are skipped). */
	void SetFrame(const TArray<FFNEarthCell>& Cells, AFNStudio* Studio, float HexRadiusCm);
	/** Reads a frame file in the stub's shape ({beat_a, beat_b, beat_ms, cells:[{lat, lon, a:{...}, b:{...}}]}). */
	bool LoadStub(const FString& Path, AFNStudio* Studio);
	void Clear();

	FFNEarthTime Time;
	TFunction<double()> ServerNowMs;

	virtual void Tick(float DeltaSeconds) override;

	static FLinearColor MaterialColor(const FString& Material);

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Columns;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mid;
	TArray<FFNEarthCell> Shown;
	TArray<int32> InstanceOf;   // cell index -> instance (-1: off the board)
	float HexR = 4.f;
	float LastT = -1.f;
	double StubStart = 0.0;
	float StubBeatS = 6.f;

	float CurrentT() const;
	void ApplyT(float T);
};
