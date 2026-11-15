#include "FNDuelRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FN7DuelWords
{
	// The server's lists, as they are (crates/fn7-plugin/src/duel.rs); keep in step when they change.
	static const TCHAR* Verbs[] = {
		TEXT("slash"), TEXT("stab"), TEXT("punch"), TEXT("kick"), TEXT("hurl"), TEXT("throw"), TEXT("parry"), TEXT("feint"), TEXT("grab"), TEXT("bite"), TEXT("smash"), TEXT("sweep"), TEXT("strike"),
		TEXT("slam"), TEXT("crush"), TEXT("choke"), TEXT("trip"), TEXT("shove"), TEXT("headbutt"), TEXT("uppercut"), TEXT("jab"), TEXT("hook"), TEXT("lunge"), TEXT("dodge"), TEXT("block"), TEXT("swing"),
		TEXT("cleave"), TEXT("pierce"), TEXT("burn"), TEXT("freeze"), TEXT("blast"), TEXT("bash"), TEXT("whip"), TEXT("lash"), TEXT("spin"), TEXT("tackle"), TEXT("toss"), TEXT("fling"), TEXT("rake"),
		TEXT("gouge"), TEXT("claw"), TEXT("stomp"), TEXT("wrestle"), TEXT("flip"), TEXT("twist"), TEXT("snap"), TEXT("break"), TEXT("shatter"), TEXT("curse"), TEXT("hex"), TEXT("mock"), TEXT("taunt"),
		TEXT("roast"), TEXT("drown"), TEXT("bury"), TEXT("impale"), TEXT("skewer"), TEXT("sucker"), TEXT("body-slam"), TEXT("suplex"), TEXT("backhand"), TEXT("pummel"), TEXT("batter") };
	static const TCHAR* Body[] = {
		TEXT("jaw"), TEXT("chin"), TEXT("nose"), TEXT("eye"), TEXT("eyes"), TEXT("ear"), TEXT("ears"), TEXT("teeth"), TEXT("tooth"), TEXT("throat"), TEXT("neck"), TEXT("shoulder"), TEXT("arm"), TEXT("elbow"),
		TEXT("wrist"), TEXT("hand"), TEXT("fist"), TEXT("finger"), TEXT("fingers"), TEXT("chest"), TEXT("ribs"), TEXT("rib"), TEXT("gut"), TEXT("belly"), TEXT("stomach"), TEXT("spine"), TEXT("back"),
		TEXT("hip"), TEXT("knee"), TEXT("knees"), TEXT("shin"), TEXT("ankle"), TEXT("foot"), TEXT("feet"), TEXT("toe"), TEXT("toes"), TEXT("skull"), TEXT("head"), TEXT("temple"), TEXT("brow"), TEXT("heart"),
		TEXT("lung"), TEXT("kidney"), TEXT("thigh"), TEXT("beard"), TEXT("hair"), TEXT("face"), TEXT("mouth"), TEXT("tongue"), TEXT("lip"), TEXT("lips"), TEXT("ego"), TEXT("pride") };
	static const TCHAR* Terrain[] = {
		TEXT("river"), TEXT("sea"), TEXT("shore"), TEXT("beach"), TEXT("sand"), TEXT("mud"), TEXT("rock"), TEXT("stone"), TEXT("cliff"), TEXT("hill"), TEXT("mountain"), TEXT("forest"), TEXT("tree"),
		TEXT("grass"), TEXT("field"), TEXT("road"), TEXT("street"), TEXT("wall"), TEXT("roof"), TEXT("bridge"), TEXT("market"), TEXT("rain"), TEXT("snow"), TEXT("fog"), TEXT("wind"), TEXT("storm"),
		TEXT("dust"), TEXT("ice"), TEXT("ash"), TEXT("moon"), TEXT("sun"), TEXT("shadow"), TEXT("lantern"), TEXT("gutter"), TEXT("puddle"), TEXT("ruins"), TEXT("rubble"), TEXT("temple"), TEXT("shrine"),
		TEXT("tower"), TEXT("gate"), TEXT("square"), TEXT("fountain"), TEXT("alley"), TEXT("harbor"), TEXT("harbour"), TEXT("dock"), TEXT("lake"), TEXT("swamp"), TEXT("desert"), TEXT("dune") };
	static const TCHAR* Stop[] = {
		TEXT("the"), TEXT("and"), TEXT("with"), TEXT("your"), TEXT("you"), TEXT("that"), TEXT("this"), TEXT("from"), TEXT("into"), TEXT("left"), TEXT("right"), TEXT("hand"), TEXT("hands"), TEXT("a"), TEXT("an"),
		TEXT("for"), TEXT("his"), TEXT("her"), TEXT("their"), TEXT("them"), TEXT("then"), TEXT("they"), TEXT("what"), TEXT("when"), TEXT("will"), TEXT("have"), TEXT("just"), TEXT("like"), TEXT("over") };

	template <int32 N> static bool In(const TCHAR* (&L)[N], const FString& W) { for (const TCHAR* X : L) if (W == X) return true; return false; }

	/** The server's words(): split on anything but letters, digits and '-', trim '-', lower case. */
	static TArray<FString> Words(const FString& S)
	{
		TArray<FString> Out;
		FString Cur;
		auto Push = [&]() { FString W = Cur; while (W.StartsWith(TEXT("-"))) W.RightChopInline(1); while (W.EndsWith(TEXT("-"))) W.LeftChopInline(1); if (!W.IsEmpty()) Out.Add(W.ToLower()); Cur.Reset(); };
		for (const TCHAR C : S) { if (FChar::IsAlnum(C) || C == TEXT('-')) Cur.AppendChar(C); else Push(); }
		Push();
		return Out;
	}
}

