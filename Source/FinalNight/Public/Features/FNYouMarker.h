#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNYouMarker.generated.h"

class UStaticMeshComponent;
class UTexture2D;
class UFNNetClient;

/**
 * "Find yourself" (Benito): a quiet marker on the table board under each of YOUR living demigods: a thin ring of
 * light on the ground in your aura colour that slowly breathes, and a small arrow floating above the piece that bobs.
 * Nothing on anyone else's pieces. Reads which demigods are yours from the server's seat view (the same message the
 * HUD reads); finds the pieces through the kit's figures. Unlit, no lights, no shadows, no collision.
 */
UCLASS()
class FINALNIGHT_API AFNYouMarker : public AActor
{
	GENERATED_BODY()
public:
	AFNYouMarker();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FMine { FString Ref; bool bAlive = true; };
	TArray<FMine> Mine;
	FLinearColor Aura = FLinearColor(1.f, 0.78f, 0.15f);
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	void ReadView(const TSharedPtr<class FJsonObject>& View);
	void Paint();

	struct FMark { TArray<UStaticMeshComponent*> Ring; UStaticMeshComponent* Arrow = nullptr; };
	TArray<FMark> Marks;   // one per demigod of yours (2)
	FMark MakeMark(int32 i);
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Owned;
	UPROPERTY() TObjectPtr<UTexture2D> Tex;
	float Time = 0.f;
	FString LastLog;
};

UCLASS()
class FINALNIGHT_API UFNYouMarkerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
