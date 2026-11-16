#include "SFNStartPanel.h"
#include "FNVoiceOver.h"
#include "FNWood.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FNNetClient.h"
#include "FNStyle.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SFNTextBox.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "FNLooks.h"
#include "FNAccount.h"
#include "SFNAccountPages.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNStartPanel"

namespace FN7Lobby
{
	TSharedPtr<FJsonObject> Parse(const FString& Body)
	{
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Body);
		return FJsonSerializer::Deserialize(R, O) ? O : nullptr;
	}

	/** "code", or "room.code" / "game.code" / "house.code". */
	FString FindCode(const TSharedPtr<FJsonObject>& O)
	{
		if (!O.IsValid()) return FString();
		FString C;
		if (O->TryGetStringField(TEXT("code"), C) && !C.IsEmpty()) return C;
		for (const TCHAR* K : { TEXT("room"), TEXT("game"), TEXT("house"), TEXT("current") })
		{
			const TSharedPtr<FJsonObject>* Sub = nullptr;
			if (O->TryGetObjectField(K, Sub) && Sub && (*Sub)->TryGetStringField(TEXT("code"), C) && !C.IsEmpty()) return C;
		}
		return FString();
	}

	TSharedPtr<FJsonObject> Sub(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TSharedPtr<FJsonObject>* S = nullptr;
		return (O.IsValid() && O->TryGetObjectField(K, S) && S) ? *S : nullptr;
	}

	double Number(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Default)
	{
		double V = Default;
		if (O.IsValid()) O->TryGetNumberField(K, V);
		return V;
	}

	/** Seconds until the next match, from whichever field the server sends (in s, or as an epoch-ms time); -1 = unknown. */
	double NextMatchSeconds(const TSharedPtr<FJsonObject>& O)
	{
		const double NowMs = (double)(FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
		for (const TSharedPtr<FJsonObject>& Where : { O, Sub(O, TEXT("last_ended")), Sub(O, TEXT("replaying")), Sub(O, TEXT("next")) })
		{
			if (!Where.IsValid()) continue;
			double V = 0.0;
			for (const TCHAR* K : { TEXT("next_match_in_s"), TEXT("replay_ends_in_s"), TEXT("next_match_in"), TEXT("starts_in_s") })
				if (Where->TryGetNumberField(K, V)) return FMath::Max(0.0, V);
			for (const TCHAR* K : { TEXT("next_match_at"), TEXT("next_match_at_ms"), TEXT("next_start_ms"), TEXT("starts_at") })
				if (Where->TryGetNumberField(K, V) && V > 1.0e12) return FMath::Max(0.0, (V - NowMs) / 1000.0);
		}
		return -1.0;
	}

	EFNSeatRole RoleOf(const FString& Kind)
	{
		if (Kind == TEXT("HUMAN")) return EFNSeatRole::Human;
		if (Kind == TEXT("AI")) return EFNSeatRole::AI;
		return EFNSeatRole::Fallen;
	}

	/** A seat at the house table: its name and a small HUMAN / AI / AWAY / OUT pill. */
	TSharedRef<SWidget> SeatChip(const FString& Name, const FString& Kind)
	{
		using namespace FN7Style;
		const EFNSeatRole Role = RoleOf(Kind);
		return SNew(SBorder)
			.BorderImage(Brush::Panel())
			.Padding(FMargin(Space::S, Space::XS))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(12, TEXT("Bold"))).Text(FText::FromString(Name.IsEmpty() ? TEXT("?") : Name))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(Brush::Pill(Role))
					.Padding(FMargin(Space::S, 1.f))
					[
						SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Kind == TEXT("AWAY") ? Color::TextDim() : Color::Aura(Role)).Text(FText::FromString(Kind))
					]
				]
			];
	}
}

