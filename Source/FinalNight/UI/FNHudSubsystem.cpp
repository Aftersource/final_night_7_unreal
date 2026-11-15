#include "FNHudSubsystem.h"

#include "Engine/World.h"
#include "FNPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "SFNHudLive.h"

bool UFNHudSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNHudSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// The kit HUD is the default (a packaged build has no local Config/FN7*.txt, so a judge must get it with none);
	// Config/FN7HudV1.txt keeps the classic UI for whoever wants it. FN7HudV2.txt is no longer read.
	if (FPaths::FileExists(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7HudV1.txt"))))
	{
		UE_LOG(LogTemp, Display, TEXT("FN7 HUD: Config/FN7HudV1.txt found: the classic UI"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 HUD: mounting the kit HUD (default)"));
	Waited = 0.f;
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UFNHudSubsystem::Poll), 0.5f);
}

bool UFNHudSubsystem::Poll(float DeltaTime)
{
	Waited += DeltaTime;
	UWorld* World = GetWorld();
	AFNPlayerController* PC = World ? Cast<AFNPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (PC && PC->Net)
	{
		FN7HudMount::Mount(PC->Net);
		PollHandle.Reset();
		return false;
	}
	return Waited < 30.f;
}

void UFNHudSubsystem::Deinitialize()
{
	if (PollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
		PollHandle.Reset();
	}
	if (FN7HudMount::IsMounted()) FN7HudMount::Unmount();
	Super::Deinitialize();
}
