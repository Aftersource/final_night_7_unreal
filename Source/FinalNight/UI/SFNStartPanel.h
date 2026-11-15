#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FJsonObject;
class SBox;
class SFNTextBox;
class UFNNetClient;

/**
 * The FN7 lobby. Shown until you are in a room, and over the game whenever it is opened (Esc or the LOBBY pill).
 * It covers the whole viewport and swallows clicks, so nothing behind it reaches the globe.
 *
 * - The server (GET /stats) and the house game (GET /api/house, polled every 3 s): LIVE or REPLAYING, beat N of M,
 *   who sits at the table (human, AI, away, out), how many watch, and the next match when the server says.
 * - JOIN THE GAME takes a seat; WATCH joins without one (room.join {house:true, viewer:true}). Both save the name to
 *   Config/FN7DisplayName.txt and connect (the root joins the house at auth.ok), or join directly when connected.
 *   JOIN never hosts: with no house game it says so.
 * - Over the game: BACK TO THE GAME, TAKE A SEAT while only watching, and OTHER ROOMS (room.list) with a JOIN each.
 */
DECLARE_DELEGATE_OneParam(FOnFNJoinChoice, bool /*bTakeSeat*/);

class FINALNIGHT_API SFNStartPanel : public SCompoundWidget
{
public:
	/** JOIN THE GAME, as the button does (the intro's bag click calls it too). */
	void JoinGame() { Enter(true); }
	SLATE_BEGIN_ARGS(SFNStartPanel) : _bInGame(false) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
		/** True while we are in a room: the lobby was opened over the game. */
		SLATE_ATTRIBUTE(bool, bInGame)
		/** JOIN (take a seat) or WATCH (stay a viewer), told before connecting. */
		SLATE_EVENT(FOnFNJoinChoice, OnChoice)
		/** Close the lobby and go back to the game. */
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFNStartPanel() override;

	/** Hosting a room from the lobby (its code shown, START waiting): the HUD keeps the lobby up until the match starts. */
	bool IsHosting() const { return !HostedCode.IsEmpty(); }
	/** Called when the lobby is shown over the game: refresh at once and ask for the room list. */
	void Opened();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TWeakObjectPtr<UFNNetClient> Net;
	TAttribute<bool> bInGame;
	FDelegateHandle MessageHandle;
	TSharedPtr<SFNTextBox> NameBox;
	TSharedPtr<SBox> HouseHolder;
	TSharedPtr<SBox> RoomsHolder;
	FOnFNJoinChoice OnChoice;
	FSimpleDelegate OnBack;

	// GET /stats
	bool bServerOnline = false;
	bool bStatsKnown = false;
	int32 PlayersConnected = 0;
	int32 RoomCount = 0;

	// GET /api/house
	struct FHouseSeat
	{
		FString Name;
		FString Kind;   // Human, AI, away, out
	};
	bool bHouseKnown = false;
	FString HouseCode;
	FString HouseSource;   // "house" or "file"
	FString HouseState;    // in_match, open, post_match ...
	bool bReplaying = false;
	int32 HouseBeat = 0;
	int32 HouseBeats = 0;
	int32 HouseViewers = 0;
	double NextMatchS = -1.0;
	TArray<FHouseSeat> HouseSeats;
	FString HouseSignature;

	// room.listed
	struct FRoomRow
	{
		FString Code;
		int32 SeatsTaken = 0;
		int32 MaxPlayers = 0;
		FString State;
	};
	TArray<FRoomRow> OtherRooms;
	bool bRoomsKnown = false;
	FString RoomsSignature;

	bool bConnected = false;
	bool bJoining = false;
	FString Progress;

	void HandleMessage(const FString& T, TSharedPtr<FJsonObject> D);
	void RefreshStats();
	void RefreshHouse();
	void ReadHouse(const TSharedPtr<FJsonObject>& O);
	FString ReadLocalLine(const TCHAR* File) const;
	TSharedRef<SWidget> MakeHouse();
	TSharedRef<SWidget> MakeRooms();
	void RebuildHouse();
	void RebuildRooms();

	FReply OnPrimary();
	FReply OnSecondary();
	FReply Enter(bool bTakeSeat);
	void JoinOther(const FString& Code);
	// Benito's start screen: PLAY VS BOTS, HOST (a code for friends, START when ready), JOIN BY CODE (beside the house game).
	uint8 Choice = 0;               // the lobby choice in flight (UFNNetClient::ELobbyIntent)
	bool bChoiceDirect = false;     // run while connected (so this panel, not the root's auth.ok, readies and starts)
	bool bAwaitLeave = false;       // left the current room first: the choice runs on room.left (the server refuses it before)
	FString HostedCode;             // the room we host, waiting for START
	bool bBackToHouse = false;      // CANCEL hosting: leave our room, then rejoin the house game on room.left
	void CancelHosting();
	TSharedPtr<SFNTextBox> CodeBox;
	// The six looks (Chris: six to pick a favourite from; Saved/FN7Hud.json "lobby_variant").
	int32 Look = 1;
	FSlateColor LInk() const;
	FSlateColor LDim() const;
	FSlateColor LAccent() const;
	// The account screens on the same tablet (ACCOUNTS.md §13): WELCOME before the lobby when the server offers
	// accounts and this computer has no session; the lobby's strip shows who is signed in (the main menu, step 5).
	enum class EPage : uint8 { Lobby = 0, Welcome = 1, Settings = 2, Demigods = 3, God = 4 };
	EPage Page = EPage::Lobby;
	TSharedPtr<class SFNWelcomePage> Welcome;
	TSharedPtr<class SFNSettingsPage> Settings;
	TSharedPtr<class SFNDemigodsPage> Demigods;
	TSharedPtr<class SFNGodPage> God;
	bool bLogoutAsk = false;
	bool bLinking = false;
	FString AccountNote;
	TSharedRef<SWidget> MakeAccountStrip();
	void OnBooted(bool bSignedIn);
	int32 PageLook() const;
	float PageWidth() const;
	void Choose(uint8 Intent);
	TSharedRef<SWidget> MakeChoices();

	bool IsViewer() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	FText GetPrimaryText() const;
	FText GetSecondaryText() const;
	EVisibility GetSecondaryVisibility() const;
	FText GetProgressText() const;
	bool CanJoin() const { return !bJoining; }
	EActiveTimerReturnType TickRefresh(double InCurrentTime, float InDeltaTime);
};
