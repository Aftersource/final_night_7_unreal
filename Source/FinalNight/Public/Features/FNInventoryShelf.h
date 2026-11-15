#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNInventoryShelf.generated.h"

class UFNNetClient;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;

/**
 * Every player's collected items on a shelf behind their god statue (Benito wants to see all their image generated items
 * collected"): one small plinth per item with its generated picture (server item image, /media/<hash>.png) or, until the
 * picture is ready, Codex's little model (FNItemIcon) and its name under it. A gained item pops in with a gold glint,
 * a destroyed one goes grey, a stolen one carries a red tag. Click a statue: that player's items come up as big cards
 * with name and description; click again (anywhere) to put them back.
 * Reads items anywhere in server messages (public items[], my_items[], look.loadout[]) by their "it:" ref, and who holds
 * them through the demigods' "player". No board rebuilds, no collision, no shadows.
 */
UCLASS()
class FINALNIGHT_API AFNInventoryShelf : public AActor
{
	GENERATED_BODY()
public:
	AFNInventoryShelf();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FItem
	{
		FString Ref, Name, Desc, Icon, State = TEXT("held"), Holder, HolderName, Hash, Url;
		float Glint = 0.f;
	};
	struct FSlot
	{
		USceneComponent* Root = nullptr;
		UStaticMeshComponent* Plinth = nullptr;
		UStaticMeshComponent* Card = nullptr;
		UStaticMeshComponent* Spark = nullptr;
		UStaticMeshComponent* Back = nullptr;     // the dark card behind an item in the open view
		UTextRenderComponent* Label = nullptr;
		TArray<UStaticMeshComponent*> Model;
		FString ModelIcon, CardHash;
		bool bGrey = false;
	};
	TMap<FString, FItem> Items;              // by "it:N"
	TMap<FString, FString> DgPlayer;         // "dg:N" -> player name
	TMap<FString, FSlot> Slots;              // by item ref
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> Tex;       // by hash
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> TexGrey;   // by hash
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Keep;
	TSet<FString> Downloading;
	FString Open;                            // the player whose items are up as cards
	bool bMouseWasDown = false;
	TArray<TPair<FString, FVector>> Anchors; // each statue and each item on show, by player, for the click
	FString LastLog;
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	void Walk(const TSharedPtr<class FJsonObject>& O, int32 Depth, const FString& InDg = FString());
	void Fetch(const FItem& It);
	FSlot& SlotFor(const FString& Ref);
	UStaticMeshComponent* Piece(USceneComponent* Parent, const TCHAR* Mesh);
};

UCLASS()
class FINALNIGHT_API UFNInventoryShelfSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
