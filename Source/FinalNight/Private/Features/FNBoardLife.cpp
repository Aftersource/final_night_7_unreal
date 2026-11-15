#include "Features/FNBoardLife.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

namespace FNBoardLifeImpl
{
	static constexpr int32 HealStart = 6, HealEnd = 12;    // beats: full damage, then it shrinks away
	static constexpr int32 FreshBeats = 2;                 // a fire burns this long, then it is ash and stumps
	static constexpr double LevelHexM[] = { 3.0, 30.0, 3000.0, 120000.0 };
	static constexpr int32 Rings = 11;                     // the board shows about this many rings round its centre

	/** Which kind of lasting damage an event leaves, if any. */
	bool KindOf(const FString& Kind, const FString& Text, uint8& Out)
	{
		for (const TCHAR* K : { TEXT("migration"), TEXT("cult_founded"), TEXT("host_raised"), TEXT("general_appointed"), TEXT("ringleader_emerged"), TEXT("order_received"), TEXT("manifest") })
			if (Kind == K) return false;   // people moving, organising: no damage to the land
		const FString S = (Kind + TEXT(" ") + Text).ToLower();
		for (const TCHAR* W : { TEXT("flood"), TEXT("tidal"), TEXT("tsunami"), TEXT("drown") }) if (S.Contains(W)) { Out = 1; return true; }
		for (const TCHAR* W : { TEXT("fire"), TEXT("burn"), TEXT("lava"), TEXT("volcan"), TEXT("meteor") }) if (S.Contains(W)) { Out = 0; return true; }
		// ruin goes by what happened (the kind), not the words: "revenge for the riot" ruins nothing
		const FString K = Kind.ToLower();
		for (const TCHAR* W : { TEXT("battle"), TEXT("storm"), TEXT("hurricane"), TEXT("quake"), TEXT("crater"), TEXT("riot"), TEXT("loot"), TEXT("siege"), TEXT("sack") }) if (K.Contains(W)) { Out = 2; return true; }
		for (const TCHAR* W : { TEXT("storm"), TEXT("hurricane"), TEXT("quake"), TEXT("crater") }) if (S.Contains(W) && K == TEXT("cast")) { Out = 2; return true; }
		return false;
	}

	double Meters(double La1, double Lo1, double La2, double Lo2)
	{
		const double A = FMath::DegreesToRadians(La1), B = FMath::DegreesToRadians(La2);
		return 6371000.0 * FMath::Acos(FMath::Clamp(FMath::Sin(A) * FMath::Sin(B) + FMath::Cos(A) * FMath::Cos(B) * FMath::Cos(FMath::DegreesToRadians(Lo2 - Lo1)), -1.0, 1.0));
	}

	float Dice(int32 Q, int32 R, int32 Salt) { return FMath::Frac(FMath::Sin((float)(Q * 127 + R * 311 + Salt * 73)) * 43758.5453f); }
}

AFNBoardLife::AFNBoardLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AFNBoardLife::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

UInstancedStaticMeshComponent* AFNBoardLife::Make(const TCHAR* Mesh, const FColor& C, const TCHAR* Name)
{
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this, Name);
	I->SetupAttachment(RootComponent);
	I->SetMobility(EComponentMobility::Movable);
	I->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	I->SetCastShadow(false);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, I);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(C));
		I->SetMaterial(0, M);
	}
	I->RegisterComponent();
	return I;
}

