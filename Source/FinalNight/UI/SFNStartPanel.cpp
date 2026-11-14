#include "SFNStartPanel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "FNNetClient.h"
#include "FNStyle.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SFNTextBox.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNStartPanel"

namespace FN7StartJson
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
}

void SFNStartPanel::Construct(const FArguments& InArgs)
{
	using namespace FN7Style;
	Net = InArgs._Net;
	OnChoice = InArgs._OnChoice;
	const FLinearColor Ink(0.1f, 0.06f, 0.02f);

	FString Name = ReadLocalLine(TEXT("FN7DisplayName.txt"));
	if (Name.IsEmpty() && Net.IsValid()) Name = Net->DisplayName;

	ChildSlot
	[
		// The scrim: covers the viewport, dims the table a little, and is hit-testable (see OnMouseButtonDown).
		SNew(SBorder)
		.BorderImage(Brush::Segment(FLinearColor(0.f, 0.f, 0.02f, 0.45f)))
		.Padding(0.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(560.f)
			[
				SNew(SBorder)
				.BorderImage(Brush::Card())
				.Padding(FMargin(Space::XL + Space::S, Space::XL))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).TextStyle(&Type::Display()).Text(LOCTEXT("Title", "FINAL NIGHT 7"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::S, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(64.f).HeightOverride(3.f).HAlign(HAlign_Left)[SNew(SImage).Image(Brush::AccentRule())]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Body()).ColorAndOpacity(Color::TextDim()).AutoWrapText(true)
						.Text(LOCTEXT("Tag", "Demigods fight over the real Earth. Write what you do; time slows while you write. Last one standing wins."))
					]

					// Server status.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XL, 0.f, 0.f)
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
							SNew(STextBlock).TextStyle(&Type::Body()).Text(this, &SFNStartPanel::GetStatusText)
						]
					]

					// Tonight's game.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::M, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Space::S, 0.f)
						[
							SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("GameLabel", "THE GAME"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).TextStyle(&Type::Body()).Font(Type::Font(13, TEXT("Bold"))).AutoWrapText(true).Text(this, &SFNStartPanel::GetGameText)
						]
					]

					// Name.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XL, 0.f, Space::XS)
					[
						SNew(STextBlock).TextStyle(&Type::Label()).ColorAndOpacity(Color::TextDim()).Text(LOCTEXT("NameLabel", "YOUR NAME"))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(NameBox, SFNTextBox)
						.Text(FText::FromString(Name))
						.HintText(LOCTEXT("NameHint", "The name the other demigods see"))
						.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit) { if (Commit == ETextCommit::OnEnter) OnJoin(); })
					]

					// JOIN.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Space::XL, 0.f, 0.f)
					[
						SNew(SBox).HeightOverride(52.f)
						[
							SNew(SButton)
							.ButtonStyle(&AccentButton())
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.IsEnabled(this, &SFNStartPanel::CanJoin)
							.OnClicked(this, &SFNStartPanel::OnJoin)
							[
								SNew(STextBlock).TextStyle(&Type::Title()).ColorAndOpacity(Ink).Text(LOCTEXT("Join", "JOIN THE GAME"))
							]
						]
					]
					// WATCH: join without taking a seat.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::S, 0.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(&PillButton())
						.IsEnabled(this, &SFNStartPanel::CanJoin)
						.OnClicked(this, &SFNStartPanel::OnWatch)
						[
							SNew(STextBlock).TextStyle(&Type::Small()).ColorAndOpacity(Color::Text()).Text(LOCTEXT("Watch", "WATCH"))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, Space::M, 0.f, 0.f)
					[
						SNew(STextBlock).TextStyle(&Type::Small()).AutoWrapText(true).Text(this, &SFNStartPanel::GetProgressText)
					]
				]
			]
		]
	];

	if (UFNNetClient* N = Net.Get()) MessageHandle = N->OnServerMessage.AddSP(this, &SFNStartPanel::HandleMessage);
	RefreshStats();
	RefreshHouse();
	RegisterActiveTimer(5.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNStartPanel::TickRefresh));
}

SFNStartPanel::~SFNStartPanel()
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MessageHandle);
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
		Progress = TEXT("Connected. Taking your seat...");
	}
	else if (T == TEXT("room.snapshot") || T == TEXT("match.snapshot"))
	{
		bJoining = false;
	}
	else if (T == TEXT("error"))
	{
		FString Msg;
		if (D.IsValid()) D->TryGetStringField(TEXT("message"), Msg);
		Progress = TEXT("The server said: ") + Msg;
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
		const TSharedPtr<FJsonObject> O = (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200) ? FN7StartJson::Parse(Resp->GetContentAsString()) : nullptr;
		Self->bServerOnline = O.IsValid();
		if (O.IsValid())
		{
			double V = 0.0;
			if (O->TryGetNumberField(TEXT("connected"), V)) Self->PlayersConnected = (int32)V;
			else if (O->TryGetNumberField(TEXT("players"), V)) Self->PlayersConnected = (int32)V;
			if (O->TryGetNumberField(TEXT("rooms"), V)) Self->Rooms = (int32)V;
		}
	});
	Req->ProcessRequest();
}

