#include "SFNHudLive.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
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
#include "Misc/FileHelper.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/SOverlay.h"
#include "SFNStartPanel.h"
#include "FNPowerDrag.h"
#include "SFNDragToken.h"
#include "SFNPanelMover.h"
#include "FNPowerRing.h"
#include "FNTableGods.h"
#include "FNMedia.h"
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

/** True when keyboard focus is in the game viewport or the HUD over it (not in an editor panel). */
static bool FN7GameHasFocus()
{
	UGameViewportClient* VC = GEngine ? GEngine->GameViewport : nullptr;
	TSharedPtr<SViewport> VP = VC ? VC->GetGameViewportWidget() : nullptr;
	return VP.IsValid() && (VP->HasKeyboardFocus() || VP->HasFocusedDescendants());
}

/** Esc (no modifiers) opens and closes the lobby, Enter opens the WILL, while we are in a room and the game has focus. */
/** True when a text field has the keyboard (the WILL, the lobby's name box): letter keys belong to it then. */
static bool FN7TypingInText()
{
	if (!FSlateApplication::IsInitialized()) return false;
	const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
	return F.IsValid() && F->GetTypeAsString().Contains(TEXT("EditableText"));
}

class FFN7LobbyKeys : public IInputProcessor
{
public:
	TFunction<bool()> OnEscape;
	TFunction<bool()> OnEnter;
	TFunction<bool()> OnReset;
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
	{
		if (E.IsShiftDown() || E.IsControlDown() || E.IsAltDown() || E.IsCommandDown() || E.IsRepeat()) return false;
		if (!FN7GameHasFocus()) return false;
		if (E.GetKey() == EKeys::Escape) return OnEscape ? OnEscape() : false;
		if (E.GetKey() == EKeys::Enter) return OnEnter ? OnEnter() : false;
		if ((E.GetKey() == EKeys::R || E.GetKey() == EKeys::Home) && !FN7TypingInText()) return OnReset ? OnReset() : false;
		return false;
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
		// Under everything: the power hexes on the table take a press (lift and drag a hex, click the pouch); any other
		// click falls through to the game.
		+ SOverlay::Slot()
		[
			SAssignNew(RingInput, SFNPowerRingInput)
			.OnHexClicked_Lambda([this](const FString& Slug) { OnPower(Slug); })
			.Visibility_Lambda([this]() { return (bInRoom && !bLobbyOpen && !Model.bViewer) ? EVisibility::Visible : EVisibility::Collapsed; })
		]
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
			.OnResetView_Lambda([this]() { ResetView(); })
			.OnCardDrag_Lambda([this](const FString& Ref, const FVector2D& Screen) -> TSharedPtr<FDragDropOperation>
			{
				const FString Why = WhyNotSummon(Ref);
				UE_LOG(LogTemp, Display, TEXT("FN7 summon card drag: %s %s"), *Ref, Why.IsEmpty() ? TEXT("-> drag") : *Why);
				if (!Why.IsEmpty()) { Toast(Why); return nullptr; }
				if (AFNTableGods* T = AFNTableGods::GetAny(FindStudio() ? FindStudio()->GetWorld() : nullptr)) T->SetFigureHeld(Ref, true);
				return FFNSummonDragDrop::New(Ref, Screen);
			})
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
	SummonOverHandle = FFNSummonDragDrop::OnOver().AddSP(this, &SFNHudLive::OnSummonOver);
	SummonDropHandle = FFNSummonDragDrop::OnDropped().AddSP(this, &SFNHudLive::OnSummonDropped);
	if (RingInput.IsValid())
	{
		TWeakPtr<SFNHudLive> WeakSelf = StaticCastSharedRef<SFNHudLive>(AsShared());
		RingInput->SetIsMine([WeakSelf](const FString& Ref) { const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin(); return Self.IsValid() && Self->WhyNotSummon(Ref).IsEmpty(); });
		RingInput->SetSummonCheck(
			[WeakSelf](const FString& Ref) { const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin(); return Self.IsValid() ? Self->WhyNotSummon(Ref) : FString(); },
			[WeakSelf](const FString& Text) { if (const TSharedPtr<SFNHudLive> Self = WeakSelf.Pin()) Self->Toast(Text); });
	}
	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNHudLive::TickTimer));

	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<FFN7LobbyKeys> Keys = MakeShared<FFN7LobbyKeys>();
		TWeakPtr<SFNHudLive> Weak = StaticCastSharedRef<SFNHudLive>(AsShared());
		Keys->OnEscape = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			if (!Self.IsValid() || !Self->bInRoom) return false;
			// Writing: Esc rolls the WILL back up (the text stays). Otherwise it opens or closes the lobby.
			if (!Self->bLobbyOpen && Self->Layout.IsValid() && Self->Layout->IsWillOpen()) { Self->Layout->RollUpWill(); return true; }
			Self->SetLobbyOpen(!Self->bLobbyOpen);
			return true;
		};
		Keys->OnEnter = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			if (!Self.IsValid() || !Self->bInRoom || Self->bLobbyOpen || !Self->Layout.IsValid() || Self->Layout->IsWillOpen() || Self->Model.bViewer) return false;
			Self->Layout->FocusWill();
			return true;
		};
		Keys->OnReset = [Weak]()
		{
			TSharedPtr<SFNHudLive> Self = Weak.Pin();
			if (!Self.IsValid() || !Self->bInRoom || Self->bLobbyOpen) return false;
			Self->ResetView();
			return true;
		};
		LobbyKeys = Keys;
		FSlateApplication::Get().RegisterInputPreProcessor(LobbyKeys);
	}
}

