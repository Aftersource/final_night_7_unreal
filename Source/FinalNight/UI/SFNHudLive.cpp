#include "SFNHudLive.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Texture2D.h"
#include "FNNetClient.h"
#include "FNPlayerController.h"
#include "FNStudio.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "ImageUtils.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Containers/Ticker.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/SOverlay.h"
#include "SFNStartPanel.h"
#include "FNPowerDrag.h"
#include "SFNDragToken.h"
#include "SFNPanelMover.h"
#include "FNStyle.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNHudLive"

namespace FN7HudJson
{
	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return (O.IsValid() && O->TryGetObjectField(K, Out) && Out) ? *Out : nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return (O.IsValid() && O->TryGetArrayField(K, Out)) ? Out : nullptr;
	}
	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		FString S;
		if (O.IsValid()) O->TryGetStringField(K, S);
		return S;
	}
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Default = 0.0)
	{
		double V = Default;
		if (O.IsValid()) O->TryGetNumberField(K, V);
		return V;
	}
	bool Has(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		return O.IsValid() && O->HasField(K) && !O->GetField<EJson::None>(K)->IsNull();
	}
}

/** Esc (no modifiers) opens and closes the lobby while we are in a room. */
class FFN7LobbyKeys : public IInputProcessor
{
public:
	TFunction<bool()> OnEscape;
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
	{
		if (E.GetKey() != EKeys::Escape || E.IsShiftDown() || E.IsControlDown() || E.IsAltDown() || E.IsCommandDown()) return false;
		return OnEscape ? OnEscape() : false;
	}
};

void SFNHudLive::Construct(const FArguments& InArgs)
{
	Net = InArgs._Net;
	Model.YouName = FText::GetEmpty();
	Model.ComposeHint = LOCTEXT("Hint", "Write what you'll do. Time slows while you write; SEND to act. Lines starting r: go to your right hand.");
	Model.PowersNote = LOCTEXT("PowersNote", "Arm a power, write its target (a place, a name or lon,lat), SEND");

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(Layout, SFNHudLayout)
			.Model(Model)
			.Visibility_Lambda([this]() { return bInRoom ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			.Timer(this, &SFNHudLive::GetTimer)
			.OnSend(this, &SFNHudLive::OnSend)
			.OnTypingStarted(this, &SFNHudLive::OnTyping)
			.OnPower(this, &SFNHudLive::OnPower)
			.OnTakeSeat_Lambda([this]() { if (UFNNetClient* N = Net.Get()) { N->bWatchOnly = false; N->bSeatAsked = true; N->TakeSeat(); } })
			.OnLobby_Lambda([this]() { SetLobbyOpen(true); })
		]
		// Out of the match: a small card under the header, never over the board's centre.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 92.f, 0.f, 0.f))
		[
			MakeOutCard()
		]
		// The lobby: covers everything (and swallows clicks) until we are in a room, and over the game when opened.
		+ SOverlay::Slot()
		[
			SAssignNew(StartPanel, SFNStartPanel)
			.Net(Net.Get())
			.bInGame_Lambda([this]() { return bInRoom; })
			.OnBack_Lambda([this]() { SetLobbyOpen(false); })
			.Visibility_Lambda([this]() { return (!bInRoom || bLobbyOpen) ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		// The power being dragged, floating over everything (never takes input).
		+ SOverlay::Slot()
		[
			SNew(SFNDragToken)
		]
	];

	if (UFNNetClient* N = Net.Get())
	{
		MessageHandle = N->OnServerMessage.AddSP(this, &SFNHudLive::HandleMessage);
		StatusHandle = N->OnStatus.AddSP(this, &SFNHudLive::HandleStatus);
	}
	DropHandle = FFNPowerDragDrop::OnDroppedOnWorld().AddSP(this, &SFNHudLive::OnPowerDropped);
	DragOverHandle = FFNPowerDragDrop::OnDragOverWorld().AddSP(this, &SFNHudLive::OnPowerDragOver);
	DragEndHandle = FFNPowerDragDrop::OnDragEnded().AddSP(this, &SFNHudLive::OnPowerDragEnded);
	FFNPowerDragDrop::SnapToBoard().BindSP(this, &SFNHudLive::SnapPowerToBoard);
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNHudLive::TickTimer));

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<FFN7LobbyKeys> Keys = MakeShared<FFN7LobbyKeys>();
		TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
		Keys->OnEscape = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			if (!Self.IsValid() || !Self->bInRoom) return false;
			Self->SetLobbyOpen(!Self->bLobbyOpen);
			return true;
		};
		LobbyKeys = Keys;
		FSlateApplication::Get().RegisterInputPreProcessor(LobbyKeys);
	}
}

void SFNHudLive::DebugPreview(bool bLobby, bool bFell)
{
	Model = SFNHudLayout::SampleModel();
	bInRoom = true;
	if (bFell)
	{
		bOut = true;
		bOutDismissed = false;
		OutBeat = Model.Beat;
		OutReason = TEXT("(preview) Both of your demigods have fallen: the Left Hand to Mara's army at Tashkent, the Right Hand to the flood.");
		OutNote.Reset();
	}
	if (Layout.IsValid()) Layout->SetModel(Model);   // the sample seats as they are (Publish would rebuild them from the live list)
	SetLobbyOpen(bLobby);
}

