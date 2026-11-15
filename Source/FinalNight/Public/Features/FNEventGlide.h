#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "FNEventGlide.generated.h"

class UFNNetClient;

/**
 * The camera glides to YOUR events and back (Benito): when an event of one of your demigods happens off the board you
 * are looking at, the board slides there at the same zoom (AFNStudio::CenterBoardAndGlobe, the duel glide's way), the
 * event plays (AFNEventFx), it holds ~4 s for a disaster and ~8 s for an action, then slides back to where you were.
 * HAND_OF_GOD §6: never while you hold, pan, zoom or type (it waits), and if you moved the view meanwhile it stays.
 * shortcut: "yours" = the event names one of your demigods' refs or names; when the server tags events with an owner,
 * use that instead.
 */
UCLASS()
class FINALNIGHT_API AFNEventGlide : public AActor
{
	GENERATED_BODY()
public:
	AFNEventGlide();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FGo { double Lat = 0, Lon = 0; float Hold = 4.f, Mag = 5.f; FString Kind; };
	TArray<FGo> Queue;
	TArray<FString> MineKeys;   // your demigods' refs and names
	FString LastKeys;
	bool bAway = false;
	double SavedLat = 0, SavedLon = 0, AwayLat = 0, AwayLon = 0;
	bool bSavedPicked = false;
	float HoldLeft = 0.f;
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MsgHandle;
	void OnMessage(const FString& Type, TSharedPtr<class FJsonObject> D);
	bool Busy();
	FString LastWhy;
};

UCLASS()
class FINALNIGHT_API UFNEventGlideSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
