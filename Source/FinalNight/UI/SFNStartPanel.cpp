#include "SFNStartPanel.h"
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

	auto OutOfGame = [this]() { return bInGame.Get() ? EVisibility::Collapsed : EVisibility::Visible; };
	auto InGame = [this]() { return bInGame.Get() ? EVisibility::Visible : EVisibility::Collapsed; };

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
			.WidthOverride(600.f)
			[
				SNew(SBorder)
				.BorderImage(FN7Wood::MarbleBrush())   // a black onyx marble tablet (Chris asked for a mystical, granite or onyx marbled look)
				.Padding(FMargin(Space::XL + Space::S, Space::XL))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)
						[
							SNew(STextBlock).TextStyle(&Type::Display()).Text(LOCTEXT("Title", "FINAL NIGHT 7"))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(Space::M, 0.f, 0.f, 6.f)
						[
							SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("LobbyWord", "LOBBY"))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(64.f).HeightOverride(3.f).HAlign(HAlign_Left)[SNew(SImage).Image(Brush::AccentRule())]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Color::TextDim()).AutoWrapText(true).Visibility_Lambda(OutOfGame)
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
							SNew(STextBlock).TextStyle(&Type::Small()).Text(this, &SFNStartPanel::GetStatusText)
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
						SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Visibility_Lambda(OutOfGame).Text(LOCTEXT("NameLabel", "YOUR NAME"))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).Visibility_Lambda(OutOfGame)
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
						SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true).Text(this, &SFNStartPanel::GetProgressText)
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
						SNew(STextBlock).TextStyle(&Type::Small()).Visibility_Lambda(InGame).Text(LOCTEXT("EscHint", "Esc opens and closes the lobby."))
					]
				]
			]
		]
	];

	RebuildHouse();
	RebuildRooms();
	if (UFNNetClient* N = Net.Get()) MessageHandle = N->OnServerMessage.AddSP(this, &SFNStartPanel::HandleMessage);
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
}

// Swallow every click on the scrim: nothing falls through to the globe or the table.
FReply SFNStartPanel::OnMouseButtonDown(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply SFNStartPanel::OnMouseButtonUp(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }
FReply SFNStartPanel::OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&) { return FReply::Handled(); }

void SFNStartPanel::HandleMessage(const FString& T, TSharedPtr<FJsonObject> D)
{
	if (T == TEXT("auth.ok"))
	{
		bConnected = true;
		if (bJoining) Progress = TEXT("Connected. Joining the game...");
	}
	else if (T == TEXT("room.snapshot") || T == TEXT("match.snapshot"))
	{
		bJoining = false;
		Progress.Reset();
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
		return SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("HouseChecking", "Looking for the game..."));
	if (HouseCode.IsEmpty())
		return SNew(STextBlock).TextStyle(&Type::Body()).AutoWrapText(true).Text(LOCTEXT("NoGame", "The server has no game running right now. This checks again every few seconds."));
	if (HouseSource == TEXT("file"))
		return SNew(STextBlock).TextStyle(&Type::Body()).Text(FText::Format(LOCTEXT("FileGame", "Room {0} (this machine's join code)"), FText::FromString(HouseCode)));

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
					SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("HouseLabel", "THE HOUSE GAME"))
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
				SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(14, TEXT("Bold"))).Text(FText::FromString(FString::Join(Bits, TEXT("  ·  "))))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).TextStyle(&Type::Small()).Visibility(Next.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible).Text(FText::FromString(Next))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
			[
				HouseSeats.Num() > 0 ? StaticCastSharedRef<SWidget>(SeatWrap)
					: StaticCastSharedRef<SWidget>(SNew(STextBlock).TextStyle(&Type::Small()).Text(LOCTEXT("EmptyTable", "Nobody at the table yet.")))
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
		SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("OtherRooms", "OTHER ROOMS"))
	];
	if (!bRoomsKnown || OtherRooms.Num() == 0)
	{
		List->AddSlot().AutoHeight()
		[
			SNew(STextBlock).TextStyle(&Type::Small()).Text(bRoomsKnown ? LOCTEXT("NoRooms", "No other public rooms right now.") : LOCTEXT("RoomsLoading", "Asking the server..."))
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

FReply SFNStartPanel::OnPrimary()
{
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
