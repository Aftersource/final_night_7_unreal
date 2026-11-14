#include "FNMedia.h"

#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace FN7MediaGen
{
	FString CachePath(const FString& Hash)
	{
		FString Safe = Hash;
		for (TCHAR& C : Safe) if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) C = TEXT('_');
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FN7Media"), Safe + TEXT(".png"));
	}
}

bool FN7Media::LoadCached(const FString& Hash, TArray<uint8>& OutPng)
{
	return !Hash.IsEmpty() && FFileHelper::LoadFileToArray(OutPng, *FN7MediaGen::CachePath(Hash), FILEREAD_Silent) && OutPng.Num() > 8;
}

void FN7Media::SaveCached(const FString& Hash, const TArray<uint8>& Png)
{
	if (!Hash.IsEmpty() && Png.Num() > 8) FFileHelper::SaveArrayToFile(Png, *FN7MediaGen::CachePath(Hash));
}

UTexture2D* FN7Media::TextureFromPng(const TArray<uint8>& Png, bool bGrey)
{
	FImage Img;
	if (Png.Num() < 8 || !FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Img)) return nullptr;
	// 8-bit PNGs decode straight to BGRA8; anything else goes through the engine's importer (no mip chain).
	if (Img.Format != ERawImageFormat::BGRA8) return bGrey ? nullptr : FImageUtils::ImportBufferAsTexture2D(Png);
	const int32 W = Img.SizeX, H = Img.SizeY;
	if (W < 1 || H < 1 || Img.RawData.Num() < W * H * 4) return nullptr;

	TArray<FColor> Level;
	Level.SetNumUninitialized(W * H);
	FMemory::Memcpy(Level.GetData(), Img.RawData.GetData(), W * H * sizeof(FColor));
	if (bGrey)
		for (FColor& C : Level)
		{
			const uint8 L = (uint8)FMath::Clamp((0.299f * C.R + 0.587f * C.G + 0.114f * C.B) * 0.6f, 0.f, 255.f);
			C = FColor(L, L, (uint8)FMath::Min(255, L + 6), C.A);
		}

	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex) return nullptr;
	Tex->SRGB = true;
	Tex->Filter = TF_Trilinear;
	Tex->NeverStream = true;
	Tex->LODGroup = TEXTUREGROUP_UI;   // no LOD bias: the full resolution is always there
	FTexturePlatformData* PD = Tex->GetPlatformData();
	{
		FTexture2DMipMap& Mip0 = PD->Mips[0];
		void* Dst = Mip0.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, Level.GetData(), Level.Num() * sizeof(FColor));
		Mip0.BulkData.Unlock();
	}
	// The rest of the chain: 2x2 box filter down to 1 px.
	int32 Lw = W, Lh = H;
	while (Lw > 1 || Lh > 1)
	{
		const int32 Nw = FMath::Max(1, Lw / 2), Nh = FMath::Max(1, Lh / 2);
		TArray<FColor> Next;
		Next.SetNumUninitialized(Nw * Nh);
		for (int32 y = 0; y < Nh; ++y)
			for (int32 x = 0; x < Nw; ++x)
			{
				uint32 R = 0, G = 0, B = 0, A = 0, N = 0;
				for (int32 dy = 0; dy < 2; ++dy)
					for (int32 dx = 0; dx < 2; ++dx)
					{
						const int32 Sx = FMath::Min(Lw - 1, x * 2 + dx), Sy = FMath::Min(Lh - 1, y * 2 + dy);
						const FColor& C = Level[Sy * Lw + Sx];
						R += C.R; G += C.G; B += C.B; A += C.A; ++N;
					}
				Next[y * Nw + x] = FColor((uint8)(R / N), (uint8)(G / N), (uint8)(B / N), (uint8)(A / N));
			}
		FTexture2DMipMap* Mip = new FTexture2DMipMap(Nw, Nh, 1);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dst = Mip->BulkData.Realloc((int64)Nw * Nh * sizeof(FColor));
		FMemory::Memcpy(Dst, Next.GetData(), Next.Num() * sizeof(FColor));
		Mip->BulkData.Unlock();
		PD->Mips.Add(Mip);
		Level = MoveTemp(Next);
		Lw = Nw;
		Lh = Nh;
	}
	Tex->UpdateResource();
	return Tex;
}