void SFNHudLive::SetLobbyOpen(bool bOpen)
{
	bLobbyOpen = bOpen;
	if (bOpen && StartPanel.IsValid()) StartPanel->Opened();
}

void SFNHudLive::ResetForNewRoom()
{
	bInRoom = false;
	bLobbyOpen = false;
	bOut = false;
	bTakeAsked = false;
	Seats.Reset();
	MySeat = -1;
	Model.Seats.Reset();
	Model.Powers.Reset();
	Model.News.Reset();
	Model.BeatsTotal = 0;
	Model.Beat = 0;
	Model.Room = FText::GetEmpty();
}

TSharedRef<SWidget> SFNHudLive::MakeOutCard()
{
	using namespace FN7Style;
	const FLinearColor OutInk(0.1f, 0.06f, 0.02f);
	return SNew(SBox)
		.WidthOverride(520.f)
		.Visibility_Lambda([this]() { return (bOut && !bOutDismissed && !bLobbyOpen && bInRoom) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder)
			.BorderImage(Brush::Card())
			.Padding(FMargin(Space::XL, Space::L))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).TextStyle(&Type::Title()).Text_Lambda([this]() { return FText::Format(LOCTEXT("YouFell", "You fell at beat {0}."), OutBeat); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XS, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(OutReason); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
				[
					SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Color::Accent()).AutoWrapText(true)
					.Visibility_Lambda([this]() { return OutNote.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					.Text_Lambda([this]() { return FText::FromString(OutNote); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(SBox).HeightOverride(40.f)
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.VAlign(VAlign_Center)
							.IsEnabled_Lambda([this]() { return !bTakeAsked; })
							.OnClicked_Lambda([this]()
							{
								if (UFNNetClient* N = Net.Get()) N->TakeSeat();
								bTakeAsked = true;
								TakeAskedAt = FPlatformTime::Seconds();
								OutNote = TEXT("Asking for a free demigod...");
								return FReply::Handled();
							})
							[
								SNew(STextBlock).TextStyle(&Type::Label()).Font(Type::Font(11, TEXT("Bold"))).ColorAndOpacity(OutInk).Text(LOCTEXT("TakeAnother", "TAKE ANOTHER DEMIGOD"))
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(&PillButton())
						.OnClicked_Lambda([this]() { bOutDismissed = true; return FReply::Handled(); })
						[
							SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("OutWatch", "WATCH"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(&PillButton())
						.OnClicked_Lambda([this]() { SetLobbyOpen(true); return FReply::Handled(); })
						[
							SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("OutLobby", "RETURN TO LOBBY"))
						]
					]
				]
			]
		];
}

SFNHudLive::~SFNHudLive()
{
	if (UFNNetClient* N = Net.Get())
	{
		N->OnServerMessage.Remove(MessageHandle);
		N->OnStatus.Remove(StatusHandle);
	}
	FFNPowerDragDrop::OnDroppedOnWorld().Remove(DropHandle);
	FFNPowerDragDrop::OnDragOverWorld().Remove(DragOverHandle);
	FFNPowerDragDrop::OnDragEnded().Remove(DragEndHandle);
	FFNPowerDragDrop::SnapToBoard().Unbind();
	if (LobbyKeys.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(LobbyKeys);
}

void SFNHudLive::HandleStatus(const FString& Status)
{
	Model.Status = FText::FromString(Status);
	Publish();
}

void SFNHudLive::HandleMessage(const FString& T, TSharedPtr<FJsonObject> D)
{
	// JOIN = TAKE A SEAT: joined as a viewer (the house is replaying, or between seats), ask for a seat once. The net's
	// bWatchOnly (WATCH) and bSeatAsked (the shared once-only guard; the root resets it at match.ended) decide.
	if (T == TEXT("room.snapshot") || T == TEXT("match.snapshot") || T == TEXT("room.updated"))
		if (UFNNetClient* N = Net.Get())
			if (N->bViewer && !N->bWatchOnly && !N->bSeatAsked)
			{
				N->bSeatAsked = true;
				N->TakeSeat();
				Model.Status = LOCTEXT("TakingSeat", "Taking a seat...");
			}
	// TAKE ANOTHER DEMIGOD answered: the house moved us to the lowest active bot seat (from the next round).
	if (T == TEXT("seat.retaken"))
	{
		const bool bMine = FN7HudJson::Str(D, TEXT("display_name")) == Model.YouName.ToString() || (int32)FN7HudJson::Num(D, TEXT("from_seat"), -2) == MySeat;
		if (bMine)
		{
			MySeat = (int32)FN7HudJson::Num(D, TEXT("to_seat"), MySeat);
			bOut = false;
			bTakeAsked = false;
			OutNote.Reset();
			Model.Status = LOCTEXT("Retaken", "You take over a new demigod from the next beat.");
			Publish();
			return;
		}
	}
	if (T == TEXT("seat.joined"))
	{
		Model.Status = FText::GetEmpty();
		if (FN7HudJson::Str(D, TEXT("display_name")) == Model.YouName.ToString() || (int32)FN7HudJson::Num(D, TEXT("seat"), -2) == MySeat)
		{
			bOut = false;   // took another demigod
			bTakeAsked = false;
		}
	}

	if (T == TEXT("auth.ok"))
	{
		Model.YouName = FText::FromString(FN7HudJson::Str(D, TEXT("display_name")));
	}
	else if (T == TEXT("room.snapshot") || T == TEXT("room.updated"))
	{
		bInRoom = true;
		const FString Code = FN7HudJson::Str(D, TEXT("code"));
		if (!Code.IsEmpty()) Model.Room = FText::FromString(Code);
		if (FN7HudJson::Str(D, TEXT("state")) == TEXT("open"))
			Model.Status = FText::Format(LOCTEXT("Lobby", "Room {0} is open. Give the code to the other players; the host starts the match."), Model.Room);
	}
	else if (T == TEXT("match.snapshot"))
	{
		bInRoom = true;
		Round = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
		const TSharedPtr<FJsonObject> PV = FN7HudJson::Obj(D, TEXT("public_state"));
		Model.Beat = (int32)FN7HudJson::Num(PV, TEXT("beat"), Model.Beat);
		Model.BeatsTotal = (int32)FN7HudJson::Num(PV, TEXT("beats_total"), Model.BeatsTotal);
		Model.Alive = (int32)FN7HudJson::Num(PV, TEXT("alive"), Model.Alive);
		ReadEntropy(FN7HudJson::Obj(PV, TEXT("entropy")));
		ReadSeatView(FN7HudJson::Obj(FN7HudJson::Obj(D, TEXT("you")), TEXT("private_state")));
		ReadDemigods(PV);
		Model.Status = FText::GetEmpty();
	}
	else if (T == TEXT("match.started"))
	{
		bInRoom = true;
		bOut = false;
		bTakeAsked = false;
		Model.Status = LOCTEXT("Started", "The match has begun.");
		Model.News.Reset();
	}
	else if (T == TEXT("phase.changed"))
	{
		Round = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
		if (FN7HudJson::Str(D, TEXT("phase")) == TEXT("compose") && WindowSeat < 0) DeadlineMs = FN7HudJson::Num(D, TEXT("deadline"), 0.0);
	}
	else if (T == TEXT("round.brief"))
	{
		bInRoom = true;
		const TSharedPtr<FJsonObject> Pub = FN7HudJson::Obj(D, TEXT("public"));
		Round = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
		Model.Beat = (int32)FN7HudJson::Num(Pub, TEXT("beat"), Round);
		Model.BeatsTotal = (int32)FN7HudJson::Num(Pub, TEXT("beats_total"), Model.BeatsTotal);
		Model.Alive = (int32)FN7HudJson::Num(Pub, TEXT("alive"), Model.Alive);
		ReadEntropy(FN7HudJson::Obj(Pub, TEXT("entropy")));
		if (const TArray<TSharedPtr<FJsonValue>>* P = FN7HudJson::Arr(Pub, TEXT("powers"))) ReadPowers(*P);
		if (const TSharedPtr<FJsonObject> Arena = FN7HudJson::Obj(Pub, TEXT("arena")))
			Model.ArenaLine = FText::Format(LOCTEXT("Arena", "Time's circle: {0} km around {1}. Stay inside."),
				FText::AsNumber((int32)FN7HudJson::Num(Arena, TEXT("radius_km"))), FText::FromString(FN7HudJson::Str(Arena, TEXT("city"))));
		ReadSeatView(FN7HudJson::Obj(D, TEXT("private")));
		if (Seats.Num() == 0) FetchGlobe();   // the PublicView only comes at the first commit: ask the globe query now
		Model.Status = FText::GetEmpty();
	}
	else if (T == TEXT("round.private_state"))
	{
		ReadSeatView(FN7HudJson::Obj(D, TEXT("state")));
	}
	else if (T == TEXT("round.committed"))
	{
		const TSharedPtr<FJsonObject> PV = FN7HudJson::Obj(D, TEXT("public_state"));
		Model.Alive = (int32)FN7HudJson::Num(PV, TEXT("alive"), Model.Alive);
		ReadEntropy(FN7HudJson::Obj(PV, TEXT("entropy")));
		ReadDemigods(PV);
	}
	else if (T == TEXT("round.outcome"))
	{
		const TSharedPtr<FJsonObject> Pub = FN7HudJson::Obj(D, TEXT("public"));
		ReadEntropy(FN7HudJson::Obj(Pub, TEXT("entropy")));
		const FText Tag = FText::Format(LOCTEXT("BeatTag", "BEAT {0}"), (int32)FN7HudJson::Num(Pub, TEXT("beat"), Round));
		if (const TArray<TSharedPtr<FJsonValue>>* Events = FN7HudJson::Arr(Pub, TEXT("events")))
		{
			int32 Added = 0;
			for (int32 i = Events->Num() - 1; i >= 0 && Added < 3; --i)
			{
				const TSharedPtr<FJsonObject> E = (*Events)[i]->AsObject();
				const FString Text = FN7HudJson::Str(E, TEXT("text"));
				if (FN7HudJson::Num(E, TEXT("magnitude"), 0.0) < 4.0 || Text.IsEmpty()) continue;
				AddNews(Tag, Text);
				++Added;
			}
		}
		const FString Caption = FN7HudJson::Str(Pub, TEXT("previous_caption"));
		if (!Caption.IsEmpty()) AddNews(LOCTEXT("Story", "STORY"), Caption);
	}
	else if (T == TEXT("round.private"))
	{
		const TSharedPtr<FJsonObject> Priv = FN7HudJson::Obj(D, TEXT("private"));
		if (const TArray<TSharedPtr<FJsonValue>>* Hands = FN7HudJson::Arr(Priv, TEXT("hands")))
			for (const TSharedPtr<FJsonValue>& H : *Hands)
			{
				const FString Said = FN7HudJson::Str(H->AsObject(), TEXT("said"));
				if (!Said.IsEmpty()) AddNews(LOCTEXT("Hands", "HANDS"), Said);
			}
	}
	else if (T == TEXT("window.opened"))
	{
		WindowSeat = (int32)FN7HudJson::Num(D, TEXT("by_seat"), -1);
		DeadlineMs = FN7HudJson::Num(D, TEXT("deadline"), 0.0);
	}
	else if (T == TEXT("window.closed"))
	{
		WindowSeat = -1;
		DeadlineMs = 0.0;
		if (UFNNetClient* N = Net.Get()) N->EndTyping();
	}
	else if (T == TEXT("action.received"))
	{
		bool bAccepted = true;
		if (D.IsValid()) D->TryGetBoolField(TEXT("accepted"), bAccepted);
		if (!bAccepted)
		{
			const FString Reason = FN7HudJson::Str(D, TEXT("reason"));
			AddNews(LOCTEXT("Refused", "REFUSED"), Reason == TEXT("cut") ? TEXT("Too slow: the window closed before your orders arrived.") : Reason);
		}
	}
	else if (T == TEXT("error"))
	{
		if (bOut && bTakeAsked)
		{
			bTakeAsked = false;
			OutNote = Model.BeatsTotal > Model.Beat
				? FString::Printf(TEXT("No demigod is free right now. The next match starts in about %d beats."), Model.BeatsTotal - Model.Beat)
				: FString(TEXT("No demigod is free right now. You'll get one next match."));
			return;
		}
		if (FN7HudJson::Str(D, TEXT("code")) == TEXT("room_full"))
		{
			Model.Status = LOCTEXT("TableFull", "Table full: watching. You'll get a seat next match.");
			Publish();
			return;
		}
		AddNews(LOCTEXT("Error", "ERROR"), FN7HudJson::Str(D, TEXT("message")));
	}
	else if (T == TEXT("seat.eliminated"))
	{
		if ((int32)FN7HudJson::Num(D, TEXT("seat"), -2) == MySeat && MySeat >= 0)
		{
			// Rules 0.7.0: this comes once, when both of your demigods have fallen.
			OutReason = FN7HudJson::Str(D, TEXT("reason"));
			OutBeat = (int32)FN7HudJson::Num(D, TEXT("round"), Round);
			OutNote.Reset();
			bOut = true;
			bOutDismissed = false;
			bTakeAsked = false;
			AddNews(LOCTEXT("Fallen", "FALLEN"), FString::Printf(TEXT("You fell: %s"), *OutReason));
		}
	}
	else if (T == TEXT("seat.revived"))
	{
		if ((int32)FN7HudJson::Num(D, TEXT("seat"), -2) == MySeat) bOut = false;
	}
	else if (T == TEXT("room.left"))
	{
		ResetForNewRoom();
	}
	else if (T == TEXT("match.ended"))
	{
		if (UFNNetClient* N = Net.Get()) N->bSeatAsked = false;   // still a viewer next match: JOIN asks again
		const TSharedPtr<FJsonObject> Summary = FN7HudJson::Obj(D, TEXT("summary"));
		FString Champ;
		if (const TArray<TSharedPtr<FJsonValue>>* C = FN7HudJson::Arr(Summary, TEXT("champions")))
			if (C->Num() > 0) Champ = FN7HudJson::Str((*C)[0]->AsObject(), TEXT("name"));
		Model.Status = FText::Format(LOCTEXT("Over", "MATCH OVER  -  champion: {0}"), FText::FromString(Champ));
		DeadlineMs = 0.0;
		WindowSeat = -1;
	}
	else
	{
		return;
	}
	Publish();
}

void SFNHudLive::ReadSeatView(const TSharedPtr<FJsonObject>& View)
{
	if (!View.IsValid()) return;
	MySeat = (int32)FN7HudJson::Num(View, TEXT("seat"), MySeat);
	const FString Name = FN7HudJson::Str(View, TEXT("name"));
	if (!Name.IsEmpty()) Model.YouName = FText::FromString(Name);
	Model.MaxHealth = FMath::RoundToInt(FN7HudJson::Num(View, TEXT("max_health"), Model.MaxHealth));
	if (const TSharedPtr<FJsonObject> You = FN7HudJson::Obj(View, TEXT("you")))
	{
		Model.Health = FMath::Clamp(FMath::RoundToInt(FN7HudJson::Num(You, TEXT("health"), Model.Health)), 0, Model.MaxHealth);
		Model.Bolts = (int32)FN7HudJson::Num(You, TEXT("bolts"), Model.Bolts);
		if (FN7HudJson::Has(You, TEXT("sparks"))) Model.Sparks = (int32)FN7HudJson::Num(You, TEXT("sparks"), 0.0);
	}
	Model.PowersNote = FText::Format(LOCTEXT("PowersNoteBolts", "{0} bolts  -  arm a power, write its target (place, name or lon,lat), SEND. Every bolt you spend gives each rival +2."), Model.Bolts);
}

void SFNHudLive::ReadDemigods(const TSharedPtr<FJsonObject>& PV)
{
	const TArray<TSharedPtr<FJsonValue>>* List = FN7HudJson::Arr(PV, TEXT("demigods"));
	if (!List) return;
	Seats.Reset();
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> G = V->AsObject();
		FSeatInfo S;
		S.Ref = FN7HudJson::Str(G, TEXT("ref"));
		S.Name = FN7HudJson::Str(G, TEXT("name"));
		S.Seat = FN7HudJson::Has(G, TEXT("seat")) ? (int32)FN7HudJson::Num(G, TEXT("seat"), -1) : -1;
		bool bAlive = true;
		if (G.IsValid()) G->TryGetBoolField(TEXT("alive"), bAlive);
		S.bAlive = bAlive;
		if (FN7HudJson::Has(G, TEXT("bolts"))) S.Bolts = (int32)FN7HudJson::Num(G, TEXT("bolts"), 0.0);
		if (FN7HudJson::Has(G, TEXT("sparks"))) S.Sparks = (int32)FN7HudJson::Num(G, TEXT("sparks"), 0.0);
		if (const TSharedPtr<FJsonObject> Img = FN7HudJson::Obj(G, TEXT("portrait_image")))
		{
			S.PortraitHash = FN7HudJson::Str(Img, TEXT("hash"));
			if (FN7HudJson::Str(Img, TEXT("status")) == TEXT("ready")) S.PortraitUrl = FN7HudJson::Str(Img, TEXT("url"));
			if (!S.PortraitHash.IsEmpty() && !S.PortraitUrl.IsEmpty()) RequestPortrait(S.PortraitHash, S.PortraitUrl);
		}
		if (S.Seat >= 0 && S.Seat == MySeat)
		{
			const FString Place = FN7HudJson::Str(G, TEXT("place"));
			const FString Region = FN7HudJson::Str(G, TEXT("region"));
			Model.Place = FText::FromString(Place == Region || Region.IsEmpty() ? Place : Place + TEXT(", ") + Region);
		}
		Seats.Add(S);
	}
}

void SFNHudLive::ReadEntropy(const TSharedPtr<FJsonObject>& E)
{
	if (!E.IsValid()) return;
	Model.Entropy = FText::FromString(FString::Printf(TEXT("%s %d"), *FN7HudJson::Str(E, TEXT("key")).ToUpper(), FMath::RoundToInt(FN7HudJson::Num(E, TEXT("value")))));
}

void SFNHudLive::ReadPowers(const TArray<TSharedPtr<FJsonValue>>& Powers)
{
	// The powers contract: every power has a mega tier (a bolt, map scale) and a mini tier (a spark, the board),
	// each { radius_km, magnitude, cost }. A server without tiers sends cost and radius_km on the power itself.
	Model.Powers.Reset();
	for (const TSharedPtr<FJsonValue>& V : Powers)
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		const FString Slug = FN7HudJson::Str(P, TEXT("slug"));
		if (Slug.IsEmpty() || Slug == TEXT("custom")) continue;
		const FText Name = FText::FromString(FN7HudJson::Str(P, TEXT("name")));
		const TSharedPtr<FJsonObject> Mega = FN7HudJson::Obj(P, TEXT("mega"));
		const TSharedPtr<FJsonObject> Mini = FN7HudJson::Obj(P, TEXT("mini"));
		auto Add = [&](const FString& Tier, const TSharedPtr<FJsonObject>& From)
		{
			FFNHudPower Out;
			Out.Slug = Slug;
			Out.Name = Name;
			Out.Tier = Tier;
			Out.Cost = (int32)FN7HudJson::Num(From, TEXT("cost"), 1);
			Out.RadiusKm = FN7HudJson::Num(From, TEXT("radius_km"), 0.0);
			PowerRadiusKm.Add(Out.Key(), Out.RadiusKm);
			Model.Powers.Add(Out);
		};
		if (Mega.IsValid() || Mini.IsValid())
		{
			if (Mega.IsValid()) Add(TEXT("mega"), Mega);
			if (Mini.IsValid()) Add(TEXT("mini"), Mini);
		}
		else
		{
			Add(FString(), P);
		}
	}
}

void SFNHudLive::AddNews(const FText& Tag, const FString& Text)
{
	if (Text.IsEmpty()) return;
	FFNHudNews N;
	N.Tag = Tag;
	N.Text = FText::FromString(Text.Len() > 220 ? Text.Left(217) + TEXT("...") : Text);
	Model.News.Insert(N, 0);
	if (Model.News.Num() > 6) Model.News.SetNum(6);
}

void SFNHudLive::FetchGlobe()
{
	UFNNetClient* N = Net.Get();
	const FString Room = Model.Room.ToString();
	if (!N || Room.IsEmpty() || bGlobePending) return;
	bGlobePending = true;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(FString::Printf(TEXT("/api/rooms/%s/query/globe?limit=1"), *Room)));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(15.f);
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bGlobePending = false;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		TSharedPtr<FJsonObject> Globe;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Globe) || !Globe.IsValid()) return;
		if (Self->Seats.Num() > 0) return;   // a commit got there first
		Self->ReadDemigods(Globe);
		Self->Publish();
	});
	Req->ProcessRequest();
}

