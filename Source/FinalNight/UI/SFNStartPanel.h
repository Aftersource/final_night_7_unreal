#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FJsonObject;
class SFNTextBox;
class UFNNetClient;

/**
 * The FN7 start screen, shown until you are in a room: title, server status (GET /stats), tonight's game
 * (GET /api/house, then /api/game/current, then Config/FN7JoinCode.txt), your name and one JOIN THE GAME button.
 * It covers the whole viewport and swallows clicks, so nothing behind it reaches the globe.
 *
 * JOIN saves the name to Config/FN7DisplayName.txt. With a game code it writes Config/FN7JoinCode.txt (the root
 * HUD joins that code at auth.ok instead of creating a room) and connects, or sends room.join when already
 * connected. With no code anywhere it connects and the client hosts as before.
 */
class FINALNIGHT_API SFNStartPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNStartPanel) {}
		SLATE_ARGUMENT(UFNNetClient*, Net)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFNStartPanel() override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TWeakObjectPtr<UFNNetClient> Net;
	FDelegateHandle MessageHandle;
	TSharedPtr<SFNTextBox> NameBox;

	bool bServerOnline = false;
	bool bStatsKnown = false;
	int32 PlayersConnected = 0;
	int32 Rooms = 0;
	FString HouseCode;
	FString HouseSource;   // "house", "current" or "file"
	bool bConnected = false;
	bool bJoining = false;
	FString Progress;

	void HandleMessage(const FString& T, TSharedPtr<FJsonObject> D);
	void RefreshStats();
	void RefreshHouse(int32 PathIndex = 0);
	FString ReadLocalLine(const TCHAR* File) const;
	FReply OnJoin();

	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	FText GetGameText() const;
	FText GetProgressText() const;
	bool CanJoin() const { return !bJoining; }
	EActiveTimerReturnType TickRefresh(double InCurrentTime, float InDeltaTime);
};
