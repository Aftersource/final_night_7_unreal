#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNPeopleLife.generated.h"

class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;
class UFNNetClient;

/**
 * A living, breathing world at people zoom (Benito asked why random people are just standing there and wants a living
 * breathing world ... how am i supposed to tell a hot girl from an ugly witch or a warrior from a fisherman?").
 * Where the board used to stand a beige pole (AFNStudio::GetPeopleSpots), a person lives: a role (fisherman, farmer,
 * smith, merchant, guard, warrior, priest, witch, noble, woman, child, elder) with a readable shape (rod, hoe, hammer,
 * sack, spear, staff, pointed hat, crown, dress, robe, cane), a name, and a routine: working at their spot (casting,
 * hoeing, hammering...), walking to a house, the shore or a neighbour, chatting, kids running, guards patrolling.
 * Hover one: its name and trade. Unlit pieces, no shadows, no collision, no board rebuilds.
 * shortcut: shapes stand in for the generated sprite library (Chris's budget call); when people[] {role, sprite} lands,
 * take the role from it and draw the sprite as an upright card instead of the shape.
 */
UCLASS()
class FINALNIGHT_API AFNPeopleLife : public AActor
{
	GENERATED_BODY()
public:
	AFNPeopleLife();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	enum ERole : uint8 { Fisherman, Farmer, Smith, Merchant, Guard, Warrior, Priest, Witch, Noble, Woman, Child, Elder, NumRoles };
	enum EState : uint8 { Work, Walk, Chat };
	struct FPerson
	{
		FIntPoint Home;
		uint8 Role = Farmer;
		bool bFemale = false, bShore = false;
		FString Name;
		FVector2D Off = FVector2D::ZeroVector, To = FVector2D::ZeroVector, Shore = FVector2D::ZeroVector;   // hexagon widths from home
		float Yaw = 0.f, Left = 0.f, Phase = 0.f, Speed = 0.8f;
		uint8 State = Work;
		FIntPoint ChatWith = FIntPoint(MAX_int32, 0);
		bool bSeen = true;
		uint8 React = 0;        // 0 living, 1 fleeing / sheltering, 2 crowding a riot, 3 cheering
		bool bHide = false;     // inside a house till it passes
		FVector2D ReactTo = FVector2D::ZeroVector;
	};
	TMap<FIntPoint, FPerson> People;
	// what happens nearby (round.outcome events): people flee and shelter from disasters, crowd riots, cheer armies
	struct FAlarm { double Lat = 0, Lon = 0, RadiusM = 0; uint8 Kind = 1; float Until = 0.f; FString What; };
	TArray<FAlarm> Alarms;
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	FString LastReact;
	float Time = 0.f, SinceDraw = 0.f;
	FString LastLog;
	UTextRenderComponent* Hover = nullptr;
	TMap<uint32, UInstancedStaticMeshComponent*> Sets;   // (mesh, colour) -> instances
	TMap<UInstancedStaticMeshComponent*, TArray<FTransform>> Frame;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Keep;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Colors;
	UInstancedStaticMeshComponent* Set(int32 Mesh, const FColor& C);
	void Add(int32 Mesh, const FColor& C, const FTransform& T);
	void Stick(const FVector& A, const FVector& B, float Thick, const FColor& C);
	void Decide(FPerson& P, const TArray<FVector2D>& Spots, const TArray<FVector2D>& Houses);

	// The server's people pictures (game.people sprite library, CLIENT_API "People and houses"): per role, the
	// pictures it has; a person whose role has one is drawn as a standing picture card instead of the shapes.
	TMap<uint8, TArray<FString>> RoleSprites;          // role -> picture hashes (several looks)
	TMap<FString, FString> SpriteUrl;                  // hash -> url
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> SpriteTex;
	UPROPERTY() TMap<FString, TObjectPtr<class UMaterialInstanceDynamic>> SpriteMat;
	TSet<FString> SpriteLoading;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> CardPool;
	FString AskedCell;
	float SinceAsk = 100.f;
	void ReadPeople(const TSharedPtr<class FJsonObject>& D);
	void FetchSprite(const FString& Hash);
	class UStaticMeshComponent* CardAt(int32 Index);
};

UCLASS()
class FINALNIGHT_API UFNPeopleLifeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
