#include "FNWood.h"

#include "Engine/Texture2D.h"
#include "FNStyle.h"
#include "TextureResource.h"
#include "Brushes/SlateNoResource.h"
#include "Templates/Function.h"

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

// --- the scroll ----------------------------------------------------------------

namespace FN7ClothGen
{
	using FN7WoodGen::Noise;

	UTexture2D* MakeRGBA(int32 W, int32 H, TFunctionRef<FLinearColor(int32, int32)> Px)
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex) return nullptr;
		Tex->SRGB = true;
		Tex->Filter = TF_Bilinear;
		Tex->AddToRoot();
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(W * H);
		for (int32 Y = 0; Y < H; ++Y)
			for (int32 X = 0; X < W; ++X)
				Pixels[Y * W + X] = Px(X, Y).ToFColor(true);
		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		return Tex;
	}

	FLinearColor Fabric(int32 X, int32 Y, int32 W, int32 H)
	{
		using FN7WoodGen::Hex;
		static const FLinearColor Cream = Hex(TEXT("EFE5CB"));
		static const FLinearColor Aged = Hex(TEXT("C7AA78"));
		static const FLinearColor Burnt = Hex(TEXT("3A2312"));
		const float Fx = (float)X, Fy = (float)Y;

		// The weave: 5 px threads, over-under. Each cell shows either a weft (horizontal) or a warp (vertical) thread.
		const float P = 5.f;
		const int32 Cx = FMath::FloorToInt(Fx / P), Cy = FMath::FloorToInt(Fy / P);
		const float Ux = (Fx - Cx * P) / P, Uy = (Fy - Cy * P) / P;
		const bool bWeft = ((Cx + Cy) & 1) == 0;
		const float Profile = bWeft ? FMath::Sin(PI * Uy) : FMath::Sin(PI * Ux);
		// Slubs: each thread thickens and pales along its run.
		const float Slub = bWeft ? Noise(Fx / 37.f, Cy * 0.71f) : Noise(Cx * 0.71f, Fy / 37.f);
		float Bright = 0.80f + 0.17f * Profile + 0.06f * Slub;

		// Age: broad stains, foxing spots, a warmer tint toward the edges.
		const float Stain = FMath::Clamp(0.5f + 0.5f * Noise(Fx / 210.f + 3.f, Fy / 170.f), 0.f, 1.f);
		const float Fox = FMath::Clamp(Noise(Fx / 23.f + 9.f, Fy / 23.f) - 0.55f, 0.f, 1.f) * 1.6f;
		FLinearColor C = FMath::Lerp(Cream, Aged, 0.18f + 0.45f * FMath::Pow(Stain, 2.f) + 0.5f * Fox);

		// Crisp torn edges on the left and right: transparent past an irregular tear, scorched just inside it.
		const float Tear = 9.f + 7.f * (0.5f + 0.5f * Noise(Fy / 31.f, 1.7f)) + 3.f * Noise(Fy / 6.f, 5.3f);
		const float Edge = (float)FMath::Min(X, W - 1 - X);
		if (Edge < Tear - 0.5f) return FLinearColor(0.f, 0.f, 0.f, 0.f);
		const float Inside = Edge - Tear;
		const float Alpha = FMath::Clamp(Inside + 0.5f, 0.f, 1.f);   // a one-pixel, crisp edge
		if (Inside < 7.f)
		{
			const float K = 1.f - Inside / 7.f;
			C = FMath::Lerp(C, Burnt, FMath::Pow(K, 1.6f) * 0.85f);
			Bright *= 1.f - 0.15f * K;
		}
		// The cloth near the rods (top and bottom) sits in their shade.
		const float V = (float)Y / H;
		Bright *= 1.f - 0.10f * FMath::Pow(FMath::Abs(V - 0.5f) * 2.f, 3.f);
		C *= Bright;
		C.A = Alpha;
		return C;
	}

	FLinearColor Rod(int32 X, int32 Y, int32 W, int32 H)
	{
		using FN7WoodGen::Hex;
		static const FLinearColor Wood = Hex(TEXT("7A4A28"));
		static const FLinearColor Knob = Hex(TEXT("5A3219"));
		const float N = ((float)Y + 0.5f) / H * 2.f - 1.f;   // -1 top .. 1 bottom
		const int32 KnobW = 40;
		const bool bKnob = X < KnobW || X >= W - KnobW;
		float Radius = 0.62f;                                 // the shaft is thinner than the knobs
		if (bKnob)
		{
			const float Kx = (float)(X < KnobW ? X : W - 1 - X) / KnobW;   // 0 at the tip .. 1 at the shaft
			Radius = 0.62f + 0.38f * FMath::Sin(FMath::Clamp(Kx, 0.f, 1.f) * PI * 0.9f + 0.15f);
		}
		if (FMath::Abs(N) > Radius) return FLinearColor(0.f, 0.f, 0.f, 0.f);
		const float Nn = N / Radius;
		const float Cyl = FMath::Sqrt(FMath::Max(0.f, 1.f - Nn * Nn));
		const float Spec = FMath::Pow(FMath::Max(0.f, 1.f - FMath::Abs(Nn + 0.45f) * 3.2f), 3.f);
		const float Grain = 0.5f + 0.5f * FMath::Sin(((float)X / 9.f + Noise((float)X / 60.f, Nn * 2.f) * 3.f) * 2.f * PI);
		FLinearColor C = bKnob ? Knob : Wood;
		C *= 0.45f + 0.65f * Cyl;
		C = FMath::Lerp(C, C * 0.82f, Grain * 0.35f);
		C += FLinearColor(1.f, 0.9f, 0.7f, 0.f) * Spec * 0.35f;
		const float Alpha = FMath::Clamp((Radius - FMath::Abs(N)) * H * 0.5f, 0.f, 1.f);
		C.A = Alpha;
		return C;
	}

	FSlateBrush ImageBrush(UTexture2D* Tex, int32 W, int32 H, ESlateBrushDrawType::Type Draw, const FMargin& Margin = FMargin(0.f))
	{
		FSlateBrush B;
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(W, H);
		B.DrawAs = Draw;
		B.Margin = Margin;
		return B;
	}
}

