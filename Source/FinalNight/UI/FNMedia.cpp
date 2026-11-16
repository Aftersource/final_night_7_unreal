#include "FNMedia.h"

#include "FNNetClient.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "ImageCore.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ImageUtils.h"
#include "Containers/Ticker.h"
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

namespace FN7MediaCheck
{
	static TMap<FString, int64> Sidecar;   // the server's byte length per hash (-1: no sidecar)
	struct FWait { TFunction<void(const TArray<uint8>&)> OnPng; TFunction<void()> OnFail; };
	static TMap<FString, TArray<FWait>> Waiting;
	static TSet<FString> Asking;
	static TWeakObjectPtr<UFNNetClient> GNet;
	static int32 Mismatch = 0, Checked = 0;
	static bool ValidHash(const FString& H)
	{
		if (H.Len() < 16 || H.Len() > 128) return false;
		for (const TCHAR C : H) if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) return false;
		return true;
	}
	static void Bad(const FString& Hash, const TCHAR* What, int64 Got, int64 Want)
	{
		++Mismatch;
		UE_LOG(LogTemp, Warning, TEXT("FN7 media: hash mismatch %s: %s %lld bytes, the server's %lld; deleted, refetched (%d mismatches so far, %d checked)"), *Hash, What, Got, Want, Mismatch, Checked);
		IFileManager::Get().Delete(*FN7MediaGen::CachePath(Hash), false, true, true);
	}
	/** The sidecar's byte length (once per hash per session), then Then(bytes). */
	static void WithSidecar(UFNNetClient* N, const FString& Hash, TFunction<void(int64)> Then)
	{
		if (const int64* B = Sidecar.Find(Hash)) { Then(*B); return; }
		if (!N) { Then(-1); return; }
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(N->MediaUrl(FString::Printf(TEXT("/media/%s.json"), *Hash)));
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1"));
		Req->SetTimeout(15.f);
		Req->OnProcessRequestComplete().BindLambda([Hash, Then](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			int64 Bytes = -1;
			if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
			{
				TSharedPtr<FJsonObject> J;
				TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
				double B = -1.0;
				FString Key;
				if (FJsonSerializer::Deserialize(Rd, J) && J.IsValid())
				{
					if (J->TryGetNumberField(TEXT("bytes"), B)) Bytes = (int64)B;
					if (J->TryGetStringField(TEXT("key"), Key) && !Key.IsEmpty() && Key != Hash)
						UE_LOG(LogTemp, Warning, TEXT("FN7 media: sidecar of %s names key %s"), *Hash, *Key);
				}
			}
			Sidecar.Add(Hash, Bytes);
			Then(Bytes);
		});
		Req->ProcessRequest();
	}
	/** A cached file checked after the fact (callers that read the cache themselves): a wrong one is deleted and counted. */
	static void CheckLater(const FString& Hash, int64 Have)
	{
		if (!ValidHash(Hash) || Asking.Contains(Hash)) return;
		Asking.Add(Hash);
		WithSidecar(GNet.Get(), Hash, [Hash, Have](int64 Want)
		{
			Asking.Remove(Hash);
			++Checked;
			if (Want >= 0 && Have != Want) Bad(Hash, TEXT("cached"), Have, Want);
		});
	}
}

bool FN7Media::LoadCached(const FString& Hash, TArray<uint8>& OutPng)
{
	using namespace FN7MediaCheck;
	if (!ValidHash(Hash) || !FFileHelper::LoadFileToArray(OutPng, *FN7MediaGen::CachePath(Hash), FILEREAD_Silent) || OutPng.Num() <= 8) return false;
	if (const int64* Want = Sidecar.Find(Hash))
	{
		if (*Want >= 0 && OutPng.Num() != *Want) { Bad(Hash, TEXT("cached"), OutPng.Num(), *Want); OutPng.Reset(); return false; }
		return true;
	}
	CheckLater(Hash, OutPng.Num());
	return true;
}