void SFNStartPanel::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Net = InArgs._Net;
	bInGame = InArgs._bInGame;
	OnChoice = InArgs._OnChoice;
	OnBack = InArgs._OnBack;
	const FLinearColor StartInk(0.1f, 0.06f, 0.02f);

	FString Name = ReadLocalLine(TEXT("FN7DisplayName.txt"));
	if (Name.IsEmpty() && Net.IsValid()) Name = Net->DisplayName;
	Look = FN7Looks::Get(TEXT("lobby"), 1);

	auto OutOfGame = [this]() { return bInGame.Get() ? EVisibility::Collapsed : EVisibility::Visible; };
	auto InGame = [this]() { return bInGame.Get() ? EVisibility::Visible : EVisibility::Collapsed; };
	auto NameVis = [this]() { return bInGame.Get() || FN7Account::IsSignedIn() ? EVisibility::Collapsed : EVisibility::Visible; };   // signed in: the account's name

	ChildSlot
	[
		// The scrim: covers the viewport, dims the table a little, and is hit-testable (see OnMouseButtonDown).
		SNew(SBorder)
		// Plainer until you're in (Chris asked for a plainer look until loaded in); a light veil over the game when opened in it.
		.BorderImage_Lambda([this]() { return bInGame.Get() ? Brush::Segment(FLinearColor(0.f, 0.f, 0.02f, 0.5f)) : Brush::Segment(FLinearColor(0.f, 0.f, 0.02f, 0.7f)); })
		.Padding(0.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride_Lambda([this]() { return FOptionalSize(PageWidth()); })
			[
				// The tablet in the chosen look (1 the black onyx marble Chris asked for, then parchment, carved wood, dark glass,
				// gilded slate, star map), switched by the 1-6 buttons beside LOBBY.
				FN7Looks::Frame(TAttribute<int32>::CreateLambda([this]() { return PageLook(); }),
					SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return (int32)Page; })
					+ SWidgetSwitcher::Slot()
					[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
						[
							SNew(STextBlock).TextStyle(&Type::Display()).ColorAndOpacity(this, &SFNStartPanel::LInk).Text(LOCTEXT("Title", "FINAL NIGHT 7"))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::M, 0.f, 0.f, 6.f)
						[
							SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("LobbyWord", "LOBBY"))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::S, 0.f, 0.f, 4.f)
						[
							FN7Looks::Switch(TAttribute<int32>::CreateLambda([this]() { return Look; }), [this](int32 V) { Look = V; FN7Looks::Set(TEXT("lobby"), V); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(64.f).HeightOverride(3.f).HAlign(HAlign_Left)[SNew(SImage).Image(Brush::AccentRule())]
					]
					// Signed in: who, GUEST, Link Google, Log out (the main menu of ACCOUNTS.md §13 step 5).
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
					[
						MakeAccountStrip()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNStartPanel::LDim).AutoWrapText(true).Visibility_Lambda(OutOfGame)
						.Text(LOCTEXT("Tag", "Demigods fight over the real Earth. Write what you do; time slows while you write. Last one standing wins."))
					]

					// Server status.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
						[
							SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)
							[
								SNew(SImage).Image(Brush::Swatch(FLinearColor::White)).ColorAndOpacity(this, &SFNStartPanel::GetStatusColor)
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Small()).Text(this, &SFNStartPanel::GetStatusText)
						]
					]

					// The house game, live.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
					[
						SAssignNew(HouseHolder, SBox)
					]

					// Name (first time in only).
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, Space::XS)
					[
						SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Visibility_Lambda(NameVis).Text(LOCTEXT("NameLabel", "YOUR NAME"))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).Visibility_Lambda(NameVis)
						[
							SAssignNew(NameBox, SFNTextBox)
							.Text(FText::FromString(Name))
							.HintText(LOCTEXT("NameHint", "The name the other demigods see"))
							.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit) { if (Commit == ETextCommit::OnEnter) OnPrimary(); })
						]
					]

					// Join the game / back to the game.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
					[
						SNew(SBox).HeightOverride(52.f)
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.IsEnabled(this, &SFNStartPanel::CanJoin)
							.OnClicked(this, &SFNStartPanel::OnPrimary)
							[
								SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(StartInk).Text(this, &SFNStartPanel::GetPrimaryText)
							]
						]
					]
					// Watch / take a seat.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::S, 0.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(&PillButton())
						.Visibility(this, &SFNStartPanel::GetSecondaryVisibility)
						.IsEnabled(this, &SFNStartPanel::CanJoin)
						.OnClicked(this, &SFNStartPanel::OnSecondary)
						[
							SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(this, &SFNStartPanel::GetSecondaryText)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::S, 0.f, 0.f)
					[
						SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Small()).AutoWrapText(true).Text(this, &SFNStartPanel::GetProgressText)
					]

					// More ways in: against the gods' AI, a room of your own for friends, or a friend's room by its code.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
					[
						MakeChoices()
					]

					// Other rooms (over the game, where we are connected).
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::L, 0.f, 0.f)
					[
						SNew(SBox).Visibility_Lambda(InGame)
						[
							SAssignNew(RoomsHolder, SBox)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::M, 0.f, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNStartPanel::LDim).Visibility_Lambda(InGame).Text(LOCTEXT("EscHint", "Esc opens and closes the lobby."))
					]
					]
					// WELCOME: a name and PLAY AS GUEST (or Google), then the lobby.
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(Welcome, SFNWelcomePage).Net(Net.Get()).Name(Name)
						.OnSignedIn_Lambda([this]() { Page = EPage::Lobby; AccountNote.Reset(); RebuildHouse(); })
						.OnSkip_Lambda([this]() { Page = EPage::Lobby; })
					]
					// SETTINGS: the account's settings document.
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(Settings, SFNSettingsPage).Net(Net.Get())
						.OnBack_Lambda([this]() { Page = EPage::Lobby; })
					]
					// DEMIGODS: the two sheets.
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(Demigods, SFNDemigodsPage).Net(Net.Get())
						.OnBack_Lambda([this]() { Page = EPage::Lobby; })
					]
					// YOUR GOD: the stone godform at your seat.
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(God, SFNGodPage).Net(Net.Get())
						.OnBack_Lambda([this]() { Page = EPage::Lobby; })
					], FMargin(Space::XL + Space::S, Space::XL))
			]
		]
	];

	RebuildHouse();
	RebuildRooms();
	if (UFNNetClient* N = Net.Get()) MessageHandle = N->OnServerMessage.AddSP(this, &SFNStartPanel::HandleMessage);
	// The account (§13 step 1): a stored session signs in; without one, WELCOME when the server offers accounts.
	{
		TWeakPtr<SFNStartPanel> Weak = StaticCastSharedRef<SFNStartPanel>(AsShared());
		FN7Account::Boot(Net.Get(), [Weak](bool bSignedIn) { if (const TSharedPtr<SFNStartPanel> Self = Weak.Pin()) Self->OnBooted(bSignedIn); });
	}
	RefreshStats();
	RefreshHouse();
	RegisterActiveTimer(3.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNStartPanel::TickRefresh));
}

SFNStartPanel::~SFNStartPanel()
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MessageHandle);
}

void SFNStartPanel::Opened()
{
	bJoining = false;
	Progress.Reset();
	RefreshStats();
	RefreshHouse();
	if (UFNNetClient* N = Net.Get()) if (bConnected) N->ListRooms();
	if (FN7Account::IsSignedIn()) FN7Voice::SayOnce(FN7Voice::ELine::Welcome, TEXT("run"));   // "Welcome, god." (once a run)
}

