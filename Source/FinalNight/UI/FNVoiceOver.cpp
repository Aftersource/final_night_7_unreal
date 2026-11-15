#include "FNVoiceOver.h"

#include "FNHudPrefs.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

namespace FN7VoiceImpl
{
	using FN7Voice::ELine;
	static const TCHAR* Asset(ELine L)
	{
		static const TCHAR* A[] = {
			TEXT("/Game/Audio/VO/VO_Welcome.VO_Welcome"),     // fn7_welcome_godnarrator_god.wav
			TEXT("/Game/Audio/VO/VO_Intro.VO_Intro"),         // fn7_intro_god.wav
			TEXT("/Game/Audio/VO/VO_Clash.VO_Clash"),         // fn7_clash_god.wav
			TEXT("/Game/Audio/VO/VO_Victory.VO_Victory"),     // fn7_victory_god.wav
			TEXT("/Game/Audio/VO/VO_Defeat.VO_Defeat"),       // fn7_defeat_god.wav
			TEXT("/Game/Audio/VO/VO_DuelWon.VO_DuelWon"),     // fn7_duel_won_god.wav
			TEXT("/Game/Audio/VO/VO_DuelLost.VO_DuelLost"),   // fn7_duel_lost_god.wav
			TEXT("/Game/Audio/VO/VO_TimeSlowed.VO_TimeSlowed"),             // fn7_time_slowed_god.wav (Time)
			TEXT("/Game/Audio/VO/VO_OrdersCut.VO_OrdersCut"),               // fn7_orders_cut_god.wav (Time)
			TEXT("/Game/Audio/VO/VO_FinalWish.VO_FinalWish"),               // fn7_final_wish_god.wav
			TEXT("/Game/Audio/VO/VO_HeatDeathNear.VO_HeatDeathNear"),       // fn7_heat_death_near_god.wav (Entropy)
			TEXT("/Game/Audio/VO/VO_DemigodFalls.VO_DemigodFalls"),         // fn7_demigod_falls_god.wav
			TEXT("/Game/Audio/VO/VO_YourDemigodFalls.VO_YourDemigodFalls"), // fn7_your_demigod_falls_god.wav
			TEXT("/Game/Audio/VO/VO_RaisedByDeath.VO_RaisedByDeath"),       // fn7_raised_by_death_god.wav (Death)
			TEXT("/Game/Audio/VO/VO_Challenged.VO_Challenged"),             // fn7_challenged_god.wav
			TEXT("/Game/Audio/VO/VO_Knockout.VO_Knockout") };               // fn7_knockout_god.wav
		return (int32)L < UE_ARRAY_COUNT(A) ? A[(int32)L] : nullptr;
	}
	static TWeakObjectPtr<UAudioComponent> Current;   // rooted while it speaks
	static void Release() { if (Current.IsValid()) { Current->RemoveFromRoot(); } Current.Reset(); }
	static ELine CurrentLine = ELine::Count;
	static TArray<ELine> Queue;
	static TSet<FString> Seen;
	static FTSTicker::FDelegateHandle Pump;
	static bool bSkipHooked = false;

