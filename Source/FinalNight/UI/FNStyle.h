#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

/** Who a seat belongs to; drives the aura colour, the label and the fallen treatment. */
enum class EFNSeatRole : uint8
{
	Human,
	AI,
	Fallen
};

/**
 * Final Night 7 HUD style kit: one source for the palette, the rounded brushes, the type scale and the spacing.
 * Every accessor returns a reference to a static, so widgets can keep the pointer for their whole life.
 */
namespace FN7Style
{
	namespace Color
	{
		FINALNIGHT_API const FLinearColor& Cobalt();      // #1B2A6B
		FINALNIGHT_API const FLinearColor& DeepViolet();  // #2A1747
		FINALNIGHT_API const FLinearColor& Panel();       // #12152A at 88% alpha
		FINALNIGHT_API const FLinearColor& PanelEdge();   // the panel's 1.5 px border, a lifted cobalt
		FINALNIGHT_API const FLinearColor& Accent();      // warm orange #FF8A2B
		FINALNIGHT_API const FLinearColor& HumanAura();   // gold #E8B84A
		FINALNIGHT_API const FLinearColor& AIAura();      // cyan #3FD6E8
		FINALNIGHT_API const FLinearColor& Fallen();      // grey #7A7F8C
		FINALNIGHT_API const FLinearColor& Text();        // off-white #EDE6D6
		FINALNIGHT_API const FLinearColor& TextDim();     // off-white at 62%
		FINALNIGHT_API const FLinearColor& Aura(EFNSeatRole Role);
	}

	/** Spacing scale, in Slate units. */
	namespace Space
	{
		constexpr float XS = 4.f;
		constexpr float S = 8.f;
		constexpr float M = 12.f;
		constexpr float L = 16.f;
		constexpr float XL = 24.f;
	}

	namespace Radius
	{
		constexpr float Panel = 10.f;
		constexpr float Card = 14.f;
		constexpr float Image = 10.f;
		constexpr float Glow = 18.f;
	}

	namespace Brush
	{
		FINALNIGHT_API const FSlateBrush* Panel();               // radius 10, 1.5 px border
		FINALNIGHT_API const FSlateBrush* Card();                // radius 14, 1.5 px border
		FINALNIGHT_API const FSlateBrush* CardFrame(EFNSeatRole Role);  // radius 14, 2 px aura border
		FINALNIGHT_API const FSlateBrush* AuraGlow(EFNSeatRole Role);   // radius 18, soft aura fill behind a card
		FINALNIGHT_API const FSlateBrush* ImageWell();           // radius 10, deep violet: where a portrait sits
		FINALNIGHT_API const FSlateBrush* Pill(EFNSeatRole Role);       // fully rounded, aura outline
		FINALNIGHT_API const FSlateBrush* PillAccent();          // fully rounded, orange fill (badges, timers)
		FINALNIGHT_API const FSlateBrush* PillYou();             // fully rounded, gold fill ("YOU")
		FINALNIGHT_API const FSlateBrush* AccentRule();          // flat orange, for the 2 px rule under a title
		FINALNIGHT_API const FSlateBrush* Swatch(const FLinearColor& Fill);  // radius 6 chip, cached per colour
	}

	/** Type scale, all on the engine's Roboto. */
	namespace Type
	{
		FINALNIGHT_API FSlateFontInfo Font(int32 Size, const TCHAR* Weight = TEXT("Regular"));

		FINALNIGHT_API const FTextBlockStyle& Display();  // 30 Bold
		FINALNIGHT_API const FTextBlockStyle& Title();    // 18 Bold
		FINALNIGHT_API const FTextBlockStyle& Name();     // 16 Bold
		FINALNIGHT_API const FTextBlockStyle& Body();     // 13 Regular
		FINALNIGHT_API const FTextBlockStyle& Small();    // 10 Regular, dim
		FINALNIGHT_API const FTextBlockStyle& Label();    // 9 Bold, tracked: Human / AI / Fallen / You

		/** The same sizes with a 1 px outline and a drop shadow, for text drawn over images or the 3D table. */
		FINALNIGHT_API const FTextBlockStyle& DisplayOverImage();
		FINALNIGHT_API const FTextBlockStyle& NameOverImage();
		FINALNIGHT_API const FTextBlockStyle& BodyOverImage();
	}

	/** A rounded input: dark well, cobalt border, orange border when focused. */
	FINALNIGHT_API const FEditableTextBoxStyle& TextBox();

	/** "Human", "AI" or "Fallen". */
	FINALNIGHT_API FText RoleLabel(EFNSeatRole Role);
}
