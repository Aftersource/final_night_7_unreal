#include "Features/FNEncounters.h"
#include "Features/FNCellGeo.h"
#include "FNStudio.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"

namespace FNEncountersImpl
{
	static const FLinearColor Marked(0.55f, 0.25f, 1.f);
	static const FLinearColor LootGold(1.f, 0.78f, 0.25f);

	static FLinearColor TypeColor(const FString& T)
	{
		if (T == TEXT("monster")) return FLinearColor(0.95f, 0.15f, 0.1f);
		if (T == TEXT("treasure")) return FLinearColor(1.f, 0.8f, 0.2f);
		if (T == TEXT("trap")) return FLinearColor(1.f, 0.45f, 0.05f);
		if (T == TEXT("stranger")) return FLinearColor(0.2f, 0.85f, 0.8f);
		if (T == TEXT("undead")) return FLinearColor(0.6f, 0.95f, 0.5f);
		if (T == TEXT("ghost")) return FLinearColor(0.75f, 0.85f, 1.f);
		if (T == TEXT("thief")) return FLinearColor(0.55f, 0.5f, 0.65f);
		return Marked;
	}

	static FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		FString S;
		if (O.IsValid()) O->TryGetStringField(K, S);
		return S;
	}

	static FString JKey(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		double N = 0.0;
		if (O.IsValid() && O->TryGetNumberField(K, N)) return FString::FromInt((int32)N);
		return JStr(O, K);
	}

	// `at` [lon, lat] when the server gives it, else the cell's centre.
	static bool Where(const TSharedPtr<FJsonObject>& O, const FString& Cell, double& Lon, double& Lat)
	{
		const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
		if (O.IsValid() && O->TryGetArrayField(TEXT("at"), At) && At && At->Num() >= 2)
		{
			Lon = (*At)[0]->AsNumber();
			Lat = (*At)[1]->AsNumber();
			return true;
		}
		return FNCellGeo::CellToLonLat(Cell, Lon, Lat);
	}
}