const FSlateBrush* FN7Wood::FabricBrush()
{
	static const FSlateBrush B = []()
	{
		const int32 W = 1024, H = 640;
		return FN7ClothGen::ImageBrush(FN7ClothGen::MakeRGBA(W, H, [W, H](int32 X, int32 Y) { return FN7ClothGen::Fabric(X, Y, W, H); }), W, H, ESlateBrushDrawType::Image);
	}();
	return &B;
}

const FSlateBrush* FN7Wood::RodBrush()
{
	static const FSlateBrush B = []()
	{
		const int32 W = 512, H = 40;
		return FN7ClothGen::ImageBrush(FN7ClothGen::MakeRGBA(W, H, [W, H](int32 X, int32 Y) { return FN7ClothGen::Rod(X, Y, W, H); }), W, H,
			ESlateBrushDrawType::Box, FMargin(44.f / W, 0.f, 44.f / W, 0.f));
	}();
	return &B;
}

const FSlateBrush* FN7Wood::ShadeBrush(bool bUp)
{
	auto Make = [](bool bFadeUp)
	{
		const int32 W = 4, H = 64;
		return FN7ClothGen::ImageBrush(FN7ClothGen::MakeRGBA(W, H, [H, bFadeUp](int32, int32 Y)
		{
			const float T = (float)Y / (H - 1);
			const float A = FMath::Pow(bFadeUp ? T : 1.f - T, 1.8f) * 0.55f;
			return FLinearColor(0.12f, 0.07f, 0.03f, A);
		}), W, H, ESlateBrushDrawType::Image);
	};
	static const FSlateBrush Down = Make(false);
	static const FSlateBrush Up = Make(true);
	return bUp ? &Up : &Down;
}

const FLinearColor& FN7Wood::RedInk()
{
	static const FLinearColor C = FN7WoodGen::Hex(TEXT("7A2312"));
	return C;
}

const FEditableTextBoxStyle& FN7Wood::InkTextBox()
{
	static const FEditableTextBoxStyle S = []()
	{
		FEditableTextBoxStyle T = FN7Style::TextBox();
		const FSlateNoResource None;
		T.SetBackgroundImageNormal(None);
		T.SetBackgroundImageHovered(None);
		T.SetBackgroundImageFocused(None);
		T.SetBackgroundImageReadOnly(None);
		FTextBlockStyle Text = FN7Style::Type::Body();
		Text.SetFont(FN7Style::Type::Font(17, TEXT("Italic")));
		Text.SetColorAndOpacity(FSlateColor(Ink()));
		Text.SetShadowOffset(FVector2D(0.f, 0.f));
		T.SetTextStyle(Text);
		T.SetForegroundColor(FSlateColor(Ink()));
		T.SetFocusedForegroundColor(FSlateColor(Ink()));
		T.SetPadding(FMargin(6.f, 6.f));
		return T;
	}();
	return S;
}