// Swallow every click on the scrim: nothing falls through to the globe or the table.
FReply SFNStartPanel::OnMouseButtonDown(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply SFNStartPanel::OnMouseButtonUp(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply SFNStartPanel::OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }

void SFNStartPanel::HandleMessage(const FString& T, TSharedPtr<FJsonObject> D)
{
	if (T == TEXT("error") && D.IsValid())
	{
		FString Msg;
		D->TryGetStringField(TEXT("message"), Msg);
		if (Msg.StartsWith(TEXT("name_reserved:")) && Welcome.IsValid())
		{
			// That name belongs to an account and this connection isn't bound to it (ACCOUNTS.md §13 step 8): sign in.
			Welcome->SetNote(TEXT("That name belongs to an account. Sign in to play as it, or pick another name."));
			Page = EPage::Welcome;
			bJoining = false;
		}
	}
	if (T == TEXT("auth.ok"))
	{
		bConnected = true;
		if (bJoining) Progress = TEXT("Connected. Joining the game...");
	}
	else if (T == TEXT("room.snapshot") || T == TEXT("match.snapshot"))
	{
		bJoining = false;
		Progress.Reset();
		// The lobby's own match: vs bots starts at once (run while connected, nobody else readies it); a hosted room
		// shows its code and waits for START; a code joins and closes the lobby.
		FString Code, State;
		if (D.IsValid()) { D->TryGetStringField(TEXT("code"), Code); D->TryGetStringField(TEXT("state"), State); }
		if (T == TEXT("room.snapshot") && Choice == (uint8)UFNNetClient::ELobbyIntent::VsBots)
		{
			if (bChoiceDirect) if (UFNNetClient* N = Net.Get()) { N->SetReady(true); N->StartMatch(); }
			Choice = 0;
			OnBack.ExecuteIfBound();
		}
		else if (T == TEXT("room.snapshot") && Choice == (uint8)UFNNetClient::ELobbyIntent::Host && !Code.IsEmpty())
		{
			HostedCode = Code;
			Choice = 0;
			if (UFNNetClient* N = Net.Get()) N->SetReady(true);
			UE_LOG(LogTemp, Display, TEXT("FN7 lobby: hosting %s"), *Code);
		}
		else if (Choice == (uint8)UFNNetClient::ELobbyIntent::Code)
		{
			Choice = 0;
			OnBack.ExecuteIfBound();
		}
	}
	else if (T == TEXT("room.left") && bBackToHouse)
	{
		// hosting cancelled: straight back into the house game (Chris noted she could not click to ignore the hosting she was about
		// to do and jump back in")
		bBackToHouse = false;
		bJoining = false;
		if (UFNNetClient* N = Net.Get()) { N->JoinHouse(false); UE_LOG(LogTemp, Display, TEXT("FN7 lobby: back to the house game")); }
		Progress.Reset();
		OnBack.ExecuteIfBound();
	}
	else if (T == TEXT("room.left") && bAwaitLeave)
	{
		bAwaitLeave = false;
		if (UFNNetClient* N = Net.Get()) N->RunLobbyIntent();
	}
	else if (T == TEXT("match.started") && !HostedCode.IsEmpty())
	{
		HostedCode.Reset();
		OnBack.ExecuteIfBound();
	}
	else if (T == TEXT("error") && Choice != 0)
	{
		FString Msg;
		if (D.IsValid()) D->TryGetStringField(TEXT("message"), Msg);
		Progress = FString::Printf(TEXT("The server said no: %s"), *Msg);
		Choice = 0;
	}
	else if (T == TEXT("room.listed"))
	{
		OtherRooms.Reset();
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (D.IsValid() && D->TryGetArrayField(TEXT("rooms"), List) && List)
			for (const TSharedPtr<FJsonValue>& V : *List)
			{
				const TSharedPtr<FJsonObject> R = V->AsObject();
				if (!R.IsValid()) continue;
				FRoomRow Row;
				R->TryGetStringField(TEXT("code"), Row.Code);
				R->TryGetStringField(TEXT("state"), Row.State);
				Row.SeatsTaken = (int32)FN7Lobby::Number(R, TEXT("seats_taken"), 0.0);
				Row.MaxPlayers = (int32)FN7Lobby::Number(R, TEXT("max_players"), 0.0);
				if (Row.Code.IsEmpty() || Row.Code.Equals(HouseCode, ESearchCase::IgnoreCase)) continue;
				OtherRooms.Add(Row);
			}
		bRoomsKnown = true;
		RebuildRooms();
	}
	else if (T == TEXT("error"))
	{
		FString Msg;
		if (D.IsValid()) D->TryGetStringField(TEXT("message"), Msg);
		if (bJoining) Progress = TEXT("The server said: ") + Msg;
		bJoining = false;
	}
}

void SFNStartPanel::RefreshStats()
{
	UFNNetClient* N = Net.Get();
	if (!N) return;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(TEXT("/stats")));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(8.f);
	TWeakPtr<SFNStartPanel> Weak = StaticCastSharedRef<SFNStartPanel>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNStartPanel> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bStatsKnown = true;
		const TSharedPtr<FJsonObject> O = (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200) ? FN7Lobby::Parse(Resp->GetContentAsString()) : nullptr;
		Self->bServerOnline = O.IsValid();
		if (O.IsValid())
		{
			double V = 0.0;
			if (O->TryGetNumberField(TEXT("connected"), V)) Self->PlayersConnected = (int32)V;
			else if (O->TryGetNumberField(TEXT("players"), V)) Self->PlayersConnected = (int32)V;
			if (O->TryGetNumberField(TEXT("rooms"), V)) Self->RoomCount = (int32)V;
		}
	});
	Req->ProcessRequest();
}

