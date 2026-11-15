#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNArmies.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UFNNetClient;
class UStaticMeshComponent;
class UTexture2D;

/**
 * "See my guys" (Benito): every army, crowd and mob the server knows (game.globe armies[], game.region crowds.items[] /
 * mobs[] / armies[]) stands on the table board as little figures: an army is a block of soldiers with a banner, sized by
 * its headcount, in its demigod's aura colour (yours glow brighter, with a taller banner); a crowd is a loose group of
 * townsfolk in the colour of the demigod it leans to (grey when neutral); a mob jostles and hops. Reads the replies
 * AFNSettlements already asks for each beat; no queries of its own. Instanced meshes, no shadows, no collision.
 * shortcut: plain figures until the server sends a generated image per army/crowd; then a billboard per group.
 */
UCLASS()
class FINALNIGHT_API AFNArmies : public AActor
{
	GENERATED_BODY()
public:
	AFNArmies();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FGroup { FString Ref, Name, Owner; int32 Kind = 0; double Lon = 0, Lat = 0, FromLon = 0, FromLat = 0, MovedAt = -100; int32 Size = 1; double SeenAt = 0; double PinLat = 0, PinLon = 0, DrawnLat = 0, DrawnLon = 0; int32 PinLevel = -1, DrawnLevel = -1; double DrawnAt = -100, DrawnR = 0; };   // Kind 0 army, 1 crowd, 2 mob

	// An army with its generated image (game.media kind "army"): a card standing on a hex base ringed in the aura.
	struct FCard { UStaticMeshComponent* Image = nullptr; UStaticMeshComponent* Base = nullptr; UStaticMeshComponent* Rim = nullptr; };
	TMap<FString, FCard> Cards;                                   // by army ref
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> ArmyTex;    // by army ref, once ready
	TMap<FString, double> MediaAskedAt;                           // by army ref
	TSet<FString> Downloading;                                    // by hash
	TArray<FString> MediaQueue;                                   // army refs asked, in order (a reply without a ref is the oldest)
	void AskMedia(const FString& Ref);
	void OnMedia(const TSharedPtr<class FJsonObject>& D);
	FCard& CardFor(const FString& Ref);
	TMap<FString, FGroup> Groups;
	TMap<FString, FLinearColor> Aura;   // demigod ref -> aura
	TSet<FString> Mine;                 // your demigod refs
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	void ReadDemigods(const TArray<TSharedPtr<class FJsonValue>>& List);
	void Upsert(const TSharedPtr<class FJsonObject>& O, int32 Kind);

	// one instanced set per colour (bodies + heads), plus banners
	struct FSet { UInstancedStaticMeshComponent* Body = nullptr; UInstancedStaticMeshComponent* Head = nullptr; UInstancedStaticMeshComponent* Flag = nullptr; UInstancedStaticMeshComponent* Tile = nullptr; };
	TMap<uint32, FSet> Sets;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Owned;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	FSet& SetFor(const FLinearColor& C);
	float Time = 0.f, SinceDraw = 0.f;
	FString LastLog;
	int32 StableLevel = -1;
	bool bWasAnimating = false;
	int32 SeenLevel = -1;   // the level last frame
	double ClickLat = 0, ClickLon = 0;
	TMap<FString, FBox2D> CardRects;   // each standing picture's outline on screen (last settled frame)
	TSet<FString> ClickedCards;          // the pictures under the pointer when a zoom began
	int32 ClickLevel = -1;   // the level a double-click zoom started from (-1: none pending)   // the map level the last settled frame showed
};

UCLASS()
class FINALNIGHT_API UFNArmiesSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