void SFNHudLive::DebugPreview(bool bLobby, bool bFell, bool bWill)
{
	Model = SFNHudLayout::SampleModel();
	bInRoom = true;
	if (bWill)
	{
		// You hold slowed time and are writing: the scroll open, the gold edge, the live bill.
		MySeat = 0;
		WindowSeat = 0;
		bSlowBill = true;
		UFNNetClient* N = Net.Get();
		SlowOpenedMs = (N ? N->NowServerMs() : 0.0) - 8000.0;
		DebtBase = 1.0;
		DebtPerS = 0.15;
		DebtCap = 6.0;
		BoltPaid = 1;
	}
	if (bFell)
	{
		bOut = true;
		bOutDismissed = false;
		OutBeat = Model.Beat;
		OutReason = TEXT("(preview) Both of your demigods have fallen: the Left Hand to Mara's army at Tashkent, the Right Hand to the flood.");
		OutNote.Reset();
	}
	Model.bPowerPanel = FindStudio() == nullptr;
	if (Layout.IsValid()) Layout->SetModel(Model);   // the sample seats as they are (Publish would rebuild them from the live list)
	UpdateRing();
	if (bWill && Layout.IsValid()) Layout->FocusWill();
	PreviewTable();
	SetLobbyOpen(bLobby);
}