	/** Any key skips the line being spoken; the key still goes on to the game (nothing is eaten). */
	class FSkip : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
		{
			if (!E.IsRepeat() && Current.IsValid() && Current->IsPlaying()) FN7Voice::Skip();
			return false;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("FN7VoiceSkip"); }
	};

	static UWorld* World()
	{
		if (GEngine && GEngine->GameViewport) if (UWorld* W = GEngine->GameViewport->GetWorld()) return W;
		if (GEngine) for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.World() && (C.WorldType == EWorldType::Game || C.WorldType == EWorldType::PIE)) return C.World();
		return nullptr;
	}
	static float Volume()
	{
		return FMath::Clamp(FN7HudPrefs::GetInt(TEXT("vol_master_pct"), 80) / 100.f * FN7HudPrefs::GetInt(TEXT("vol_voice_pct"), 100) / 100.f, 0.f, 1.f);
	}
	static void Start(ELine L)
	{
		CurrentLine = L;
		USoundWave* W = LoadObject<USoundWave>(nullptr, Asset(L));
		UWorld* Wd = World();
		const float Vol = Volume();
		if (!W || !Wd || Vol <= 0.001f)
		{
			UE_LOG(LogTemp, Display, TEXT("FN7 voice: %s not spoken (%s)"), FN7Voice::Name(L), !W ? TEXT("no asset") : !Wd ? TEXT("no world") : TEXT("voice volume 0"));
			Release();
			return;
		}
		UAudioComponent* C = UGameplayStatics::CreateSound2D(Wd, W, Vol, 1.f, 0.f, nullptr, false, false);
		UE_LOG(LogTemp, Display, TEXT("FN7 voice: %s (%s, %.1f s) at %.2f (master %d%%, voice %d%%)%s"), FN7Voice::Name(L), *W->GetName(), W->Duration, Vol,
			FN7HudPrefs::GetInt(TEXT("vol_master_pct"), 80), FN7HudPrefs::GetInt(TEXT("vol_voice_pct"), 100), C ? TEXT("") : TEXT(" [no audio device]"));
		if (!C) { Release(); return; }
		C->bIsUISound = true;   // keeps speaking through a pause
		C->AddToRoot();
		C->Play();
		Current = C;
	}
	static bool Tick(float)
	{
		if (Current.IsValid() && Current->IsPlaying()) return true;
		Release();
		CurrentLine = ELine::Count;
		if (Queue.Num() == 0) { Pump.Reset(); return false; }
		const ELine Next = Queue[0];
		Queue.RemoveAt(0);
		Start(Next);
		return true;
	}
}

const TCHAR* FN7Voice::Name(ELine L)
{
	static const TCHAR* N[] = { TEXT("welcome"), TEXT("intro"), TEXT("clash"), TEXT("victory"), TEXT("defeat"), TEXT("duel won"), TEXT("duel lost"),
		TEXT("time slowed"), TEXT("orders cut"), TEXT("final wish"), TEXT("heat death near"), TEXT("a demigod falls"), TEXT("your demigod falls"), TEXT("raised by death"), TEXT("challenged"), TEXT("knockout") };
	return (int32)L < UE_ARRAY_COUNT(N) ? N[(int32)L] : TEXT("?");
}

void FN7Voice::Say(ELine L)
{
	using namespace FN7VoiceImpl;
	if (L >= ELine::Count) return;
	if (!bSkipHooked && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(MakeShared<FSkip>());
		bSkipHooked = true;
	}
	// one at a time: a line already speaking or waiting is not queued twice; at most three wait
	if (L == CurrentLine && Current.IsValid() && Current->IsPlaying()) return;
	if (Queue.Contains(L) || Queue.Num() >= 3) return;
	Queue.Add(L);
	if (!Pump.IsValid()) Pump = FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7Voice"), 0.1f, &FN7VoiceImpl::Tick);
	if (!(Current.IsValid() && Current->IsPlaying())) Tick(0.f);
	else UE_LOG(LogTemp, Display, TEXT("FN7 voice: %s waits for %s"), Name(L), Name(CurrentLine));
}

void FN7Voice::SayOnce(ELine L, const FString& Key)
{
	const FString K = FString::Printf(TEXT("%d:%s"), (int32)L, *Key);
	if (FN7VoiceImpl::Seen.Contains(K)) return;
	FN7VoiceImpl::Seen.Add(K);
	Say(L);
}

void FN7Voice::Skip()
{
	using namespace FN7VoiceImpl;
	if (!Current.IsValid()) return;
	UE_LOG(LogTemp, Display, TEXT("FN7 voice: %s skipped"), Name(CurrentLine));
	Current->Stop();
	Release();
}
