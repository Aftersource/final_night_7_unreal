#include "Features/FNSettlements.h"
#include "Features/FNCellGeo.h"
#include "FNStudio.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"

namespace FNSettlementsImpl
{
	static const int32 HousesAt[8] = { 0, 3, 6, 10, 15, 21, 28, 36 };   // by level: hamlet .. great city .. legendary

	static FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O.IsValid()) O->TryGetStringField(K, S); return S; }
	static double JNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0) { double N = Def; if (O.IsValid()) O->TryGetNumberField(K, N); return N; }
	static TSharedPtr<FJsonObject> JObj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* P = nullptr; return O.IsValid() && O->TryGetObjectField(K, P) && P ? *P : nullptr; }
	static const TArray<TSharedPtr<FJsonValue>>* JArr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>* A = nullptr; return O.IsValid() && O->TryGetArrayField(K, A) ? A : nullptr; }
	static bool JLonLat(const TSharedPtr<FJsonValue>& V, double& Lon, double& Lat)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!V.IsValid() || !V->TryGetArray(A) || !A || A->Num() < 2) return false;
		Lon = (*A)[0]->AsNumber(); Lat = (*A)[1]->AsNumber();
		return true;
	}
	static float Hash01(int32 A, int32 B) { return (float)((uint32)(A * 73856093 ^ B * 19349663) % 1000u) / 1000.f; }
	static double AngDeg(double Lo1, double La1, double Lo2, double La2)
	{
		const double A = FMath::DegreesToRadians(La1), B = FMath::DegreesToRadians(La2), D = FMath::DegreesToRadians(Lo2 - Lo1);
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Sin(A) * FMath::Sin(B) + FMath::Cos(A) * FMath::Cos(B) * FMath::Cos(D), -1.0, 1.0)));
	}
}

AFNSettlements::AFNSettlements()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

UInstancedStaticMeshComponent* AFNSettlements::MakeIsm(const TCHAR* Name, const TCHAR* Mesh, const FLinearColor& Color)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	return C;
}

void AFNSettlements::Bind(AFNStudio* InStudio, TFunction<void(const FString&, TSharedPtr<FJsonObject>)> InQuery)
{
	Studio = InStudio;
	QueryFn = MoveTemp(InQuery);
	if (!BaseMat) BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Walls) Walls = MakeIsm(TEXT("TownWalls"), TEXT("/Engine/BasicShapes/Cube.Cube"), FLinearColor(0.72f, 0.62f, 0.48f));
	if (!Roofs) Roofs = MakeIsm(TEXT("TownRoofs"), TEXT("/Engine/BasicShapes/Cone.Cone"), FLinearColor(0.55f, 0.16f, 0.1f));
	if (!Dirt) Dirt = MakeIsm(TEXT("RoadDirt"), TEXT("/Engine/BasicShapes/Cube.Cube"), FLinearColor(0.36f, 0.25f, 0.15f));
}

void AFNSettlements::UpsertTown(const FString& Key, const TSharedPtr<FJsonObject>& S, const TSharedPtr<FJsonObject>& Node)
{
	using namespace FNSettlementsImpl;
	if (!S.IsValid() || Key.IsEmpty()) return;
	FTown& T = Towns.FindOrAdd(Key);
	double Lon = T.Lon, Lat = T.Lat;
	if (!FNCellGeo::CellToLonLat(JStr(S, TEXT("center_cell")), Lon, Lat))
		if (const TArray<TSharedPtr<FJsonValue>>* At = JArr(Node, TEXT("at"))) if (At->Num() >= 2) { Lon = (*At)[0]->AsNumber(); Lat = (*At)[1]->AsNumber(); }
	T.Lon = Lon; T.Lat = Lat;
	const int32 Lv = FMath::Clamp((int32)JNum(S, TEXT("level"), JNum(S, TEXT("to"), T.Level)), 1, 7);
	if (Lv != T.Level) T.Pop = 0.f;
	T.Level = Lv;
	if (const TArray<TSharedPtr<FJsonValue>>* C = JArr(S, TEXT("settlement_cells"))) T.Cells = FMath::Max(1, C->Num());
}

