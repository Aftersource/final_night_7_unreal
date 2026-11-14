#include "FNPowerDrag.h"

TSharedRef<FFNPowerDragDrop> FFNPowerDragDrop::New(const FString& InPower, TSharedPtr<SWidget> InDecorator)
{
	TSharedRef<FFNPowerDragDrop> Op = MakeShared<FFNPowerDragDrop>();
	Op->Power = InPower;
	Op->Decorator = InDecorator;
	Op->Construct();
	return Op;
}

void FFNPowerDragDrop::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	if (!bDropWasHandled) OnDroppedOnWorld().Broadcast(Power, MouseEvent.GetScreenSpacePosition());
	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
}

FFNPowerDragDrop::FOnPowerDroppedOnWorld& FFNPowerDragDrop::OnDroppedOnWorld()
{
	static FOnPowerDroppedOnWorld Delegate;
	return Delegate;
}