static double JNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0) { double V = Def; if (O.IsValid()) O->TryGetNumberField(K, V); return V; }
static FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O.IsValid()) O->TryGetStringField(K, V); return V; }

FString FN7DuelRules::Breakdown(const TSharedPtr<FJsonObject>& B)
{
	if (!B.IsValid()) return FString();
	// What the words earned.
	TArray<FString> Parts;
	int32 TagSum = 0;
	const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
	if (B->TryGetArrayField(TEXT("tags"), Tags))
		for (const TSharedPtr<FJsonValue>& V : *Tags)
		{
			const FString T = V->AsString();
			const int32 Val = T == TEXT("repeat") ? -2 : (T == TEXT("echo") || T == TEXT("short") || T == TEXT("spam") || T == TEXT("gibberish")) ? -1 : T == TEXT("empty") ? -3 : 1;
			TagSum += Val;
			Parts.Add(FString::Printf(TEXT("%+d %s"), Val, T == TEXT("terrain") ? TEXT("place") : *T));
		}
	const TSharedPtr<FJsonObject>* Mods = nullptr;
	B->TryGetObjectField(TEXT("modifiers"), Mods);
	const TSharedPtr<FJsonObject> M = Mods ? *Mods : nullptr;
	const int32 Wit = (int32)JNum(M, TEXT("wit"), JNum(B, TEXT("wit"), (double)TagSum));
	FString Out = FString::Join(Parts, TEXT(" "));
	if (Wit != TagSum || Parts.Num() == 0) Out += FString::Printf(TEXT("%swit %+d"), Out.IsEmpty() ? TEXT("") : TEXT(" = "), Wit);   // the server caps wit at -3..+4
	// The rest of the modifier.
	TArray<FString> Rest;
	const int32 Health = (int32)JNum(M, TEXT("health"));
	if (Health > 0) Rest.Add(FString::Printf(TEXT("hale %+d"), Health)); else if (Health < 0) Rest.Add(FString::Printf(TEXT("hurt %+d"), Health));
	for (const TPair<const TCHAR*, const TCHAR*>& K : { TPair<const TCHAR*, const TCHAR*>(TEXT("gear"), TEXT("gear")), { TEXT("follow_up"), TEXT("follow-up") }, { TEXT("home"), TEXT("home") }, { TEXT("situation"), TEXT("situation") }, { TEXT("handicap"), TEXT("handicap") } })
	{
		const int32 V = (int32)JNum(M, K.Key);
		if (V != 0) Rest.Add(FString::Printf(TEXT("%s %+d"), K.Value, V));
	}
	if (Rest.Num()) Out += (Out.IsEmpty() ? TEXT("") : TEXT("  |  ")) + FString::Join(Rest, TEXT("  "));
	// The die and the verdict.
	const FString Verdict = JStr(B, TEXT("verdict"));
	const int32 Roll = (int32)JNum(B, TEXT("roll")), Mod = (int32)JNum(B, TEXT("modifier")), Dc = (int32)JNum(B, TEXT("target_dc"), 10.0);
	const int32 Total = (int32)JNum(B, TEXT("total"), (double)(Roll + Mod));
	const double Dmg = JNum(B, TEXT("damage"));
	const FString DmgS = FMath::IsNearlyEqual(Dmg, FMath::RoundToDouble(Dmg)) ? FString::Printf(TEXT("%d"), (int32)FMath::RoundToDouble(Dmg)) : FString::Printf(TEXT("%.1f"), Dmg);
	FString V;
	if (Verdict == TEXT("penalty")) V = TEXT("TOO FAST: -0.5 to you");
	else if (Verdict == TEXT("crit_success")) V = FString::Printf(TEXT("CRIT! HIT %s"), *DmgS);
	else if (Verdict == TEXT("success")) V = FString::Printf(TEXT("HIT %s"), *DmgS);
	else if (Verdict == TEXT("crit_fail")) V = TEXT("FUMBLE");
	else V = TEXT("MISS");
	const FString Die = Verdict == TEXT("penalty") ? FString(TEXT("no roll")) : FString::Printf(TEXT("d20 %d %s %d = %d vs %d"), Roll, Mod < 0 ? TEXT("-") : TEXT("+"), FMath::Abs(Mod), Total, Dc);
	Out += (Out.IsEmpty() ? TEXT("") : TEXT("  |  ")) + Die + TEXT("  →  ") + V;
	return Out;
}

