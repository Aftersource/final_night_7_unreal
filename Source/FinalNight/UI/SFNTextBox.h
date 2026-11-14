#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "FNStyle.h"

class SMultiLineEditableTextBox;

/** The FN7 rounded input, single-line or multi-line, on FN7Style::TextBox(). */
class FINALNIGHT_API SFNTextBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNTextBox)
		: _bMultiLine(false)
		, _MinDesiredWidth(240.f)
		, _Style(nullptr)
	{}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(FText, HintText)
		SLATE_ARGUMENT(bool, bMultiLine)
		SLATE_ARGUMENT(float, MinDesiredWidth)
		/** nullptr = FN7Style::TextBox(); FN7Wood::WillTextBox() for the carved WILL box. */
		SLATE_ARGUMENT(const FEditableTextBoxStyle*, Style)
		SLATE_EVENT(FOnTextChanged, OnTextChanged)
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FText GetText() const;
	void SetText(const FText& InText);

	/** The inner editable widget, for FSlateApplication::SetKeyboardFocus. */
	TSharedPtr<SWidget> GetEditable() const;

private:
	TSharedPtr<SEditableTextBox> Single;
	TSharedPtr<SMultiLineEditableTextBox> Multi;
};
