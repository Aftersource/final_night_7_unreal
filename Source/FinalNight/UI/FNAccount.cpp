#include "FNAccount.h"

#include "FNNetClient.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#endif

namespace FN7AccountState
{
	static FN7Account::FConfig Config;
	static FN7Account::FAccount Me;
	static FString Token;          // in memory once read; never logged
	static bool bTokenRead = false;
	static int32 RetryAfter = 0;
	// Google: the poll token stays in memory only.
	static FString PollToken;
	static double PollUntil = 0.0;
	static double PollEvery = 2.0;
	static FTSTicker::FDelegateHandle PollTick;
	static uint32 GoogleGen = 0;

	static FString TokenPath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FN7Account"), TEXT("session")); }

	static void ReadToken()
	{
		if (bTokenRead) return;
		bTokenRead = true;
		FString S;
		if (FFileHelper::LoadFileToString(S, *TokenPath())) Token = S.TrimStartAndEnd();
		UE_LOG(LogTemp, Display, TEXT("FN7 account: session file %s"), Token.IsEmpty() ? TEXT("none") : *FString::Printf(TEXT("read (%d chars)"), Token.Len()));
	}

	static void StoreToken(const FString& T)
	{
		Token = T;
		bTokenRead = true;
		const FString Path = TokenPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
#if PLATFORM_MAC || PLATFORM_LINUX
		chmod(TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(FPaths::GetPath(Path))), 0700);
#endif
		FFileHelper::SaveStringToFile(T, *Path);
#if PLATFORM_MAC || PLATFORM_LINUX
		chmod(TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(Path)), 0600);   // the account: this user only
#endif
	}

	static void DropToken()
	{
		Token.Reset();
		bTokenRead = true;
		IFileManager::Get().Delete(*TokenPath(), false, true, true);
		Me = FN7Account::FAccount();
	}

	static TSharedPtr<FJsonObject> Parse(const FString& S)
	{
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(S);
		return FJsonSerializer::Deserialize(R, O) ? O : nullptr;
	}

	static FString Write(const TSharedPtr<FJsonObject>& O)
	{
		FString S;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(O.ToSharedRef(), W);
		return S;
	}

	static void ReadAccount(const TSharedPtr<FJsonObject>& O)
	{
		const TSharedPtr<FJsonObject>* A = nullptr;
		if (!O.IsValid() || !O->TryGetObjectField(TEXT("account"), A) || !A || !A->IsValid()) return;
		FN7Account::FAccount N;
		N.bValid = true;
		(*A)->TryGetStringField(TEXT("id"), N.Id);
		(*A)->TryGetStringField(TEXT("display_name"), N.DisplayName);
		(*A)->TryGetBoolField(TEXT("guest"), N.bGuest);
		const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
		if ((*A)->TryGetArrayField(TEXT("identities"), Ids))
			for (const TSharedPtr<FJsonValue>& V : *Ids)
			{
				FString P;
				if (V.IsValid() && V->AsObject().IsValid() && V->AsObject()->TryGetStringField(TEXT("provider"), P) && P == TEXT("google")) N.bGoogle = true;
			}
		const TArray<TSharedPtr<FJsonValue>>* Ss = nullptr;
		if (O->TryGetArrayField(TEXT("sessions"), Ss))
		{
			for (const TSharedPtr<FJsonValue>& V : *Ss)
			{
				const TSharedPtr<FJsonObject> S = V.IsValid() ? V->AsObject() : nullptr;
				if (!S.IsValid()) continue;
				FN7Account::FSession X;
				S->TryGetStringField(TEXT("id"), X.Id);
				S->TryGetStringField(TEXT("device_label"), X.Device);
				S->TryGetNumberField(TEXT("last_seen_at"), X.LastSeen);
				S->TryGetBoolField(TEXT("current"), X.bCurrent);
				N.Sessions.Add(X);
			}
		}
		else if (Me.bValid && Me.Id == N.Id)
		{
			N.Sessions = Me.Sessions;
		}
		Me = N;
	}

	/** One request. The token goes only in the Authorization header; nothing about it is logged. */
	static void Request(UFNNetClient* Net, const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, bool bAuth, FN7Account::FDone Done)
	{
		if (!Net) { if (Done) Done(0, nullptr); return; }
		ReadToken();
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(Net->MediaUrl(Path));   // the server's origin (+ the tunnel's ?key= gate, as every route)
		Req->SetVerb(Verb);
		Req->SetTimeout(15.f);
		Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
		if (bAuth && !Token.IsEmpty()) Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
		if (Body.IsValid())
		{
			Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Req->SetContentAsString(Write(Body));
		}
		Req->OnProcessRequestComplete().BindLambda([Verb, Path, Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			const int32 Code = (bOk && Resp.IsValid()) ? Resp->GetResponseCode() : 0;
			const TSharedPtr<FJsonObject> O = Resp.IsValid() ? Parse(Resp->GetContentAsString()) : nullptr;
			RetryAfter = Code == 429 && Resp.IsValid() ? FCString::Atoi(*Resp->GetHeader(TEXT("Retry-After"))) : 0;
			FString Err, Detail;
			if (O.IsValid()) { O->TryGetStringField(TEXT("error"), Err); O->TryGetStringField(TEXT("detail"), Detail); }
			UE_LOG(LogTemp, Display, TEXT("FN7 account: %s %s -> %d%s%s"), *Verb, *Path, Code, Err.IsEmpty() ? TEXT("") : *(TEXT(" ") + Err), Detail.IsEmpty() ? TEXT("") : *(TEXT(" (") + Detail + TEXT(")")));
			if (Code == 401 && !Path.StartsWith(TEXT("/api/auth/"))) DropToken();   // the session is gone: back to the welcome
			if (Done) Done(Code, O);
		});
		Req->ProcessRequest();
	}
}