void FN7Media::SaveCached(const FString& Hash, const TArray<uint8>& Png)
{
	if (!FN7MediaCheck::ValidHash(Hash) || Png.Num() <= 8) return;
	FFileHelper::SaveArrayToFile(Png, *FN7MediaGen::CachePath(Hash));
	FN7MediaCheck::CheckLater(Hash, Png.Num());
}

FString FN7Media::PathFor(const FString& Hash) { return FString::Printf(TEXT("/media/%s.png"), *Hash); }
void FN7Media::SetNet(UFNNetClient* Net) { FN7MediaCheck::GNet = Net; }
int32 FN7Media::Mismatches() { return FN7MediaCheck::Mismatch; }

void FN7Media::Fetch(UFNNetClient* Net, const FString& Hash, TFunction<void(const TArray<uint8>&)> OnPng, TFunction<void()> OnFail)
{
	using namespace FN7MediaCheck;
	if (!Net || !ValidHash(Hash))
	{
		UE_LOG(LogTemp, Display, TEXT("FN7 media: not fetched (%s)"), Net ? *FString::Printf(TEXT("bad hash \"%s\""), *Hash) : TEXT("no client"));
		if (OnFail) OnFail();
		return;
	}
	GNet = Net;
	{
		// the launch count: how many pictures were wrong in the first 90 s (Chris's editor shows if this was the cause)
		static bool bSummary = false;
		if (!bSummary)
		{
			bSummary = true;
			FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7MediaSummary"), 90.f, [](float)
			{
				UE_LOG(LogTemp, Display, TEXT("FN7 media: %d hash mismatches in the first 90 s (%d pictures checked against their sidecars)"), Mismatch, Checked);
				return false;
			});
		}
	}
	if (TArray<FWait>* W = Waiting.Find(Hash)) { W->Add({ MoveTemp(OnPng), MoveTemp(OnFail) }); return; }
	Waiting.Add(Hash).Add({ MoveTemp(OnPng), MoveTemp(OnFail) });
	auto Finish = [Hash](const TArray<uint8>* Png)
	{
		TArray<FWait> Cbs;
		Waiting.RemoveAndCopyValue(Hash, Cbs);
		for (FWait& F : Cbs) { if (Png) { if (F.OnPng) F.OnPng(*Png); } else if (F.OnFail) F.OnFail(); }
	};
	TWeakObjectPtr<UFNNetClient> WN(Net);
	WithSidecar(Net, Hash, [WN, Hash, Finish](int64 Want)
	{
		++Checked;
		TArray<uint8> Disk;
		if (FFileHelper::LoadFileToArray(Disk, *FN7MediaGen::CachePath(Hash), FILEREAD_Silent) && Disk.Num() > 8)
		{
			if (Want < 0 || Disk.Num() == Want) { Finish(&Disk); return; }
			Bad(Hash, TEXT("cached"), Disk.Num(), Want);
		}
		if (!WN.IsValid()) { Finish(nullptr); return; }
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(WN->MediaUrl(PathFor(Hash)));
		Req->SetVerb(TEXT("GET"));
		Req->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1"));
		Req->SetTimeout(30.f);
		Req->OnProcessRequestComplete().BindLambda([Hash, Want, Finish](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { Finish(nullptr); return; }
			const TArray<uint8> Png = Resp->GetContent();
			if (Png.Num() <= 8 || (Want >= 0 && Png.Num() != Want))
			{
				++Mismatch;
				UE_LOG(LogTemp, Warning, TEXT("FN7 media: hash mismatch %s: downloaded %d bytes, the server's %lld; not shown (%d mismatches so far)"), *Hash, Png.Num(), Want, Mismatch);
				Finish(nullptr);
				return;
			}
			FFileHelper::SaveArrayToFile(Png, *FN7MediaGen::CachePath(Hash));
			Finish(&Png);
		});
		Req->ProcessRequest();
	});
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
