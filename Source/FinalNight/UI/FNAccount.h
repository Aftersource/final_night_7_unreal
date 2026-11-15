#pragma once

#include "CoreMinimal.h"

class UFNNetClient;
class FJsonObject;

/**
 * The player's account on the game server (ACCOUNTS.md, CLIENT_API.md "Accounts, settings and demigod personalities"):
 * a guest from a name, Google sign-in through the system browser and a poll, the settings document, the two demigod
 * sheets, the god form, and the bind that makes this websocket this account (right after auth.ok, before any room).
 *
 * The session token is the account. It is kept in one file only, Saved/FN7Account/session (git-ignored Saved/, the
 * file user-only), sent only as the Authorization header over HTTP to the game server, and never logged, never put in
 * a URL and never sent on the websocket. The Google poll token lives in memory only.
 */
namespace FN7Account
{
	struct FConfig
	{
		bool bKnown = false;     // the server answered /api/auth/config
		bool bGuest = false;     // guest accounts offered
		bool bGoogle = false;    // the Google button
		FString Moderation;      // "rules" or the model's name
	};
	struct FSession
	{
		FString Id, Device;
		double LastSeen = 0.0;
		bool bCurrent = false;
	};
	struct FAccount
	{
		bool bValid = false;
		FString Id, DisplayName;
		bool bGuest = true;
		bool bGoogle = false;    // a Google identity is linked
		TArray<FSession> Sessions;
	};

	/** code 0 = no answer (the server unreachable); Body is the parsed JSON (nullptr when not JSON); RetryAfter in s. */
	using FDone = TFunction<void(int32 Code, TSharedPtr<FJsonObject> Body)>;

	FINALNIGHT_API const FConfig& Config();
	FINALNIGHT_API const FAccount& Me();
	FINALNIGHT_API bool HasToken();
	FINALNIGHT_API bool IsSignedIn();
	/** "Final Night 7 on Mac": the session list's device label (no computer or user name leaves the machine). */
	FINALNIGHT_API FString DeviceLabel();
	/** Seconds from the last 429's Retry-After (0 when none). */
	FINALNIGHT_API int32 LastRetryAfter();
	/** The error's human text ("detail"), else its code, else a fallback. */
	FINALNIGHT_API FString ErrorText(int32 Code, const TSharedPtr<FJsonObject>& Body);

	/** Boot (§13 step 1): a stored token -> GET /api/me (401 drops it); then GET /api/auth/config. Done(signed in). */
	FINALNIGHT_API void Boot(UFNNetClient* Net, TFunction<void(bool bSignedIn)> Done);
	FINALNIGHT_API void FetchConfig(UFNNetClient* Net, FDone Done);
	FINALNIGHT_API void FetchMe(UFNNetClient* Net, FDone Done);
	/** POST /api/auth/guest: on 201 the token is stored and Me() filled. */
	FINALNIGHT_API void Guest(UFNNetClient* Net, const FString& Name, const FString& ClaimSecret, FDone Done);
	/** POST /api/auth/google/start (bLink: with the session, linking Google to this guest), then polls every interval
	 *  until complete / failed / expired; the login URL opens in the system browser. Done(200 complete | other). */
	FINALNIGHT_API void Google(UFNNetClient* Net, bool bLink, const FString& Name, FDone Done);
	FINALNIGHT_API void CancelGoogle();
	FINALNIGHT_API bool IsGoogleInFlight();
	/** POST /api/auth/logout; the token is dropped whatever the answer. */
	FINALNIGHT_API void Logout(UFNNetClient* Net, FDone Done);

	/** Any account route with the session (GET/PUT/POST/DELETE; Body may be null). */
	FINALNIGHT_API void Call(UFNNetClient* Net, const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, FDone Done);
	/** POST /api/game/bind {resume_token} (after auth.ok of an auth.login, before room.join). */
	FINALNIGHT_API void Bind(UFNNetClient* Net, const FString& ResumeToken, FDone Done);
}