using namespace FN7AccountState;

const FN7Account::FConfig& FN7Account::Config() { return FN7AccountState::Config; }
const FN7Account::FAccount& FN7Account::Me() { return FN7AccountState::Me; }
bool FN7Account::HasToken() { ReadToken(); return !Token.IsEmpty(); }
bool FN7Account::IsSignedIn() { return HasToken() && FN7AccountState::Me.bValid; }
int32 FN7Account::LastRetryAfter() { return RetryAfter; }

FString FN7Account::DeviceLabel()
{
	return FString::Printf(TEXT("Final Night 7 on %s"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
}

FString FN7Account::ErrorText(int32 Code, const TSharedPtr<FJsonObject>& Body)
{
	FString D, E;
	if (Body.IsValid()) { Body->TryGetStringField(TEXT("detail"), D); Body->TryGetStringField(TEXT("error"), E); }
	if (Code == 429) return FString::Printf(TEXT("Too many tries: wait %d s."), FMath::Max(1, RetryAfter));
	if (!D.IsEmpty()) return D;
	if (!E.IsEmpty()) return E.Replace(TEXT("_"), TEXT(" "));
	return Code == 0 ? FString(TEXT("The server isn't answering.")) : FString::Printf(TEXT("The server answered %d."), Code);
}

void FN7Account::FetchConfig(UFNNetClient* Net, FDone Done)
{
	Request(Net, TEXT("GET"), TEXT("/api/auth/config"), nullptr, false, [Done](int32 Code, TSharedPtr<FJsonObject> O)
	{
		FN7AccountState::Config = FConfig();
		if (Code == 200 && O.IsValid())
		{
			FN7AccountState::Config.bKnown = true;
			O->TryGetBoolField(TEXT("guest"), FN7AccountState::Config.bGuest);
			O->TryGetBoolField(TEXT("google"), FN7AccountState::Config.bGoogle);
			O->TryGetStringField(TEXT("moderation"), FN7AccountState::Config.Moderation);
		}
		if (Done) Done(Code, O);
	});
}

void FN7Account::FetchMe(UFNNetClient* Net, FDone Done)
{
	Request(Net, TEXT("GET"), TEXT("/api/me"), nullptr, true, [Done](int32 Code, TSharedPtr<FJsonObject> O)
	{
		if (Code == 200) ReadAccount(O);
		if (Done) Done(Code, O);
	});
}

void FN7Account::Boot(UFNNetClient* Net, TFunction<void(bool)> Done)
{
	TWeakObjectPtr<UFNNetClient> W(Net);
	FetchConfig(Net, [W, Done](int32, TSharedPtr<FJsonObject>)
	{
		if (!HasToken() || !W.IsValid()) { if (Done) Done(false); return; }
		FetchMe(W.Get(), [Done](int32 Code, TSharedPtr<FJsonObject>)
		{
			UE_LOG(LogTemp, Display, TEXT("FN7 account: boot %s"), Code == 200 ? *FString::Printf(TEXT("signed in as %s%s"), *FN7AccountState::Me.DisplayName, FN7AccountState::Me.bGuest ? TEXT(" (guest)") : TEXT("")) : TEXT("not signed in"));
			if (Done) Done(Code == 200);
		});
	});
}

void FN7Account::Guest(UFNNetClient* Net, const FString& Name, const FString& ClaimSecret, FDone Done)
{
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("display_name"), Name.TrimStartAndEnd().Left(24));
	B->SetStringField(TEXT("device_label"), DeviceLabel());
	if (!ClaimSecret.IsEmpty()) B->SetStringField(TEXT("claim_secret"), ClaimSecret);
	Request(Net, TEXT("POST"), TEXT("/api/auth/guest"), B, false, [Done](int32 Code, TSharedPtr<FJsonObject> O)
	{
		FString T;
		if (Code == 201 && O.IsValid() && O->TryGetStringField(TEXT("session_token"), T) && !T.IsEmpty())
		{
			StoreToken(T);
			ReadAccount(O);
			UE_LOG(LogTemp, Display, TEXT("FN7 account: guest %s created, session stored"), *FN7AccountState::Me.DisplayName);
		}
		if (Done) Done(Code, O);
	});
}

bool FN7Account::IsGoogleInFlight() { return !PollToken.IsEmpty(); }

void FN7Account::CancelGoogle()
{
	PollToken.Reset();
	++GoogleGen;
	if (PollTick.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(PollTick); PollTick.Reset(); }
}

void FN7Account::Google(UFNNetClient* Net, bool bLink, const FString& Name, FDone Done)
{
	CancelGoogle();
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("device_label"), DeviceLabel());
	if (!Name.TrimStartAndEnd().IsEmpty()) B->SetStringField(TEXT("display_name"), Name.TrimStartAndEnd().Left(24));
	TWeakObjectPtr<UFNNetClient> W(Net);
	const uint32 Gen = GoogleGen;
	Request(Net, TEXT("POST"), TEXT("/api/auth/google/start"), B, bLink, [W, Done, Gen](int32 Code, TSharedPtr<FJsonObject> O)
	{
		FString Url, Poll;
		double Exp = 0.0, Every = 2000.0;
		if (Gen != GoogleGen) return;
		if (Code != 200 || !O.IsValid() || !O->TryGetStringField(TEXT("login_url"), Url) || !O->TryGetStringField(TEXT("poll_token"), Poll)) { if (Done) Done(Code, O); return; }
		O->TryGetNumberField(TEXT("expires"), Exp);
		O->TryGetNumberField(TEXT("interval_ms"), Every);
		PollToken = Poll;
		PollEvery = FMath::Max(1.0, Every / 1000.0);
		PollUntil = FPlatformTime::Seconds() + 600.0;
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);   // the system browser, never an embedded view
		UE_LOG(LogTemp, Display, TEXT("FN7 account: Google sign-in opened in the browser; polling every %.0f s"), PollEvery);
		auto Step = MakeShared<TFunction<void()>>();
		*Step = [W, Done, Gen, Step]()
		{
			if (Gen != GoogleGen || PollToken.IsEmpty() || !W.IsValid()) return;
			if (FPlatformTime::Seconds() > PollUntil) { CancelGoogle(); if (Done) Done(408, nullptr); return; }
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("poll_token"), PollToken);
			Request(W.Get(), TEXT("POST"), TEXT("/api/auth/poll"), P, false, [W, Done, Gen, Step](int32 C, TSharedPtr<FJsonObject> R)
			{
				if (Gen != GoogleGen) return;
				FString St, T;
				if (R.IsValid()) R->TryGetStringField(TEXT("status"), St);
				if (C == 200 && St == TEXT("pending"))
				{
					PollTick = FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7GooglePoll"), (float)PollEvery, [Step](float) { (*Step)(); return false; });
					return;
				}
				PollToken.Reset();
				if (C == 200 && St == TEXT("complete") && R->TryGetStringField(TEXT("session_token"), T) && !T.IsEmpty())
				{
					StoreToken(T);   // a link replaces the old token (that session is revoked now)
					ReadAccount(R);
					UE_LOG(LogTemp, Display, TEXT("FN7 account: Google sign-in complete (%s), session stored"), *FN7AccountState::Me.DisplayName);
					if (Done) Done(200, R);
					return;
				}
				if (Done) Done(C == 200 ? 400 : C, R);   // failed (its error / detail), or gone (404: start again)
			});
		};
		(*Step)();
	});
}

void FN7Account::Logout(UFNNetClient* Net, FDone Done)
{
	Request(Net, TEXT("POST"), TEXT("/api/auth/logout"), nullptr, true, [Done](int32 Code, TSharedPtr<FJsonObject> O)
	{
		DropToken();
		UE_LOG(LogTemp, Display, TEXT("FN7 account: logged out (%d)"), Code);
		if (Done) Done(Code, O);
	});
}

void FN7Account::Call(UFNNetClient* Net, const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, FDone Done)
{
	Request(Net, Verb, Path, Body, true, Done);
}

void FN7Account::Bind(UFNNetClient* Net, const FString& ResumeToken, FDone Done)
{
	TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("resume_token"), ResumeToken);
	Request(Net, TEXT("POST"), TEXT("/api/game/bind"), B, true, Done);
}