void SFNHudLive::ResetView()
{
	AFNStudio* St = FindStudio();
	if (St)
	{
		St->ResetView();
		if (UWorld* W = St->GetWorld())
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (APawn* Me = PC->GetPawn()) Me->SetActorLocation(St->GetSeatLocation());
				PC->SetControlRotation(St->GetSeatRotation());
			}
	}
	SFNPanelMover::ResetAll();
	AddNews(LOCTEXT("ViewTag", "VIEW"), TEXT("Back to the start: the board on your demigod, the camera at your seat, the panels home."));
	Publish();
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
	FFNSummonDragDrop::OnOver().Remove(SummonOverHandle);
	FFNSummonDragDrop::OnDropped().Remove(SummonDropHandle);
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
	else if (T == TEXT("phase.event"))
	{
		// Time slowed: when it is you, the Slowed event carries the bill's terms (rules 0.7.0).
		const TSharedPtr<FJsonObject> Pub = FN7HudJson::Obj(D, TEXT("public"));
		if (FN7HudJson::Str(Pub, TEXT("event")) == TEXT("slowed") && (int32)FN7HudJson::Num(Pub, TEXT("by_seat"), -2) == MySeat && MySeat >= 0)
		{
			bSlowBill = FN7HudJson::Has(Pub, TEXT("debt_per_second"));
			UFNNetClient* N = Net.Get();
			SlowOpenedMs = FN7HudJson::Num(Pub, TEXT("opened_at_ms"), N ? N->NowServerMs() : 0.0);
			DebtBase = FN7HudJson::Num(Pub, TEXT("debt_base"), 0.0);
			DebtPerS = FN7HudJson::Num(Pub, TEXT("debt_per_second"), 0.0);
			DebtCap = FN7HudJson::Num(Pub, TEXT("debt_cap"), 1.0e9);
			BoltPaid = FN7HudJson::Has(Pub, TEXT("bolt_paid")) ? (int32)FN7HudJson::Num(Pub, TEXT("bolt_paid"), 1.0) : 1;
		}
		return;
	}
	else if (T == TEXT("window.closed"))
	{
		bSlowBill = false;
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
	// Rules 0.7.0: your two demigods in full (each with its own health), your aura, the purse, the debt.
	if (const TArray<TSharedPtr<FJsonValue>>* Hands = FN7HudJson::Arr(View, TEXT("demigods")))
	{
		Model.Mine.Reset();
		MinePortraitHash.Reset();
		MineDioramaHash.Reset();
		double DebtSum = 0.0;
		bool bDebt = false;
		for (const TSharedPtr<FJsonValue>& V : *Hands)
		{
			const TSharedPtr<FJsonObject> G = V.IsValid() ? V->AsObject() : nullptr;
			if (!G.IsValid()) continue;
			FFNHudDemigod Dg;
			Dg.Ref = FN7HudJson::Str(G, TEXT("ref"));
			Dg.Name = FText::FromString(FN7HudJson::Str(G, TEXT("name")));
			Dg.Side = FN7HudJson::Str(G, TEXT("side"));
			Dg.MaxHealth = FMath::Max(1, FMath::RoundToInt(FN7HudJson::Num(G, TEXT("max_health"), 10.0)));
			Dg.Health = FMath::Clamp(FMath::RoundToInt(FN7HudJson::Num(G, TEXT("health"), Dg.MaxHealth)), 0, Dg.MaxHealth);
			bool bAliveHand = true;
			G->TryGetBoolField(TEXT("alive"), bAliveHand);
			Dg.bAlive = bAliveHand;
			if (FN7HudJson::Has(G, TEXT("debt_open"))) { DebtSum += FN7HudJson::Num(G, TEXT("debt_open"), 0.0); bDebt = true; }
			FString Hash, DioHash;
			if (const TSharedPtr<FJsonObject> Img = FN7HudJson::Obj(G, TEXT("portrait_image")))
			{
				Hash = FN7HudJson::Str(Img, TEXT("hash"));
				const FString Url = FN7HudJson::Str(Img, TEXT("status")) == TEXT("ready") ? FN7HudJson::Str(Img, TEXT("url")) : FString();
				if (!Hash.IsEmpty() && !Url.IsEmpty()) RequestPortrait(Hash, Url);
			}
			if (const TSharedPtr<FJsonObject> Dio = FN7HudJson::Obj(G, TEXT("diorama_image")))
			{
				DioHash = FN7HudJson::Str(Dio, TEXT("hash"));
				const FString Url = FN7HudJson::Str(Dio, TEXT("status")) == TEXT("ready") ? FN7HudJson::Str(Dio, TEXT("url")) : FString();
				if (!DioHash.IsEmpty() && !Url.IsEmpty()) RequestPortrait(DioHash, Url);
			}
			Model.Mine.Add(Dg);
			MinePortraitHash.Add(Hash);
			MineDioramaHash.Add(DioHash);
		}
		// The seat view's demigods ARE yours: the truth for summoning (never names: a taken-over seat keeps the bot's).
		MineRefs.Reset();
		MineAlive.Reset();
		for (const FFNHudDemigod& Dg : Model.Mine) if (!Dg.Ref.IsEmpty()) { MineRefs.Add(Dg.Ref); MineAlive.Add(Dg.Ref, Dg.bAlive); }
		if (!FN7HudJson::Has(View, TEXT("seat")))
			for (const TSharedPtr<FJsonValue>& V : *Hands)
				if (const TSharedPtr<FJsonObject> G = V.IsValid() ? V->AsObject() : nullptr)
					if (FN7HudJson::Has(G, TEXT("seat"))) { MySeat = (int32)FN7HudJson::Num(G, TEXT("seat"), MySeat); break; }
		{
			FString Log = FString::Printf(TEXT("seat=%d"), MySeat);
			for (const FFNHudDemigod& Dg : Model.Mine) Log += FString::Printf(TEXT(" %s(%s,%s)"), *Dg.Ref, *Dg.Side, Dg.bAlive ? TEXT("alive") : TEXT("fallen"));
			if (Log != LastMineLog) { LastMineLog = Log; UE_LOG(LogTemp, Display, TEXT("FN7 mine: %s"), *Log); }
		}
		if (Model.Mine.Num() == 2 && Model.Mine[0].Side == TEXT("right"))   // Left first
		{
			Model.Mine.Swap(0, 1);
			MinePortraitHash.Swap(0, 1);
			MineDioramaHash.Swap(0, 1);
		}
		if (bDebt) Model.Debt = DebtSum;
	}
	else if (const TSharedPtr<FJsonObject> You = FN7HudJson::Obj(View, TEXT("you")))
	{
		if (FN7HudJson::Has(You, TEXT("debt_open"))) Model.Debt = FN7HudJson::Num(You, TEXT("debt_open"), 0.0);
	}
	const FString Aura = FN7HudJson::Str(View, TEXT("aura"));
	if (Aura.StartsWith(TEXT("#")) && Aura.Len() >= 7) Model.YourAura = FLinearColor::FromSRGBColor(FColor::FromHex(Aura));
	if (const TSharedPtr<FJsonObject> Charges = FN7HudJson::Obj(View, TEXT("charges")))
	{
		Model.Bolts = (int32)FN7HudJson::Num(Charges, TEXT("bolts"), Model.Bolts);
		if (FN7HudJson::Has(Charges, TEXT("sparks"))) Model.Sparks = (int32)FN7HudJson::Num(Charges, TEXT("sparks"), 0.0);
	}
	const FString Player = FN7HudJson::Str(View, TEXT("player"));
	if (!Player.IsEmpty() && Model.Mine.Num() > 0) Model.YouName = FText::FromString(Player);
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
		S.Side = FN7HudJson::Str(G, TEXT("side"));
		S.Player = FN7HudJson::Str(G, TEXT("player"));
		const FString AuraHex = FN7HudJson::Str(G, TEXT("aura"));
		if (AuraHex.StartsWith(TEXT("#")) && AuraHex.Len() >= 7) { S.Aura = FLinearColor::FromSRGBColor(FColor::FromHex(AuraHex)); S.bHasAura = true; }
		S.Controller = FN7HudJson::Str(G, TEXT("controller"));
		if (G.IsValid()) G->TryGetBoolField(TEXT("connected"), S.bConnected);
		if (const TArray<TSharedPtr<FJsonValue>>* Pos = FN7HudJson::Arr(G, TEXT("pos")))
			if (Pos->Num() >= 2) { S.Lon = (*Pos)[0]->AsNumber(); S.Lat = (*Pos)[1]->AsNumber(); S.bHasPos = true; }
		if (const TSharedPtr<FJsonObject> Dio = FN7HudJson::Obj(G, TEXT("diorama_image")))
		{
			S.DioramaHash = FN7HudJson::Str(Dio, TEXT("hash"));
			const FString Url = FN7HudJson::Str(Dio, TEXT("status")) == TEXT("ready") ? FN7HudJson::Str(Dio, TEXT("url")) : FString();
			if (!S.DioramaHash.IsEmpty() && !Url.IsEmpty()) RequestPortrait(S.DioramaHash, Url);
		}
		if (S.Seat >= 0 && S.Seat == MySeat)
		{
			const FString Place = FN7HudJson::Str(G, TEXT("place"));
			const FString Region = FN7HudJson::Str(G, TEXT("region"));
			Model.Place = FText::FromString(Place == Region || Region.IsEmpty() ? Place : Place + TEXT(", ") + Region);
		}
		Seats.Add(S);
	}
	// The players (gods): public_state.players[] when the server sends it.
	if (const TArray<TSharedPtr<FJsonValue>>* PL = FN7HudJson::Arr(PV, TEXT("players")))
	{
		Players.Reset();
		for (const TSharedPtr<FJsonValue>& V : *PL)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			FPlayerInfo P;
			P.Seat = FN7HudJson::Has(O, TEXT("seat")) ? (int32)FN7HudJson::Num(O, TEXT("seat"), -1) : -1;
			P.Player = FN7HudJson::Str(O, TEXT("player"));
			const FString A = FN7HudJson::Str(O, TEXT("aura"));
			if (A.StartsWith(TEXT("#")) && A.Len() >= 7) { P.Aura = FLinearColor::FromSRGBColor(FColor::FromHex(A)); P.bHasAura = true; }
			O->TryGetBoolField(TEXT("alive"), P.bAlive);
			P.Controller = FN7HudJson::Str(O, TEXT("controller"));
			O->TryGetBoolField(TEXT("connected"), P.bConnected);
			Players.Add(P);
		}
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
	// The disk cache first (Saved/FN7Media): full fidelity, a full mip chain.
	{
		TArray<uint8> Cached;
		if (FN7Media::LoadCached(Hash, Cached))
			if (UTexture2D* Tex = FN7Media::TextureFromPng(Cached))
			{
				Portraits.Add(Hash, TStrongObjectPtr<UTexture2D>(Tex));
				MediaPng.Add(Hash, MoveTemp(Cached));
				return;
			}
	}
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
		const TArray<uint8> Png = Resp->GetContent();
		UTexture2D* Tex = FN7Media::TextureFromPng(Png);
		if (!Tex) Tex = FImageUtils::ImportBufferAsTexture2D(Png);
		if (!Tex) return;
		FN7Media::SaveCached(Hash, Png);
		Self->MediaPng.Add(Hash, Png);
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
	// Your cards lead with the diorama (the dioramas should lead), the portrait as a small inset.
	for (int32 i = 0; i < Model.Mine.Num(); ++i)
	{
		UTexture2D* Portrait = MinePortraitHash.IsValidIndex(i) ? MediaTexture(MinePortraitHash[i]) : nullptr;
		UTexture2D* Dio = MineDioramaHash.IsValidIndex(i) ? MediaTexture(MineDioramaHash[i], !Model.Mine[i].bAlive) : nullptr;
		Model.Mine[i].PortraitTexture = Dio ? Dio : Portrait;
		Model.Mine[i].InsetTexture = Dio ? Portrait : nullptr;
	}
	if (Seats.Num() > 0) Model.AliveOf = Seats.Num();
	for (FFNHudPower& P : Model.Powers) P.bArmed = P.Key() == ArmedPower;
	if (UFNNetClient* N = Net.Get()) Model.bViewer = N->bViewer;
	Model.bPowerPanel = FindStudio() == nullptr;   // the powers live on the table; the HUD rows only without one
	if (Layout.IsValid()) Layout->SetModel(Model);
	UpdateRing();
	UpdateTable();
}

