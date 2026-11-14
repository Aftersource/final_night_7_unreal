#include "FNPlayerController.h"
#include "FNNetClient.h"
#include "FNStudio.h"
#include "SFNRootWidget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Widgets/SWeakWidget.h"
#include "InputCoreTypes.h"

void AFNPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Studio = GetWorld()->SpawnActor<AFNStudio>(AFNStudio::StaticClass(), FVector(0.f, 0.f, 2000.f), FRotator::ZeroRotator, P);
	BaseRot = Studio->GetSeatRotation();
	if (APawn* Me = GetPawn()) Me->SetActorLocation(Studio->GetSeatLocation());
	SetControlRotation(BaseRot);

	Net = NewObject<UFNNetClient>(this);

	if (UGameViewportClient* VC = GetWorld()->GetGameViewport())
	{
		SAssignNew(Root, SFNRootWidget).Net(Net).Studio(Studio);
		RootContainer = SNew(SWeakWidget).PossiblyNullContent(Root.ToSharedRef());
		VC->AddViewportWidgetContent(RootContainer.ToSharedRef(), 10);
	}

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
}

void AFNPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Net) Net->Stop();
	if (RootContainer.IsValid())
		if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
			VC->RemoveViewportWidgetContent(RootContainer.ToSharedRef());
	Root.Reset();
	RootContainer.Reset();
	Super::EndPlay(Reason);
}

FName AFNPlayerController::TraceUnderCursor() const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit) || !Hit.GetComponent()) return NAME_None;
	for (const FName& T : Hit.GetComponent()->ComponentTags)
	{
		const FString S = T.ToString();
		if (S.StartsWith(TEXT("FN:"))) return FName(*S.RightChop(3));
	}
	return NAME_None;
}

void AFNPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!Root.IsValid() || !Studio) return;

	// Lean the view toward the mouse, like the Unity client's smooth look-at camera
	float MX = 0.f, MY = 0.f;
	int32 SX = 0, SY = 0;
	GetViewportSize(SX, SY);
	if (SX > 0 && SY > 0 && GetMousePosition(MX, MY))
	{
		const float NX = FMath::Clamp(MX / SX * 2.f - 1.f, -1.f, 1.f);
		const float NY = FMath::Clamp(MY / SY * 2.f - 1.f, -1.f, 1.f);
		// a gentle lean only: at the old 7/12 degrees a click near the screen edge swung the whole view
		const FRotator Target(-NY * 2.5f, NX * 4.f, 0.f);
		Lean = FMath::RInterpTo(Lean, Root->IsTyping() ? FRotator::ZeroRotator : Target, DeltaTime, 3.f);
	}
	SetControlRotation(BaseRot + Lean + Studio->GetShake());

	const FName Id = Root->IsTyping() ? NAME_None : TraceUnderCursor();
	Studio->SetHovered(Id);
	Root->SetHovered(Id);
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && !Root->IsTyping())
	{
		Studio->PlaySfx(EFNSfx::Click, Id != NAME_None ? 1.f : 0.5f);
		// during a match a click on the table map zooms in there (web-map style) instead of opening WILL
		// a click on a demigod (map marker, minifig or silhouette) centres the map on it and opens its card
		if (Id != NAME_None && Studio->ClickDemigod(Id)) {}
		else if (Id != NAME_None || !Studio->MapClick(true)) Root->OnWorldClick(Id);
	}
	if (!Root->IsTyping())
	{
		if (WasInputKeyJustPressed(EKeys::RightMouseButton)) Studio->MapClick(false);
		if (WasInputKeyJustPressed(EKeys::Home)) Studio->MapHome();
	}
}
