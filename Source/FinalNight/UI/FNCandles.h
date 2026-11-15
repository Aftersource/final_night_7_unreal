#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNCandles.generated.h"

/**
 * Flames on the table's candles (Chris, 2026-11-15, asked for a nicer particle effect above the candle, sized to where the wick
 * is might be different for different height candles"): each of the studio's wax candles gets a wick and a flickering
 * flame at ITS wick top, computed from that candle's own wax bounds (no fixed offset), and its candle light moved onto the
 * flame. Cheap: a generated flame texture on an unlit translucent card that turns to the camera, plus a soft inner core;
 * the studio's old orange flame spheres are hidden.
 */
UCLASS()
class FINALNIGHT_API AFNCandles : public AActor
{
	GENERATED_BODY()
public:
	AFNCandles();
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FFlame
	{
		TWeakObjectPtr<class UStaticMeshComponent> Wax;
		TWeakObjectPtr<class UPointLightComponent> Light;
		class UStaticMeshComponent* Wick = nullptr;
		class UStaticMeshComponent* Card = nullptr;
		FVector Top = FVector::ZeroVector;
		float Height = 0.f, Seed = 0.f;
	};
	TArray<FFlame> Flames;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Keep;
	UPROPERTY() TObjectPtr<class UTexture2D> FlameTex;
	bool bBuilt = false;
	float Time = 0.f;
	void Build();
};

UCLASS()
class FINALNIGHT_API UFNCandlesSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
