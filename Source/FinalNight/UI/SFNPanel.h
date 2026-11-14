#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "FNStyle.h"

/** A rounded FN7 panel with an optional title and a 2 px orange rule under it. */
class FINALNIGHT_API SFNPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNPanel)
		: _Padding(FMargin(FN7Style::Space::L))
		, _bAccentRule(true)
	{}
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ARGUMENT(FMargin, Padding)
		SLATE_ARGUMENT(bool, bAccentRule)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<FText> Title;
	EVisibility GetTitleVisibility() const;
};