void SFNStartPanel::RefreshHouse(int32)
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
		const TSharedPtr<FJsonObject> O = (bOk && HttpCode == 200) ? FN7StartJson::Parse(Resp->GetContentAsString()) : nullptr;
		const FString Code = FN7StartJson::FindCode(O);
		if (!Code.IsEmpty())
		{
			Self->HouseCode = Code.ToUpper();
			Self->HouseSource = TEXT("house");
			FString State, Phase;
			double Round = 0.0;
			const TArray<TSharedPtr<FJsonValue>>* SeatList = nullptr;
			O->TryGetStringField(TEXT("state"), State);
			O->TryGetStringField(TEXT("phase"), Phase);
			O->TryGetNumberField(TEXT("round"), Round);
			const int32 SeatCount = O->TryGetArrayField(TEXT("seats"), SeatList) && SeatList ? SeatList->Num() : 0;
			Self->HouseDetail = State == TEXT("in_match")
				? FString::Printf(TEXT("beat %d, %d at the table"), (int32)Round, SeatCount)
				: FString::Printf(TEXT("%s, %d at the table"), State.IsEmpty() ? TEXT("open") : *State, SeatCount);
			return;
		}
		// No house (404) or no server: the last word is Config/FN7JoinCode.txt, if this machine has one.
		const FString Local = Self->ReadLocalLine(TEXT("FN7JoinCode.txt"));
		Self->HouseCode = Local.ToUpper();
		Self->HouseSource = Local.IsEmpty() ? FString() : TEXT("file");
		Self->HouseDetail.Reset();
	});
	Req->ProcessRequest();
}

FString SFNStartPanel::ReadLocalLine(const TCHAR* File) const
{
	FString Txt;
	if (!FFileHelper::LoadFileToString(Txt, *FPaths::Combine(FPaths::ProjectConfigDir(), File))) return FString();
	TArray<FString> Lines;
	Txt.ParseIntoArrayLines(Lines);
	return Lines.Num() ? Lines[0].TrimStartAndEnd() : FString();
}

FReply SFNStartPanel::OnJoin() { return Enter(true); }
FReply SFNStartPanel::OnWatch() { return Enter(false); }

FReply SFNStartPanel::Enter(bool bTakeSeat)
{
	UFNNetClient* N = Net.Get();
	if (!N || bJoining) return FReply::Handled();
	OnChoice.ExecuteIfBound(bTakeSeat);

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
	if (!bConnected)
	{
		Progress = FString::Printf(TEXT("Connecting and joining %s..."), *HouseCode);
		N->Start();
	}
	else
	{
		Progress = FString::Printf(TEXT("Joining %s..."), *HouseCode);
		N->JoinHouse();
	}
	return FReply::Handled();
}

FText SFNStartPanel::GetStatusText() const
{
	if (!bStatsKnown) return LOCTEXT("Checking", "Checking the server...");
	if (!bServerOnline) return LOCTEXT("Offline", "The server isn't answering. It may be restarting; this checks again every few seconds.");
	return FText::Format(LOCTEXT("Online", "Server online  -  {0} {0}|plural(one=player,other=players) connected  -  {1} {1}|plural(one=room,other=rooms)"),
		PlayersConnected, Rooms);
}

static FSlateColor FN7StartOnlineColor(bool bKnown, bool bOnline)
{
	if (!bKnown) return FN7Style::Color::TextDim();
	return bOnline ? FLinearColor(0.35f, 0.9f, 0.5f) : FLinearColor(0.95f, 0.35f, 0.3f);
}

FSlateColor SFNStartPanel::GetStatusColor() const
{
	return FN7StartOnlineColor(bStatsKnown, bServerOnline);
}

FText SFNStartPanel::GetGameText() const
{
	if (HouseCode.IsEmpty()) return LOCTEXT("NoGame", "The server has no game running right now.");
	if (HouseSource == TEXT("file")) return FText::Format(LOCTEXT("FileGame", "Room {0} (this machine's join code)"), FText::FromString(HouseCode));
	if (HouseDetail.IsEmpty()) return FText::Format(LOCTEXT("LiveGame", "The house game  -  room {0}"), FText::FromString(HouseCode));
	return FText::Format(LOCTEXT("LiveGameDetail", "The house game  -  room {0}  -  {1}"), FText::FromString(HouseCode), FText::FromString(HouseDetail));
}

FText SFNStartPanel::GetProgressText() const
{
	return FText::FromString(Progress);
}

EActiveTimerReturnType SFNStartPanel::TickRefresh(double, float)
{
	RefreshStats();
	if (!bJoining) RefreshHouse();
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
