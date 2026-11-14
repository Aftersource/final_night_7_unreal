#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

/**
 * Procedural wood for the FN7 HUD, generated once at first use into transient textures (no asset pipeline):
 * warm tan grain with fine fibres, an inset carved groove, a bevelled edge and candle-lit shading.
 * Brushes draw as 9-slice boxes so the carved border stays crisp at any size.
 */
namespace FN7Wood
{
	/** Parchment-on-wood for the WILL box: pale grain, carved border. */
	FINALNIGHT_API const FSlateBrush* WillBoxBrush();

	/** A carved tan wood block for power tiles. */
	FINALNIGHT_API const FSlateBrush* TileBrush();

	/** The WILL box text style: the parchment brush, dark ink text, padding inside the carved border. */
	FINALNIGHT_API const FEditableTextBoxStyle& WillTextBox();

	/** Dark ink for text and icons carved into the wood. */
	FINALNIGHT_API const FLinearColor& Ink();
}