void AFNSettlements::UpsertRoad(const TSharedPtr<FJsonObject>& R, bool bNew)
{
	using namespace FNSettlementsImpl;
	const FString A = JStr(R, TEXT("from_cell")), B = JStr(R, TEXT("to_cell"));
	if (A.IsEmpty() || B.IsEmpty()) return;
	const FString Key = A + TEXT(">") + B;
	const bool bHad = Roads.Contains(Key);
	FRoad& Rd = Roads.FindOrAdd(Key);
	bool bPath = false;
	if (const TArray<TSharedPtr<FJsonValue>>* P = JArr(R, TEXT("path"))) if (P->Num() >= 2)
		bPath = JLonLat((*P)[0], Rd.LoA, Rd.LaA) && JLonLat(P->Last(), Rd.LoB, Rd.LaB);
	if (!bPath) { FNCellGeo::CellToLonLat(A, Rd.LoA, Rd.LaA); FNCellGeo::CellToLonLat(B, Rd.LoB, Rd.LaB); }
	Rd.Wear = (float)JNum(R, TEXT("wear"), Rd.Wear);
	Rd.Fade = -1.f;
	if (!bHad) Rd.Grow = bNew ? 0.f : 1.f;   // a road we see form draws itself in; one already there is just there
}

void AFNSettlements::Apply(const TSharedPtr<FJsonObject>& Payload)
{
	using namespace FNSettlementsImpl;
	if (!Payload.IsValid()) return;
	// game.globe: the regions (for asking what's under the board) and every road
	if (const TArray<TSharedPtr<FJsonValue>>* Rg = JArr(Payload, TEXT("regions")))
	{
		TArray<FRegion> Got;
		for (const TSharedPtr<FJsonValue>& V : *Rg)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			FRegion G; G.Ref = JStr(O, TEXT("ref"));
			const TSharedPtr<FJsonValue> C = O.IsValid() ? O->TryGetField(TEXT("center")) : nullptr;
			if (!G.Ref.IsEmpty() && JLonLat(C, G.Lon, G.Lat)) Got.Add(G);
		}
		if (Got.Num() > 0) { Regions = MoveTemp(Got); AskRegionsUnderBoard(false); }
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Rd = JArr(Payload, TEXT("roads")))
	{
		const bool bAll = Payload->HasField(TEXT("arena"));   // game.globe lists every road: the rest have faded
		TSet<FString> Seen;
		for (const TSharedPtr<FJsonValue>& V : *Rd)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			UpsertRoad(O, false);
			Seen.Add(JStr(O, TEXT("from_cell")) + TEXT(">") + JStr(O, TEXT("to_cell")));
		}
		if (bAll) for (TPair<FString, FRoad>& KV : Roads) if (!Seen.Contains(KV.Key) && KV.Value.Fade < 0.f) KV.Value.Fade = 0.f;
	}
	// game.region: its cities; game.node: the node itself and its children
	for (const TCHAR* ListKey : { TEXT("cities"), TEXT("children") })
		if (const TArray<TSharedPtr<FJsonValue>>* L = JArr(Payload, ListKey))
			for (const TSharedPtr<FJsonValue>& V : *L)
			{
				const TSharedPtr<FJsonObject> N = V.IsValid() ? V->AsObject() : nullptr;
				if (const TSharedPtr<FJsonObject> S = JObj(N, TEXT("settlement"))) UpsertTown(JStr(N, TEXT("ref")), S, N);
			}
	if (const TSharedPtr<FJsonObject> S = JObj(Payload, TEXT("settlement"))) UpsertTown(JStr(Payload, TEXT("ref")), S, Payload);
	if (const TSharedPtr<FJsonObject> N = JObj(Payload, TEXT("node"))) if (const TSharedPtr<FJsonObject> S = JObj(N, TEXT("settlement"))) UpsertTown(JStr(N, TEXT("ref")), S, N);
}