void SFNHudLive::RequestPortrait(const FString& Hash, const FString& Url)
{
	UFNNetClient* N = Net.Get();
	if (!N || Portraits.Contains(Hash) || PortraitRequests.Contains(Hash)) return;
	PortraitRequests.Add(Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
	Req->OnProcessRequestComplete().BindLambda([Weak, Hash](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<SFNHudLive> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->PortraitRequests.Remove(Hash);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(Resp->GetContent());
		if (!Tex) return;
		Self->Portraits.Add(Hash, TStrongObjectPtr<UTexture2D>(Tex));
		Self->Publish();
	});
	Req->ProcessRequest();
}

void SFNHudLive::Publish()
{
	Model.Seats.Reset();
	for (const FSeatInfo& S : Seats)
	{
		FFNHudSeat Out;
		Out.Name = FText::FromString(S.Name);
		Out.Role = !S.bAlive ? EFNSeatRole::Fallen : (S.Seat >= 0 ? EFNSeatRole::Human : EFNSeatRole::AI);
		Out.bIsYou = S.Seat >= 0 && S.Seat == MySeat;
		Out.bHoldsTime = S.Seat >= 0 && S.Seat == WindowSeat;
		Out.Bolts = S.Bolts;
		Out.Sparks = S.Sparks;
		if (const TStrongObjectPtr<UTexture2D>* Tex = Portraits.Find(S.PortraitHash)) Out.PortraitTexture = Tex->Get();
		Model.Seats.Add(Out);
	}
	for (FFNHudPower& P : Model.Powers) P.bArmed = P.Key() == ArmedPower;
	if (UFNNetClient* N = Net.Get()) Model.bViewer = N->bViewer;
	if (Layout.IsValid()) Layout->SetModel(Model);
}

void SFNHudLive::OnTyping()
{
	if (UFNNetClient* N = Net.Get())
		if (N->bFn7 && ArmedPower.IsEmpty()) N->BeginTyping();
}

void SFNHudLive::OnSend(const FString& Text)
{
	UFNNetClient* N = Net.Get();
	if (!N) return;
	if (!ArmedPower.IsEmpty())
	{
		// ArmedPower is "slug" or "slug:tier" (mega spends a bolt, mini a spark; a server without tiers takes mini).
		FString ArmedSlug = ArmedPower, ArmedTier;
		ArmedPower.Split(TEXT(":"), &ArmedSlug, &ArmedTier);
		if (ArmedSlug.IsEmpty()) ArmedSlug = ArmedPower;
		N->CastPower(Round, ArmedSlug, Text, ArmedTier.IsEmpty() ? FString(TEXT("mini")) : ArmedTier);
		AddNews(LOCTEXT("Cast", "CAST"), FString::Printf(TEXT("%s at %s"), *ArmedPower, *Text));
		ArmedPower.Reset();
		Publish();
		return;
	}
	FString Left, Right;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		const FString Line = L.TrimStartAndEnd();
		if (Line.StartsWith(TEXT("r:"), ESearchCase::IgnoreCase)) Right += (Right.IsEmpty() ? TEXT("") : TEXT("\n")) + Line.RightChop(2).TrimStart();
		else if (Line.StartsWith(TEXT("l:"), ESearchCase::IgnoreCase)) Left += (Left.IsEmpty() ? TEXT("") : TEXT("\n")) + Line.RightChop(2).TrimStart();
		else Left += (Left.IsEmpty() ? TEXT("") : TEXT("\n")) + Line;
	}
	N->SubmitOrders(Round, Left, Right);
	N->EndTyping();
	AddNews(LOCTEXT("You", "YOU"), Text);
	Publish();
}

