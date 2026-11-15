#include "FNEarthField.h"

#include "FNStudio.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FN7Earth
{
	static const TCHAR* MeshPath = TEXT("/Game/Earth/SM_HexPrism.SM_HexPrism");
	static const TCHAR* MatPath = TEXT("/Game/Earth/M_EarthColumn.M_EarthColumn");
	static constexpr float MeshR = 50.f, MeshH = 100.f;   // the unit column's circumradius and height (cm)
	static constexpr float Lift = 0.6f;                   // above the board's own tiles, so the field reads on top

	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def)
	{
		double V = Def;
		if (O.IsValid() && O->HasTypedField<EJson::Number>(K)) V = O->GetNumberField(K);
		return V;
	}
	static FFNEarthState State(const TSharedPtr<FJsonObject>& O)
	{
		FFNEarthState S;
		S.Height = (float)Num(O, TEXT("height"), 1.0);
		if (O.IsValid()) O->TryGetStringField(TEXT("material"), S.Material);
		S.Water = (float)Num(O, TEXT("water"), 0.0);
		S.Heat = (float)FMath::Max(Num(O, TEXT("heat"), 0.0), FMath::Max(Num(O, TEXT("lava"), 0.0), Num(O, TEXT("fire"), 0.0)));
		return S;
	}
}

AFNEarthField::AFNEarthField()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Columns = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Columns"));
	Columns->SetupAttachment(RootComponent);
	Columns->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Columns->NumCustomDataFloats = 10;   // colour A (3), wet A, heat A, colour B (3), wet B, heat B
	Columns->SetMobility(EComponentMobility::Movable);
}

AFNEarthField* AFNEarthField::Get(UWorld* World, bool bCreate)
{
	if (!World) return nullptr;
	TActorIterator<AFNEarthField> It(World);
	if (It) return *It;
	return bCreate ? World->SpawnActor<AFNEarthField>() : nullptr;
}

FLinearColor AFNEarthField::MaterialColor(const FString& M)
{
	static const TMap<FString, FLinearColor> C = {
		{ TEXT("water"), FLinearColor(0.03f, 0.12f, 0.32f) }, { TEXT("ocean"), FLinearColor(0.02f, 0.08f, 0.25f) },
		{ TEXT("sand"), FLinearColor(0.62f, 0.52f, 0.32f) }, { TEXT("desert"), FLinearColor(0.66f, 0.5f, 0.28f) },
		{ TEXT("grass"), FLinearColor(0.18f, 0.42f, 0.12f) }, { TEXT("forest"), FLinearColor(0.06f, 0.24f, 0.08f) },
		{ TEXT("rock"), FLinearColor(0.32f, 0.3f, 0.28f) }, { TEXT("snow"), FLinearColor(0.88f, 0.9f, 0.94f) },
		{ TEXT("ice"), FLinearColor(0.7f, 0.85f, 0.95f) }, { TEXT("lava"), FLinearColor(0.45f, 0.06f, 0.01f) },
		{ TEXT("ash"), FLinearColor(0.2f, 0.2f, 0.21f) }, { TEXT("mud"), FLinearColor(0.25f, 0.18f, 0.1f) },
		{ TEXT("city"), FLinearColor(0.45f, 0.42f, 0.4f) } };
	const FLinearColor* V = C.Find(M.ToLower());
	return V ? *V : FLinearColor(0.3f, 0.3f, 0.28f);
}

void AFNEarthField::Clear()
{
	if (Columns) Columns->ClearInstances();
	Shown.Reset();
	InstanceOf.Reset();
	LastT = -1.f;
}

void AFNEarthField::SetFrame(const TArray<FFNEarthCell>& Cells, AFNStudio* Studio, float HexRadiusCm)
{
	using namespace FN7Earth;
	Clear();
	if (!Mid)
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MatPath)) Mid = UMaterialInstanceDynamic::Create(M, this);
	if (Columns->GetStaticMesh() == nullptr) Columns->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	if (Mid) Columns->SetMaterial(0, Mid);
	Shown = Cells;
	InstanceOf.Init(-1, Shown.Num());
	const float S = Studio ? (float)Studio->GetActorScale3D().X : 1.f;
	for (FFNEarthCell& C : Shown) C.bOnBoard = Studio && Studio->BoardPointFor(C.Lat, C.Lon, C.World);
	// Column size: as given, or from the spacing of neighbouring cells on the board (the stub's case), so they tile.
	HexR = HexRadiusCm;
	if (HexR <= 0.f)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < FMath::Min(Shown.Num(), 40); ++i)
			for (int32 j = 0; j < Shown.Num(); ++j)
				if (i != j && Shown[i].bOnBoard && Shown[j].bOnBoard)
					Best = FMath::Min(Best, (float)FVector::Dist2D(Shown[i].World, Shown[j].World));
		HexR = (Best < TNumericLimits<float>::Max() ? Best / S : 7.f) / 1.7320508f;
	}
	TArray<FTransform> Xf;
	TArray<int32> Index;
	for (int32 i = 0; i < Shown.Num(); ++i)
	{
		FFNEarthCell& C = Shown[i];
		if (!C.bOnBoard) continue;
		const FVector Scale(HexR * S / MeshR, HexR * S / MeshR, FMath::Max(0.05f, C.A.Height) * S / MeshH);
		Xf.Add(FTransform(FRotator::ZeroRotator, C.World + FVector(0, 0, Lift * S), Scale));
		Index.Add(i);
	}
	const TArray<int32> Added = Columns->AddInstances(Xf, /*bShouldReturnIndices*/ true, /*bWorldSpace*/ true);
	for (int32 k = 0; k < Index.Num(); ++k)
	{
		InstanceOf[Index[k]] = Added.IsValidIndex(k) ? Added[k] : k;
		const FFNEarthCell& C = Shown[Index[k]];
		const FLinearColor Ca = MaterialColor(C.A.Material), Cb = MaterialColor(C.B.Material);
		const float Data[10] = { Ca.R, Ca.G, Ca.B, C.A.Water, C.A.Heat, Cb.R, Cb.G, Cb.B, C.B.Water, C.B.Heat };
		Columns->SetCustomData(InstanceOf[Index[k]], TArrayView<const float>(Data, 10), false);
	}
	Columns->MarkRenderStateDirty();
	LastT = -1.f;
	ApplyT(0.f);
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: frame of %d cells, %d on the board (beats %d -> %d)"), Shown.Num(), Index.Num(), Time.BeatA, Time.BeatB);
}