AFNEncounters::AFNEncounters()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFNEncounters::Bind(AFNStudio* InStudio)
{
	Studio = InStudio;
	if (!BaseMat) BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

AFNEncounters::FMark& AFNEncounters::Upsert(const FString& Key, bool bLoot, const FString& Cell, const TSharedPtr<FJsonObject>& Src)
{
	FMark* Found = Marks.Find(Key);
	if (!Found)
	{
		FMark& M = Marks.Add(Key);
		M.bLoot = bLoot;
		M.Base = NewObject<UStaticMeshComponent>(this);
		M.Base->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, bLoot ? TEXT("/Engine/BasicShapes/Cube.Cube") : TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
		M.Base->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M.Base->SetCastShadow(false);
		M.Base->SetMobility(EComponentMobility::Movable);
		M.Base->SetupAttachment(RootComponent);
		M.Base->RegisterComponent();
		M.Text = NewObject<UTextRenderComponent>(this);
		M.Text->SetHorizontalAlignment(EHTA_Center);
		M.Text->SetVerticalAlignment(EVRTA_TextBottom);
		M.Text->SetCastShadow(false);
		M.Text->SetMobility(EComponentMobility::Movable);
		M.Text->SetupAttachment(RootComponent);
		M.Text->RegisterComponent();
		M.Text->SetText(FText::FromString(bLoot ? TEXT("") : TEXT("?")));
		Paint(M, bLoot ? FNEncountersImpl::LootGold : FNEncountersImpl::Marked);
		Found = &M;
	}
	FNEncountersImpl::Where(Src, Cell, Found->Lon, Found->Lat);
	return *Found;
}

void AFNEncounters::Paint(FMark& M, const FLinearColor& Color)
{
	if (M.Base && BaseMat)
	{
		UMaterialInstanceDynamic* Mi = UMaterialInstanceDynamic::Create(BaseMat, M.Base);
		Mi->SetVectorParameterValue(TEXT("Color"), Color);
		M.Base->SetMaterial(0, Mi);
	}
	if (M.Text) M.Text->SetTextRenderColor(Color.ToFColor(true));
}

void AFNEncounters::Reveal(FMark& M, const FString& Type, const FString& Outcome)
{
	if (M.bRevealed) return;
	M.bRevealed = true;
	M.Type = Type;
	M.ShowFor = 9.f;
	M.Age = 0.f;
	Paint(M, FNEncountersImpl::TypeColor(Type));
	const FString Line = Outcome.IsEmpty() ? Type.ToUpper() : Type.ToUpper() + TEXT("\n") + (Outcome.Len() > 48 ? Outcome.Left(46) + TEXT("...") : Outcome);
	if (M.Text) M.Text->SetText(FText::FromString(Line));
	if (AFNStudio* St = Studio.Get()) St->FlashAt(M.Lon, M.Lat);
}

void AFNEncounters::Leave(const FString& Key)
{
	Finished.Add(Key);
	if (FMark* M = Marks.Find(Key)) if (M->Leave < 0.f) M->Leave = 0.f;
}

void AFNEncounters::Apply(const TSharedPtr<FJsonObject>& Payload)
{
	using namespace FNEncountersImpl;
	if (!Payload.IsValid()) return;
	const bool bFullList = Payload->HasField(TEXT("hidden"));   // a game.encounters reply lists every live encounter
	TSet<FString> Seen;
	const TArray<TSharedPtr<FJsonValue>>* Enc = nullptr;
	if (Payload->TryGetArrayField(TEXT("encounters"), Enc) && Enc)
		for (const TSharedPtr<FJsonValue>& V : *Enc)
		{
			const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
			if (!E.IsValid()) continue;
			const FString Key = TEXT("e:") + JKey(E, TEXT("id"));
			Seen.Add(Key);
			if (Finished.Contains(Key)) continue;
			bool bRev = false;
			E->TryGetBoolField(TEXT("revealed"), bRev);
			if (bRev && !Marks.Contains(Key)) { Finished.Add(Key); continue; }   // revealed before we were watching: history, not news
			FMark& M = Upsert(Key, false, JStr(E, TEXT("cell")), E);
			if (bRev && !M.bRevealed)
			{
				const TSharedPtr<FJsonObject>* Out = nullptr;
				const FString Text = E->TryGetObjectField(TEXT("outcome"), Out) && Out ? JStr(*Out, TEXT("text")) : FString();
				Reveal(M, JStr(E, TEXT("type")), Text);
			}
		}
	if (bFullList)
		for (const TPair<FString, FMark>& KV : Marks)
			if (!KV.Value.bLoot && !Seen.Contains(KV.Key) && !KV.Value.bRevealed) Leave(KV.Key);

	const TArray<TSharedPtr<FJsonValue>>* Loot = nullptr;
	if (Payload->TryGetArrayField(TEXT("loot"), Loot) && Loot)
	{
		TMap<FString, TArray<FString>> Icons;
		for (const TSharedPtr<FJsonValue>& V : *Loot)
		{
			const TSharedPtr<FJsonObject> L = V.IsValid() ? V->AsObject() : nullptr;
			if (!L.IsValid()) continue;
			FString Pile = JKey(L, TEXT("pile"));
			if (Pile.IsEmpty()) Pile = JStr(L, TEXT("cell"));
			if (Finished.Contains(TEXT("l:") + Pile)) continue;
			Upsert(TEXT("l:") + Pile, true, JStr(L, TEXT("cell")), L);
			Icons.FindOrAdd(Pile).AddUnique(JStr(L, TEXT("icon")));
		}
		for (const TPair<FString, TArray<FString>>& KV : Icons)
			if (FMark* M = Marks.Find(TEXT("l:") + KV.Key)) if (M->Text) M->Text->SetText(FText::FromString(FString::Join(KV.Value, TEXT(" + "))));
	}
}

void AFNEncounters::ApplyEvent(const TSharedPtr<FJsonObject>& Event)
{
	using namespace FNEncountersImpl;
	const FString Kind = JStr(Event, TEXT("event"));
	if (Kind == TEXT("encounter.marked"))
		Upsert(TEXT("e:") + JKey(Event, TEXT("id")), false, JStr(Event, TEXT("cell")), Event);
	else if (Kind == TEXT("encounter.revealed") && !Finished.Contains(TEXT("e:") + JKey(Event, TEXT("id"))))
		Reveal(Upsert(TEXT("e:") + JKey(Event, TEXT("id")), false, JStr(Event, TEXT("cell")), Event), JStr(Event, TEXT("type")), JStr(Event, TEXT("text")));
	else if (Kind == TEXT("encounter.faded"))
		Leave(TEXT("e:") + JKey(Event, TEXT("id")));
	else if (Kind == TEXT("loot.dropped"))
	{
		Finished.Remove(TEXT("l:") + JKey(Event, TEXT("pile")));
		FMark& M = Upsert(TEXT("l:") + JKey(Event, TEXT("pile")), true, JStr(Event, TEXT("cell")), Event);
		TArray<FString> Icons;
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Event->TryGetArrayField(TEXT("items"), Items) && Items)
			for (const TSharedPtr<FJsonValue>& V : *Items) if (V.IsValid() && V->AsObject().IsValid()) Icons.AddUnique(JStr(V->AsObject(), TEXT("icon")));
		if (M.Text) M.Text->SetText(FText::FromString(FString::Join(Icons, TEXT(" + "))));
		if (AFNStudio* St = Studio.Get()) St->FlashAt(M.Lon, M.Lat);
	}
	else if (Kind == TEXT("loot.taken"))
		Leave(TEXT("l:") + JKey(Event, TEXT("pile")));
}