void SFNStartPanel::RefreshHouse()
{
	UFNNetClient* N = Net.Get();
	if (!N) return;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(TEXT("/api/house")));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(8.f);
	TWeakPtr<SFNStartPanel> Weak = StaticCastSharedRef<SFNStartPanel>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNStartPanel> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		const int32 HttpCode = Resp.IsValid() ? Resp->GetResponseCode() : 0;
		Self->ReadHouse((bOk && HttpCode == 200) ? FN7Lobby::Parse(Resp->GetContentAsString()) : nullptr);
		Self->RebuildHouse();
	});
	Req->ProcessRequest();
}

void SFNStartPanel::ReadHouse(const TSharedPtr<FJsonObject>& O)
{
	bHouseKnown = true;
	HouseSeats.Reset();
	const FString Code = FN7Lobby::FindCode(O);
	if (Code.IsEmpty())
	{
		// No house (404) or no server: the last word is Config/FN7JoinCode.txt, if this machine has one.
		const FString Local = ReadLocalLine(TEXT("FN7JoinCode.txt"));
		HouseCode = Local.ToUpper();
		HouseSource = Local.IsEmpty() ? FString() : TEXT("file");
		HouseState.Reset();
		bReplaying = false;
		NextMatchS = -1.0;
		return;
	}
	HouseCode = Code.ToUpper();
	HouseSource = TEXT("house");
	O->TryGetStringField(TEXT("state"), HouseState);
	FString Mode;
	O->TryGetStringField(TEXT("mode"), Mode);
	const TSharedPtr<FJsonObject> Replay = FN7Lobby::Sub(O, TEXT("replaying"));
	bReplaying = Replay.IsValid() || Mode.StartsWith(TEXT("replay"));
	HouseBeat = (int32)FN7Lobby::Number(O, TEXT("round"), 0.0);
	HouseBeats = (int32)FN7Lobby::Number(FN7Lobby::Sub(O, TEXT("rules")), TEXT("beats_total"), 0.0);
	if (Replay.IsValid())
	{
		HouseBeat = (int32)FN7Lobby::Number(Replay, TEXT("round"), FN7Lobby::Number(Replay, TEXT("beat"), HouseBeat));
		HouseBeats = (int32)FN7Lobby::Number(Replay, TEXT("beats_total"), HouseBeats);
	}
	HouseViewers = (int32)FN7Lobby::Number(O, TEXT("viewers"), 0.0);
	NextMatchS = FN7Lobby::NextMatchSeconds(O);

	const TArray<TSharedPtr<FJsonValue>>* SeatList = nullptr;
	if (O->TryGetArrayField(TEXT("seats"), SeatList) && SeatList)
		for (const TSharedPtr<FJsonValue>& V : *SeatList)
		{
			const TSharedPtr<FJsonObject> S = V->AsObject();
			if (!S.IsValid()) continue;
			FHouseSeat Seat;
			FString Status;
			bool bBot = false;
			S->TryGetStringField(TEXT("display_name"), Seat.Name);
			S->TryGetStringField(TEXT("status"), Status);
			S->TryGetBoolField(TEXT("is_bot"), bBot);
			if (Status == TEXT("eliminated")) Seat.Kind = TEXT("OUT");
			else if (Status == TEXT("away") || Status == TEXT("left")) Seat.Kind = TEXT("AWAY");
			else Seat.Kind = bBot ? TEXT("AI") : TEXT("HUMAN");
			HouseSeats.Add(Seat);
		}
}

void SFNStartPanel::RebuildHouse()
{
	FString Sig = FString::Printf(TEXT("%d|%s|%s|%s|%d|%d|%d|%d|%d"), bHouseKnown ? 1 : 0, *HouseCode, *HouseSource, *HouseState,
		bReplaying ? 1 : 0, HouseBeat, HouseBeats, HouseViewers, NextMatchS < 0.0 ? -1 : FMath::RoundToInt(NextMatchS));
	for (const FHouseSeat& S : HouseSeats) Sig += TEXT("|") + S.Name + TEXT(":") + S.Kind;
	if (Sig == HouseSignature && HouseHolder.IsValid() && HouseHolder->GetChildren()->Num() > 0) return;
	HouseSignature = Sig;
	if (HouseHolder.IsValid()) HouseHolder->SetContent(MakeHouse());
}

