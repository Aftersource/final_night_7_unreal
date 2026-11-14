#include "SFNPortraitCard.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SFNPortraitCard"

// --- hourglass --------------------------------------------------------------

void SFNHourglass::Construct(const FArguments& InArgs)
{
	Color = InArgs._Color;
	Size = InArgs._Size;
}

FVector2D SFNHourglass::ComputeDesiredSize(float) const
{
	return Size;
}

int32 SFNHourglass::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry, const FSlateRect&,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool) const
{
	const FVector2f L = FVector2f(AllottedGeometry.GetLocalSize());
	const float W = L.X, H = L.Y, I = 1.f;
	const FLinearColor Tint = Color.Get() * InWidgetStyle.GetColorAndOpacityTint();

	// Frame: top bar, the two glass halves meeting at the waist, bottom bar.
	TArray<FVector2f> Frame = {
		FVector2f(I, I), FVector2f(W - I, I), FVector2f(W * 0.5f + 0.6f, H * 0.5f), FVector2f(W - I, H - I),
		FVector2f(I, H - I), FVector2f(W * 0.5f - 0.6f, H * 0.5f), FVector2f(I, I)
	};
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Frame,
		ESlateDrawEffect::None, Tint, true, 1.5f);

	// Sand: a short stack in the lower bulb.
	TArray<FVector2f> Sand = { FVector2f(W * 0.3f, H - 2.5f), FVector2f(W * 0.7f, H - 2.5f) };
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Sand,
		ESlateDrawEffect::None, Tint, true, 2.f);
	return LayerId + 1;
}

// --- portrait card ----------------------------------------------------------

void SFNPortraitCard::Construct(const FArguments& InArgs)
{
	FallbackImage = InArgs._Image;
	Name = InArgs._Name;
	Role = InArgs._Role;
	bIsYou = InArgs._bIsYou;
	bShowHourglass = InArgs._bShowHourglass;
	HourglassText = InArgs._HourglassText;
	bPulse = InArgs._bPulse;

	const float Pad = FN7Style::Space::S;
	const float GlowPad = 4.f;
	ImageSide = FMath::Max(48.f, InArgs._CardWidth - 2.f * (Pad + GlowPad));

	RoundedImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	RoundedImage.OutlineSettings = FSlateBrushOutlineSettings(FN7Style::Radius::Image);
	RoundedImage.ImageSize = FVector2D(ImageSide, ImageSide);

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(InArgs._CardWidth)
		[
			SNew(SOverlay)

			// The aura: a soft rounded fill a few px larger than the card.
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage_Lambda([this]() { return FN7Style::Brush::AuraGlow(Role.Get()); })
				.BorderBackgroundColor(this, &SFNPortraitCard::GetGlowTint)
				.Padding(GlowPad)
				[
					SNew(SBorder)
					.BorderImage_Lambda([this]() { return FN7Style::Brush::CardFrame(Role.Get()); })
					.Padding(Pad)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
							.WidthOverride(ImageSide)
							.HeightOverride(ImageSide)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SNew(SImage).Image(FN7Style::Brush::ImageWell())
								]
								+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.TextStyle(&FN7Style::Type::DisplayOverImage())
									.Text(this, &SFNPortraitCard::GetInitial)
									.Visibility(this, &SFNPortraitCard::GetPlaceholderVisibility)
								]
								+ SOverlay::Slot()
								[
									SNew(SImage)
									.Image(this, &SFNPortraitCard::GetImage)
									.ColorAndOpacity(this, &SFNPortraitCard::GetImageTint)
								]
								// Time's window: the hourglass badge, top right.
								+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FN7Style::Space::XS)
								[
									SNew(SBorder)
									.BorderImage(FN7Style::Brush::PillAccent())
									.Padding(FMargin(6.f, 3.f))
									.Visibility_Lambda([this]() { return bShowHourglass.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
										[
											SNew(SFNHourglass).Color(FLinearColor(0.08f, 0.05f, 0.03f))
										]
										+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
										[
											SNew(STextBlock)
											.TextStyle(&FN7Style::Type::Label())
											.ColorAndOpacity(FLinearColor(0.08f, 0.05f, 0.03f))
											.Text(HourglassText)
											.Visibility_Lambda([this]() { return HourglassText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
										]
									]
								]
							]
						]

						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, FN7Style::Space::S, 0.f, FN7Style::Space::XS)
						[
							SNew(STextBlock)
							.TextStyle(&FN7Style::Type::NameOverImage())
							.Text(Name)
							.ColorAndOpacity_Lambda([this]() { return FSlateColor(Role.Get() == EFNSeatRole::Fallen ? FN7Style::Color::TextDim() : FN7Style::Color::Text()); })
						]

						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(SBorder)
							.BorderImage_Lambda([this]() { return FN7Style::Brush::Pill(Role.Get()); })
							.Padding(FMargin(FN7Style::Space::S, 2.f))
							[
								SNew(STextBlock)
								.TextStyle(&FN7Style::Type::Label())
								.Text_Lambda([this]() { return FN7Style::RoleLabel(Role.Get()); })
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(FN7Style::Color::Aura(Role.Get())); })
							]
						]
					]
				]
			]

			// "YOU", riding the top edge of the card.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)
			[
				SNew(SBorder)
				.BorderImage(FN7Style::Brush::PillYou())
				.Padding(FMargin(FN7Style::Space::S, 1.f))
				.RenderTransform(FSlateRenderTransform(FVector2f(0.f, -9.f)))
				.Visibility_Lambda([this]() { return bIsYou.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				[
					SNew(STextBlock)
					.TextStyle(&FN7Style::Type::Label())
					.ColorAndOpacity(FLinearColor(0.1f, 0.07f, 0.02f))
					.Text(LOCTEXT("You", "YOU"))
				]
			]
		]
	];

	if (bPulse) RegisterActiveTimer(1.f / 30.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFNPortraitCard::TickPulse));
}

