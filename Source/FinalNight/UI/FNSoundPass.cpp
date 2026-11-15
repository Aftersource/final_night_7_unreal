#include "FNSoundPass.h"

#include "FNHudPrefs.h"
#include "FNStudio.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace FN7SoundImpl
{
	static constexpr int32 Rate = 44100;
	static constexpr float TwoPi = 6.28318530718f;
	static TArray<float> Make(float Seconds) { TArray<float> A; A.SetNumZeroed((int32)(Seconds * Rate)); return A; }

	static TArray<TArray<float>>& Bank()
	{
		static TArray<TArray<float>> B;
		if (B.Num() > 0) return B;
		B.SetNum((int32)FN7Sound::ESnd::Count);
		FRandomStream R(777);
		using FN7Sound::ESnd;
		{ // MiniWhoosh: a soft breath of air across the board
			TArray<float> A = Make(0.55f); float L1 = 0.f, L2 = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate, u = t / 0.55f, n = R.FRandRange(-1.f, 1.f), a = 0.04f + 0.22f * u;
				L1 += a * (n - L1); L2 += 0.5f * a * (L1 - L2);
				A[i] = 0.5f * (L1 - L2) * FMath::Sin(PI * u);
			}
			B[(int32)ESnd::MiniWhoosh] = MoveTemp(A);
		}
		{ // MegaWhoosh: a deep rush falling out of the sky
			TArray<float> A = Make(1.6f); float L1 = 0.f, L2 = 0.f, Ph = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate, u = t / 1.6f, n = R.FRandRange(-1.f, 1.f), a = 0.01f + 0.12f * u;
				L1 += a * (n - L1); L2 += 0.5f * a * (L1 - L2);
				Ph += TwoPi * (90.f - 50.f * u) / Rate;
				A[i] = (0.9f * (L1 - L2) + 0.25f * FMath::Sin(Ph) * u) * FMath::Sin(PI * FMath::Min(1.f, u * 1.15f));
			}
			B[(int32)ESnd::MegaWhoosh] = MoveTemp(A);
		}
		{ // Rumble: the ground heaving, low and uneven
			TArray<float> A = Make(2.4f); float L1 = 0.f, L2 = 0.f, Ph = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate, n = R.FRandRange(-1.f, 1.f);
				L1 += 0.012f * (n - L1); L2 += 0.012f * (L1 - L2);
				Ph += TwoPi * (38.f + 6.f * FMath::Sin(TwoPi * 1.3f * t)) / Rate;
				const float Env = FMath::Min(1.f, t / 0.25f) * FMath::Exp(-FMath::Max(0.f, t - 0.9f) * 2.f);
				A[i] = Env * (3.2f * L2 * (0.7f + 0.3f * FMath::Sin(TwoPi * 5.f * t)) + 0.22f * FMath::Sin(Ph));
			}
			B[(int32)ESnd::Rumble] = MoveTemp(A);
		}
		{ // Clash: steel on steel, inharmonic partials over a bright burst
			TArray<float> A = Make(1.1f);
			const float F[6] = { 523.f, 1187.f, 1673.f, 2311.f, 3101.f, 4217.f }, Amp[6] = { 0.16f, 0.12f, 0.1f, 0.08f, 0.06f, 0.04f }, Dec[6] = { 3.f, 4.5f, 6.f, 8.f, 11.f, 15.f };
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate; float v = 0.f;
				for (int32 k = 0; k < 6; ++k) v += Amp[k] * FMath::Sin(TwoPi * F[k] * t + k) * FMath::Exp(-t * Dec[k]);
				A[i] = v + 0.35f * R.FRandRange(-1.f, 1.f) * FMath::Exp(-t * 60.f);
			}
			B[(int32)ESnd::Clash] = MoveTemp(A);
		}
		{ // Thump: a blow that lands
			TArray<float> A = Make(0.3f); float Ph = 0.f, L = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate;
				Ph += TwoPi * (60.f + 110.f * FMath::Exp(-t * 30.f)) / Rate;
				L += 0.25f * (R.FRandRange(-1.f, 1.f) - L);
				A[i] = 0.6f * FMath::Sin(Ph) * FMath::Exp(-t * 14.f) + 0.35f * L * FMath::Exp(-t * 45.f);
			}
			B[(int32)ESnd::Thump] = MoveTemp(A);
		}
		{ // Swish: a blow through empty air
			TArray<float> A = Make(0.32f); float L1 = 0.f, L2 = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate, u = t / 0.32f, n = R.FRandRange(-1.f, 1.f), a = 0.1f + 0.5f * FMath::Sin(PI * u);
				L1 += a * (n - L1); L2 += 0.6f * a * (L1 - L2);
				A[i] = 0.35f * (L1 - L2) * FMath::Sin(PI * u);
			}
			B[(int32)ESnd::Swish] = MoveTemp(A);
		}
		{ // Puff: a whiff, nothing there
			TArray<float> A = Make(0.22f); float L = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate;
				L += 0.08f * (R.FRandRange(-1.f, 1.f) - L);
				A[i] = 0.7f * L * FMath::Min(1.f, t / 0.02f) * FMath::Exp(-t * 18.f);
			}
			B[(int32)ESnd::Puff] = MoveTemp(A);
		}
		{ // Victory: a rising major chord, notes stacking up and ringing
			TArray<float> A = Make(2.6f);
			const float N[4] = { 261.63f, 329.63f, 392.f, 523.25f };
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate; float v = 0.f;
				for (int32 k = 0; k < 4; ++k)
				{
					const float tk = t - 0.18f * k;
					if (tk <= 0.f) continue;
					const float Env = FMath::Min(1.f, tk / 0.03f) * FMath::Exp(-tk * 1.1f);
					v += 0.09f * Env * (FMath::Sin(TwoPi * N[k] * tk) + 0.4f * FMath::Sin(TwoPi * 2.f * N[k] * tk) + 0.15f * FMath::Sin(TwoPi * 3.f * N[k] * tk));
				}
				A[i] = v;
			}
			B[(int32)ESnd::Victory] = MoveTemp(A);
		}
		{ // Fall: a low tone sinking away
			TArray<float> A = Make(1.8f); float Ph = 0.f;
			for (int32 i = 0; i < A.Num(); ++i)
			{
				const float t = (float)i / Rate;
				Ph += TwoPi * (110.f * FMath::Pow(0.5f, t / 1.4f)) / Rate;
				A[i] = 0.32f * (FMath::Sin(Ph) + 0.3f * FMath::Sin(2.f * Ph)) * FMath::Min(1.f, t / 0.05f) * FMath::Exp(-t * 1.6f);
			}
			B[(int32)ESnd::Fall] = MoveTemp(A);
		}
		return B;
	}
}