void SFNHudLive::PreviewTable()
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	auto Load = [this](const TCHAR* Rel) -> FString
	{
		const FString Key = FString(TEXT("preview:")) + Rel;
		if (!Portraits.Contains(Key))
		{
			TArray<uint8> Png;
			if (FFileHelper::LoadFileToArray(Png, *FPaths::Combine(FPaths::ProjectDir(), Rel)))
				if (UTexture2D* T = FN7Media::TextureFromPng(Png))
				{
					Portraits.Add(Key, TStrongObjectPtr<UTexture2D>(T));
					MediaPng.Add(Key, Png);
				}
		}
		return Key;
	};
	const FString DioA = Load(TEXT("docs/style-refs/approved/diorama_maxzoom.png"));
	const FString DioB = Load(TEXT("docs/style-refs/approved/event_maxzoom.png"));
	struct FSample { const TCHAR* Name; int32 Seat; uint32 Aura; bool bHuman; bool bConnected; bool bRightFallen; double La, Lo; };
	const FSample Samples[] = {
		{ TEXT("Chris"), 0, 0xE69F00, true, true, false, 46.9, 8.2 },
		{ TEXT("Benito"), 1, 0x009E73, true, true, false, 45.8, 10.6 },
		{ TEXT("Vex"), 2, 0xF0E442, false, true, false, 47.6, 10.1 },
		{ TEXT("Mara"), 3, 0xF28CC0, false, false, true, 46.2, 7.4 },
	};
	TArray<FFNTableGod> Gods;
	int32 Ref = 0;
	for (const FSample& Sm : Samples)
	{
		FFNTableGod G;
		G.Seat = Sm.Seat;
		G.Player = Sm.Name;
		G.Aura = FLinearColor::FromSRGBColor(FColor((Sm.Aura >> 16) & 255, (Sm.Aura >> 8) & 255, Sm.Aura & 255));
		G.bHuman = Sm.bHuman;
		G.bConnected = Sm.bConnected;
		G.bMine = Sm.Seat == 0;
		for (int32 h = 0; h < 2; ++h)
		{
			FFNTableDemigod D;
			D.Ref = FString::Printf(TEXT("dg:%d"), Ref++);
			D.Name = FString::Printf(TEXT("%s's %s Hand"), Sm.Name, h == 0 ? TEXT("Left") : TEXT("Right"));
			D.Side = h == 0 ? TEXT("left") : TEXT("right");
			D.bAlive = !(h == 1 && Sm.bRightFallen);
			D.bHasPos = true;
			D.Lat = Sm.La + h * 0.5;
			D.Lon = Sm.Lo + h * 0.6;
			D.Diorama = MediaTexture(h == 0 ? DioA : DioB);
			D.DioramaGrey = D.bAlive ? nullptr : MediaTexture(h == 0 ? DioA : DioB, true);
			G.Demigods.Add(D);
		}
		Gods.Add(G);
	}
	if (AFNTableGods* T = AFNTableGods::Get(St, true))
	{
		T->SetGods(Gods, 0);
		T->SetShown(true);
	}
	PreviewMine = { TEXT("dg:0"), TEXT("dg:1") };
}

