#include "FNWood.h"

#include "Engine/Texture2D.h"
#include "FNStyle.h"
#include "TextureResource.h"

namespace FN7WoodGen
{
	struct FParams
	{
		int32 W = 256;
		int32 H = 256;
		FLinearColor Light;
		FLinearColor Dark;
		float GrainFreq = 8.f;      // grain lines per texture height
		float GrainStrength = 0.5f;
		float Fibres = 0.08f;
		float Blotches = 0.f;
		int32 Bevel = 4;            // px of bevelled edge
		int32 GrooveInset = 12;     // px from the edge to the groove's centre
		int32 GrooveHalf = 3;       // px half-width of the groove
		float Seed = 0.f;
	};

	float Noise(float X, float Y) { return FMath::PerlinNoise2D(FVector2D(X, Y)); }

	FLinearColor Pixel(int32 X, int32 Y, const FParams& P)
	{
		const float Sx = (float)X / P.H + P.Seed;   // isotropic coordinates: grain runs along X
		const float Sy = (float)Y / P.H + P.Seed * 0.37f;

		const float Warp = Noise(Sx * 1.2f, Sy * 5.f) * 1.3f + Noise(Sx * 0.35f + 7.f, Sy * 0.9f) * 2.2f;
		const float Ring = 0.5f + 0.5f * FMath::Sin((Sy * P.GrainFreq + Warp) * 2.f * PI);
		const float Fibre = Noise(Sx * 3.f, Sy * 110.f) * 0.5f + 0.5f;
		const float Blot = Noise(Sx * 2.5f + 3.f, Sy * 2.5f) * 0.5f + 0.5f;
		float T = FMath::Pow(Ring, 2.2f) * P.GrainStrength + (Fibre - 0.5f) * P.Fibres * 2.f + (Blot - 0.5f) * P.Blotches;
		FLinearColor C = FMath::Lerp(P.Light, P.Dark, FMath::Clamp(T, 0.f, 1.f));

		// Candle light from the upper left: brighter there, warmer and darker toward the far corner.
		const float U = (float)X / P.W, V = (float)Y / P.H;
		const float D = FMath::Sqrt(FMath::Square(U - 0.3f) + FMath::Square(V - 0.25f));
		const float Light = 1.06f - 0.32f * FMath::Pow(D, 1.4f);
		C *= Light;
		C = FMath::Lerp(C, C * FLinearColor(1.f, 0.86f, 0.62f), 0.18f * (1.f - Light + 0.3f));

		// Distance to the nearest edge, and whether that edge faces the light (top or left).
		const int32 L = X, Tp = Y, R = P.W - 1 - X, B = P.H - 1 - Y;
		const int32 E = FMath::Min(FMath::Min(L, Tp), FMath::Min(R, B));
		const bool bLit = FMath::Min(L, Tp) <= FMath::Min(R, B);

		// Bevel: a lit rim on the top/left, a shaded rim on the bottom/right.
		if (E < P.Bevel)
		{
			const float K = 1.f - (float)E / P.Bevel;
			C = bLit ? FMath::Lerp(C, C * 1.28f, 0.8f * K) : FMath::Lerp(C, C * 0.55f, 0.85f * K);
		}

		// The carved groove: a V cut, shadowed on the side that faces away from the light.
		const float Tg = (float)(E - P.GrooveInset) / FMath::Max(1, P.GrooveHalf);
		if (FMath::Abs(Tg) < 1.f)
		{
			const float Depth = 1.f - FMath::Abs(Tg);
			const bool bFacesLight = bLit ? (Tg > 0.f) : (Tg < 0.f);
			C *= 1.f - 0.30f * Depth;
			C = bFacesLight ? FMath::Lerp(C, C * 1.35f, 0.45f * Depth) : FMath::Lerp(C, C * 0.6f, 0.5f * Depth);
		}
		// Just inside the groove the field sits a hair lower: a soft inner shadow on the lit sides.
		else if (E > P.GrooveInset && E < P.GrooveInset + P.GrooveHalf * 3 && bLit)
		{
			const float K = 1.f - (float)(E - P.GrooveInset - P.GrooveHalf) / (P.GrooveHalf * 2.f);
			C *= 1.f - 0.10f * FMath::Clamp(K, 0.f, 1.f);
		}
		C.A = 1.f;
		return C;
	}

