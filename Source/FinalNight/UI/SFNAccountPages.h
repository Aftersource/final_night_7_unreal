#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UFNNetClient;
class SFNTextBox;
class SEditableTextBox;
class SBox;
class FJsonObject;

/**
 * The account screens, each on the lobby's tablet with its own six looks (FNLooks; Chris: six to pick a favourite):
 * WELCOME (ACCOUNTS.md §13 steps 2-4: a name, PLAY AS GUEST, SIGN IN WITH GOOGLE when the server offers it).
 * SETTINGS (§6: the whole document, GET on open, PUT on APPLY; the server's detail shown on a refusal).
 * DEMIGODS (§7: the two sheets, Left and Right; one moderation pass per save, answered ok / rephrase / refuse).
 * Every page: a header (title, the screen's word, the 1-6 switch), its look's ink, the same calls whatever the look.
 */
namespace FN7AccountUi
{
	/** "FINAL NIGHT 7  WELCOME  [1..6]": the page header with its look switch (Screen = the look's key). */
	TSharedRef<SWidget> Header(const FText& Title, const FText& Word, const FString& Screen, int32* Look);
}

class FINALNIGHT_API SFNWelcomePage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNWelcomePage) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
		SLATE_ARGUMENT(FString, Name)
		/** Signed in (guest or Google): on to the lobby. */
		SLATE_EVENT(FSimpleDelegate, OnSignedIn)
		/** "play without an account": the lobby as before (the name the lobby keeps). */
		SLATE_EVENT(FSimpleDelegate, OnSkip)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	int32 GetLook() const { return Look; }
	/** A message to show on arrival (the session ended, the name is reserved...). */
	void SetNote(const FString& Note) { Error = Note; }

private:
	TWeakObjectPtr<UFNNetClient> Net;
	FSimpleDelegate OnSignedIn, OnSkip;
	int32 Look = 1;
	TSharedPtr<SFNTextBox> NameBox;
	TSharedPtr<SEditableTextBox> SecretBox;
	FString Error;
	bool bBusy = false;
	bool bNeedSecret = false;
	bool bGoogleWait = false;

	void PlayAsGuest();
	void SignInGoogle();
	FSlateColor Ink() const;
	FSlateColor Dim() const;
	FSlateColor Accent() const;
};

class FINALNIGHT_API SFNSettingsPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNSettingsPage) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	int32 GetLook() const { return Look; }
	/** GET /api/me/settings and show it (called each time the page opens). */
	void Open();

private:
	TWeakObjectPtr<UFNNetClient> Net;
	FSimpleDelegate OnBack;
	int32 Look = 1;
	int32 Tab = 0;
	TSharedPtr<FJsonObject> Doc;     // the document being edited (PUT whole)
	TSharedPtr<FJsonObject> Server;  // the last copy the server answered (REVERT)
	FString Status;
	bool bBad = false;
	bool bBusy = false;
	TSharedPtr<SBox> Body;
	TSharedPtr<SFNTextBox> BindAction, BindChord;

	void Rebuild();
	void Apply();
	TSharedRef<SWidget> TabBody(int32 InTab);
	TSharedPtr<FJsonObject> Sec(const FString& Key) const;
	TSharedRef<SWidget> Row(const FText& Label, TSharedRef<SWidget> Control) const;
	TSharedRef<SWidget> Slider(const FText& Label, const FString& SecKey, const FString& Key, float Min, float Max, float Def, bool bPercent);
	TSharedRef<SWidget> Toggle(const FText& Label, const FString& SecKey, const FString& Key, bool bDef);
	TSharedRef<SWidget> Choice(const FText& Label, const FString& SecKey, const FString& Key, const TArray<FString>& Values, const TArray<FText>& Labels);
	TSharedRef<SWidget> Field(const FText& Label, const FString& SecKey, const FString& Key, const FText& Hint, int32 MaxLen);
	TSharedRef<SWidget> Keybinds();
	FSlateColor Ink() const;
	FSlateColor Dim() const;
	FSlateColor Accent() const;
};

class FINALNIGHT_API SFNDemigodsPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNDemigodsPage) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	int32 GetLook() const { return Look; }
	/** GET /api/me/demigods and fill both cards. */
	void Open();

private:
	struct FCard
	{
		FString Hand;   // left / right
		FString MatchName;
		TSharedPtr<SFNTextBox> Name, Personality, Style, Creed;
		FString Status;
		bool bBad = false;
		bool bBusy = false;
		// A rephrase: what you wrote, what was saved, and why (KEEP = nothing to do, EDIT = yours back in the editor).
		TSharedPtr<FJsonObject> Original, Final;
		FString Reason;
	};
	TWeakObjectPtr<UFNNetClient> Net;
	FSimpleDelegate OnBack;
	int32 Look = 1;
	FCard Cards[2];
	int32 Limit[4] = { 32, 600, 200, 200 };   // name, personality, style, creed

	TSharedRef<SWidget> MakeCard(int32 i);
	TSharedRef<SWidget> Labeled(const FText& Label, TSharedPtr<SFNTextBox>& Box, int32 LimitIdx, bool bMulti, float Height, const FText& Hint);
	TSharedRef<SWidget> Rephrased(int32 i);
	void Fill(int32 i, const TSharedPtr<FJsonObject>& Demigod);
	void SetTexts(int32 i, const TSharedPtr<FJsonObject>& Sheet);
	void Save(int32 i);
	FSlateColor Ink() const;
	FSlateColor Dim() const;
	FSlateColor Accent() const;
};