void SFNPortraitCard::SetTexture(UTexture2D* InTexture)
{
	Texture.Reset(InTexture);
	RoundedImage.SetResourceObject(InTexture);
	RoundedImage.ImageSize = FVector2D(ImageSide, ImageSide);
}

bool SFNPortraitCard::SetImageFromPng(const TArray<uint8>& Bytes)
{
	if (Bytes.Num() == 0) return false;
	UTexture2D* Tex = FImageUtils::ImportBufferAsTexture2D(Bytes);
	if (!Tex) return false;
	SetTexture(Tex);
	return true;
}

void SFNPortraitCard::ClearImage()
{
	Texture.Reset();
	RoundedImage.SetResourceObject(nullptr);
}

const FSlateBrush* SFNPortraitCard::GetImage() const
{
	if (Texture.IsValid()) return &RoundedImage;
	return FallbackImage.Get();
}

EVisibility SFNPortraitCard::GetPlaceholderVisibility() const
{
	return GetImage() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
}

FSlateColor SFNPortraitCard::GetImageTint() const
{
	return Role.Get() == EFNSeatRole::Fallen ? FLinearColor(0.42f, 0.42f, 0.46f, 1.f) : FLinearColor::White;
}

FSlateColor SFNPortraitCard::GetGlowTint() const
{
	if (!bPulse || Role.Get() == EFNSeatRole::Fallen) return FLinearColor::White;
	const double T = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
	const float Speed = bIsYou.Get() ? 2.6f : 1.6f;
	const float A = 0.7f + 0.3f * FMath::Sin((float)T * Speed);
	return FLinearColor(1.f, 1.f, 1.f, A);
}

FText SFNPortraitCard::GetInitial() const
{
	const FString N = Name.Get().ToString().TrimStartAndEnd();
	return N.IsEmpty() ? FText::FromString(TEXT("?")) : FText::FromString(N.Left(1).ToUpper());
}

EActiveTimerReturnType SFNPortraitCard::TickPulse(double, float)
{
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
