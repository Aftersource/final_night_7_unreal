#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SBox;
class SEditableTextBox;
class SScrollBox;

/** One room.chat_msg (server: {seat | null, display_name, text, at}). */
struct FFNChatMsg
{
	int32 Seat = -1;                                            // -1: a viewer (no seat)
	FString Name, Text;
	FLinearColor Color = FLinearColor(0.62f, 0.62f, 0.66f);    // the seat's aura; viewers grey
	double At = 0.0;                                            // server epoch ms
	double Seen = 0.0;                                          // FPlatformTime when it arrived (the floating looks fade by it)
};

/**
 * The room chat: a chat above your will ... totally responsive to the world scroll and how expanded that is and
 * how expanded your WILL is ... a few versions ... little buttons to switch back and forth so Benito can choose").
 * Docked above the WILL; its height is the free space the HUD measures between the WORLD scroll and the WILL every
 * frame (FreeHeight), so it never overlaps either; too little room and it folds to a one-line ticker. Enter sends
 * (room.chat), Esc leaves; it never opens a slow window (only the WILL does). Six looks, switched by the 1-6 buttons in
 * its corner and kept in Saved/FN7Hud.json: 1 parchment strip, 2 stone tablet, 3 bubbles over the table rim, 4 dark
 * glass, 5 a fanned deck of six cards, 6 bubbles beside each speaker's figure. Looks 3 and 6 draw their bubbles on the
 * full-screen SFNChatFloat; docked, they keep only the line you type in.
 */
class FINALNIGHT_API SFNChat : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNChat) : _Width(470.f) {}
		SLATE_ATTRIBUTE(float, FreeHeight)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetOnSend(TFunction<void(const FString&)> F) { OnSend = MoveTemp(F); }
	void Add(const FFNChatMsg& M);
	void SetVariant(int32 V);
	int32 GetVariant() const { return Variant; }
	bool HasChatFocus() const;
	void LeaveFocus();
	const TArray<FFNChatMsg>& Messages() const { return Msgs; }
	bool IsTicker() const { return bTicker; }
	/** Collapsed (the default) the chat is one small speech-bubble icon in its look, with an unread count and one pulse
	 *  per new message; a click opens exactly the chat, Esc or a click outside folds it back (the chat needs
	 *  to be actually just a small indicator in whatever the selected style is ... a proper icon, and then expand to what
	 *  it is now"). The floating bubbles stay quiet while it's folded. */
	bool IsOpen() const { return bOpen; }
	void Open();
	void Close();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TArray<FFNChatMsg> Msgs;
	int32 Variant = 4;
	TAttribute<float> FreeHeight;
	float Width = 470.f;
	TFunction<void(const FString&)> OnSend;
	TSharedPtr<SBox> Body, Area;
	TSharedPtr<SScrollBox> List;
	TSharedPtr<SEditableTextBox> Input;
	bool bTicker = false, bBuiltTicker = false;
	int32 BuiltVariant = -1;
	float FanT = 0.f;              // the 6-fan opens on hover
	bool bFanHover = false;
	bool bOpen = false;            // folded to the icon by default
	int32 Unread = 0;              // messages since it was folded
	float Pulse = 0.f;             // 1 -> 0: the icon's one pulse for a new message
	bool bHadFocus = false;        // opened and typed in: losing focus away from it folds it
	bool bMouseWasDown = false;
	TSharedRef<SWidget> Icon();

	void Rebuild();
	void RefreshArea();
	TSharedRef<SWidget> Frame(TSharedRef<SWidget> Inner, bool bSlim) const;
	TSharedRef<SWidget> Buttons() const;
	TSharedRef<SWidget> Row(const FFNChatMsg& M) const;
	TSharedRef<SWidget> Fan();
	TSharedRef<SWidget> TickerLine() const;
};

/** The floating bubbles of looks 3 (over the table rim) and 6 (beside each speaker's figure): paint only, no clicks. */
class FINALNIGHT_API SFNChatFloat : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFNChatFloat) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SFNChat> InChat, TFunction<bool(int32 Seat, FVector2D& OutViewportPx)> InAnchor);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakPtr<SFNChat> Chat;
	TFunction<bool(int32, FVector2D&)> Anchor;
};