bool AFNEarthField::LoadStub(const FString& Path, AFNStudio* Studio)
{
	FString Raw;
	TSharedPtr<FJsonObject> O;
	if (!FFileHelper::LoadFileToString(Raw, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), O) || !O.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FN7 earth: no stub at %s"), *Path);
		return false;
	}
	TArray<FFNEarthCell> Cells;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (O->TryGetArrayField(TEXT("cells"), Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> C = V.IsValid() ? V->AsObject() : nullptr;
			if (!C.IsValid()) continue;
			FFNEarthCell Cell;
			C->TryGetStringField(TEXT("key"), Cell.Key);
			Cell.Lat = FN7Earth::Num(C, TEXT("lat"), 0.0);
			Cell.Lon = FN7Earth::Num(C, TEXT("lon"), 0.0);
			const TSharedPtr<FJsonObject>* A = nullptr; const TSharedPtr<FJsonObject>* B = nullptr;
			C->TryGetObjectField(TEXT("a"), A);
			C->TryGetObjectField(TEXT("b"), B);
			Cell.A = FN7Earth::State(A ? *A : nullptr);
			Cell.B = FN7Earth::State(B ? *B : (A ? *A : nullptr));
			Cells.Add(Cell);
		}
	Time = FFNEarthTime();
	Time.Mode = FFNEarthTime::EMode::Scrub;
	Time.bLoop = true;
	Time.BeatA = (int32)FN7Earth::Num(O, TEXT("beat_a"), 0.0);
	Time.BeatB = (int32)FN7Earth::Num(O, TEXT("beat_b"), 1.0);
	StubBeatS = (float)FN7Earth::Num(O, TEXT("beat_ms"), 6000.0) / 1000.f;
	StubStart = FPlatformTime::Seconds();
	SetFrame(Cells, Studio, 0.f);   // 0: size the columns from the cells' spacing
	return true;
}

float AFNEarthField::CurrentT() const
{
	switch (Time.Mode)
	{
	case FFNEarthTime::EMode::Live:
	{
		const double Now = ServerNowMs ? ServerNowMs() : 0.0;
		if (Time.EndMs <= Time.StartMs) return 1.f;
		return (float)FMath::Clamp((Now - Time.StartMs) / (Time.EndMs - Time.StartMs), 0.0, 1.0);
	}
	case FFNEarthTime::EMode::Scrub:
		if (Time.bLoop)
		{
			// The stub: A -> B over a beat, hold a second, back to A, again.
			const float P = FMath::Fmod((float)(FPlatformTime::Seconds() - StubStart), StubBeatS + 2.f);
			return FMath::Clamp(P / StubBeatS, 0.f, 1.f);
		}
		return FMath::Clamp(Time.ScrubT, 0.f, 1.f);
	case FFNEarthTime::EMode::Reconnect:
	default:
		return 1.f;
	}
}

void AFNEarthField::ApplyT(float T)
{
	if (FMath::IsNearlyEqual(T, LastT, 0.002f)) return;
	LastT = T;
	if (Mid) Mid->SetScalarParameterValue(TEXT("BeatT"), T);
	// Heights blend here (the GPU blends the colours): one batched transform update.
	const float Ease = T * T * (3.f - 2.f * T);
	for (int32 i = 0; i < Shown.Num(); ++i)
	{
		const int32 Inst = InstanceOf.IsValidIndex(i) ? InstanceOf[i] : -1;
		if (Inst < 0) continue;
		const FFNEarthCell& C = Shown[i];
		FTransform X;
		if (!Columns->GetInstanceTransform(Inst, X, true)) continue;
		const float H = FMath::Lerp(C.A.Height, C.B.Height, Ease);
		FVector S = X.GetScale3D();
		S.Z = FMath::Max(0.05f, H) * (S.X * FN7Earth::MeshR / HexR) / FN7Earth::MeshH;
		X.SetScale3D(S);
		Columns->UpdateInstanceTransform(Inst, X, true, false, false);
	}
	Columns->MarkRenderStateDirty();
}

void AFNEarthField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Shown.Num() > 0) ApplyT(CurrentT());
}

// Development: play the stub frame file on the board (tools/earth_stub/earth_frames.json), or clear it.
static FAutoConsoleCommandWithWorldAndArgs GFN7EarthStub(TEXT("FN7.EarthStub"),
	TEXT("FN7.EarthStub [off|path]: play an earth-field stub on the board (A -> B, looping)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AFNEarthField* F = AFNEarthField::Get(World, true);
		if (!F) return;
		if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)) { F->Clear(); return; }
		TActorIterator<AFNStudio> It(World);
		const FString Path = Args.Num() > 0 ? Args[0] : FPaths::ProjectDir() / TEXT("tools/earth_stub/earth_frames.json");
		F->LoadStub(Path, It ? *It : nullptr);
	}));
