#include "SFNTextBox.h"

#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"

void SFNTextBox::Construct(const FArguments& InArgs)
{
	TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
	if (InArgs._bMultiLine)
	{
		Inner = SAssignNew(Multi, SMultiLineEditableTextBox)
			.Style(&FN7Style::TextBox())
			.Text(InArgs._Text)
			.HintText(InArgs._HintText)
			.AutoWrapText(true)
			.OnTextChanged(InArgs._OnTextChanged)
			.OnTextCommitted(InArgs._OnTextCommitted);
	}
	else
	{
		Inner = SAssignNew(Single, SEditableTextBox)
			.Style(&FN7Style::TextBox())
			.Text(InArgs._Text)
			.HintText(InArgs._HintText)
			.OnTextChanged(InArgs._OnTextChanged)
			.OnTextCommitted(InArgs._OnTextCommitted);
	}

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(InArgs._MinDesiredWidth)
		[
			Inner
		]
	];
}

FText SFNTextBox::GetText() const
{
	if (Multi.IsValid()) return Multi->GetText();
	return Single.IsValid() ? Single->GetText() : FText::GetEmpty();
}

void SFNTextBox::SetText(const FText& InText)
{
	if (Multi.IsValid()) Multi->SetText(InText);
	else if (Single.IsValid()) Single->SetText(InText);
}

TSharedPtr<SWidget> SFNTextBox::GetEditable() const
{
	if (Multi.IsValid()) return Multi;
	return Single;
}
