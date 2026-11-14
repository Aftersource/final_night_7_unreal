#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNSettlements.generated.h"

class AFNStudio;
class FJsonObject;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

/**
 * Benito's features 1 + 7 on the table map (rules 0.8.0):
 *  - GROWING SETTLEMENTS: every city's `settlement` {level 1..7, center_cell, settlement_cells} is a cluster of little
 *    houses (walls + roofs) around its centre: more houses, taller, and a keep in the middle as it levels up
 *    (1 hamlet .. 5 great city). settlement.grew / shrank re-raise it with a pop and a flash.
 *  - PATHS WEAR IN: every road {from_cell, to_cell, wear, path} is a dotted dirt track between the two centres, wider and
 *    darker with wear. road.formed draws it in from one end; road.faded fades it out.
 * Data: game.globe (roads, regions), game.region (cities with settlement), game.node, dm_events. Instanced meshes only,
 * no lights, no shadows. It asks for the regions under the board itself (through QueryFn), at most every 3 s.
 */
UCLASS()
class FINALNIGHT_API AFNSettlements : public AActor
{
	GENERATED_BODY()
public:
	AFNSettlements();
	void Bind(AFNStudio* InStudio, TFunction<void(const FString&, TSharedPtr<FJsonObject>)> InQuery);
	void Apply(const TSharedPtr<FJsonObject>& Payload);
	void ApplyEvent(const TSharedPtr<FJsonObject>& Event);
	/** Call once a beat: refreshes the roads (game.globe) and the regions under the board (game.region). */
	void Refresh();
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FTown { double Lon = 0.0, Lat = 0.0; int32 Level = 1; int32 Cells = 1; float Pop = 1.f; };
	struct FRoad { double LoA = 0.0, LaA = 0.0, LoB = 0.0, LaB = 0.0; float Wear = 1.f; float Grow = 1.f; float Fade = -1.f; };
	struct FRegion { FString Ref; double Lon = 0.0, Lat = 0.0; };
	TMap<FString, FTown> Towns;
	TMap<FString, FRoad> Roads;
	TArray<FRegion> Regions;
	TArray<FString> Asked;               // the regions last asked for
	TWeakObjectPtr<AFNStudio> Studio;
	TFunction<void(const FString&, TSharedPtr<FJsonObject>)> QueryFn;
	float SinceAsk = 99.f, SinceDraw = 99.f;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Walls;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Roofs;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Dirt;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;

	UInstancedStaticMeshComponent* MakeIsm(const TCHAR* Name, const TCHAR* Mesh, const FLinearColor& Color);
	void UpsertTown(const FString& Key, const TSharedPtr<FJsonObject>& Settlement, const TSharedPtr<FJsonObject>& Node);
	void UpsertRoad(const TSharedPtr<FJsonObject>& R, bool bNew);
	void AskRegionsUnderBoard(bool bForce);
	void Draw();
};
