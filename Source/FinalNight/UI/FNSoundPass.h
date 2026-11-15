#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * The sound pass: small procedural sounds for the game's moments, played through AFNStudio's own synth voices
 * (PlayBuffer), on the account's volumes (master x sfx; the ambient bed rides master x music and swells with
 * Entropy). No assets: every buffer is made once in code, mono 44.1 kHz.
 *  MiniWhoosh / MegaWhoosh  a power cast (a mini soft, a mega deep, with the studio's Impact under it)
 *  Rumble                   the ground heaving (a quake on the board)
 *  Clash                    a duel locks: steel on steel
 *  Thump / Swish / Puff     a blow lands / misses / whiffs ("you have no pistol")
 *  Victory / Fall           a rising chord when your side wins / a low falling tone when one of yours falls
 */
namespace FN7Sound
{
	enum class ESnd : uint8 { MiniWhoosh, MegaWhoosh, Rumble, Clash, Thump, Swish, Puff, Victory, Fall, Count };
	FINALNIGHT_API void Play(UWorld* World, ESnd Snd, float Gain = 1.f, float DelaySeconds = 0.f);
	FINALNIGHT_API const TCHAR* Name(ESnd Snd);
	/** Every buffer as a 16-bit WAV in Dir (to listen to them outside the game). */
	FINALNIGHT_API void WriteWavs(const FString& Dir);
}
