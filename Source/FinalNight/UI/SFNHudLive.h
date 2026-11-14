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

	/** Screenshots without joining a game: the sample table behind, then the lobby over the game and/or the "you fell" card. */
	void DebugPreview(bool bLobby, bool bFell);

private:
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MessageHandle;
	FDelegateHandle StatusHandle;
	FDelegateHandle DropHandle, DragOverHandle, DragEndHandle;
	void OnPowerDropped(const FString& Power, const FVector2D& ScreenPosition);
	void OnPowerDragOver(const FString& Power, const FVector2D& ScreenPosition);
	void OnPowerDragEnded(const FString& Power);
	TOptional<FVector2D> SnapPowerToBoard(const FString& Power, const FVector2D& ScreenPosition);
	class AFNStudio* FindStudio() const;
	TMap<FString, double> PowerRadiusKm;
	TSharedPtr<SFNHudLayout> Layout;
	TSharedPtr<class SFNStartPanel> StartPanel;
	bool bInRoom = false;

	// The lobby over the game (Esc or the LOBBY pill).
	bool bLobbyOpen = false;
	void SetLobbyOpen(bool bOpen);
	TSharedPtr<class IInputProcessor> LobbyKeys;

	// Out of the match (both demigods fell): "You fell at beat N", TAKE ANOTHER DEMIGOD, WATCH, RETURN TO LOBBY.
	bool bOut = false;
	bool bOutDismissed = false;
	bool bTakeAsked = false;
	double TakeAskedAt = 0.0;
	int32 OutBeat = 0;
	FString OutReason;
	FString OutNote;
	TSharedRef<SWidget> MakeOutCard();
	void ResetForNewRoom();
	FFNHudModel Model;

	struct FSeatInfo
	{
		FString Ref;
		FString Name;
		int32 Seat = -1;      // -1 = the server's AI
		bool bAlive = true;
		FString PortraitHash;
		FString PortraitUrl;
		int32 Bolts = -1;
		int32 Sparks = -1;
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
	/** FN7.Hud ... lobby / fell: show those states on the mounted HUD for a screenshot. */
	FINALNIGHT_API void Preview(bool bLobby, bool bFell);
}
