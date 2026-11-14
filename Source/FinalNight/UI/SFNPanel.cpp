#include "SFNPanel.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SFNPanel::Construct(const FArguments& InArgs)
{
	Title = InArgs._Title;
	const bool bRule = InArgs._bAccentRule;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FN7Style::Brush::Panel())
		.Padding(InArgs._Padding)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.TextStyle(&FN7Style::Type::Title())
				.Text(Title)
				.Visibility(this, &SFNPanel::GetTitleVisibility)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, FN7Style::Space::XS, 0.f, FN7Style::Space::M)
			[
				SNew(SBox)
				.HeightOverride(2.f)
				.WidthOverride(48.f)
				.HAlign(HAlign_Left)
				.Visibility_Lambda([this, bRule]() { return bRule ? GetTitleVisibility() : EVisibility::Collapsed; })
				[
					SNew(SImage).Image(FN7Style::Brush::AccentRule())
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				InArgs._Content.Widget
			]
		]
	];
}

EVisibility SFNPanel::GetTitleVisibility() const
{
	return Title.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}
