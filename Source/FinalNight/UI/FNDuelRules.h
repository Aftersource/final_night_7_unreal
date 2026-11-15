#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The duel's rules made visible. Players could not see how the rules work or what they are supposed to type.
 * Every duel.blow carries its die, its modifiers and the tags its words earned (server CLIENT_API "Lines": wit = an
 * action verb +1, a second verb +1, a body part +1, an item the fighter holds +1, a terrain or place word +1, the foe's
 * story +1, at most +4; a repeat -2, an echo -1, under three words -1, gibberish -1). This reads them back as one line,
 * and mirrors the server's word lists for the live wit meter (crates/fn7-plugin/src/duel.rs: VERBS, BODY, TERRAIN).
 */
namespace FN7DuelRules
{
	/** "+1 verb +1 combo +1 body | hale +1 | home +1 | d20 14 +5 = 19 vs 10 -> HIT 2" for a duel.blow (or its counter). */
	FINALNIGHT_API FString Breakdown(const TSharedPtr<FJsonObject>& Blow);
	/** 0 a hit, 1 a miss / fumble / too fast, 2 a natural 20 or a crit. */
	FINALNIGHT_API int32 Tone(const TSharedPtr<FJsonObject>& Blow);
	FINALNIGHT_API FLinearColor ToneColor(int32 Tone);

	/** What the server would see in a line as it is typed (the same rules, client side). */
	struct FWit
	{
		bool bVerb = false, bCombo = false, bBody = false, bItem = false, bPlace = false, bStory = false;
		bool bShort = false, bRepeat = false, bSpam = false;
		int32 Score = 0;   // -3..+4, as the server scores it
	};
	/** Items: the held items' names; Place: the scene's place words; FoeLore: the foe's name and player; Said: your lines so far. */
	FINALNIGHT_API FWit Wit(const FString& Line, const TArray<FString>& Items, const TArray<FString>& Place, const TArray<FString>& FoeLore, const TArray<FString>& Said);
	/** The server's lore words of some text (lower case, 4+ letters, not filler). */
	FINALNIGHT_API TArray<FString> LoreWords(const TArray<FString>& Texts);
}
