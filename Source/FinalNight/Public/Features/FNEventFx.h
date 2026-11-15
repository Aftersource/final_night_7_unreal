#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNEventFx.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;
class UFNNetClient;

/**
 * Events in the world (Benito): each beat's world events (round.outcome public.events[] with "at" [lon,lat]) play ON
 * the table board where they happened: a shockwave ring spreading over the hexes, a burst themed by the kind (fire
 * sparks rise, water drops arc and fall, rubble for battles and storms) and a floating "-1,401" when people died.
 * About 4 seconds each, staggered so a beat's events don't land at once. Unlit, no lights, no shadows, no collision.
 * The globe already gets its lightning (AFNStudio::FlashAt); casts are the earth playback's.
 */
UCLASS()
class FINALNIGHT_API AFNEventFx : public AActor
{
	GENERATED_BODY()
public:
	AFNEventFx();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	/** Play one event at a point on the planet (Kind: the server's kind or words like "flood", "fire"). */
	void Play(double Lon, double Lat, const FString& Kind, float Magnitude, int32 Dead, float Delay = 0.f);

private:
	struct FFx
	{
		double Lon = 0.0, Lat = 0.0;
		int32 Theme = 0;       // 0 generic, 1 fire, 2 water, 3 storm/battle
		float Mag = 5.f, T = 0.f;
		int32 Dead = 0;
		TArray<UStaticMeshComponent*> Ring, Bits;
		TArray<FVector> Vel;   // the bits' flight (cm/s, before scaling to the ring)
		UTextRenderComponent* Num = nullptr;
	};
	TArray<FFx> Live;
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	UStaticMeshComponent* Piece(UStaticMesh* Mesh, int32 Theme);
	void Free(FFx& F);
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Colors;   // one flat colour per theme
	int32 Seq = 0;
};

UCLASS()
class FINALNIGHT_API UFNEventFxSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