int32 FN7DuelRules::Tone(const TSharedPtr<FJsonObject>& B)
{
	const FString V = JStr(B, TEXT("verdict"));
	if ((int32)JNum(B, TEXT("roll")) == 20 || V == TEXT("crit_success")) return 2;
	return V == TEXT("success") ? 0 : 1;
}

FLinearColor FN7DuelRules::ToneColor(int32 T)
{
	return T == 2 ? FLinearColor(1.f, 0.78f, 0.25f) : T == 1 ? FLinearColor(0.6f, 0.6f, 0.62f) : FLinearColor(0.55f, 0.95f, 0.55f);
}

TArray<FString> FN7DuelRules::LoreWords(const TArray<FString>& Texts)
{
	using namespace FN7DuelWords;
	TArray<FString> Out;
	for (const FString& T : Texts)
		for (const FString& W : Words(T))
			if (W.Len() >= 4 && !In(Stop, W)) Out.AddUnique(W);
	return Out;
}

FN7DuelRules::FWit FN7DuelRules::Wit(const FString& Line, const TArray<FString>& Items, const TArray<FString>& Place, const TArray<FString>& FoeLore, const TArray<FString>& Said)
{
	using namespace FN7DuelWords;
	FWit R;
	const TArray<FString> Ws = Words(Line);
	if (Ws.Num() == 0) { R.Score = -3; return R; }
	TArray<const TCHAR*> Found;
	for (const FString& W : Ws) for (const TCHAR* V : Verbs) if (W.StartsWith(V) && !Found.Contains(V)) Found.Add(V);
	R.bVerb = Found.Num() >= 1;
	R.bCombo = Found.Num() >= 2;
	for (const FString& W : Ws)
	{
		R.bBody |= In(Body, W);
		R.bItem |= Items.Contains(W);
		R.bPlace |= In(Terrain, W) || Place.Contains(W);
		R.bStory |= FoeLore.Contains(W);
	}
	const FString Norm = FString::Join(Ws, TEXT(" "));
	for (const FString& P : Said) R.bRepeat |= FString::Join(Words(P), TEXT(" ")) == Norm;
	R.bShort = Ws.Num() < 3;
	int32 NoVowel = 0;
	for (const FString& W : Ws) { bool bV = false; for (const TCHAR C : W) bV |= FString(TEXT("aeiouy")).Contains(FString::Chr(C)) || C > 127; if (!bV) ++NoVowel; }
	R.bSpam = NoVowel * 2 > Ws.Num();
	const int32 Up = FMath::Min(4, (int32)R.bVerb + (int32)R.bCombo + (int32)R.bBody + (int32)R.bItem + (int32)R.bPlace + (int32)R.bStory);
	const int32 Down = (R.bRepeat ? 2 : 0) + (R.bShort ? 1 : 0) + (R.bSpam ? 1 : 0);
	R.Score = FMath::Clamp(Up - Down, -3, 4);
	return R;
}