const TCHAR* FN7Sound::Name(ESnd S)
{
	static const TCHAR* N[] = { TEXT("mini whoosh"), TEXT("mega whoosh"), TEXT("rumble"), TEXT("clash"), TEXT("thump"), TEXT("swish"), TEXT("puff"), TEXT("victory"), TEXT("fall") };
	return (int32)S < UE_ARRAY_COUNT(N) ? N[(int32)S] : TEXT("?");
}

void FN7Sound::Play(UWorld* World, ESnd Snd, float Gain, float DelaySeconds)
{
	if (!World || Snd >= ESnd::Count) return;
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(World); It) St = *It;
	const TArray<TArray<float>>& B = FN7SoundImpl::Bank();
	UE_LOG(LogTemp, Display, TEXT("FN7 sound: %s x%.2f (master %d%%, sfx %d%%)%s"), Name(Snd), Gain, FN7HudPrefs::GetInt(TEXT("vol_master_pct"), 80), FN7HudPrefs::GetInt(TEXT("vol_sfx_pct"), 80), St ? TEXT("") : TEXT(" [no studio]"));
	if (St) St->PlayBuffer(&B[(int32)Snd], Gain, DelaySeconds);
}

void FN7Sound::WriteWavs(const FString& Dir)
{
	const TArray<TArray<float>>& B = FN7SoundImpl::Bank();
	IFileManager::Get().MakeDirectory(*Dir, true);
	for (int32 s = 0; s < B.Num(); ++s)
	{
		const TArray<float>& A = B[s];
		TArray<uint8> W;
		auto U32 = [&W](uint32 V) { W.Append(reinterpret_cast<const uint8*>(&V), 4); };
		auto U16 = [&W](uint16 V) { W.Append(reinterpret_cast<const uint8*>(&V), 2); };
		const uint32 Data = A.Num() * 2;
		W.Append(reinterpret_cast<const uint8*>("RIFF"), 4); U32(36 + Data); W.Append(reinterpret_cast<const uint8*>("WAVE"), 4);
		W.Append(reinterpret_cast<const uint8*>("fmt "), 4); U32(16); U16(1); U16(1); U32(FN7SoundImpl::Rate); U32(FN7SoundImpl::Rate * 2); U16(2); U16(16);
		W.Append(reinterpret_cast<const uint8*>("data"), 4); U32(Data);
		for (const float F : A) { const int16 V = (int16)FMath::Clamp(F * 30000.f, -32767.f, 32767.f); W.Append(reinterpret_cast<const uint8*>(&V), 2); }
		FFileHelper::SaveArrayToFile(W, *FPaths::Combine(Dir, FString(Name((ESnd)s)).Replace(TEXT(" "), TEXT("_")) + TEXT(".wav")));
	}
}