void AFNSettlements::ApplyEvent(const TSharedPtr<FJsonObject>& E)
{
	using namespace FNSettlementsImpl;
	const FString Kind = JStr(E, TEXT("event"));
	if (Kind == TEXT("settlement.grew") || Kind == TEXT("settlement.shrank"))
	{
		UpsertTown(JStr(E, TEXT("place")), E, E);   // the event carries to, center_cell, settlement_cells
		if (FTown* T = Towns.Find(JStr(E, TEXT("place")))) { T->Pop = 0.f; if (AFNStudio* St = Studio.Get()) St->FlashAt(T->Lon, T->Lat); }
	}
	else if (Kind == TEXT("road.formed")) UpsertRoad(E, true);
	else if (Kind == TEXT("road.faded"))
	{
		if (FRoad* R = Roads.Find(JStr(E, TEXT("from_cell")) + TEXT(">") + JStr(E, TEXT("to_cell")))) if (R->Fade < 0.f) R->Fade = 0.f;
	}
}

void AFNSettlements::Refresh()
{
	if (!QueryFn) return;
	TSharedPtr<FJsonObject> G = MakeShared<FJsonObject>();
	G->SetNumberField(TEXT("limit"), 1);
	QueryFn(TEXT("game.globe"), G);
	AskRegionsUnderBoard(true);
}

void AFNSettlements::AskRegionsUnderBoard(bool bForce)
{
	using namespace FNSettlementsImpl;
	AFNStudio* St = Studio.Get();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!St || !PC || !QueryFn || Regions.Num() == 0) return;
	if (!bForce && SinceAsk < 3.f) return;
	FVector RimC; double RimR = 0.0; FRotator Rot;
	St->GetBoardRim(RimC, RimR, Rot);
	FVector2D Px;
	double Lat = 0.0, Lon = 0.0, HexM = 0.0;
	FVector Wc;
	if (!PC->ProjectWorldLocationToScreen(RimC, Px) || !St->HexAtScreen(Px, Lat, Lon, Wc, HexM)) return;
	TArray<TPair<double, FString>> Near;
	for (const FRegion& G : Regions) Near.Emplace(AngDeg(Lon, Lat, G.Lon, G.Lat), G.Ref);
	Near.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B) { return A.Key < B.Key; });
	TArray<FString> Want;
	for (int32 i = 0; i < Near.Num() && i < 2; ++i) Want.Add(Near[i].Value);
	if (!bForce && Want == Asked) return;
	Asked = Want;
	SinceAsk = 0.f;
	for (const FString& Ref : Want)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("ref"), Ref);
		A->SetNumberField(TEXT("limit"), 1);
		QueryFn(TEXT("game.region"), A);
	}
}

void AFNSettlements::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SinceAsk += DeltaSeconds;
	SinceDraw += DeltaSeconds;
	bool bAnim = false;
	for (TPair<FString, FTown>& KV : Towns) if (KV.Value.Pop < 1.f) { KV.Value.Pop = FMath::Min(1.f, KV.Value.Pop + DeltaSeconds / 0.7f); bAnim = true; }
	TArray<FString> Gone;
	for (TPair<FString, FRoad>& KV : Roads)
	{
		FRoad& R = KV.Value;
		if (R.Grow < 1.f) { R.Grow = FMath::Min(1.f, R.Grow + DeltaSeconds / 2.5f); bAnim = true; }
		if (R.Fade >= 0.f) { R.Fade += DeltaSeconds / 1.5f; bAnim = true; if (R.Fade >= 1.f) Gone.Add(KV.Key); }
	}
	for (const FString& K : Gone) Roads.Remove(K);
	if (SinceAsk > 3.f) AskRegionsUnderBoard(false);   // the board moved to another region: ask for its towns
	if (bAnim || SinceDraw > 0.25f) { SinceDraw = 0.f; Draw(); }
}

