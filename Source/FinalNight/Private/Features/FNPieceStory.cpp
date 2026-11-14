#include "Features/FNPieceStory.h"
#include "FNStudio.h"
#include "../../UI/FNTableGods.h"
#include "../../UI/FNStoryDecorable.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace FNPieceStoryImpl
{
	static FString ToText(const TSharedPtr<FJsonObject>& O)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(O.ToSharedRef(), W);
		return Out;
	}

	// The server's story -> what the kit figure reads (followers as a small count, wardrobe_stage at the top level).
	static TSharedPtr<FJsonObject> Translate(const TSharedPtr<FJsonObject>& S)
	{
		TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
		T->Values = S->Values;   // items, scars, tales, deeds pass through
		double F = 0.0;
		if (S->TryGetNumberField(TEXT("followers"), F))
			T->SetNumberField(TEXT("followers"), FMath::Clamp(FMath::RoundToInt(FMath::LogX(10.0, FMath::Max(1.0, F)) * 2.0), 0, 10));
		const TSharedPtr<FJsonObject>* W = nullptr;
		if (S->TryGetObjectField(TEXT("wardrobe"), W) && W && W->IsValid())
		{
			double Stage = 0.0;
			(*W)->TryGetNumberField(TEXT("stage"), Stage);
			T->SetNumberField(TEXT("wardrobe_stage"), Stage);
			FString Cond;
			if ((*W)->TryGetStringField(TEXT("condition"), Cond)) T->SetStringField(TEXT("wardrobe_condition"), Cond);
		}
		return T;
	}
}

AFNPieceStory::AFNPieceStory()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFNPieceStory::Bind(AFNStudio* InStudio) { Studio = InStudio; }

void AFNPieceStory::Apply(const TSharedPtr<FJsonObject>& Payload)
{
	using namespace FNPieceStoryImpl;
	const TArray<TSharedPtr<FJsonValue>>* Dg = nullptr;
	if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("demigods"), Dg) || !Dg) return;
	for (const TSharedPtr<FJsonValue>& V : *Dg)
	{
		const TSharedPtr<FJsonObject> D = V.IsValid() ? V->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* S = nullptr;
		FString Ref;
		if (!D.IsValid() || !D->TryGetStringField(TEXT("ref"), Ref) || !D->TryGetObjectField(TEXT("story"), S) || !S || !S->IsValid()) continue;
		const FString Raw = ToText(*S);
		if (const FString* Was = Applied.Find(Ref)) if (*Was == Raw) continue;
		Applied.Add(Ref, Raw);
		Pending.Add(Ref, Translate(*S));
	}
	TryApply();
}

void AFNPieceStory::TryApply()
{
	if (Pending.Num() == 0) return;
	AFNTableGods* Gods = AFNTableGods::Get(Studio.Get(), false);
	if (!Gods) return;
	TArray<FString> Done;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& KV : Pending)
		if (AFNDemigodFigure* Fig = Gods->FindFigure(KV.Key))
		{
			static_cast<IFNStoryDecorable*>(Fig)->ApplyStory(KV.Value);
			Done.Add(KV.Key);
		}
	for (const FString& K : Done) Pending.Remove(K);
}

void AFNPieceStory::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TryApply();   // figures appear a little after the data (the kit builds them from the same messages)
}
