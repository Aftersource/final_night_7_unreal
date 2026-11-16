#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNIsoGlobe.generated.h"

/**
 * The iso globe: an isosurface that is only visible ahead of the joining ... a globe-based
 * isosurface", for the jam's RTX path tracing prize): a gyroid lattice shell around the studio's globe, the zero set of
 * max(|gyroid| - t, | |p|/R - 1.22 | - 0.07), meshed once at runtime with surface nets (no assets), in reflective gold
 * so a path tracer has something to bounce off. Shown only while the globe guard says you haven't joined yet; it turns
 * slowly; hidden the moment you join.
 */
UCLASS()
class FINALNIGHT_API AFNIsoGlobe : public AActor
{
	GENERATED_BODY()
public:
	AFNIsoGlobe();
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> Shell;
	UPROPERTY() TObjectPtr<class UStaticMesh> Mesh;
	bool bBuilt = false;
	float Spin = 0.f;
	void Build();
};

UCLASS()
class FINALNIGHT_API UFNIsoGlobeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
