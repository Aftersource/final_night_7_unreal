#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNStarBackdrop.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * The star world around the table: 3-5 layers of quick parallax ... nebula and stars for the mystical feel,
 * discrete ... motion relative to the globe itself"; Benito's approved look in docs/design/star_backdrop/).
 *
 * One camera-centred sphere with one unlit sky material (Content/Stars/M_StarSky) that composites four star layers by
 * view direction: dust (with the black-to-navy sky), far, mid and near stars with the four-point sparkles. Each layer
 * turns with the globe (the studio's BoardRoot, which every globe turn goes through: drags, centring glides, the idle
 * spin) by its own parallax factor, accumulated a frame at a time so a long spin never flips. One opaque sky surface:
 * no translucency sorting, no fog (Is Sky), no exposure surprises. The stone walls and the tablet behind the globe
 * are hidden so the stars show.
 */
UCLASS()
class FINALNIGHT_API AFNStarBackdrop : public AActor
{
	GENERATED_BODY()

public:
	AFNStarBackdrop();
	/** The one backdrop in this world (spawned on first call once the studio exists). */
	static AFNStarBackdrop* Ensure(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 NumLayers = 4;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sky;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mid;
	TWeakObjectPtr<class USceneComponent> GlobeRoot;
	FQuat LastGlobe = FQuat::Identity;
	bool bHaveGlobe = false;
	FQuat Layer[NumLayers];
	bool bSetUp = false;
	float LastReport = 0.f;

	void SetUp();
	void PushLayers();
};
