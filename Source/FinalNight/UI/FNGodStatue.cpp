#include "FNGodStatue.h"

#include "FNMedia.h"
#include "FNNetClient.h"
#include "FNTableGods.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "UObject/UObjectIterator.h"

namespace FN7GodStatueImpl
{
	static constexpr int32 MaxTris = 80000;   // heavier: the picture (backend rule, 2026-11-15)

	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O.IsValid()) O->TryGetStringField(K, V); return V; }
	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double D = 0.0) { double V = D; if (O.IsValid()) O->TryGetNumberField(K, V); return V; }
	static TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O.IsValid() && O->TryGetObjectField(K, V) && V ? *V : nullptr; }
	static const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>* V = nullptr; return O.IsValid() && O->TryGetArrayField(K, V) ? V : nullptr; }

	static UFNNetClient* Net()
	{
		for (TObjectIterator<UFNNetClient> It; It; ++It) if (!It->HasAnyFlags(RF_ClassDefaultObject)) return *It;
		return nullptr;
	}
	static FString CachePath(const FString& Sha, const TCHAR* Ext)
	{
		FString Safe;
		for (const TCHAR C : Sha) if (FChar::IsAlnum(C)) Safe.AppendChar(C);
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FN7Statues"), Safe.Left(64) + Ext);
	}
	/** Bytes by sha256: the cache when it matches, else the server (verified, then cached). */
	static void Fetch(const FString& Url, const FString& Sha, const TCHAR* Ext, TFunction<void(const TArray<uint8>&)> Done)
	{
		TArray<uint8> B;
		const FString Path = Sha.IsEmpty() ? FString() : CachePath(Sha, Ext);
		if (!Path.IsEmpty() && FFileHelper::LoadFileToArray(B, *Path) && B.Num() > 0) { Done(B); return; }
		UFNNetClient* N = Net();
		if (!N || Url.IsEmpty()) return;
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
		R->SetURL(N->MediaUrl(Url));
		R->SetVerb(TEXT("GET"));
		R->SetTimeout(180.f);
		R->OnProcessRequestComplete().BindLambda([Path, Sha, Url, Done](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
		{
			if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { UE_LOG(LogTemp, Display, TEXT("FN7 god statue: %s not fetched (%d)"), *Url, Resp.IsValid() ? Resp->GetResponseCode() : 0); return; }
			const TArray<uint8>& Body = Resp->GetContent();
			if (!Sha.IsEmpty())
			{
				FSHA256Signature Sig;
				if (FPlatformMisc::GetSHA256Signature(Body.GetData(), Body.Num(), Sig) && !Sig.ToString().Equals(Sha, ESearchCase::IgnoreCase))
				{
					UE_LOG(LogTemp, Warning, TEXT("FN7 god statue: %s sha256 mismatch, not used"), *Url);
					return;
				}
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
				FFileHelper::SaveArrayToFile(Body, *Path);
			}
			Done(Body);
		});
		R->ProcessRequest();
	}
}

FLinearColor FN7GodStatue::StoneColor(const FString& Stone)
{
	const FString S = Stone.ToLower();
	if (S.Contains(TEXT("onyx")) || S.Contains(TEXT("obsidian")) || S.Contains(TEXT("black"))) return FLinearColor(0.035f, 0.035f, 0.04f);
	if (S.Contains(TEXT("granite"))) return FLinearColor(0.33f, 0.32f, 0.31f);
	if (S.Contains(TEXT("sandstone"))) return FLinearColor(0.62f, 0.46f, 0.28f);
	if (S.Contains(TEXT("jade"))) return FLinearColor(0.12f, 0.42f, 0.24f);
	if (S.Contains(TEXT("bronze"))) return FLinearColor(0.42f, 0.24f, 0.09f);
	if (S.Contains(TEXT("gold"))) return FLinearColor(0.78f, 0.55f, 0.16f);
	return FLinearColor(0.82f, 0.8f, 0.76f);   // white marble
}

