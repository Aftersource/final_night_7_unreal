#include "FNStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "FN7Style"

namespace
{
	FLinearColor StyleHex(const TCHAR* InHex, float Alpha = 1.f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(InHex));
		C.A = Alpha;
		return C;
	}

	FLinearColor StyleWithAlpha(const FLinearColor& C, float Alpha)
	{
		FLinearColor Out = C;
		Out.A = Alpha;
		return Out;
	}

	int32 StyleRoleIndex(EFNSeatRole Role) { return FMath::Clamp((int32)Role, 0, 2); }

	FTextBlockStyle StyleMakeText(const FSlateFontInfo& Font, const FLinearColor& Color, bool bOverImage)
	{
		FTextBlockStyle S;
		S.SetFont(Font);
		S.SetColorAndOpacity(FSlateColor(Color));
		if (bOverImage)
		{
			S.Font.OutlineSettings = FFontOutlineSettings(1, FLinearColor(0.f, 0.f, 0.f, 0.85f));
			S.SetShadowOffset(FVector2D(1.f, 1.5f));
			S.SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f));
		}
		return S;
	}
}

// --- palette ----------------------------------------------------------------

const FLinearColor& FN7Style::Color::Cobalt()     { static const FLinearColor C = StyleHex(TEXT("1B2A6B")); return C; }
const FLinearColor& FN7Style::Color::DeepViolet() { static const FLinearColor C = StyleHex(TEXT("2A1747")); return C; }
const FLinearColor& FN7Style::Color::Panel()      { static const FLinearColor C = StyleHex(TEXT("12152A"), 0.88f); return C; }
const FLinearColor& FN7Style::Color::PanelEdge()  { static const FLinearColor C = StyleHex(TEXT("33468F"), 0.9f); return C; }
const FLinearColor& FN7Style::Color::Accent()     { static const FLinearColor C = StyleHex(TEXT("FF8A2B")); return C; }
const FLinearColor& FN7Style::Color::HumanAura()  { static const FLinearColor C = StyleHex(TEXT("E8B84A")); return C; }
const FLinearColor& FN7Style::Color::AIAura()     { static const FLinearColor C = StyleHex(TEXT("3FD6E8")); return C; }
const FLinearColor& FN7Style::Color::Fallen()     { static const FLinearColor C = StyleHex(TEXT("7A7F8C")); return C; }
const FLinearColor& FN7Style::Color::Text()       { static const FLinearColor C = StyleHex(TEXT("EDE6D6")); return C; }
const FLinearColor& FN7Style::Color::TextDim()    { static const FLinearColor C = StyleHex(TEXT("EDE6D6"), 0.62f); return C; }

const FLinearColor& FN7Style::Color::Aura(EFNSeatRole Role)
{
	switch (Role)
	{
	case EFNSeatRole::Human: return HumanAura();
	case EFNSeatRole::AI:    return AIAura();
	default:                 return Fallen();
	}
}

// --- brushes ----------------------------------------------------------------

const FSlateBrush* FN7Style::Brush::Panel()
{
	static const FSlateRoundedBoxBrush B(Color::Panel(), Radius::Panel, Color::PanelEdge(), 1.5f);
	return &B;
}

const FSlateBrush* FN7Style::Brush::Card()
{
	static const FSlateRoundedBoxBrush B(Color::Panel(), Radius::Card, Color::PanelEdge(), 1.5f);
	return &B;
}

const FSlateBrush* FN7Style::Brush::CardFrame(EFNSeatRole Role)
{
	static const FSlateRoundedBoxBrush B[3] = {
		FSlateRoundedBoxBrush(Color::Panel(), Radius::Card, Color::HumanAura(), 2.f),
		FSlateRoundedBoxBrush(Color::Panel(), Radius::Card, Color::AIAura(), 2.f),
		FSlateRoundedBoxBrush(Color::Panel(), Radius::Card, Color::Fallen(), 2.f),
	};
	return &B[StyleRoleIndex(Role)];
}

const FSlateBrush* FN7Style::Brush::AuraGlow(EFNSeatRole Role)
{
	static const FSlateRoundedBoxBrush B[3] = {
		FSlateRoundedBoxBrush(StyleWithAlpha(Color::HumanAura(), 0.34f), Radius::Glow),
		FSlateRoundedBoxBrush(StyleWithAlpha(Color::AIAura(), 0.30f), Radius::Glow),
		FSlateRoundedBoxBrush(StyleWithAlpha(Color::Fallen(), 0.18f), Radius::Glow),
	};
	return &B[StyleRoleIndex(Role)];
}

