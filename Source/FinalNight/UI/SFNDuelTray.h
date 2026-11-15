#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class SEditableTextBox;

/** One duel as the HUD knows it (challenge mode 0.8.1, panels 0.8.2, live typing; server CLIENT_API.md). */
struct FFNDuelSession
{
	FString DuelId;
	FString A, B, AName, BName, ASkin, BSkin;   // a = the challenger
	FString MySide;                              // "a" / "b" when one of Chris's demigods fights; "" for a watcher
	float HpA = 10.f, HpB = 10.f, MaxA = 10.f, MaxB = 10.f;
	FString Range, Initiative;
	double DeadlineMs = 0.0;                     // server epoch ms (0: none, e.g. an AI lock)
	TMap<int32, TSharedPtr<FJsonObject>> Panels; // by n
	TSharedPtr<FJsonObject> Scene;
	TSet<int32> Ticked;                          // panels whose input close this client ticked

	/** A side's live line: len and tail from duel.typing (or the keyboard for your own side). */
	struct FSideLine
	{
		int32 Len = 0;
		FString Tail;
		double FrameAt = 0.0;    // FPlatformTime when the last frame arrived
		double AtMs = 0.0;       // the frame's server stamp, and its seq (newer frames only)
		int32 Seq = -1;
		FString Sent;            // the line just sent (duel.blow): revealed letter by letter, then cleared
		double SentAt = 0.0;
		bool bTypeOut = false;   // nobody saw it typed (an AI's line): it types itself out, *****p
	};
	FSideLine LineA, LineB;
	FSideLine& Line(const FString& Side) { return Side == TEXT("b") ? LineB : LineA; }

	TArray<FString> Log;     // the latest blows, newest last
	bool bOver = false;
	bool bSending = false;   // your line waits for a token (the bucket was empty): never dropped
	FString Result;
	double OverAt = 0.0;

	void AddLog(const FString& L) { Log.Add(L); while (Log.Num() > 4) Log.RemoveAt(0); }
};

DECLARE_DELEGATE_OneParam(FOnFNDuelSend, const FString& /*Line*/);
DECLARE_DELEGATE_OneParam(FOnFNDuelTyping, const FString& /*CurrentText*/);

/**
 * The duel's screen frame around the box (top is the opponent's text, bottom is your words, Enter to submit):
 * TOP your opponent's line as they type it (stars, the newest letters readable for an instant; an AI's line types
 * itself out when it strikes), their name and health; the panel's caption over the box; BOTTOM your own line, masked
 * the same way, the box you type in (the two duellists only; Enter strikes), the typing window bar with its lock
 * marker, the countdown and the latest blow. A watcher sees side b on top, side a below, and types nothing.
 */
class FINALNIGHT_API SFNDuelTray : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNDuelTray) {}
		SLATE_ATTRIBUTE(TSharedPtr<FFNDuelSession>, Session)
		SLATE_ATTRIBUTE(double, ServerNowMs)
		SLATE_ATTRIBUTE(FString, Caption)
		SLATE_EVENT(FOnFNDuelSend, OnSend)
		SLATE_EVENT(FOnFNDuelTyping, OnTyping)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The keyboard into the line (a duel of yours just locked, or Enter while it runs). */
	void FocusInput();
	bool HasInputFocus() const;
	void ClearInput();

private:
	TAttribute<TSharedPtr<FFNDuelSession>> Session;
	TAttribute<double> ServerNowMs;
	TAttribute<FString> CaptionText;
	FOnFNDuelSend OnSend;
	FOnFNDuelTyping OnTyping;
	TSharedPtr<SEditableTextBox> Input;
	double MyTypedAt = 0.0;
	FString MyText;

	FText SideText(const FString& Side) const;
	FText NameText(const FString& Side) const;
	TOptional<float> HpFrac(const FString& Side) const;
	TOptional<float> WindowFrac() const;
	FText WindowText() const;
	FText LogText() const;
	FText StatusText(const FString& Side) const;
	FString CaptionNow() const;
	FString TopSide() const;
	FString BottomSide() const;
	bool IsMine() const;
};
