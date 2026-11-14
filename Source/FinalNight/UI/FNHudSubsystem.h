#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/Ticker.h"
#include "FNHudSubsystem.generated.h"

/**
 * Mounts the live style-kit HUD (SFNHudLive) when a game or PIE world begins play and the git-ignored
 * per-machine file Config/FN7HudV2.txt exists. It waits for the player controller's net client, mounts once,
 * and unmounts when the world goes away. Without the file nothing changes.
 */
UCLASS()
class FINALNIGHT_API UFNHudSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FTSTicker::FDelegateHandle PollHandle;
	float Waited = 0.f;
	bool Poll(float DeltaTime);
};