const FSlateBrush* FN7Style::Brush::ImageWell()
{
	static const FSlateRoundedBoxBrush B(Color::DeepViolet(), Radius::Image);
	return &B;
}

const FSlateBrush* FN7Style::Brush::Pill(EFNSeatRole Role)
{
	// The outline-only constructor rounds by height: a true pill at any size.
	static const FSlateRoundedBoxBrush B[3] = {
		FSlateRoundedBoxBrush(StyleWithAlpha(Color::HumanAura(), 0.20f), Color::HumanAura(), 1.f),
		FSlateRoundedBoxBrush(StyleWithAlpha(Color::AIAura(), 0.18f), Color::AIAura(), 1.f),
		FSlateRoundedBoxBrush(StyleWithAlpha(Color::Fallen(), 0.18f), Color::Fallen(), 1.f),
	};
	return &B[StyleRoleIndex(Role)];
}

const FSlateBrush* FN7Style::Brush::PillAccent()
{
	static const FSlateRoundedBoxBrush B(StyleWithAlpha(Color::Accent(), 0.92f), Color::Accent(), 1.f);
	return &B;
}

const FSlateBrush* FN7Style::Brush::PillYou()
{
	static const FSlateRoundedBoxBrush B(Color::HumanAura(), Color::HumanAura(), 1.f);
	return &B;
}

const FSlateBrush* FN7Style::Brush::AccentRule()
{
	static const FSlateColorBrush B(Color::Accent());
	return &B;
}

const FSlateBrush* FN7Style::Brush::AccentRing()
{
	static const FSlateRoundedBoxBrush B(FLinearColor::Transparent, Radius::Card, Color::Accent(), 2.5f);
	return &B;
}

const FSlateBrush* FN7Style::Brush::Swatch(const FLinearColor& Fill)
{
	static TMap<uint32, TUniquePtr<FSlateRoundedBoxBrush>> Cache;
	const uint32 Key = GetTypeHash(Fill.ToFColor(false));
	if (TUniquePtr<FSlateRoundedBoxBrush>* Found = Cache.Find(Key)) return Found->Get();
	return Cache.Add(Key, MakeUnique<FSlateRoundedBoxBrush>(Fill, 6.f, Color::PanelEdge(), 1.f)).Get();
}

const FSlateBrush* FN7Style::Brush::Segment(const FLinearColor& Fill)
{
	static TMap<uint32, TUniquePtr<FSlateRoundedBoxBrush>> Cache;
	const uint32 Key = GetTypeHash(Fill.ToFColor(false));
	if (TUniquePtr<FSlateRoundedBoxBrush>* Found = Cache.Find(Key)) return Found->Get();
	return Cache.Add(Key, MakeUnique<FSlateRoundedBoxBrush>(Fill, 2.f)).Get();
}

// --- type -------------------------------------------------------------------

FSlateFontInfo FN7Style::Type::Font(int32 Size, const TCHAR* Weight)
{
	return FCoreStyle::GetDefaultFontStyle(Weight, Size);
}

const FTextBlockStyle& FN7Style::Type::Display() { static const FTextBlockStyle S = StyleMakeText(Font(30, TEXT("Bold")), Color::Text(), false); return S; }
const FTextBlockStyle& FN7Style::Type::Title()   { static const FTextBlockStyle S = StyleMakeText(Font(18, TEXT("Bold")), Color::Text(), false); return S; }
const FTextBlockStyle& FN7Style::Type::Name()    { static const FTextBlockStyle S = StyleMakeText(Font(16, TEXT("Bold")), Color::Text(), false); return S; }
const FTextBlockStyle& FN7Style::Type::Body()    { static const FTextBlockStyle S = StyleMakeText(Font(13), Color::Text(), false); return S; }
const FTextBlockStyle& FN7Style::Type::Small()   { static const FTextBlockStyle S = StyleMakeText(Font(10), Color::TextDim(), false); return S; }

