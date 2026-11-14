#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNDayNight.generated.h"

class AFNStudio;
class ULightComponent;
class FJsonObject;

/**
 * Benito's feature 6: day and night on the board (rules 0.8.0 `time_of_day` {day, phase, light 0..1, beat_in_day,
 * beats_per_day}). Tints the studio's own board lights (BoardKey = the sun, Moon, TabletLight) by phase and light:
 * a warm dawn, a white day, an orange dusk, a cool blue night with the moon up. It changes only colour and intensity
 * (never shadows), easing over ~2 s. It leaves alone the lights the studio animates every frame (globe, candles, flashes).
 */
UCLASS()
class FINALNIGHT_API AFNDayNight : public AActor
{
	GENERATED_BODY()
public:
	AFNDayNight();
	void Bind(AFNStudio* InStudio);
	/** Takes a `time_of_day` object, or any object that carries one (round.brief.public, round.outcome.public, public_state). */
	void Apply(const TSharedPtr<FJsonObject>& Payload);
	/** A dm_events[] entry; reacts to `time.phase` {day, phase, light}. */
	void ApplyEvent(const TSharedPtr<FJsonObject>& Event);
	virtual void Tick(float DeltaSeconds) override;

	FString Phase;
	int32 Day = 0;
	float Light = 1.f;

private:
	struct FLamp { TWeakObjectPtr<ULightComponent> Comp; float BaseIntensity = 0.f; FLinearColor BaseColor = FLinearColor::White; int32 Role = 0; };
	TArray<FLamp> Lamps;
	TWeakObjectPtr<AFNStudio> Studio;
	float Mix = 1.f;          // 0 = the previous look, 1 = the target look
	float FromLight = 1.f, ToLight = 1.f;
	FLinearColor FromTint = FLinearColor::White, ToTint = FLinearColor::White;
	void FindLamps();
	void SetTarget(const FString& InPhase, float InLight);
	void Push(float L, const FLinearColor& Tint);
};