void AFNSettlements::Draw()
{
	using namespace FNSettlementsImpl;
	AFNStudio* St = Studio.Get();
	if (!St || !Walls || !Roofs || !Dirt) return;
	FVector RimC; double RimR = 100.0; FRotator Rot;
	St->GetBoardRim(RimC, RimR, Rot);
	const FVector Up = St->GetActorUpVector(), Fwd = St->GetActorForwardVector(), Right = St->GetActorRightVector();
	const float Size = (float)RimR * 0.06f;
	TArray<FTransform> W, R, D;

	for (const TPair<FString, FTown>& KV : Towns)
	{
		const FTown& T = KV.Value;
		FVector C;
		if (!St->BoardPointFor(T.Lat, T.Lon, C)) continue;
		const float Pop = T.Pop >= 1.f ? 1.f : FMath::Sin(T.Pop * PI * 0.5f) * (1.f + 0.25f * FMath::Sin(T.Pop * PI));   // a little overshoot
		const int32 N = FMath::Min(HousesAt[T.Level], 8 + T.Cells);
		const float House = Size * 0.16f;
		const int32 Seed = GetTypeHash(KV.Key);
		for (int32 i = 0; i < N; ++i)
		{
			const float Ang = i * 2.39996f + Hash01(Seed, i) * 0.4f;      // golden-angle spiral
			const float Rad = Size * 0.17f * FMath::Sqrt(i + 0.6f);
			const float H = House * (0.8f + 0.18f * T.Level) * (0.75f + 0.5f * Hash01(Seed, i + 99));
			const FVector At = C + (Fwd * FMath::Cos(Ang) + Right * FMath::Sin(Ang)) * Rad * Pop;
			const FQuat Q = FQuat(Up, Hash01(Seed, i + 7) * PI) * Rot.Quaternion();
			W.Add(FTransform(Q, At + Up * (H * 0.5f * Pop), FVector(House, House, H) * Pop / 100.f));
			R.Add(FTransform(Q, At + Up * ((H + House * 0.3f) * Pop), FVector(House * 1.25f, House * 1.25f, House * 0.6f) * Pop / 100.f));
		}
		if (T.Level >= 3)   // a keep in the middle for a town and up
		{
			const float K = House * 1.3f, KH = House * (1.2f + 0.6f * T.Level);
			W.Add(FTransform(Rot.Quaternion(), C + Up * (KH * 0.5f * Pop), FVector(K, K, KH) * Pop / 100.f));
			R.Add(FTransform(Rot.Quaternion(), C + Up * ((KH + K * 0.4f) * Pop), FVector(K * 1.2f, K * 1.2f, K * 0.9f) * Pop / 100.f));
		}
	}

	for (const TPair<FString, FRoad>& KV : Roads)
	{
		const FRoad& Rd = KV.Value;
		const float Fade = Rd.Fade >= 0.f ? 1.f - Rd.Fade : 1.f;
		const float Width = Size * 0.07f * (0.6f + FMath::Min(Rd.Wear, 10.f) / 10.f) * Fade;
		const int32 Dabs = 24;
		FVector Prev;
		bool bPrev = St->BoardPointFor(Rd.LaA, Rd.LoA, Prev);
		for (int32 k = 1; k <= Dabs; ++k)
		{
			const float T = (float)k / Dabs;
			if (T > Rd.Grow + 1e-3f) break;
			FVector P;
			const bool bOn = St->BoardPointFor(FMath::Lerp(Rd.LaA, Rd.LaB, (double)T), FMath::Lerp(Rd.LoA, Rd.LoB, (double)T), P);
			if (bOn && bPrev)
			{
				const FVector Dir = (P - Prev).GetSafeNormal();
				const float Len = FMath::Max(1.f, (float)FVector::Dist(P, Prev) * 0.7f);
				const FQuat Q = FRotationMatrix::MakeFromXZ(Dir.IsNearlyZero() ? Fwd : Dir, Up).ToQuat();
				D.Add(FTransform(Q, (P + Prev) * 0.5f + Up * (Size * 0.01f), FVector(Len, Width, Size * 0.01f) / 100.f));
			}
			Prev = P; bPrev = bOn;
		}
	}

	Walls->ClearInstances(); Roofs->ClearInstances(); Dirt->ClearInstances();
	if (W.Num()) Walls->AddInstances(W, false, true);
	if (R.Num()) Roofs->AddInstances(R, false, true);
	if (D.Num()) Dirt->AddInstances(D, false, true);
}
