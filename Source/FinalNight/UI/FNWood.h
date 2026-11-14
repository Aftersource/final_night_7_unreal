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

	/** Old woven linen for the scroll: warp and weft threads with slubs, age stains, and crisp torn, scorched edges
	 *  on the left and right (transparent beyond the tear). */
	FINALNIGHT_API const FSlateBrush* FabricBrush();

	/** A turned wooden scroll rod with knobs on both ends (9-slice: the knobs keep their shape at any width). */
	FINALNIGHT_API const FSlateBrush* RodBrush();

	/** A soft shadow strip: dark at the top fading down (bUp = false) or dark at the bottom fading up (bUp = true). */
	FINALNIGHT_API const FSlateBrush* ShadeBrush(bool bUp);

	/** Writing straight onto the cloth: no box, ink-brown italic text, a faded ink hint. */
	FINALNIGHT_API const FEditableTextBoxStyle& InkTextBox();

	/** Red-brown ink for headings inscribed on the scroll. */
	FINALNIGHT_API const FLinearColor& RedInk();

	/** A carved stone tablet: dark mottled slate with specks and hairline cracks, a chiselled bevel lit from the top
	 *  left and a carved groove inside it, rounded corners (9-slice: the bevel and groove stay crisp at any size). */
	FINALNIGHT_API const FSlateBrush* StoneBrush();
}