namespace FN7ViewportPx
{
	/** Slate absolute (desktop) position <-> game viewport pixel, through the viewport widget's geometry. */
	bool AbsToPixel(const FVector2D& Abs, FVector2D& OutPixel)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> W = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!W.IsValid() || !VC->Viewport) return false;
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Local = FVector2D(G.AbsoluteToLocal(Abs));
		const FVector2D Size = FVector2D(G.GetLocalSize());
		const FIntPoint Px = VC->Viewport->GetSizeXY();
		if (Size.X <= 0.0 || Size.Y <= 0.0) return false;
		OutPixel = Local * FVector2D(Px.X / Size.X, Px.Y / Size.Y);
		return Local.X >= 0.0 && Local.Y >= 0.0 && Local.X <= Size.X && Local.Y <= Size.Y;
	}

	bool PixelToAbs(const FVector2D& Pixel, FVector2D& OutAbs)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> W = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!W.IsValid() || !VC->Viewport) return false;
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Size = FVector2D(G.GetLocalSize());
		const FIntPoint Px = VC->Viewport->GetSizeXY();
		if (Px.X <= 0 || Px.Y <= 0) return false;
		OutAbs = FVector2D(G.LocalToAbsolute(Pixel * FVector2D(Size.X / Px.X, Size.Y / Px.Y)));
		return true;
	}
}

