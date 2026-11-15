#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNHouseCards.generated.h"

class UFNNetClient;
class UStaticMeshComponent;
class UTexture2D;
class UMaterialInstanceDynamic;

/**
 * Benito asked why the houses look so boring and blocky, when they should be interesting, unique and image generated, based
 * around who's living in that village or house or terrain". At houses and people zoom, every building the server knows
 * under the board (game.buildings {cell}: kind, look, settlement, wrecked, sprite) stands as its generated picture: a
 * card on its hexagon turned to the camera, sized to the hexagon, greyed when wrecked. Buildings whose picture is not
 * ready keep the board's block house. Cards only; no board rebuilds, no collision, no shadows.
 */
UCLASS()
class FINALNIGHT_API AFNHouseCards : public AActor
{
	GENERATED_BODY()
public:
	AFNHouseCards();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FBuilding { int32 Q = 0, R = 0; FString Level, Kind, Settlement, Hash, Url; bool bWrecked = false; };
	TMap<FString, FBuilding> Buildings;   // cell -> building
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	void FetchPicture(const FString& Hash, const FString& Url);
	UStaticMeshComponent* CardAt(int32 Index);

	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> Tex;
	UPROPERTY() TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> Mat;
	UPROPERTY() TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> MatGrey;
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> TexGrey;
	TSet<FString> Loading;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pool;
	FString AskedCell;
	float SinceAsk = 100.f, SinceDraw = 0.f;
	int32 LastShown = -1;
};

UCLASS()
class FINALNIGHT_API UFNHouseCardsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
