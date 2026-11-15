#include "FNHudPrefs.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace FN7HudPrefsState
{
	bool bLoaded = false;
	TMap<FString, FVector2D> Offsets;
	TArray<FString> Order;
	TMap<FString, int32> Ints;

	FString Path() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FN7Hud.json")); }

	void Load()
	{
		if (bLoaded) return;
		bLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path())) return;
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid()) return;
		const TSharedPtr<FJsonObject>* Panels = nullptr;
		if (Root->TryGetObjectField(TEXT("panels"), Panels) && Panels)
			for (const auto& KV : (*Panels)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
				if (KV.Value->TryGetArray(XY) && XY && XY->Num() == 2)
					Offsets.Add(FString(KV.Key), FVector2D((*XY)[0]->AsNumber(), (*XY)[1]->AsNumber()));
			}
		const TArray<TSharedPtr<FJsonValue>>* Powers = nullptr;
		if (Root->TryGetArrayField(TEXT("powers"), Powers) && Powers)
			for (const TSharedPtr<FJsonValue>& V : *Powers) Order.Add(V->AsString());
		const TSharedPtr<FJsonObject>* IntsObj = nullptr;
		if (Root->TryGetObjectField(TEXT("ints"), IntsObj) && IntsObj)
			for (const auto& KV : (*IntsObj)->Values) Ints.Add(FString(KV.Key), (int32)KV.Value->AsNumber());
	}

	void Save()
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Panels = MakeShared<FJsonObject>();
		for (const auto& KV : Offsets)
			Panels->SetArrayField(KV.Key, { MakeShared<FJsonValueNumber>(KV.Value.X), MakeShared<FJsonValueNumber>(KV.Value.Y) });
		Root->SetObjectField(TEXT("panels"), Panels);
		TArray<TSharedPtr<FJsonValue>> Powers;
		for (const FString& S : Order) Powers.Add(MakeShared<FJsonValueString>(S));
		Root->SetArrayField(TEXT("powers"), Powers);
		TSharedRef<FJsonObject> IntsObj = MakeShared<FJsonObject>();
		for (const auto& KV : Ints) IntsObj->SetNumberField(KV.Key, KV.Value);
		Root->SetObjectField(TEXT("ints"), IntsObj);
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, W);
		FFileHelper::SaveStringToFile(Out, *Path());
	}
}

FVector2D FN7HudPrefs::GetPanelOffset(const FString& Id)
{
	FN7HudPrefsState::Load();
	const FVector2D* O = FN7HudPrefsState::Offsets.Find(Id);
	return O ? *O : FVector2D::ZeroVector;
}

void FN7HudPrefs::SetPanelOffset(const FString& Id, const FVector2D& Offset)
{
	FN7HudPrefsState::Load();
	FN7HudPrefsState::Offsets.Add(Id, Offset);
	FN7HudPrefsState::Save();
}

const TArray<FString>& FN7HudPrefs::GetPowerOrder()
{
	FN7HudPrefsState::Load();
	return FN7HudPrefsState::Order;
}

void FN7HudPrefs::SetPowerOrder(const TArray<FString>& Order)
{
	FN7HudPrefsState::Load();
	FN7HudPrefsState::Order = Order;
	FN7HudPrefsState::Save();
}

bool FN7HudPrefs::ReduceFlashing() { return GetInt(TEXT("reduce_flashing"), 0) != 0; }

float FN7HudPrefs::TextScale()
{
	static const float S = FMath::Clamp(GetInt(TEXT("text_scale_pct"), 100) / 100.f, 0.75f, 2.f);
	return S;
}

int32 FN7HudPrefs::GetInt(const FString& Key, int32 Default)
{
	FN7HudPrefsState::Load();
	const int32* V = FN7HudPrefsState::Ints.Find(Key);
	return V ? *V : Default;
}

void FN7HudPrefs::SetInt(const FString& Key, int32 Value)
{
	FN7HudPrefsState::Load();
	FN7HudPrefsState::Ints.Add(Key, Value);
	FN7HudPrefsState::Save();
}
