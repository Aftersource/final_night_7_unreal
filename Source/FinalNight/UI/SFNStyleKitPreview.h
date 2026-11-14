#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * A test sheet for the FN7 style kit: palette, type scale, a panel, four portrait cards (human + YOU + hourglass,
 * human, AI, fallen) and the text box. Toggle it over the game viewport with the console command FN7.StyleKit.
 */
class FINALNIGHT_API SFNStyleKitPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNStyleKitPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