UStaticMesh* FN7GodStatue::MeshFromGlb(const TArray<uint8>& G, UObject* Outer, FString& Why, int32& OutTris)
{
	using namespace FN7GodStatueImpl;
	OutTris = 0;
	auto U32 = [&G](int32 At) { uint32 V = 0; FMemory::Memcpy(&V, G.GetData() + At, 4); return V; };
	if (G.Num() < 28 || U32(0) != 0x46546C67u) { Why = TEXT("not a GLB"); return nullptr; }
	const int32 JsonLen = (int32)U32(12);
	if (U32(16) != 0x4E4F534Au || 20 + JsonLen + 8 > G.Num()) { Why = TEXT("no JSON chunk"); return nullptr; }
	const int32 BinLen = (int32)U32(20 + JsonLen), BinAt = 28 + JsonLen;
	if (U32(24 + JsonLen) != 0x004E4942u || BinAt + BinLen > G.Num()) { Why = TEXT("no BIN chunk"); return nullptr; }
	const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(G.GetData() + 20), JsonLen);
	const FString Js(Conv.Length(), Conv.Get());
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Js), J) || !J.IsValid()) { Why = TEXT("bad JSON"); return nullptr; }
	if (const TArray<TSharedPtr<FJsonValue>>* Req = Arr(J, TEXT("extensionsRequired")))
		if (Req->Num() > 0) { Why = TEXT("needs ") + (*Req)[0]->AsString(); return nullptr; }   // Draco / meshopt: not read here
	const TArray<TSharedPtr<FJsonValue>>* Accs = Arr(J, TEXT("accessors"));
	const TArray<TSharedPtr<FJsonValue>>* Views = Arr(J, TEXT("bufferViews"));
	const TArray<TSharedPtr<FJsonValue>>* Meshes = Arr(J, TEXT("meshes"));
	if (!Accs || !Views || !Meshes || Meshes->Num() == 0) { Why = TEXT("no mesh"); return nullptr; }
	const uint8* Bin = G.GetData() + BinAt;
	// An accessor's element k, component c, as a float (or an index).
	struct FAcc { const uint8* Base = nullptr; int32 Count = 0, Comps = 0, Ct = 0, Stride = 0; };
	auto Get = [&](int32 Idx, FAcc& A) -> bool
	{
		if (!Accs->IsValidIndex(Idx)) return false;
		const TSharedPtr<FJsonObject> Ao = (*Accs)[Idx]->AsObject();
		const int32 Bv = (int32)Num(Ao, TEXT("bufferView"), -1);
		if (!Views->IsValidIndex(Bv)) return false;
		const TSharedPtr<FJsonObject> Vo = (*Views)[Bv]->AsObject();
		const FString Type = Str(Ao, TEXT("type"));
		A.Comps = Type == TEXT("SCALAR") ? 1 : Type == TEXT("VEC2") ? 2 : Type == TEXT("VEC3") ? 3 : Type == TEXT("VEC4") ? 4 : 0;
		A.Ct = (int32)Num(Ao, TEXT("componentType"));
		A.Count = (int32)Num(Ao, TEXT("count"));
		const int32 Cs = (A.Ct == 5126 || A.Ct == 5125) ? 4 : (A.Ct == 5123 || A.Ct == 5122) ? 2 : 1;
		const int64 Off = (int64)Num(Vo, TEXT("byteOffset")) + (int64)Num(Ao, TEXT("byteOffset"));
		A.Stride = (int32)Num(Vo, TEXT("byteStride"), 0.0);
		if (A.Stride <= 0) A.Stride = Cs * A.Comps;
		if (A.Comps == 0 || Off < 0 || Off + (int64)A.Stride * FMath::Max(0, A.Count - 1) + Cs * A.Comps > BinLen) return false;
		A.Base = Bin + Off;
		return true;
	};
	auto F = [](const FAcc& A, int32 K, int32 C) -> float { float V = 0.f; FMemory::Memcpy(&V, A.Base + (int64)A.Stride * K + 4 * C, 4); return V; };
	auto I = [](const FAcc& A, int32 K) -> uint32
	{
		const uint8* P = A.Base + (int64)A.Stride * K;
		if (A.Ct == 5125) { uint32 V; FMemory::Memcpy(&V, P, 4); return V; }
		if (A.Ct == 5123) { uint16 V; FMemory::Memcpy(&V, P, 2); return V; }
		return *P;
	};
	TArray<FVector3f> Pos, Nrm;
	TArray<uint32> Idx;
	for (const TSharedPtr<FJsonValue>& Mv : *Meshes)
		if (const TArray<TSharedPtr<FJsonValue>>* Prims = Arr(Mv->AsObject(), TEXT("primitives")))
			for (const TSharedPtr<FJsonValue>& Pv : *Prims)
			{
				const TSharedPtr<FJsonObject> P = Pv->AsObject();
				if (Num(P, TEXT("mode"), 4.0) != 4.0) continue;   // triangles only
				const TSharedPtr<FJsonObject> At = Obj(P, TEXT("attributes"));
				FAcc Pa, Na, Ia;
				if (!Get((int32)Num(At, TEXT("POSITION"), -1), Pa) || Pa.Ct != 5126 || Pa.Comps != 3) continue;
				const bool bN = Get((int32)Num(At, TEXT("NORMAL"), -1), Na) && Na.Ct == 5126 && Na.Comps == 3 && Na.Count == Pa.Count;
				const uint32 Base = (uint32)Pos.Num();
				// glTF: Y up, +Z the front, metres; Unreal: Z up, +X the front (toward the table), centimetres.
				for (int32 k = 0; k < Pa.Count; ++k)
				{
					Pos.Add(FVector3f(F(Pa, k, 2), -F(Pa, k, 0), F(Pa, k, 1)) * 100.f);
					Nrm.Add(bN ? FVector3f(F(Na, k, 2), -F(Na, k, 0), F(Na, k, 1)) : FVector3f::ZeroVector);
				}
				if (Get((int32)Num(P, TEXT("indices"), -1), Ia) && Ia.Comps == 1)
					for (int32 k = 0; k + 2 < Ia.Count; k += 3) { Idx.Add(Base + I(Ia, k)); Idx.Add(Base + I(Ia, k + 1)); Idx.Add(Base + I(Ia, k + 2)); }
				else
					for (int32 k = 0; k + 2 < Pa.Count; k += 3) { Idx.Add(Base + k); Idx.Add(Base + k + 1); Idx.Add(Base + k + 2); }
			}
	OutTris = Idx.Num() / 3;
	if (Pos.Num() == 0 || OutTris == 0) { Why = TEXT("no triangles"); return nullptr; }
	// Normals where the file had none: summed face normals.
	bool bNeed = false;
	for (const FVector3f& N : Nrm) bNeed |= N.IsNearlyZero();
	if (bNeed)
	{
		for (FVector3f& N : Nrm) N = FVector3f::ZeroVector;
		for (int32 t = 0; t + 2 < Idx.Num(); t += 3)
		{
			if (Idx[t] >= (uint32)Pos.Num() || Idx[t + 1] >= (uint32)Pos.Num() || Idx[t + 2] >= (uint32)Pos.Num()) continue;
			const FVector3f Fn = FVector3f::CrossProduct(Pos[Idx[t + 2]] - Pos[Idx[t]], Pos[Idx[t + 1]] - Pos[Idx[t]]);
			Nrm[Idx[t]] += Fn; Nrm[Idx[t + 1]] += Fn; Nrm[Idx[t + 2]] += Fn;
		}
	}
	FMeshDescription Md;
	FStaticMeshAttributes A(Md);
	A.Register();
	A.GetVertexInstanceUVs().SetNumChannels(1);
	const FPolygonGroupID Grp = Md.CreatePolygonGroup();
	A.GetPolygonGroupMaterialSlotNames()[Grp] = FName(TEXT("Stone"));
	Md.ReserveNewVertices(Pos.Num());
	Md.ReserveNewVertexInstances(Pos.Num());
	Md.ReserveNewTriangles(OutTris);
	TArray<FVertexInstanceID> Vi;
	Vi.Reserve(Pos.Num());
	for (int32 k = 0; k < Pos.Num(); ++k)
	{
		const FVertexID V = Md.CreateVertex();
		A.GetVertexPositions()[V] = Pos[k];
		const FVertexInstanceID Ins = Md.CreateVertexInstance(V);
		const FVector3f N = Nrm[k].GetSafeNormal(SMALL_NUMBER, FVector3f::UpVector);
		FVector3f T = FVector3f::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector).GetSafeNormal();
		A.GetVertexInstanceNormals()[Ins] = N;
		A.GetVertexInstanceTangents()[Ins] = T;
		A.GetVertexInstanceBinormalSigns()[Ins] = 1.f;
		A.GetVertexInstanceUVs().Set(Ins, 0, FVector2f(0.f, 0.f));
		Vi.Add(Ins);
	}
	for (int32 t = 0; t + 2 < Idx.Num(); t += 3)
	{
		const uint32 a = Idx[t], b = Idx[t + 1], c = Idx[t + 2];
		if (a >= (uint32)Vi.Num() || b >= (uint32)Vi.Num() || c >= (uint32)Vi.Num() || a == b || b == c || a == c) continue;
		Md.CreateTriangle(Grp, { Vi[a], Vi[b], Vi[c] });
	}
	UStaticMesh* Sm = NewObject<UStaticMesh>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	Sm->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName(TEXT("Stone"))));
	UStaticMesh::FBuildMeshDescriptionsParams Prm;
	Prm.bBuildSimpleCollision = false;
	Prm.bFastBuild = true;
	Prm.bAllowCpuAccess = false;
	Sm->BuildFromMeshDescriptions({ &Md }, Prm);
	return Sm;
}