	UTexture2D* Make(const FParams& P)
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(P.W, P.H, PF_B8G8R8A8);
		if (!Tex) return nullptr;
		Tex->SRGB = true;
		Tex->Filter = TF_Bilinear;
		Tex->AddToRoot();   // lives for the whole session; brushes keep only raw pointers

		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(P.W * P.H);
		for (int32 Y = 0; Y < P.H; ++Y)
			for (int32 X = 0; X < P.W; ++X)
				Pixels[Y * P.W + X] = Pixel(X, Y, P).ToFColor(true);

		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		return Tex;
	}

	FSlateBrush MakeBoxBrush(UTexture2D* Tex, const FParams& P, const FMargin& PxMargin)
	{
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(P.W, P.H);
		B.DrawAs = ESlateBrushDrawType::Box;
		B.Margin = FMargin(PxMargin.Left / P.W, PxMargin.Top / P.H, PxMargin.Right / P.W, PxMargin.Bottom / P.H);
		return B;
	}

	FLinearColor Hex(const TCHAR* H) { return FLinearColor::FromSRGBColor(FColor::FromHex(H)); }
}

const FLinearColor& FN7Wood::Ink()
{
	static const FLinearColor C = FN7WoodGen::Hex(TEXT("2A1A0D"));
	return C;
}

const FSlateBrush* FN7Wood::WillBoxBrush()
{
	using namespace FN7WoodGen;
	static const FSlateBrush B = []()
	{
		FParams P;
		P.W = 512; P.H = 192;
		P.Light = Hex(TEXT("F3E8CF"));
		P.Dark = Hex(TEXT("CDB389"));
		P.GrainFreq = 5.f;
		P.GrainStrength = 0.32f;
		P.Fibres = 0.06f;
		P.Blotches = 0.10f;
		P.Bevel = 5;
		P.GrooveInset = 14;
		P.GrooveHalf = 4;
		P.Seed = 3.1f;
		const float M = 24.f;
		return MakeBoxBrush(Make(P), P, FMargin(M));
	}();
	return &B;
}

const FSlateBrush* FN7Wood::TileBrush()
{
	using namespace FN7WoodGen;
	static const FSlateBrush B = []()
	{
		FParams P;
		P.W = 160; P.H = 160;
		P.Light = Hex(TEXT("E6CFA6"));
		P.Dark = Hex(TEXT("A97F50"));
		P.GrainFreq = 9.f;
		P.GrainStrength = 0.55f;
		P.Fibres = 0.09f;
		P.Bevel = 6;
		P.GrooveInset = 11;
		P.GrooveHalf = 2;
		P.Seed = 11.7f;
		return MakeBoxBrush(Make(P), P, FMargin(18.f));
	}();
	return &B;
}

const FEditableTextBoxStyle& FN7Wood::WillTextBox()
{
	static const FEditableTextBoxStyle S = []()
	{
		FEditableTextBoxStyle T = FN7Style::TextBox();
		FSlateBrush Normal = *WillBoxBrush();
		Normal.TintColor = FSlateColor(FLinearColor(0.93f, 0.93f, 0.93f));
		FSlateBrush Bright = *WillBoxBrush();
		Bright.TintColor = FSlateColor(FLinearColor::White);
		T.SetBackgroundImageNormal(Normal);
		T.SetBackgroundImageHovered(Bright);
		T.SetBackgroundImageFocused(Bright);
		T.SetBackgroundImageReadOnly(Normal);
		FTextBlockStyle Text = FN7Style::Type::Body();
		Text.SetFont(FN7Style::Type::Font(15));
		Text.SetColorAndOpacity(FSlateColor(Ink()));
		Text.SetShadowOffset(FVector2D(0.f, 1.f));
		Text.SetShadowColorAndOpacity(FLinearColor(1.f, 0.97f, 0.88f, 0.55f));   // engraved: a light lip under each letter
		T.SetTextStyle(Text);
		T.SetForegroundColor(FSlateColor(Ink()));
		T.SetFocusedForegroundColor(FSlateColor(Ink()));
		T.SetPadding(FMargin(30.f, 22.f, 30.f, 22.f));
		return T;
	}();
	return S;
}