void AFNBoardLife::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	using namespace FNBoardLifeImpl;
	if (Type == TEXT("match.started") || Type == TEXT("room.created")) { Zones.Reset(); return; }
	if (Type != TEXT("round.outcome") || !D.IsValid()) return;
	const TSharedPtr<FJsonObject>* Pub = nullptr;
	if (!D->TryGetObjectField(TEXT("public"), Pub) || !Pub) return;
	int32 B = Beat + 1;
	(*Pub)->TryGetNumberField(TEXT("beat"), B);
	if (B < Beat) Zones.Reset();   // a new match began
	Beat = B;
	const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
	if (!(*Pub)->TryGetArrayField(TEXT("events"), Ev) || !Ev) return;
	for (const TSharedPtr<FJsonValue>& V : *Ev)
	{
		const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
		if (!E.IsValid() || !E->TryGetArrayField(TEXT("at"), At) || !At || At->Num() < 2) continue;
		uint8 K = 0;
		if (!KindOf(E->GetStringField(TEXT("kind")), E->GetStringField(TEXT("text")), K)) continue;
		double Mag = 5.0; E->TryGetNumberField(TEXT("magnitude"), Mag);
		FZone Z;
		Z.Lon = (*At)[0]->AsNumber(); Z.Lat = (*At)[1]->AsNumber();
		Z.Kind = (EKind)K;
		Z.RadiusM = FMath::Clamp(Mag, 1.0, 10.0) * (K == 0 ? 8000.0 : K == 1 ? 10000.0 : 4000.0);
		Z.Beat = Beat;
		Zones.Add(Z);
		UE_LOG(LogTemp, Display, TEXT("FNBoardLife: %s scars %.2f,%.2f (%.0f km) at beat %d [%s: %s]"), K == 0 ? TEXT("fire") : K == 1 ? TEXT("flood") : TEXT("ruin"), Z.Lat, Z.Lon, Z.RadiusM / 1000.0, Beat, *E->GetStringField(TEXT("kind")), *E->GetStringField(TEXT("text")).Left(80));
	}
	if (Zones.Num() > 64) Zones.RemoveAt(0, Zones.Num() - 64);
}

