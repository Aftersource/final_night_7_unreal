#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UFNNetClient;
class SFNTextBox;
class SEditableTextBox;

/**
 * The account screens, each on the lobby's tablet with its own six looks (FNLooks; Chris: six to pick a favourite):
 * WELCOME (ACCOUNTS.md §13 steps 2-4: a name, PLAY AS GUEST, SIGN IN WITH GOOGLE when the server offers it).
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