TSharedRef<SWidget> SFNStartPanel::MakeHouse()
{
	using namespace FN7Style;
	if (!bHouseKnown)
		return SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("HouseChecking", "Looking for the game..."));
	if (HouseCode.IsEmpty())
		return SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Body()).AutoWrapText(true).Text(LOCTEXT("NoGame", "The server has no game running right now. This checks again every few seconds."));
	if (HouseSource == TEXT("file"))
		return SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Body()).Text(FText::Format(LOCTEXT("FileGame", "Room {0} (this machine's join code)"), FText::FromString(HouseCode)));

	// Live (green), replaying (violet) or between matches (dim).
	const bool bLive = !bReplaying && HouseState == TEXT("in_match");
	const FText StateWord = bReplaying ? LOCTEXT("Replaying", "REPLAYING") : bLive ? LOCTEXT("Live", "LIVE") : LOCTEXT("Between", "BETWEEN MATCHES");
	const FLinearColor StateColor = bReplaying ? FLinearColor(0.62f, 0.45f, 0.95f) : bLive ? FLinearColor(0.35f, 0.9f, 0.5f) : Color::TextDim();

	TArray<FString> Bits;
	Bits.Add(FString::Printf(TEXT("room %s"), *HouseCode));
	if (HouseBeat > 0) Bits.Add(HouseBeats > 0 ? FString::Printf(TEXT("beat %d of %d"), HouseBeat, HouseBeats) : FString::Printf(TEXT("beat %d"), HouseBeat));
	Bits.Add(FString::Printf(TEXT("%d watching"), HouseViewers));
	FString Next;
	if (NextMatchS >= 0.0) Next = FString::Printf(TEXT("Next match in %d s"), FMath::CeilToInt(NextMatchS));
	else if (bReplaying) Next = TEXT("A new match starts when this replay ends.");
	else if (bLive && HouseBeats > HouseBeat) Next = FString::Printf(TEXT("Next match in about %d beats"), HouseBeats - HouseBeat);

	TSharedRef<SWrapBox> SeatWrap = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(Space::S, Space::S));
	for (const FHouseSeat& S : HouseSeats) SeatWrap->AddSlot()[FN7Lobby::SeatChip(S.Name, S.Kind)];

	return SNew(SBorder)
		.BorderImage(Brush::Panel())
		.Padding(FMargin(Space::L, Space::M))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("HouseLabel", "THE HOUSE GAME"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
				[
					SNew(SBox).WidthOverride(8.f).HeightOverride(8.f)[SNew(SImage).Image(Brush::Swatch(StateColor))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(StateColor).Text(StateWord)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
			[
				SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Body()).Font(Type::Font(14, TEXT("Bold"))).Text(FText::FromString(FString::Join(Bits, TEXT("  ·  "))))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Small()).Visibility(Next.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible).Text(FText::FromString(Next))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
			[
				HouseSeats.Num() > 0 ? StaticCastSharedRef<SWidget>(SeatWrap)
					: StaticCastSharedRef<SWidget>(SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Small()).Text(LOCTEXT("EmptyTable", "Nobody at the table yet.")))
			]
		];
}

void SFNStartPanel::RebuildRooms()
{
	FString Sig = bRoomsKnown ? TEXT("k") : TEXT("u");
	for (const FRoomRow& R : OtherRooms) Sig += FString::Printf(TEXT("|%s:%d:%d:%s"), *R.Code, R.SeatsTaken, R.MaxPlayers, *R.State);
	if (Sig == RoomsSignature && RoomsHolder.IsValid() && RoomsHolder->GetChildren()->Num() > 0) return;
	RoomsSignature = Sig;
	if (RoomsHolder.IsValid()) RoomsHolder->SetContent(MakeRooms());
}

TSharedRef<SWidget> SFNStartPanel::MakeRooms()
{
	using namespace FN7Style;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, Space::XS)
	[
		SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("OtherRooms", "OTHER ROOMS"))
	];
	if (!bRoomsKnown || OtherRooms.Num() == 0)
	{
		List->AddSlot().AutoHeight()
		[
			SNew(STextBlock).ColorAndOpacity(this, &SFNStartPanel::LInk).TextStyle(&Type::Small()).Text(bRoomsKnown ? LOCTEXT("NoRooms", "No other public rooms right now.") : LOCTEXT("RoomsLoading", "Asking the server..."))
		];
		return List;
	}
	for (int32 i = 0; i < OtherRooms.Num() && i < 6; ++i)
	{
		const FRoomRow Row = OtherRooms[i];
		const FString Seats = Row.MaxPlayers > 0 ? FString::Printf(TEXT("%d / %d seats"), Row.SeatsTaken, Row.MaxPlayers) : FString::Printf(TEXT("%d seated"), Row.SeatsTaken);
		List->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SBorder)
			.BorderImage(Brush::Panel())
			.Padding(FMargin(Space::M, Space::XS))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::M, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(13, TEXT("Bold"))).Text(FText::FromString(Row.Code))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).Text(FText::FromString(Seats + TEXT("  ·  ") + Row.State.Replace(TEXT("_"), TEXT(" "))))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&PillButton())
					.OnClicked_Lambda([this, Code = Row.Code]() { JoinOther(Code); return FReply::Handled(); })
					[
						SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("JoinRow", "JOIN"))
					]
				]
			]
		];
	}
	return List;
}

FString SFNStartPanel::ReadLocalLine(const TCHAR* File) const
{
	FString Txt;
	if (!FFileHelper::LoadFileToString(Txt, *FPaths::Combine(FPaths::ProjectConfigDir(), File))) return FString();
	TArray<FString> Lines;
	Txt.ParseIntoArrayLines(Lines);
	return Lines.Num() ? Lines[0].TrimStartAndEnd() : FString();
}

bool SFNStartPanel::IsViewer() const
{
	const UFNNetClient* N = Net.Get();
	return N && N->bViewer;
}

void SFNStartPanel::CancelHosting()
{
	UFNNetClient* N = Net.Get();
	if (!N || HostedCode.IsEmpty()) return;
	UE_LOG(LogTemp, Display, TEXT("FN7 lobby: cancel hosting %s, back to the house game"), *HostedCode);
	HostedCode.Reset();
	bBackToHouse = true;
	bJoining = false;
	Progress = TEXT("Leaving your room, back to the house game...");
	N->LeaveRoom();
}

FReply SFNStartPanel::OnPrimary()
{
	if (!HostedCode.IsEmpty()) { CancelHosting(); return FReply::Handled(); }   // BACK TO THE GAME while hosting = cancel it
	if (bInGame.Get())
	{
		OnBack.ExecuteIfBound();   // Back to the game
		return FReply::Handled();
	}
	return Enter(true);            // JOIN THE GAME
}

