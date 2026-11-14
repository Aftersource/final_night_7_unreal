#include "FNPowerDrag.h"

#include "Framework/Application/SlateApplication.h"

namespace FN7PowerDragState
{
	TWeakPtr<FFNPowerDragDrop> Active;
	FVector2D LastDropPosition = FVector2D::ZeroVector;
	double LastDropTime = -1.0;
	FString LastDropPower;
}

TSharedRef<FFNPowerDragDrop> FFNPowerDragDrop::New(const FString& InPower, const FVector2D& InScreenPosition, const FString& InTier)
{
	TSharedRef<FFNPowerDragDrop> Op = MakeShared<FFNPowerDragDrop>();
	Op->Power = InPower;
	Op->Tier = InTier;
	Op->ScreenPosition = InScreenPosition;
	Op->Construct();
	FN7PowerDragState::Active = Op;
	if (InTier == TEXT("mega")) OnMegaArmed().Broadcast(true, InPower);
	return Op;
}

TSharedPtr<FFNPowerDragDrop> FFNPowerDragDrop::GetActive()
{
	return FN7PowerDragState::Active.Pin();
}

void FFNPowerDragDrop::OnDragged(const FDragDropEvent& DragDropEvent)
{
	ScreenPosition = DragDropEvent.GetScreenSpacePosition();
	OnDragOverWorld().Broadcast(Power, ScreenPosition);
	FDragDropOperation::OnDragged(DragDropEvent);
}

void FFNPowerDragDrop::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	ScreenPosition = MouseEvent.GetScreenSpacePosition();
	FVector2D Landed = ScreenPosition;
	if (SnapToBoard().IsBound())
		if (const TOptional<FVector2D> Snapped = SnapToBoard().Execute(Power, ScreenPosition)) Landed = Snapped.GetValue();
	FN7PowerDragState::LastDropPosition = Landed;
	FN7PowerDragState::LastDropTime = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
	FN7PowerDragState::LastDropPower = Power;

	if (!bDropWasHandled) OnDroppedOnWorld().Broadcast(Power, ScreenPosition);
	FN7PowerDragState::Active.Reset();
	OnDragEnded().Broadcast(Power);
	if (Tier == TEXT("mega")) OnMegaArmed().Broadcast(false, Power);   // dropped or cancelled
	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
}

FFNPowerDragDrop::FOnMegaArmed& FFNPowerDragDrop::OnMegaArmed()
{
	static FOnMegaArmed Delegate;
	return Delegate;
}

FFNPowerDragDrop::FOnSnapToBoard& FFNPowerDragDrop::SnapToBoard()
{
	static FOnSnapToBoard Delegate;
	return Delegate;
}

FFNPowerDragDrop::FOnPowerAtScreen& FFNPowerDragDrop::OnDragOverWorld()
{
	static FOnPowerAtScreen Delegate;
	return Delegate;
}

FFNPowerDragDrop::FOnPowerAtScreen& FFNPowerDragDrop::OnDroppedOnWorld()
{
	static FOnPowerAtScreen Delegate;
	return Delegate;
}

FFNPowerDragDrop::FOnPowerDragEnded& FFNPowerDragDrop::OnDragEnded()
{
	static FOnPowerDragEnded Delegate;
	return Delegate;
}

bool FFNPowerDragDrop::GetLastDrop(FVector2D& OutScreenPosition, double& OutTime, FString& OutPower)
{
	if (FN7PowerDragState::LastDropTime < 0.0) return false;
	OutScreenPosition = FN7PowerDragState::LastDropPosition;
	OutTime = FN7PowerDragState::LastDropTime;
	OutPower = FN7PowerDragState::LastDropPower;
	return true;
}
