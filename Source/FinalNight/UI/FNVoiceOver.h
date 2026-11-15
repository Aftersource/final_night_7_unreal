#pragma once

#include "CoreMinimal.h"

/**
 * The narrator's voiceovers (production_assets/audio/vo, the "_god" voice; imported as SoundWaves in
 * Content/Audio/VO): one line at a time (later ones wait their turn), any key skips the one speaking without
 * eating the key, on the account's volumes (master x voice).
 *  Welcome   the signed-in lobby first opens (once a run)      "Welcome, god. Your two hands wait on the table below..."
 *  Intro     a match starts (once a match)                     "Seven nights. Seven wars. ... FINAL NIGHT SEVEN."
 *  Clash     your duel locks                                   "CLASH! Will against will."
 *  Victory   the end screen, your side won                     "The night is yours. ..."
 *  Defeat    the end screen, your side fell                    "Your hands have fallen. ..."
 *  DuelWon   your fighter's duel ends, won                     "Your words cut deeper. The duel is yours."
 *  DuelLost  your fighter's duel ends, lost                    "Outspoken. Outfought. Rise, and speak faster next time."
 */
namespace FN7Voice
{
	enum class ELine : uint8 { Welcome, Intro, Clash, Victory, Defeat, DuelWon, DuelLost,
		// batch 2 (doomcast, 2026-11-15)
		TimeSlowed, OrdersCut, FinalWish, HeatDeathNear, DemigodFalls, YourDemigodFalls, RaisedByDeath, Challenged, Knockout, Count };
	FINALNIGHT_API void Say(ELine Line);
	/** Say it only the first time this run sees Key (a match id, a duel id, "welcome"). */
	FINALNIGHT_API void SayOnce(ELine Line, const FString& Key);
	FINALNIGHT_API void Skip();
	FINALNIGHT_API const TCHAR* Name(ELine Line);
}
