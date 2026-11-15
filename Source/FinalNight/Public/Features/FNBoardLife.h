#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNBoardLife.generated.h"

class UInstancedStaticMeshComponent;
class UFNNetClient;
class UTexture2D;

/**
 * Disasters leave the world changed (Benito described forests burning to nothing, buildings destroyed, and floods; damage persists and
 * heals slowly"). Every fire / flood / battle-or-storm event the server reports (round.outcome public.events[] with an
 * "at") leaves a damage zone on the Earth. On whatever part of the board you look at, at every zoom, the hexagons inside
 * a zone change: fire -> the forest burns (flames while fresh, then black stumps on ash); flood -> water stands over the
 * land, houses half sunk; battle / storm / quake -> rubble, houses gone. The board's own hex states (AFNStudio::
 * SetHexOverride) clear the trees and houses there, so it is seamless with the rest of the table. Zones heal: after
 * HealStart beats they shrink, and are gone after HealEnd beats.
 * shortcut: zones come from event text + magnitude, not the server's per-cell earth.effects (not live yet); when that
 * lands, feed its cells in here instead.
 */
UCLASS()
class FINALNIGHT_API AFNBoardLife : public AActor
{
	GENERATED_BODY()
public:
	AFNBoardLife();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	enum class EKind : uint8 { Fire, Flood, Ruin };
	struct FZone { double Lat = 0, Lon = 0, RadiusM = 0; EKind Kind = EKind::Fire; int32 Beat = 0; };
	TArray<FZone> Zones;
	int32 Beat = 0;

	TMap<FString, uint8> Applied;   // cell key -> the EFNHexState we set there
	FString AppliedFor;             // dev: the view (level, centre) the Config/FN7DevScar.txt test zones were laid for
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);

	UInstancedStaticMeshComponent* Make(const TCHAR* Mesh, const FColor& C, const TCHAR* Name);
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Tex;
	UInstancedStaticMeshComponent* Flame = nullptr;
	UInstancedStaticMeshComponent* Stump = nullptr;
	UInstancedStaticMeshComponent* Ash = nullptr;
	UInstancedStaticMeshComponent* Water = nullptr;
	UInstancedStaticMeshComponent* Rubble = nullptr;
	float Time = 0.f, SinceDraw = 0.f;
	FString LastLog;
};

UCLASS()
class FINALNIGHT_API UFNBoardLifeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