void AFNBoardLife::Tick(float DeltaSeconds)
{
	using namespace FNBoardLifeImpl;
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	SinceDraw += DeltaSeconds;
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNBoardLife::OnMessage); break; }
	if (SinceDraw < 0.1f) return;
	SinceDraw = 0.f;
	if (!Flame)
	{
		Flame = Make(TEXT("/Engine/BasicShapes/Cone.Cone"), FColor(255, 110, 20), TEXT("LifeFlame"));
		Stump = Make(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FColor(40, 28, 20), TEXT("LifeStump"));
		Ash = Make(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FColor(70, 66, 62), TEXT("LifeAsh"));
		Water = Make(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FColor(40, 110, 200), TEXT("LifeWater"));
		Rubble = Make(TEXT("/Engine/BasicShapes/Cube.Cube"), FColor(150, 140, 130), TEXT("LifeRubble"));
	}
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;
	int32 Q0 = 0, R0 = 0;
	float T0 = 0.f, T1 = 0.f;
	FVector C0 = FVector::ZeroVector, C1 = FVector::ZeroVector;
	const bool bGrid = St && St->BoardCentreHex(Q0, R0) && St->BoardHexTop(Q0, R0, true, T0, C0) && St->BoardHexTop(Q0 + 1, R0, true, T1, C1);
	const int32 Level = St ? FMath::Clamp(St->GetMapLevel(), 0, 3) : 3;
	const bool bCalm = St && !St->IsZoomAnimating() && !St->IsHandHolding() && !St->IsBoardPanning();   // HAND_OF_GOD §6

	// dev aid: with Config/FN7DevScar.txt present, one fire, one flood and one ruin sit around the board centre at every zoom
	const FString DevKey = FString::Printf(TEXT("%d %d %d %d"), Level, Q0, R0, Zones.Num());
	if (bGrid && DevKey != AppliedFor && FPaths::FileExists(FPaths::ProjectConfigDir() / TEXT("FN7DevScar.txt")))
	{
		Zones.RemoveAll([](const FZone& Z) { return Z.Beat == -1000; });
		const int32 Off[3][2] = { { 0, 3 }, { 4, -3 }, { -4, 0 } };
		for (int32 k = 0; k < 3; ++k)
		{
			FZone Z; Z.Kind = (EKind)k; Z.RadiusM = LevelHexM[Level] * 1.6; Z.Beat = -1000;
			if (St->BoardHexLatLon(Q0 + Off[k][0], R0 + Off[k][1], Z.Lat, Z.Lon)) Zones.Add(Z);
		}
		AppliedFor = FString::Printf(TEXT("%d %d %d %d"), Level, Q0, R0, Zones.Num());
	}
	// how big each zone is now: full, then shrinking as it heals
	Zones.RemoveAll([&](const FZone& Z) { return Z.Beat != -1000 && Beat - Z.Beat >= HealEnd; });
	auto RadiusNow = [&](const FZone& Z)
	{
		const int32 Age = Z.Beat == -1000 ? 0 : Beat - Z.Beat;
		return Age < HealStart ? Z.RadiusM : Z.RadiusM * (1.0 - double(Age - HealStart) / double(HealEnd - HealStart));
	};

	// the damaged hexagons of the board in view: one look per hexagon (the latest zone wins)
	struct FHit { int32 Q, R; EKind Kind; bool bFresh; };
	TArray<FHit> Hits;
	TMap<FString, uint8> Want;
	if (bGrid && Zones.Num() > 0)
	{
		const double HexM = LevelHexM[Level];
		for (int32 K = 0; K <= Rings; ++K)
		{
			static const FIntPoint Dir[6] = { {1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}, {1,-1} };
			FIntPoint H = K == 0 ? FIntPoint(0, 0) : Dir[4] * K;
			for (int32 Side = 0; Side < (K == 0 ? 1 : 6); ++Side)
				for (int32 Step = 0; Step < FMath::Max(K, 1); ++Step)
				{
					const int32 Q = Q0 + H.X, R = R0 + H.Y;
					if (K > 0) H += Dir[Side];
					double La, Lo;
					float TopCm; FVector TopW;
					if (!St->BoardHexLatLon(Q, R, La, Lo) || !St->BoardHexTop(Q, R, true, TopCm, TopW) || TopCm < 1.5f) continue;   // land only (sea sits at 0.6 cm)
					for (int32 z = Zones.Num() - 1; z >= 0; --z)
					{
						const FZone& Z = Zones[z];
						// at least the hexagon it struck, at every zoom
						const double M = Meters(La, Lo, Z.Lat, Z.Lon);
						if (M > FMath::Max(RadiusNow(Z), HexM * 0.55)) continue;
						if (Z.Kind == EKind::Ruin && M > HexM * 0.55 && Dice(Q, R, 99) > 0.45f) continue;   // ruin is patchy: some streets stand
						const bool bFresh = Z.Beat == -1000 ? Q % 2 == 0 : Beat - Z.Beat < FreshBeats;   // dev: half burning, half burnt
						Hits.Add({ Q, R, Z.Kind, bFresh });
						const EFNHexState S = Z.Kind == EKind::Flood ? EFNHexState::Flooded : (Z.Kind == EKind::Fire && bFresh) ? EFNHexState::Burning : EFNHexState::Ash;
						Want.Add(St->BoardCellKey(Q, R), (uint8)S);
						break;
					}
				}
		}
	}

	// tell the board (its trees and houses go from these hexagons), only while nothing moves (§6)
	if (St && bCalm)
	{
		bool bChanged = false;
		for (const TPair<FString, uint8>& It : Want)
		{
			const uint8* Had = Applied.Find(It.Key);
			if (Had && *Had == It.Value) continue;
			St->SetHexOverride(It.Key, NAN, (EFNHexState)It.Value);
			bChanged = true;
		}
		for (const TPair<FString, uint8>& It : Applied)
			if (!Want.Contains(It.Key) && (uint8)St->HexStateAt(It.Key) == It.Value)   // healed (and nobody else changed it since)
			{
				St->SetHexOverride(It.Key, NAN, EFNHexState::Normal);
				bChanged = true;
			}
		if (bChanged) { Applied = Want; St->RebuildBoard(); }
	}

	// what stands on them: flames and stumps, water, rubble
	TArray<FTransform> F, Sp, A, Wt, Rb;
	const float D = bGrid ? FMath::Max(FVector::Dist2D(C0, C1), 1.f) : 10.f;
	for (const FHit& H : Hits)
	{
		float Tp; FVector Top;
		if (!St->BoardHexTop(H.Q, H.R, true, Tp, Top)) continue;
		auto Spot = [&](int32 i) { return Top + FVector((Dice(H.Q, H.R, i) - 0.5f) * D * 0.6f, (Dice(H.Q, H.R, i + 7) - 0.5f) * D * 0.6f, 0.f); };
		switch (H.Kind)
		{
		case EKind::Fire:
			A.Add(FTransform(FRotator::ZeroRotator, Top + FVector(0, 0, D * 0.01f), FVector(D * 0.95f, D * 0.95f, D * 0.02f) / 100.f));
			for (int32 i = 0; i < 3; ++i)
			{
				Sp.Add(FTransform(FRotator::ZeroRotator, Spot(i) + FVector(0, 0, D * 0.07f), FVector(D * 0.07f, D * 0.07f, D * 0.14f) / 100.f));
				if (H.bFresh)
				{
					const float Ht = D * (0.35f + 0.15f * FMath::Sin(Time * 9.f + i * 2.1f + H.Q));
					F.Add(FTransform(FRotator::ZeroRotator, Spot(i) + FVector(0, 0, D * 0.14f + Ht * 0.5f), FVector(D * 0.2f, D * 0.2f, Ht) / 100.f));
				}
			}
			break;
		case EKind::Flood:
			// standing water up to the houses' middles: what is left stands half sunk
			Wt.Add(FTransform(FRotator::ZeroRotator, Top + FVector(0, 0, D * 0.09f + D * 0.02f * FMath::Sin(Time * 1.5f + H.Q + H.R)), FVector(D * 1.05f, D * 1.05f, D * 0.18f) / 100.f));
			break;
		case EKind::Ruin:
			A.Add(FTransform(FRotator::ZeroRotator, Top + FVector(0, 0, D * 0.01f), FVector(D * 0.95f, D * 0.95f, D * 0.02f) / 100.f));
			for (int32 i = 0; i < 4; ++i)
			{
				const float Sz = D * (0.1f + 0.1f * Dice(H.Q, H.R, i + 20));
				Rb.Add(FTransform(FRotator(Dice(H.Q, H.R, i + 30) * 40.f, Dice(H.Q, H.R, i + 40) * 360.f, Dice(H.Q, H.R, i + 50) * 40.f), Spot(i) + FVector(0, 0, Sz * 0.4f), FVector(Sz) / 100.f));
			}
			break;
		}
	}
	auto Fill = [](UInstancedStaticMeshComponent* I, const TArray<FTransform>& T)
	{
		if (T.Num() == 0) { if (I->GetInstanceCount() > 0) I->ClearInstances(); return; }
		if (I->GetInstanceCount() != T.Num()) { I->ClearInstances(); I->AddInstances(T, false, true); }
		else I->BatchUpdateInstancesTransforms(0, T, true, true);
	};
	Fill(Flame, F); Fill(Stump, Sp); Fill(Ash, A); Fill(Water, Wt); Fill(Rubble, Rb);

	const FString Log = FString::Printf(TEXT("%d zones, %d hexagons scarred on the board at level %d"), Zones.Num(), Hits.Num(), Level);
	if (Log != LastLog) { LastLog = Log; UE_LOG(LogTemp, Display, TEXT("FNBoardLife: %s"), *Log); }
}

bool UFNBoardLifeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNBoardLifeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNBoardLife>(AFNBoardLife::StaticClass(), FTransform::Identity, P);
}
