#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "SFNHudLayout.h"

class FJsonObject;
class UFNNetClient;
class UTexture2D;

/**
 * SFNHudLayout driven by the live game: listens to UFNNetClient::OnServerMessage, keeps an FFNHudModel current
 * (room, beat, entropy, seats with their /media portraits, your health/bolts/place, news, powers, Time's window
 * and the compose countdown), and sends input back through the net client (BeginTyping on the first keystroke,
 * SubmitOrders on SEND, CastPower when a power is armed).
 */
class FINALNIGHT_API SFNHudLive : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNHudLive) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFNHudLive() override;

private:
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MessageHandle;
	FDelegateHandle StatusHandle;
	TSharedPtr<SFNHudLayout> Layout;
	TSharedPtr<class SFNStartPanel> StartPanel;
	bool bInRoom = false;
	FFNHudModel Model;

	struct FSeatInfo
	{
		FString Ref;
		FString Name;
		int32 Seat = -1;      // -1 = the server's AI
		bool bAlive = true;
		FString PortraitHash;
		FString PortraitUrl;
	};
	TArray<FSeatInfo> Seats;
	int32 MySeat = -1;
	int32 Round = 0;
	int32 WindowSeat = -1;
	double DeadlineMs = 0.0;
	FString ArmedPower;
	TMap<FString, TStrongObjectPtr<UTexture2D>> Portraits;  // by media hash
	TSet<FString> PortraitRequests;

	void HandleMessage(const FString& T, TSharedPtr<FJsonObject> D);
	void HandleStatus(const FString& Status);
	void ReadSeatView(const TSharedPtr<FJsonObject>& View);
	void ReadDemigods(const TSharedPtr<FJsonObject>& PublicView);
	void ReadEntropy(const TSharedPtr<FJsonObject>& Entropy);
	void ReadPowers(const TArray<TSharedPtr<class FJsonValue>>& Powers);
	void AddNews(const FText& Tag, const FString& Text);
	void RequestPortrait(const FString& Hash, const FString& Url);
	void FetchGlobe();
	bool bGlobePending = false;
	void Publish();

	void OnTyping();
	void OnSend(const FString& Text);
	void OnPower(const FString& Slug);
	FText GetTimer() const;
	EActiveTimerReturnType TickTimer(double InCurrentTime, float InDeltaTime);
};

/** Mount / unmount the live HUD over the game viewport (hides the old root HUD while it is up). */
namespace FN7HudMount
{
	FINALNIGHT_API bool Mount(UFNNetClient* Net);
	FINALNIGHT_API void Unmount();
	FINALNIGHT_API bool IsMounted();
}