const FTextBlockStyle& FN7Style::Type::Label()
{
	static const FTextBlockStyle S = []()
	{
		FSlateFontInfo F = Font(9, TEXT("Bold"));
		F.LetterSpacing = 140;
		return StyleMakeText(F, Color::Text(), false);
	}();
	return S;
}

const FTextBlockStyle& FN7Style::Type::DisplayOverImage() { static const FTextBlockStyle S = StyleMakeText(Font(30, TEXT("Bold")), Color::Text(), true); return S; }
const FTextBlockStyle& FN7Style::Type::NameOverImage()    { static const FTextBlockStyle S = StyleMakeText(Font(16, TEXT("Bold")), Color::Text(), true); return S; }
const FTextBlockStyle& FN7Style::Type::BodyOverImage()    { static const FTextBlockStyle S = StyleMakeText(Font(13), Color::Text(), true); return S; }

// --- input ------------------------------------------------------------------

const FEditableTextBoxStyle& FN7Style::TextBox()
{
	static const FEditableTextBoxStyle S = []()
	{
		const FLinearColor Well = StyleHex(TEXT("0D1022"), 0.94f);
		FEditableTextBoxStyle T = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		T.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Well, Radius::Panel, Color::PanelEdge(), 1.5f));
		T.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Well, Radius::Panel, StyleHex(TEXT("4A60B8")), 1.5f));
		T.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Well, Radius::Panel, Color::Accent(), 1.5f));
		T.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Well, Radius::Panel, Color::Fallen(), 1.f));
		T.SetTextStyle(Type::Body());
		T.SetForegroundColor(FSlateColor(Color::Text()));
		T.SetFocusedForegroundColor(FSlateColor(Color::Text()));
		T.SetBackgroundColor(FSlateColor(FLinearColor::White));
		T.SetPadding(FMargin(Space::M, Space::S));
		return T;
	}();
	return S;
}

// --- buttons ----------------------------------------------------------------

namespace
{
	FButtonStyle StyleMakePillButton(const FLinearColor& Fill, const FLinearColor& Edge, const FLinearColor& HoverFill, const FLinearColor& HoverEdge)
	{
		FButtonStyle B;
		B.SetNormal(FSlateRoundedBoxBrush(Fill, Edge, 1.f));
		B.SetHovered(FSlateRoundedBoxBrush(HoverFill, HoverEdge, 1.5f));
		B.SetPressed(FSlateRoundedBoxBrush(HoverFill, HoverEdge, 1.5f));
		B.SetDisabled(FSlateRoundedBoxBrush(StyleWithAlpha(Fill, 0.4f), StyleWithAlpha(Edge, 0.4f), 1.f));
		B.SetNormalPadding(FMargin(FN7Style::Space::M, 6.f));
		B.SetPressedPadding(FMargin(FN7Style::Space::M, 7.f, FN7Style::Space::M, 5.f));
		B.SetNormalForeground(FSlateColor(FN7Style::Color::Text()));
		B.SetHoveredForeground(FSlateColor(FN7Style::Color::Text()));
		B.SetPressedForeground(FSlateColor(FN7Style::Color::Text()));
		return B;
	}
}

const FButtonStyle& FN7Style::PillButton()
{
	static const FButtonStyle S = StyleMakePillButton(StyleHex(TEXT("12152A"), 0.92f), Color::PanelEdge(), StyleHex(TEXT("1B2A6B"), 0.95f), Color::Accent());
	return S;
}

const FButtonStyle& FN7Style::PillButtonArmed()
{
	static const FButtonStyle S = StyleMakePillButton(StyleWithAlpha(Color::Accent(), 0.9f), Color::Accent(), Color::Accent(), StyleHex(TEXT("FFC08A")));
	return S;
}

const FButtonStyle& FN7Style::AccentButton()
{
	static const FButtonStyle S = StyleMakePillButton(Color::Accent(), Color::Accent(), StyleHex(TEXT("FF9F4F")), StyleHex(TEXT("FFC08A")));
	return S;
}

FText FN7Style::RoleLabel(EFNSeatRole Role)
{
	switch (Role)
	{
	case EFNSeatRole::Human: return LOCTEXT("Human", "HUMAN");
	case EFNSeatRole::AI:    return LOCTEXT("AI", "AI");
	default:                 return LOCTEXT("Fallen", "FALLEN");
	}
}

#undef LOCTEXT_NAMESPACE
