#include "FNPowerDrag.h"

#include "Framework/Application/SlateApplication.h"

namespace FN7PowerDragState
{
	TWeakPtr<FFNPowerDragDrop> Active;
	FVector2D LastDropPosition = FVector2D::ZeroVector;
	double LastDropTime = -1.0;
	FString LastDropPower;
}

TSharedRef<FFNPowerDragDrop> FFNPowerDragDrop::New(const FString& InPower, const FVector2D& InScreenPosition)
{
	TSharedRef<FFNPowerDragDrop> Op = MakeShared<FFNPowerDragDrop>();
	Op->Power = InPower;
	Op->ScreenPosition = InScreenPosition;
	Op->Construct();
	FN7PowerDragState::Active = Op;
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
	FN7PowerDragState::Active.Reset();

	if (!bDropWasHandled) OnDroppedOnWorld().Broadcast(Power, ScreenPosition);
	OnDragEnded().Broadcast(Power);
	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
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