AFNStudio* SFNHudLive::FindStudio() const
{
	UFNNetClient* N = Net.Get();
	AFNPlayerController* PC = N ? Cast<AFNPlayerController>(N->GetOuter()) : nullptr;
	return PC ? PC->Studio.Get() : nullptr;
}

TOptional<FVector2D> SFNHudLive::SnapPowerToBoard(const FString&, const FVector2D& ScreenPosition)
{
	if (SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return {};   // over the HUD: the token follows the cursor
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	APlayerController* PC = N ? Cast<APlayerController>(N->GetOuter()) : nullptr;
	FVector2D Pixel, Abs, HexPixel;
	double La, Lo, HexM;
	FVector World;
	if (!St || !PC || !FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) || !St->HexAtScreen(Pixel, La, Lo, World, HexM)) return {};
	if (!PC->ProjectWorldLocationToScreen(World, HexPixel) || !FN7ViewportPx::PixelToAbs(HexPixel, Abs)) return {};
	return Abs;
}

void SFNHudLive::OnPowerDragOver(const FString& Power, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	FVector2D Pixel;
	if (!St) return;
	const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();
	const FString Key = (Op.IsValid() && !Op->Tier.IsEmpty()) ? Power + TEXT(":") + Op->Tier : Power;
	const double* Radius = PowerRadiusKm.Find(Key);
	const bool bOver = FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) && !SFNPanelMover::IsOverAnyPanel(ScreenPosition);
	St->ShowAreaOfEffect(bOver, Pixel, Radius ? *Radius : 0.0);
}