void FN7GodStatue::Ask(AFNGodPedestal* Pedestal, const FString& Player)
{
	using namespace FN7GodStatueImpl;
	UFNNetClient* N = Net();
	if (!N || !Pedestal || Player.IsEmpty()) return;
	TWeakObjectPtr<AFNGodPedestal> Weak(Pedestal);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
	R->SetURL(N->MediaUrl(TEXT("/api/assets/figures?name=") + FGenericPlatformHttp::UrlEncode(Player) + TEXT("&subject_kind=god")));
	R->SetVerb(TEXT("GET"));
	R->SetTimeout(15.f);
	R->OnProcessRequestComplete().BindLambda([Weak, Player](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		TSharedPtr<FJsonObject> O;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), O) || !O.IsValid()) return;
		const TSharedPtr<FJsonObject> God = Obj(O, TEXT("god"));
		if (!God.IsValid() || Str(God, TEXT("source")) != TEXT("account")) return;   // house gods: the packaged statues
		const TSharedPtr<FJsonObject> Img = Obj(God, TEXT("image")), Statue = Obj(God, TEXT("statue"));
		const FString Stone = Str(God, TEXT("stone"));
		const int32 Tris = (int32)Num(Statue, TEXT("tris"), 0.0);
		UE_LOG(LogTemp, Display, TEXT("FN7 god statue: %s has an account god (stone %s, statue %s, %d tris)"), *Player, *Stone, Statue.IsValid() ? *Str(Statue, TEXT("status")) : TEXT("none"), Tris);
		// The picture at once (and to stay, when the statue is too heavy or not made).
		const FString ImgUrl = !Str(Img, TEXT("file_url")).IsEmpty() ? Str(Img, TEXT("file_url")) : Str(Img, TEXT("url"));
		if (!ImgUrl.IsEmpty())
			Fetch(ImgUrl, Str(Img, TEXT("sha256")), TEXT(".png"), [Weak, Player](const TArray<uint8>& B)
			{
				if (AFNGodPedestal* P = Weak.Get()) if (UTexture2D* T = FN7Media::TextureFromPng(B)) { P->ShowGodPicture(T, Player); }
			});
		const FString GlbUrl = Str(Statue, TEXT("file_url"));
		if (GlbUrl.IsEmpty()) return;
		if (Tris > MaxTris) { UE_LOG(LogTemp, Display, TEXT("FN7 god statue: %s's statue has %d triangles (over %d): the picture stays"), *Player, Tris, MaxTris); return; }
		Fetch(GlbUrl, Str(Statue, TEXT("sha256")), TEXT(".glb"), [Weak, Player, Stone](const TArray<uint8>& B)
		{
			AFNGodPedestal* P = Weak.Get();
			if (!P) return;
			FString Why; int32 T = 0;
			UStaticMesh* M = FN7GodStatue::MeshFromGlb(B, P, Why, T);
			if (!M) { UE_LOG(LogTemp, Warning, TEXT("FN7 god statue: %s's GLB not used (%s): the picture stays"), *Player, *Why); return; }
			if (T > MaxTris) { UE_LOG(LogTemp, Display, TEXT("FN7 god statue: %s's GLB has %d triangles: the picture stays"), *Player, T); return; }
			UE_LOG(LogTemp, Display, TEXT("FN7 god statue: %s's statue built (%d triangles, %d KB)"), *Player, T, B.Num() / 1024);
			P->ShowRuntimeStatue(M, FN7GodStatue::StoneColor(Stone), Player);
		});
	});
	R->ProcessRequest();
}
