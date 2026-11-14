#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNShrines.generated.h"

class AFNStudio;
class FJsonObject;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;

/**
 * Benito's feature 8: shrines to your demigod (rules 0.8.0 `shrines[]` {id, cell, at, owner, owner_seat, worshippers,
 * power spark/heal/ward, level, owner_alive}). Each is a stone plinth with a spire in its owner's aura colour (taller per
 * level), its power written under the tip, and little worshippers circling it (more as `worshippers` grows).
 * shrine.built raises it from the ground with a flash; shrine.level grows it. A shrine whose owner has fallen goes grey.
 * Data: game.globe (every shrine, plus the demigods' aura), public_state, dm_events. No lights, no shadows.
 */
UCLASS()
class FINALNIGHT_API AFNShrines : public AActor
{
	GENERATED_BODY()
public:
	AFNShrines();
	void Bind(AFNStudio* InStudio);
	void Apply(const TSharedPtr<FJsonObject>& Payload);
	void ApplyEvent(const TSharedPtr<FJsonObject>& Event);
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FShrine
	{
		double Lon = 0.0, Lat = 0.0;
		int32 Level = 1, Worshippers = 0;
		FString Owner, Power;
		bool bAlive = true;
		float Rise = 1.f, Age = 0.f;
		FLinearColor Color = FLinearColor(0.9f, 0.85f, 0.7f);
		TObjectPtr<UStaticMeshComponent> Plinth = nullptr, Spire = nullptr, Tip = nullptr;
		TObjectPtr<UInstancedStaticMeshComponent> Folk = nullptr;
		TObjectPtr<UTextRenderComponent> Label = nullptr;
	};
	TMap<FString, FShrine> Shrines;
	TMap<FString, FLinearColor> Auras;   // dg:N -> its aura colour
	TWeakObjectPtr<AFNStudio> Studio;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;

	FShrine& Upsert(const FString& Key, const TSharedPtr<FJsonObject>& Src);
	UStaticMeshComponent* Piece(const TCHAR* Mesh);
	void Paint(UPrimitiveComponent* C, const FLinearColor& Color);
	void Repaint(FShrine& S);
};