UTexture2D* SFNHudLive::MediaTexture(const FString& Hash, bool bGrey)
{
	if (Hash.IsEmpty()) return nullptr;
	const TStrongObjectPtr<UTexture2D>* Tex = Portraits.Find(Hash);
	if (!Tex) return nullptr;
	if (!bGrey) return Tex->Get();
	if (const TStrongObjectPtr<UTexture2D>* G = GreyMedia.Find(Hash)) return G->Get();
	if (const TArray<uint8>* Png = MediaPng.Find(Hash))
		if (UTexture2D* G = FN7Media::TextureFromPng(*Png, true))
		{
			GreyMedia.Add(Hash, TStrongObjectPtr<UTexture2D>(G));
			return G;
		}
	return Tex->Get();
}

void SFNHudLive::UpdateTable()
{
	AFNStudio* St = FindStudio();
	if (!St || Seats.Num() == 0) return;
	// Gods: the players list, else the demigods grouped by their player.
	TArray<FFNTableGod> Gods;
	auto GodFor = [&Gods](const FString& Player, int32 Seat) -> FFNTableGod&
	{
		for (FFNTableGod& G : Gods) if ((!Player.IsEmpty() && G.Player == Player) || (Player.IsEmpty() && Seat >= 0 && G.Seat == Seat)) return G;
		FFNTableGod& G = Gods.AddDefaulted_GetRef();
		G.Player = Player;
		G.Seat = Seat;
		return G;
	};
	for (const FPlayerInfo& P : Players)
	{
		FFNTableGod& G = GodFor(P.Player, P.Seat);
		G.Seat = P.Seat;
		if (P.bHasAura) G.Aura = P.Aura;
		G.bAlive = P.bAlive;
		G.bConnected = P.bConnected;
		G.bHuman = P.Controller == TEXT("human");
	}
	for (const FSeatInfo& S : Seats)
	{
		FFNTableGod& G = GodFor(S.Player.IsEmpty() ? S.Name : S.Player, S.Seat);
		if (G.Seat < 0) G.Seat = S.Seat;
		if (S.bHasAura && Players.Num() == 0) G.Aura = S.Aura;
		if (Players.Num() == 0)
		{
			G.bHuman = S.Controller.IsEmpty() ? S.Seat >= 0 : S.Controller == TEXT("human");
			G.bConnected = S.bConnected;
			G.bAlive = G.Demigods.Num() == 0 ? S.bAlive : (G.bAlive || S.bAlive);
		}
		FFNTableDemigod D;
		D.Ref = S.Ref;
		D.Name = S.Name;
		D.Side = S.Side;
		D.bAlive = S.bAlive;
		D.bHasPos = S.bHasPos;
		D.Lat = S.Lat;
		D.Lon = S.Lon;
		D.Diorama = MediaTexture(S.DioramaHash);
		D.DioramaGrey = S.bAlive ? nullptr : MediaTexture(S.DioramaHash, true);
		if (D.Side == TEXT("right") && G.Demigods.Num() == 1 && G.Demigods[0].Side != TEXT("right")) G.Demigods.Add(D);
		else if (D.Side != TEXT("right")) G.Demigods.Insert(D, 0);
		else G.Demigods.Add(D);
	}
	for (FFNTableGod& G : Gods)
	{
		G.bMine = MySeat >= 0 && G.Seat == MySeat;
		for (const FFNTableDemigod& D : G.Demigods) G.bMine |= MineRefs.Contains(D.Ref);
	}
	if (AFNTableGods* T = AFNTableGods::Get(St, true))
	{
		T->SetGods(Gods, MySeat);
		T->SetShown(bInRoom);
	}
}

double SFNHudLive::RadiusFor(const FString& Slug, const FString& Tier) const
{
	if (const double* R = PowerRadiusKm.Find(Slug + TEXT(":") + Tier)) return *R;
	if (const double* R = PowerRadiusKm.Find(Slug)) return *R;
	return 0.0;
}

void SFNHudLive::UpdateRing()
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	if (RingInput.IsValid()) RingInput->SetStudio(St);
	const bool bShow = bInRoom && Model.Powers.Num() > 0 && !Model.bViewer;
	AFNPowerRing* Ring = AFNPowerRing::Get(St, bShow);
	if (!Ring) return;
	// One hex per power (the tier is where it lands), in this machine's saved order.
	TArray<FString> Slugs;
	TArray<FText> Names;
	for (const FFNHudPower& P : Model.Powers)
		if (!Slugs.Contains(P.Slug)) { Slugs.Add(P.Slug); Names.Add(P.Name); }
	Ring->SetPowers(Slugs, Names);
	Ring->SetPurse(Model.Bolts, Model.Sparks);
	Ring->SetShown(bShow);
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
		SetArmed(FString());
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

bool SFNHudLive::IsMyDemigod(const FString& Ref) const
{
	if (MineRefs.Contains(Ref) || PreviewMine.Contains(Ref)) return true;
	for (const FSeatInfo& S : Seats) if (S.Ref == Ref) return S.Seat >= 0 && S.Seat == MySeat;
	return false;
}

