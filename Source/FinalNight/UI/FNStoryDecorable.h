#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Something on the table that a demigod's story can dress: AFNDemigodFigure implements it (the style kit's figure,
 * on the pedestals and on the board). Benito's piece-story feature (Private/Features/) calls ApplyStory with the
 * demigod's story object whenever it changes, and the figure shows it: followers as small figures around the base,
 * held items as tiny icons, the wardrobe stage as a tint and trim, scars as marks. Unknown fields are ignored.
 */
class FINALNIGHT_API IFNStoryDecorable
{
public:
	virtual ~IFNStoryDecorable() = default;

	/** The demigod's story (the piece-story payload); called again on every change, with the full object each time. */
	virtual void ApplyStory(const TSharedPtr<FJsonObject>& Story) = 0;
};