void SFNHudLive::OnPowerDragEnded(const FString&)
{
	if (AFNStudio* St = FindStudio()) St->ShowAreaOfEffect(false, FVector2D::ZeroVector, 0.0);
}

void SFNHudLive::OnPowerDropped(const FString& Power, const FVector2D& ScreenPosition)
{
	if (SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return;   // let go over a panel: nothing happens
	// Dropped on the board: cast at that hex. Anywhere else (the globe, empty table): arm it, write the target, SEND.
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	FVector2D Pixel;
	double La, Lo, HexM;
	FVector World;
	if (N && St && FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) && St->HexAtScreen(Pixel, La, Lo, World, HexM))
	{
		const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();   // still set while OnDroppedOnWorld runs
		const FString Tier = (Op.IsValid() && !Op->Tier.IsEmpty()) ? Op->Tier : FString(TEXT("mini"));
		N->CastAt(Round, Power, Lo, La, Tier);
		St->FlashAt(Lo, La);
		ArmedPower.Reset();
		AddNews(LOCTEXT("Cast", "CAST"), FString::Printf(TEXT("%s at %.2f%s %.2f%s"), *Power, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
		Publish();
		return;
	}
	ArmedPower = Power;
	AddNews(LOCTEXT("Armed", "ARMED"), FString::Printf(TEXT("%s: drop it on a board hex, or write its target and SEND."), *Power));
	Publish();
}

void SFNHudLive::OnPower(const FString& Slug)
{
	ArmedPower = ArmedPower == Slug ? FString() : Slug;
	Publish();
}

FText SFNHudLive::GetTimer() const
{
	if (DeadlineMs <= 0.0) return FText::GetEmpty();
	const double NowMs = (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	const int32 Secs = FMath::Max(0, FMath::CeilToInt((DeadlineMs - NowMs) / 1000.0));
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60));
}

EActiveTimerReturnType SFNHudLive::TickTimer(double, float)
{
	// TAKE ANOTHER DEMIGOD unanswered for a few seconds: say when the next chance comes.
	if (bOut && bTakeAsked && FPlatformTime::Seconds() - TakeAskedAt > 6.0)
	{
		bTakeAsked = false;
		OutNote = Model.BeatsTotal > Model.Beat
			? FString::Printf(TEXT("No demigod is free right now. The next match starts in about %d beats."), Model.BeatsTotal - Model.Beat)
			: FString(TEXT("No demigod is free right now. You'll get one next match."));
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

// --- mounting ---------------------------------------------------------------

namespace FN7HudMount
{
	TSharedPtr<SWidget> GMountWeak;
	TSharedPtr<SWidget> GMountLive;
	TArray<TWeakPtr<SWidget>> GHidden;

	void FindByType(const TSharedRef<SWidget>& W, const FName& Type, TArray<TSharedRef<SWidget>>& Out)
	{
		if (W->GetType() == Type) Out.Add(W);
		FChildren* Kids = W->GetChildren();
		if (!Kids) return;
		for (int32 i = 0; i < Kids->Num(); ++i) FindByType(Kids->GetChildAt(i), Type, Out);
	}

	void SetOldHudHidden(bool bHide)
	{
		if (!bHide)
		{
			for (const TWeakPtr<SWidget>& W : GHidden)
				if (TSharedPtr<SWidget> P = W.Pin()) P->SetVisibility(EVisibility::SelfHitTestInvisible);
			GHidden.Reset();
			return;
		}
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SViewport> VP = VC ? VC->GetGameViewportWidget() : nullptr;
		if (!VP.IsValid()) return;
		TArray<TSharedRef<SWidget>> Found;
		FindByType(VP.ToSharedRef(), FName(TEXT("SFNRootWidget")), Found);
		for (const TSharedRef<SWidget>& W : Found)
		{
			W->SetVisibility(EVisibility::Collapsed);
			GHidden.Add(W);
		}
	}

	bool Mount(UFNNetClient* Net)
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		if (!VC || !Net) return false;
		Unmount();
		GMountLive = SNew(SFNHudLive).Net(Net);
		GMountWeak = SNew(SWeakWidget).PossiblyNullContent(GMountLive);
		VC->AddViewportWidgetContent(GMountWeak.ToSharedRef(), 50);
		SetOldHudHidden(true);
		return true;
	}

	void Unmount()
	{
		UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
		if (VC && GMountWeak.IsValid()) VC->RemoveViewportWidgetContent(GMountWeak.ToSharedRef());
		GMountWeak.Reset();
		GMountLive.Reset();
		SetOldHudHidden(false);
	}

	bool IsMounted() { return GMountLive.IsValid(); }

	void Preview(bool bLobby, bool bFell)
	{
		if (GMountLive.IsValid()) StaticCastSharedPtr<SFNHudLive>(GMountLive)->DebugPreview(bLobby, bFell);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFN7HudCommand(
	TEXT("FN7.Hud"),
	TEXT("FN7.Hud on | off [connect] [shot] [quit] [lobby] [fell]: the live style-kit HUD over the game (hides the old HUD while on). ")
	TEXT("lobby / fell = preview the lobby over the game / the out card on the sample table (no game joined). ")
	TEXT("connect = start the net client now; shot = save Saved/FN7HudLive_<n>.png every 8 s; quit = exit after 4 shots."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const bool bOff = Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase);
		if (bOff) { FN7HudMount::Unmount(); return; }
		const bool bConnect = Args.Contains(TEXT("connect"));
		const bool bShot = Args.Contains(TEXT("shot"));
		const bool bQuit = Args.Contains(TEXT("quit"));
		const bool bAoe = Args.Contains(TEXT("aoe"));
		const bool bLobby = Args.Contains(TEXT("lobby"));
		const bool bFell = Args.Contains(TEXT("fell"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		// Wait for the player controller and its net client, then mount (and optionally connect / photograph).
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudMount"), 0.5f, [WeakWorld, bConnect, bShot, bQuit, bAoe, bLobby, bFell](float)
		{
			UWorld* W = WeakWorld.Get();
			AFNPlayerController* PC = W ? Cast<AFNPlayerController>(W->GetFirstPlayerController()) : nullptr;
			if (!PC || !PC->Net) return true;
			FN7HudMount::Mount(PC->Net);
			if (bConnect) PC->Net->Start();
			if (bLobby || bFell) FN7HudMount::Preview(bLobby, bFell);
			if (bAoe)
			{
				// Test: light an Earthquake's area (250 km) around the board hex at 42% / 78% of the viewport, as a drag would.
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudAoe"), 13.f, [WeakPC](float)
				{
					AFNPlayerController* P = WeakPC.Get();
					UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
					if (!P || !P->Studio || !VC || !VC->Viewport) return false;
					const FIntPoint Px = VC->Viewport->GetSizeXY();
					const FVector2D Pixel(Px.X * 0.42, Px.Y * 0.78);
					double La = 0, Lo = 0, HexM = 0;
					FVector World;
					const bool bHit = P->Studio->HexAtScreen(Pixel, La, Lo, World, HexM);
					UE_LOG(LogTemp, Warning, TEXT("FN7 AoE test: hex hit=%d lat=%.3f lon=%.3f hex=%.0f m"), bHit ? 1 : 0, La, Lo, HexM);
					P->Studio->ShowAreaOfEffect(true, Pixel, 250.0);
					return false;
				});
			}
			if (bShot)
			{
				for (int32 i = 1; i <= 4; ++i)
				{
					FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudShot"), 8.f * i, [i](float)
					{
						FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), FString::Printf(TEXT("FN7HudLive_%d.png"), i)), true, false);
						return false;
					});
				}
				if (bQuit)
				{
					FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudQuit"), 36.f, [](float)
					{
						FPlatformMisc::RequestExit(false, TEXT("FN7.Hud quit"));
						return false;
					});
				}
			}
			return false;
		});
	}));

#undef LOCTEXT_NAMESPACE