FString SFNHudLive::WhyNotSummon(const FString& Ref) const
{
	if (Model.bViewer) return TEXT("You're watching: take a seat to summon.");
	if (!IsMyDemigod(Ref)) return TEXT("That's a rival's demigod.");
	if (const bool* bA = MineAlive.Find(Ref)) { if (!*bA) return TEXT("That demigod has fallen."); }
	else for (const FSeatInfo& S : Seats) if (S.Ref == Ref && !S.bAlive) return TEXT("That demigod has fallen.");
	return FString();
}

void SFNHudLive::Toast(const FString& Text)
{
	Model.Status = FText::FromString(Text);
	ToastUntil = FPlatformTime::Seconds() + 3.0;
	Publish();
}

void SFNHudLive::OnSummonOver(const FString& Ref, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	if (!St) return;
	double La = 0.0, Lo = 0.0;
	FVector World;
	FVector2D Pixel;
	const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
	St->ShowAreaOfEffect(Where == 1, Pixel, 0.0);           // the gold target hex on the board
	St->ShowMegaOnGlobe(Where == 2, La, Lo, 300.0);          // a small ring on the globe
}

void SFNHudLive::OnSummonDropped(const FString& Ref, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	if (!St) return;
	double La = 0.0, Lo = 0.0;
	FVector World;
	FVector2D Pixel;
	const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
	St->ShowAreaOfEffect(false, FVector2D::ZeroVector, 0.0);
	St->ShowMegaOnGlobe(false, 0.0, 0.0, 0.0);
	AFNTableGods* T = AFNTableGods::GetAny(St->GetWorld());
	if (T) T->SetFigureHeld(Ref, false);
	if (Where == 0) return;   // let go anywhere else: it goes back
	if (N) N->Summon(Round, Ref, Lo, La);
	if (T) T->SetLocalPos(Ref, La, Lo);
	FString Name = Ref;
	for (const FSeatInfo& S : Seats) if (S.Ref == Ref && !S.Name.IsEmpty()) Name = S.Name;
	for (const FFNHudDemigod& D : Model.Mine) if (Name == Ref && !D.Name.IsEmpty() && ((Ref.EndsWith(TEXT("0")) && D.Side != TEXT("right")) || (Ref.EndsWith(TEXT("1")) && D.Side == TEXT("right")))) Name = D.Name.ToString();
	AddNews(LOCTEXT("Summon", "SUMMON"), FString::Printf(TEXT("%s summoned to %.2f%s %.2f%s (sent)"), *Name, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
	Publish();
}

int32 SFNHudLive::RingTarget(const FVector2D& ScreenPosition, double& OutLat, double& OutLon, FVector& OutWorld, FVector2D& OutPixel) const
{
	AFNStudio* St = FindStudio();
	if (!St || !FN7ViewportPx::AbsToPixel(ScreenPosition, OutPixel) || SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return 0;
	if (St->GlobePointAtScreen(OutPixel, OutLat, OutLon, OutWorld)) return 2;   // MEGA: over the globe
	double HexM;
	if (St->HexAtScreen(OutPixel, OutLat, OutLon, OutWorld, HexM)) return 1;   // MINI: on the board
	return 0;
}

void SFNHudLive::OnPowerDragOver(const FString& Power, const FVector2D& ScreenPosition)
{
	AFNStudio* St = FindStudio();
	FVector2D Pixel;
	if (!St) return;
	const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();
	if (Op.IsValid() && Op->bFromRing)
	{
		// A hex in the hand: over the globe it is a MEGA (its area ringed on the globe), over the board a MINI.
		double La = 0.0, Lo = 0.0;
		FVector World;
		const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
		St->ShowMegaOnGlobe(Where == 2, La, Lo, RadiusFor(Power, TEXT("mega")));
		St->ShowAreaOfEffect(Where == 1, Pixel, RadiusFor(Power, TEXT("mini")));
		if ((Where == 2) != bOverGlobe)
		{
			bOverGlobe = Where == 2;
			FFNPowerDragDrop::OnMegaArmed().Broadcast(bOverGlobe, Power);
		}
		if (AFNPowerRing* Ring = AFNPowerRing::Get(St, false))
		{
			APlayerController* PC = St->GetWorld() ? St->GetWorld()->GetFirstPlayerController() : nullptr;
			const float S = (float)St->GetActorScale3D().X;
			if (Where == 2 && PC && PC->PlayerCameraManager)
				Ring->MoveLifted(World + (PC->PlayerCameraManager->GetCameraLocation() - World).GetSafeNormal() * 14.f * S);
			else if (Where == 1)
				Ring->MoveLifted(World + FVector(0, 0, 9.f * S));
			else if (PC)
			{
				// Anywhere else: hover over the table plane under the cursor.
				FVector O, D, C;
				double R;
				FRotator TR;
				if (PC->DeprojectScreenPositionToWorld(Pixel.X, Pixel.Y, O, D) && St->GetBoardRim(C, R, TR) && D.Z < -1e-3)
					Ring->MoveLifted(O + D * ((C.Z + 14.f * S - O.Z) / D.Z));
			}
		}
		return;
	}
	const FString Key = (Op.IsValid() && !Op->Tier.IsEmpty()) ? Power + TEXT(":") + Op->Tier : Power;
	const double* Radius = PowerRadiusKm.Find(Key);
	const bool bOver = FN7ViewportPx::AbsToPixel(ScreenPosition, Pixel) && !SFNPanelMover::IsOverAnyPanel(ScreenPosition);
	St->ShowAreaOfEffect(bOver, Pixel, Radius ? *Radius : 0.0);
}

void SFNHudLive::OnPowerDragEnded(const FString& Power)
{
	if (AFNStudio* St = FindStudio())
	{
		St->ShowAreaOfEffect(false, FVector2D::ZeroVector, 0.0);
		St->ShowMegaOnGlobe(false, 0.0, 0.0, 0.0);
		if (AFNPowerRing* Ring = AFNPowerRing::Get(St, false)) Ring->EndLift();   // the hex flies back to its place
	}
	if (bOverGlobe)
	{
		bOverGlobe = false;
		FFNPowerDragDrop::OnMegaArmed().Broadcast(false, Power);
	}
}

void SFNHudLive::OnPowerDropped(const FString& Power, const FVector2D& ScreenPosition)
{
	if (SFNPanelMover::IsOverAnyPanel(ScreenPosition)) return;   // let go over a panel: nothing happens
	{
		const TSharedPtr<FFNPowerDragDrop> RingOp = FFNPowerDragDrop::GetActive();   // still set while OnDroppedOnWorld runs
		if (RingOp.IsValid() && RingOp->bFromRing)
		{
			// Where the hex lands decides: over the globe a MEGA (a bolt), on the board a MINI (a spark), else nothing.
			AFNStudio* RSt = FindStudio();
			UFNNetClient* RN = Net.Get();
			double La = 0.0, Lo = 0.0;
			FVector World;
			FVector2D Pixel;
			const int32 Where = RingTarget(ScreenPosition, La, Lo, World, Pixel);
			if (RN && RSt && Where > 0)
			{
				const bool bMega = Where == 2;
				RN->CastAt(Round, Power, Lo, La, bMega ? TEXT("mega") : TEXT("mini"));
				RSt->FlashAt(Lo, La);
				RSt->FocusBoard(La, Lo, bMega ? 3 : 2);   // the board zooms to where it struck: regions for a mega, villages for a mini
				SetArmed(FString());
				AddNews(bMega ? LOCTEXT("Mega", "MEGA") : LOCTEXT("Mini", "MINI"),
					FString::Printf(TEXT("%s at %.2f%s %.2f%s"), *Power, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
				Publish();
			}
			return;
		}
	}
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
		SetArmed(FString());
		AddNews(LOCTEXT("Cast", "CAST"), FString::Printf(TEXT("%s at %.2f%s %.2f%s"), *Power, FMath::Abs(La), La >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(Lo), Lo >= 0 ? TEXT("E") : TEXT("W")));
		Publish();
		return;
	}
	{
		const TSharedPtr<FFNPowerDragDrop> Op = FFNPowerDragDrop::GetActive();   // still set while OnDroppedOnWorld runs
		SetArmed((Op.IsValid() && !Op->Tier.IsEmpty()) ? Power + TEXT(":") + Op->Tier : Power);
	}
	AddNews(LOCTEXT("Armed", "ARMED"), FString::Printf(TEXT("%s: drop it on a board hex, or write its target and SEND."), *Power));
	Publish();
}

void SFNHudLive::OnPower(const FString& Slug)
{
	SetArmed(ArmedPower == Slug ? FString() : Slug);
	Publish();
}

void SFNHudLive::SetArmed(const FString& Key)
{
	if (Key == ArmedPower) return;
	FString OldSlug, OldTier, NewSlug, NewTier;
	if (!ArmedPower.Split(TEXT(":"), &OldSlug, &OldTier)) OldSlug = ArmedPower;
	if (!Key.Split(TEXT(":"), &NewSlug, &NewTier)) NewSlug = Key;
	if (OldTier == TEXT("mega")) FFNPowerDragDrop::OnMegaArmed().Broadcast(false, OldSlug);
	ArmedPower = Key;
	if (NewTier == TEXT("mega")) FFNPowerDragDrop::OnMegaArmed().Broadcast(true, NewSlug);
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
	// Every 10 s, the real frame rate into the log ("FN7 fps"), so a PIE session's speed can be read afterwards.
	{
		static uint64 FpsFrame = 0;
		static double FpsTime = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (FpsTime <= 0.0) { FpsTime = Now; FpsFrame = GFrameCounter; }
		else if (Now - FpsTime >= 10.0)
		{
			const double Fps = (double)(GFrameCounter - FpsFrame) / (Now - FpsTime);
			UE_LOG(LogTemp, Display, TEXT("FN7 fps %.1f (%.1f ms/frame)"), Fps, Fps > 0.0 ? 1000.0 / Fps : 0.0);
			FpsTime = Now;
			FpsFrame = GFrameCounter;
		}
	}
	if (ToastUntil > 0.0 && FPlatformTime::Seconds() > ToastUntil) { ToastUntil = 0.0; Model.Status = FText::GetEmpty(); Publish(); }
	// TAKE ANOTHER DEMIGOD unanswered for a few seconds: say when the next chance comes.
	if (bOut && bTakeAsked && FPlatformTime::Seconds() - TakeAskedAt > 6.0)
	{
		bTakeAsked = false;
		OutNote = Model.BeatsTotal > Model.Beat
			? FString::Printf(TEXT("No demigod is free right now. The next match starts in about %d beats."), Model.BeatsTotal - Model.Beat)
			: FString(TEXT("No demigod is free right now. You'll get one next match."));
	}
	// Time's live bill while you hold slowed time: debt(t) = min(cap, base + per_second * t), plus the bolt.
	const bool bHolding = MySeat >= 0 && WindowSeat == MySeat;
	FText Bill;
	if (bHolding)
	{
		if (bSlowBill)
		{
			UFNNetClient* N = Net.Get();
			const double Secs = FMath::Max(0.0, ((N ? N->NowServerMs() : SlowOpenedMs) - SlowOpenedMs) / 1000.0);
			FNumberFormattingOptions One;
			One.SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1);
			Bill = FText::Format(LOCTEXT("BillNow", "{0} {0}|plural(one=bolt,other=bolts) + {1} debt"), BoltPaid, FText::AsNumber(FMath::Min(DebtCap, DebtBase + DebtPerS * Secs), &One));
		}
		else
		{
			Bill = LOCTEXT("BillPlain", "1 bolt + debt, growing while you write");
		}
	}
	if (Layout.IsValid()) Layout->SetTimeBill(Bill, bHolding);
	Model.TimeBill = Bill;
	Model.bHoldingTime = bHolding;
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

	void Preview(bool bLobby, bool bFell, bool bWill)
	{
		if (GMountLive.IsValid()) StaticCastSharedPtr<SFNHudLive>(GMountLive)->DebugPreview(bLobby, bFell, bWill);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFN7HudCommand(
	TEXT("FN7.Hud"),
	TEXT("FN7.Hud on | off [connect] [shot] [quit] [lobby] [fell]: the live style-kit HUD over the game (hides the old HUD while on). ")
	TEXT("preview / lobby / fell / will = the sample table, plus the lobby over the game / the out card / the WILL open in slowed time (no game joined). ")
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
		const bool bWill = Args.Contains(TEXT("will"));
		const bool bPreview = Args.Contains(TEXT("preview"));
		const bool bPour = Args.Contains(TEXT("pour"));
		const bool bMega = Args.Contains(TEXT("mega"));
		const bool bSummon = Args.Contains(TEXT("summon"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		// Wait for the player controller and its net client, then mount (and optionally connect / photograph).
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudMount"), 0.5f, [WeakWorld, bConnect, bShot, bQuit, bAoe, bLobby, bFell, bWill, bPreview, bPour, bMega, bSummon](float)
		{
			UWorld* W = WeakWorld.Get();
			AFNPlayerController* PC = W ? Cast<AFNPlayerController>(W->GetFirstPlayerController()) : nullptr;
			if (!PC || !PC->Net) return true;
			FN7HudMount::Mount(PC->Net);
			if (bConnect) PC->Net->Start();
			if (bLobby || bFell || bWill || bPreview) FN7HudMount::Preview(bLobby, bFell, bWill);
			if (bSummon)
			{
				// Test: your Left Hand held (lifted, glowing) from just before the second shot, and summoned across the board.
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudSummon"), 15.2f, [WeakPC](float)
				{
					AFNPlayerController* P = WeakPC.Get();
					if (AFNTableGods* T = P ? AFNTableGods::GetAny(P->GetWorld()) : nullptr)
					{
						T->SetFigureHeld(TEXT("dg:0"), true);
						T->SetLocalPos(TEXT("dg:0"), 45.6, 11.8);
					}
					return false;
				});
			}
			if (bMega)
			{
				// Test: a mega in hand from just before the second shot (the fault lines fade in), with its ring on the globe.
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudMega"), 15.4f, [WeakPC](float)
				{
					FFNPowerDragDrop::OnMegaArmed().Broadcast(true, TEXT("earthquake"));
					if (AFNPlayerController* P = WeakPC.Get())
						if (AFNStudio* S = P->Studio.Get()) S->ShowMegaOnGlobe(true, 35.7, 139.7, 900.0);
					return false;
				});
			}
			if (bPour)
			{
				// Test: click the pouch just before the second shot (shot 1 closed, 2 mid-burst, 3 landed).
				TWeakObjectPtr<AFNPlayerController> WeakPC(PC);
				FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7HudPour"), 15.55f, [WeakPC](float)
				{
					AFNPlayerController* P = WeakPC.Get();
					if (AFNPowerRing* Ring = P ? AFNPowerRing::Get(P->Studio.Get(), false) : nullptr) Ring->TogglePouch();
					return false;
				});
			}
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

// --- FN7.Fps [seconds]: the real frame rate, logged once a second (the log's frame counter wraps every 1000) -------
static FAutoConsoleCommand GFN7FpsCommand(
	TEXT("FN7.Fps"),
	TEXT("FN7.Fps [seconds=20]: log the frame rate and frame time once a second."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 Secs = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 600) : 20;
		if (Args.Contains(TEXT("dump")))
			FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7FpsDump"), 10.f, [](float)
			{
				if (GEngine) GEngine->Exec(nullptr, TEXT("stat dumpframe -ms=0.5"));
				return false;
			});
		struct FState { uint64 Frame = 0; double Time = 0.0; int32 Left = 0; };
		TSharedRef<FState> St = MakeShared<FState>();
		St->Frame = GFrameCounter;
		St->Time = FPlatformTime::Seconds();
		St->Left = Secs;
		FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7Fps"), 1.f, [St](float)
		{
			const uint64 F = GFrameCounter;
			const double T = FPlatformTime::Seconds();
			const double Fps = (double)(F - St->Frame) / FMath::Max(T - St->Time, 1e-3);
			UE_LOG(LogTemp, Display, TEXT("FN7 fps %.1f (%.1f ms/frame)"), Fps, Fps > 0.0 ? 1000.0 / Fps : 0.0);
			St->Frame = F;
			St->Time = T;
			return --St->Left > 0;
		});
	}));