void AFNEncounters::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AFNStudio* St = Studio.Get();
	if (!St || Marks.Num() == 0) return;
	FVector RimC;
	double RimR = 100.0;
	FRotator TableRot;
	St->GetBoardRim(RimC, RimR, TableRot);
	const FVector Up = St->GetActorUpVector();
	const float Size = (float)RimR * 0.06f;   // a marker is ~6% of the board's radius, readable at any zoom
	const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector CamLoc = Cam ? Cam->GetCameraLocation() : RimC + Up * 500.f;

	TArray<FString> Gone;
	for (TPair<FString, FMark>& KV : Marks)
	{
		FMark& M = KV.Value;
		M.Age += DeltaSeconds;
		if (M.bRevealed && M.Leave < 0.f && M.Age > M.ShowFor) { M.Leave = 0.f; Finished.Add(KV.Key); }
		float Scale = FMath::Min(1.f, M.Age / 0.35f);   // pops in
		float Lift = 0.f;
		if (M.Leave >= 0.f)
		{
			M.Leave += DeltaSeconds;
			const float A = FMath::Clamp(M.Leave / 0.9f, 0.f, 1.f);
			Scale *= 1.f - A;
			Lift = M.bLoot ? A * Size * 3.f : -A * Size * 0.5f;   // loot is lifted away; an encounter sinks
			if (A >= 1.f) { Gone.Add(KV.Key); continue; }
		}
		FVector W;
		const bool bOn = St->BoardPointFor(M.Lat, M.Lon, W) && Scale > 0.01f;
		M.Base->SetVisibility(bOn);
		M.Text->SetVisibility(bOn);
		if (!bOn) continue;
		const float Pulse = M.bRevealed || M.bLoot ? 1.f : 0.9f + 0.1f * FMath::Sin(M.Age * 3.f);
		if (M.bLoot)
		{
			M.Base->SetWorldLocation(W + Up * (Size * 0.35f + Lift + Size * 0.08f * FMath::Sin(M.Age * 2.f)));
			M.Base->SetWorldRotation(TableRot + FRotator(45.f, M.Age * 60.f, 45.f));
			M.Base->SetWorldScale3D(FVector(Size * 0.35f * Scale / 100.f));
		}
		else
		{
			M.Base->SetWorldLocation(W + Up * (Size * 0.03f + Lift));
			M.Base->SetWorldRotation(TableRot);
			M.Base->SetWorldScale3D(FVector(Size * Pulse * Scale / 100.f, Size * Pulse * Scale / 100.f, Size * 0.04f / 100.f));
		}
		const FVector TextAt = W + Up * (Size * (M.bLoot ? 0.8f : 0.5f) + Lift);
		M.Text->SetWorldLocation(TextAt);
		M.Text->SetWorldRotation(FRotationMatrix::MakeFromXZ(CamLoc - TextAt, Up).Rotator());
		M.Text->SetWorldSize(Size * (M.bRevealed ? 0.45f : M.bLoot ? 0.35f : 0.9f) * FMath::Max(Scale, 0.01f));
	}
	for (const FString& K : Gone)
	{
		if (FMark* M = Marks.Find(K))
		{
			if (M->Base) M->Base->DestroyComponent();
			if (M->Text) M->Text->DestroyComponent();
		}
		Marks.Remove(K);
	}
}
