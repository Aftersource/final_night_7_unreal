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
		// Chris asked for a gentle motion, like a slow pan slowed by about 8x. A critically damped follow (Unity's SmoothDamp): it starts
		// moving the moment the pointer moves, eases in and out, never snaps, and doesn't depend on the frame rate (the
		// old RInterpTo(3) took 60% of the remaining angle per frame at 5 fps: late, then a lurch). ~1.6 s smooth time.
		auto Damp = [](double Cur, double Goal, double& Vel, double SmoothTime, double Dt)
		{
			const double Omega = 2.0 / FMath::Max(SmoothTime, 1e-4);
			const double X = Omega * Dt;
			const double E = 1.0 / (1.0 + X + 0.48 * X * X + 0.235 * X * X * X);
			const double Change = Cur - Goal;
			const double Temp = (Vel + Omega * Change) * Dt;
			Vel = (Vel - Omega * Temp) * E;
			return Goal + (Change + Temp) * E;
		};
		const FRotator Goal = Root->IsTyping() ? FRotator::ZeroRotator : Target;
		const double SmoothTime = 1.6;
		Lean.Pitch = Damp(Lean.Pitch, Goal.Pitch, LeanVel.Pitch, SmoothTime, DeltaTime);
		Lean.Yaw = Damp(Lean.Yaw, Goal.Yaw, LeanVel.Yaw, SmoothTime, DeltaTime);
	}
	SetControlRotation(BaseRot + Lean + Studio->GetShake());

	const FName Id = Root->IsTyping() ? NAME_None : TraceUnderCursor();
	Studio->SetHovered(Id);
	Root->SetHovered(Id);
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && !Root->IsTyping())
	{
		Studio->PlaySfx(EFNSfx::Click, Id != NAME_None ? 1.f : 0.5f);
		// FN7 rule (Chris, 2026-11-14, asked that the view stop warping around; Mac session with the backend's go-ahead): a single
		// click NEVER moves the board view: no click-to-zoom on the map (MapClick) and no centring on a clicked demigod
		// marker (ClickDemigod -> CenterMapOn). View changes: the wheel, right-click (out), RESET VIEW / R / Home.
		const FString IdS = Id.ToString();
		const bool bPiece = IdS.StartsWith(TEXT("Hero")) || IdS.StartsWith(TEXT("Seat"));
		if (Id != NAME_None && !bPiece) Root->OnWorldClick(Id);
	}
	if (!Root->IsTyping())
	{
		if (WasInputKeyJustPressed(EKeys::RightMouseButton)) Studio->MapClick(false);
		if (WasInputKeyJustPressed(EKeys::Home)) Studio->MapHome();
	}
}