FReply SFNStartPanel::OnSecondary()
{
	if (bInGame.Get())
	{
		// TAKE A SEAT (only shown while watching).
		OnChoice.ExecuteIfBound(true);
		if (UFNNetClient* N = Net.Get())
		{
			N->bWatchOnly = false;
			N->bSeatAsked = true;
			N->TakeSeat();
		}
		OnBack.ExecuteIfBound();
		return FReply::Handled();
	}
	return Enter(false);           // WATCH
}

FReply SFNStartPanel::Enter(bool bTakeSeat)
{
	UFNNetClient* N = Net.Get();
	if (!N || bJoining) return FReply::Handled();
	OnChoice.ExecuteIfBound(bTakeSeat);
	N->bWatchOnly = !bTakeSeat;   // the root's auth.ok join uses it: JoinHouse(Net->bWatchOnly)
	N->bSeatAsked = false;

	const FString Name = NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd().Left(24) : FString();
	if (!Name.IsEmpty()) FFileHelper::SaveStringToFile(Name + TEXT("\n"), *FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7DisplayName.txt")));

	// JOIN never hosts: without the house game there is nothing to join.
	if (HouseCode.IsEmpty())
	{
		Progress = TEXT("The server has no game running right now. This checks again every few seconds.");
		return FReply::Handled();
	}
	bJoining = true;

	// The root joins the house at auth.ok (room.join {house:true}); already connected, ask for it directly.
	const TCHAR* How = bTakeSeat ? TEXT("Joining") : TEXT("Watching");
	if (!bConnected)
	{
		Progress = FString::Printf(TEXT("Connecting. %s %s..."), How, *HouseCode);
		N->Start();
	}
	else
	{
		Progress = FString::Printf(TEXT("%s %s..."), How, *HouseCode);
		N->JoinHouse(!bTakeSeat);
	}
	return FReply::Handled();
}

FSlateColor SFNStartPanel::LInk() const { return FN7Looks::Ink(Look); }
FSlateColor SFNStartPanel::LDim() const { return FN7Looks::Dim(Look); }
FSlateColor SFNStartPanel::LAccent() const { return FN7Looks::Accent(Look); }

int32 SFNStartPanel::PageLook() const
{
	if (Page == EPage::Welcome && Welcome.IsValid()) return Welcome->GetLook();
	if (Page == EPage::Settings && Settings.IsValid()) return Settings->GetLook();
	if (Page == EPage::Demigods && Demigods.IsValid()) return Demigods->GetLook();
	if (Page == EPage::God && God.IsValid()) return God->GetLook();
	return Look;
}
float SFNStartPanel::PageWidth() const { return Page == EPage::Demigods ? 1000.f : Page == EPage::God ? 900.f : Page == EPage::Settings ? 760.f : 600.f; }

void SFNStartPanel::OnBooted(bool bSignedIn)
{
	const FN7Account::FConfig& C = FN7Account::Config();
	Page = (!bSignedIn && !bInGame.Get() && C.bKnown && C.bGuest) ? EPage::Welcome : EPage::Lobby;
	if (bSignedIn && Settings.IsValid()) Settings->Open();   // reads the settings and keeps reduce flashing / text size locally
	if (bSignedIn && Page == EPage::Lobby) FN7Voice::SayOnce(FN7Voice::ELine::Welcome, TEXT("run"));
	UE_LOG(LogTemp, Display, TEXT("FN7 login: %s"), bSignedIn ? TEXT("signed in, the lobby") : Page == EPage::Welcome ? TEXT("welcome (no session)") : TEXT("no accounts on this server, the lobby"));
}

TSharedRef<SWidget> SFNStartPanel::MakeAccountStrip()
{
	using namespace FN7Style;
	auto SignedIn = [this]() { return FN7Account::IsSignedIn() ? EVisibility::Visible : EVisibility::Collapsed; };
	auto Small = [](const FText& T, TFunction<void()> F)
	{
		return SNew(SButton).ButtonStyle(&PillButton()).OnClicked_Lambda([F]() { F(); return FReply::Handled(); })
			[ SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(T) ];
	};
	return SNew(SVerticalBox).Visibility_Lambda(SignedIn)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("SignedAs", "SIGNED IN AS"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(this, &SFNStartPanel::LInk).Text_Lambda([]() { return FText::FromString(FN7Account::Me().DisplayName); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(Brush::PillAccent()).Padding(FMargin(Space::S, 1.f))
				.Visibility_Lambda([]() { return FN7Account::Me().bGuest ? EVisibility::Visible : EVisibility::Collapsed; })
				[ SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(FLinearColor(0.08f, 0.05f, 0.02f)).Text(LOCTEXT("GuestBadge", "GUEST")) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[
				SNew(SBox).Visibility_Lambda([this]() { return FN7Account::Me().bGuest && FN7Account::Config().bGoogle && !bLinking ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					Small(LOCTEXT("LinkGoogle", "LINK GOOGLE"), [this]()
					{
						bLinking = true;
						AccountNote = TEXT("Continue in your browser: sign in with Google there.");
						TWeakPtr<SFNStartPanel> Weak = StaticCastSharedRef<SFNStartPanel>(AsShared());
						FN7Account::Google(Net.Get(), true, FString(), [Weak](int32 Code, TSharedPtr<FJsonObject> O)
						{
							const TSharedPtr<SFNStartPanel> Self = Weak.Pin();
							if (!Self.IsValid()) return;
							Self->bLinking = false;
							FString E;
							if (O.IsValid()) O->TryGetStringField(TEXT("error"), E);
							Self->AccountNote = Code == 200 ? FString(TEXT("Google is linked: this account is yours on any computer now."))
								: E == TEXT("identity_in_use") ? FString(TEXT("That Google account already has its own account: log out, then sign in with Google instead."))
								: FN7Account::ErrorText(Code, O);
						});
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[
				SNew(SBox).Visibility_Lambda([this]() { return bLinking ? EVisibility::Visible : EVisibility::Collapsed; })
				[ Small(LOCTEXT("CancelLink", "CANCEL"), [this]() { FN7Account::CancelGoogle(); bLinking = false; AccountNote.Reset(); }) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[
				SNew(SBox).Visibility_Lambda([this]() { return bLogoutAsk || bInGame.Get() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					Small(LOCTEXT("LogOut", "LOG OUT"), [this]()
					{
						if (FN7Account::Me().bGuest) { bLogoutAsk = true; return; }   // a guest can't come back: ask first
						FN7Account::Logout(Net.Get(), [](int32, TSharedPtr<FJsonObject>) {});
						Page = EPage::Welcome;
					})
				]
			]
		]
		// The signed-in menu: the account's own screens.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::XS, 0.f)
			[ Small(LOCTEXT("MenuSettings", "SETTINGS"), [this]() { Page = EPage::Settings; if (Settings.IsValid()) Settings->Open(); UE_LOG(LogTemp, Display, TEXT("FN7 lobby: settings opened")); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::XS, 0.f)
			[ Small(LOCTEXT("MenuDemigods", "DEMIGODS"), [this]() { Page = EPage::Demigods; if (Demigods.IsValid()) Demigods->Open(); UE_LOG(LogTemp, Display, TEXT("FN7 lobby: demigods opened")); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, Space::XS, 0.f)
			[ Small(LOCTEXT("MenuGod", "YOUR GOD"), [this]() { Page = EPage::God; if (God.IsValid()) God->Open(); UE_LOG(LogTemp, Display, TEXT("FN7 lobby: god opened")); }) ]
		]
		// A guest logging out gives the account up (ACCOUNTS.md §2): say so first.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(SHorizontalBox).Visibility_Lambda([this]() { return bLogoutAsk ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNStartPanel::LAccent).AutoWrapText(true)
				.Text_Lambda([]() { return FN7Account::Config().bGoogle
					? LOCTEXT("GuestLogoutG", "A guest can't sign back in: logging out gives this account and its name up. LINK GOOGLE keeps it.")
					: LOCTEXT("GuestLogout", "A guest can't sign back in: logging out gives this account and its name up."); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[ Small(LOCTEXT("LogOutAnyway", "LOG OUT ANYWAY"), [this]() { bLogoutAsk = false; FN7Account::Logout(Net.Get(), [](int32, TSharedPtr<FJsonObject>) {}); Page = EPage::Welcome; }) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::XS, 0.f, 0.f, 0.f)
			[ Small(LOCTEXT("Keep", "KEEP IT"), [this]() { bLogoutAsk = false; }) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
		[
			SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNStartPanel::LDim).AutoWrapText(true)
			.Text_Lambda([this]()
			{
				if (!AccountNote.IsEmpty()) return FText::FromString(AccountNote);
				if (FN7Account::Me().bGuest) return FN7Account::Config().bGoogle ? LOCTEXT("GuestNoteG", "Link Google to keep this account.") : LOCTEXT("GuestNote", "This computer keeps you signed in.");
				return FText::GetEmpty();
			})
		];
}

TSharedRef<SWidget> SFNStartPanel::MakeChoices()
{
	using namespace FN7Style;
	const FLinearColor StartInk(0.06f, 0.04f, 0.03f);
	auto Big = [this, StartInk](const FText& Label, const FText& Under, uint8 Intent, bool bAccent)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(44.f)
				[
					SNew(SButton)
					.ButtonStyle(bAccent ? &AccentButton() : &PillButton())
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.IsEnabled_Lambda([this]() { return Choice == 0 && HostedCode.IsEmpty(); })
					.OnClicked_Lambda([this, Intent]() { Choose(Intent); return FReply::Handled(); })
					[
						SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(bAccent ? FSlateColor(StartInk) : FSlateColor(Color::Text())).Text(Label)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(Under)
			];
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("OrLabel", "OR PLAY YOUR OWN MATCH"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, Space::S, 0.f)
			[
				Big(LOCTEXT("VsBots", "PLAY VS BOTS"), LOCTEXT("VsBotsUnder", "you and three AI demigods, 60 beats"), (uint8)UFNNetClient::ELobbyIntent::VsBots, true)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				Big(LOCTEXT("Host", "HOST A ROOM"), LOCTEXT("HostUnder", "a code for your friends, START when ready"), (uint8)UFNNetClient::ELobbyIntent::Host, false)
			]
		]
		// Hosting: the code to share and START.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return HostedCode.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(this, &SFNStartPanel::LAccent)
					.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("ROOM %s"), *HostedCode)); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("HostShare", "give this code to your friends; START when everyone is in"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).HeightOverride(40.f).WidthOverride(130.f)
				[
					SNew(SButton).ButtonStyle(&AccentButton()).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked_Lambda([this]()
					{
						if (UFNNetClient* N = Net.Get()) { N->SetReady(true); N->StartMatch(); }
						UE_LOG(LogTemp, Display, TEXT("FN7 lobby: START %s"), *HostedCode);
						Progress = FString::Printf(TEXT("Starting %s..."), *HostedCode);
						return FReply::Handled();
					})
					[
						SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(StartInk).Text(LOCTEXT("Start", "START"))
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(40.f).WidthOverride(110.f)
				[
					SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ToolTipText(LOCTEXT("CancelHostTip", "Close your room and go back to the house game"))
					.OnClicked_Lambda([this]() { CancelHosting(); return FReply::Handled(); })
					[
						SNew(STextBlock).TextStyle(&Type::Name()).ColorAndOpacity(this, &SFNStartPanel::LInk).Text(LOCTEXT("CancelHost", "CANCEL"))
					]
				]
			]
		]
		// JOIN BY CODE.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(this, &SFNStartPanel::LDim).Text(LOCTEXT("ByCode", "JOIN BY CODE"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SAssignNew(CodeBox, SFNTextBox)
				.HintText(LOCTEXT("CodeHint", "a friend's room code"))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit) { if (Commit == ETextCommit::OnEnter) Choose((uint8)UFNNetClient::ELobbyIntent::Code); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space::S, 0.f, 0.f, 0.f)
			[
				SNew(SButton).ButtonStyle(&PillButton())
				.IsEnabled_Lambda([this]() { return Choice == 0 && CodeBox.IsValid() && !CodeBox->GetText().IsEmptyOrWhitespace(); })
				.OnClicked_Lambda([this]() { Choose((uint8)UFNNetClient::ELobbyIntent::Code); return FReply::Handled(); })
				[
					SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("JoinCode", "JOIN"))
				]
			]
		];
}

void SFNStartPanel::PlayAgainVsBots() { Choose((uint8)UFNNetClient::ELobbyIntent::VsBots); }

void SFNStartPanel::Choose(uint8 Intent)
{
	UFNNetClient* N = Net.Get();
	if (!N || Choice != 0) return;
	const FString Code = CodeBox.IsValid() ? CodeBox->GetText().ToString().TrimStartAndEnd().ToUpper() : FString();
	if (Intent == (uint8)UFNNetClient::ELobbyIntent::Code && Code.IsEmpty()) return;
	const FString Name = NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd().Left(24) : FString();
	if (!Name.IsEmpty()) FFileHelper::SaveStringToFile(Name + TEXT("\n"), *FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7DisplayName.txt")));
	Choice = Intent;
	OnChoice.ExecuteIfBound(true);
	N->bWatchOnly = false;
	N->bSeatAsked = false;
	N->LobbyIntent = (UFNNetClient::ELobbyIntent)Intent;
	N->LobbyCode = Code;
	const bool bBots = Intent == (uint8)UFNNetClient::ELobbyIntent::VsBots, bHost = Intent == (uint8)UFNNetClient::ELobbyIntent::Host;
	const FString What = bBots ? FString(TEXT("a match against the AI")) : bHost ? FString(TEXT("your room")) : Code;
	UE_LOG(LogTemp, Display, TEXT("FN7 lobby: %s (%s)"), bBots ? TEXT("play vs bots") : bHost ? TEXT("host") : TEXT("join by code"), bConnected ? TEXT("connected") : TEXT("connecting"));
	if (!bConnected)
	{
		bChoiceDirect = false;   // the root runs it at auth.ok
		Progress = FString::Printf(TEXT("Connecting. Opening %s..."), *What);
		N->Start();
	}
	else
	{
		bChoiceDirect = true;
		if (bInGame.Get()) { bAwaitLeave = true; N->LeaveRoom(); }   // leave the room we are in; the choice runs on room.left
		else N->RunLobbyIntent();
		Progress = FString::Printf(TEXT("Opening %s..."), *What);
	}
}

void SFNStartPanel::JoinOther(const FString& Code)
{
	UFNNetClient* N = Net.Get();
	if (!N || Code.IsEmpty()) return;
	OnChoice.ExecuteIfBound(true);
	N->bWatchOnly = false;
	N->bSeatAsked = false;
	if (bInGame.Get()) N->LeaveRoom();   // leave the room we are in, then join the other one
	N->JoinRoom(Code);
	Progress = FString::Printf(TEXT("Joining %s..."), *Code);
	OnBack.ExecuteIfBound();
}

FText SFNStartPanel::GetStatusText() const
{
	if (!bStatsKnown) return LOCTEXT("Checking", "Checking the server...");
	if (!bServerOnline) return LOCTEXT("Offline", "The server isn't answering. It may be restarting; this checks again every few seconds.");
	return FText::Format(LOCTEXT("Online", "Server online  -  {0} {0}|plural(one=player,other=players) connected  -  {1} {1}|plural(one=room,other=rooms)"),
		PlayersConnected, RoomCount);
}

static FSlateColor FN7LobbyOnlineColor(bool bKnown, bool bOnline)
{
	if (!bKnown) return FN7Style::Color::TextDim();
	return bOnline ? FLinearColor(0.35f, 0.9f, 0.5f) : FLinearColor(0.95f, 0.35f, 0.3f);
}

FSlateColor SFNStartPanel::GetStatusColor() const
{
	return FN7LobbyOnlineColor(bStatsKnown, bServerOnline);
}

FText SFNStartPanel::GetPrimaryText() const
{
	return bInGame.Get() ? LOCTEXT("Back", "BACK TO THE GAME") : LOCTEXT("Join", "JOIN THE GAME");
}

FText SFNStartPanel::GetSecondaryText() const
{
	return bInGame.Get() ? LOCTEXT("TakeSeat", "TAKE A SEAT") : LOCTEXT("Watch", "WATCH");
}

EVisibility SFNStartPanel::GetSecondaryVisibility() const
{
	if (!bInGame.Get()) return EVisibility::Visible;
	return IsViewer() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SFNStartPanel::GetProgressText() const
{
	return FText::FromString(Progress);
}

EActiveTimerReturnType SFNStartPanel::TickRefresh(double, float)
{
	// Only while the lobby is on screen.
	if (GetVisibility() == EVisibility::Collapsed) return EActiveTimerReturnType::Continue;
	RefreshStats();
	if (!bJoining) RefreshHouse();
	if (bInGame.Get() && bConnected)
		if (UFNNetClient* N = Net.Get()) N->ListRooms();
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
