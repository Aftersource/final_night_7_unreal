#include "FNEarthPlay.h"

#include "FNNetClient.h"
#include "FNStudio.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Base64.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace FN7EarthPlay
{
	static const TCHAR* HexMesh = TEXT("/Game/Earth/SM_HexPrism.SM_HexPrism");
	static const TCHAR* ColumnMat = TEXT("/Game/Earth/M_EarthColumn.M_EarthColumn");
	static const TCHAR* BaseMatPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static constexpr float CmPerLevel = 1.5f;      // one of the sim's 16 height levels on the table
	static constexpr float MoveTime = 1.0f;        // rise / sink / blend over a second
	static constexpr float CapH = 0.9f;            // a cap's thickness (cm): reads from the seat

	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0) { double V = Def; if (O.IsValid()) O->TryGetNumberField(K, V); return V; }
	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString V; if (O.IsValid()) O->TryGetStringField(K, V); return V; }
	static TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TSharedPtr<FJsonObject>* V = nullptr; return O.IsValid() && O->TryGetObjectField(K, V) ? *V : nullptr; }
	static const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>* V = nullptr; return O.IsValid() && O->TryGetArrayField(K, V) ? V : nullptr; }
	static bool LonLat(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double& Lon, double& Lat)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = Arr(O, K);
		if (!A || A->Num() < 2) return false;
		Lon = (*A)[0]->AsNumber();
		Lat = (*A)[1]->AsNumber();
		return true;
	}
	/** The globe's own mapping (AFNStudio::LonLatToGlobe, in BoardRoot space): east on the right, Z north. */
	static FVector GlobePoint(double Lon, double Lat, float R)
	{
		const double La = FMath::DegreesToRadians(Lat), Lo = FMath::DegreesToRadians(Lon);
		return FVector(R * FMath::Cos(La) * FMath::Cos(Lo), -R * FMath::Cos(La) * FMath::Sin(Lo), R * FMath::Sin(La));
	}
	static TArray<uint8> Bytes(const FString& B64) { TArray<uint8> Out; FBase64::Decode(B64, Out); return Out; }

	/** The board's ring order (docs/EARTH_SIM.md §7): ring 0, then ring k from (q - k, r + k), k steps per direction. */
	static const TArray<FIntPoint>& RingOrder()
	{
		static TArray<FIntPoint> Order;
		if (Order.Num() == 0)
		{
			static const FIntPoint Dirs[6] = { {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1} };
			Order.Add(FIntPoint(0, 0));
			for (int32 K = 1; K <= 22; ++K)
			{
				FIntPoint H(-K, K);
				for (const FIntPoint& D : Dirs)
					for (int32 S = 0; S < K; ++S) { Order.Add(H); H += D; }
			}
		}
		return Order;
	}

	/** What a hex shows on top: colour, wetness, heat (M_EarthColumn's per-instance data); false = nothing to draw. */
	static bool Look(const AFNEarthPlay::FRec& R, const FString& Top, FLinearColor& C, float& Wet, float& Heat)
	{
		Wet = 0.f; Heat = 0.f;
		if (R.Lava >= 24 || Top == TEXT("lava")) { C = FLinearColor(0.95f, 0.18f, 0.02f); Heat = 0.8f + 0.6f * R.Lava / 255.f; return true; }
		if (R.Fire >= 24 || Top == TEXT("fire")) { C = FLinearColor(1.f, 0.42f, 0.06f); Heat = 0.6f + 0.5f * R.Fire / 255.f; return true; }
		if (Top == TEXT("rift") || R.Crack >= 128) { C = FLinearColor(0.08f, 0.015f, 0.01f); Heat = 0.35f; return true; }
		if ((R.Water >= 24 && R.H16 >= 128) || Top == TEXT("flood")) { C = FLinearColor(0.12f, 0.35f, 0.85f); Wet = 0.6f; return true; }
		if (R.Ash >= 24 || Top == TEXT("ash")) { C = FLinearColor(0.32f, 0.31f, 0.3f); return true; }
		if (R.Snow >= 24 || Top == TEXT("snow")) { C = FLinearColor(1.f, 1.f, 1.f); return true; }
		if (Top == TEXT("swarm")) { C = FLinearColor(0.06f, 0.05f, 0.035f); return true; }
		return false;
	}

	static EFNHexState StateOf(const AFNEarthPlay::FRec& R, const FString& Top, int32 DLevels)
	{
		if (R.Lava >= 24 || Top == TEXT("lava")) return EFNHexState::Lava;
		if (R.Fire >= 24 || Top == TEXT("fire")) return EFNHexState::Burning;
		if ((R.Water >= 24 && R.H16 >= 128) || Top == TEXT("flood")) return EFNHexState::Flooded;
		if (R.Ash >= 24 || Top == TEXT("ash")) return EFNHexState::Ash;
		if (DLevels > 0 || Top == TEXT("rift")) return EFNHexState::NewLand;
		return EFNHexState::Normal;
	}

	static FLinearColor KindColor(const FString& K)
	{
		if (K == TEXT("storm")) return FLinearColor(0.1f, 0.1f, 0.12f);
		if (K == TEXT("blizzard")) return FLinearColor(0.7f, 0.75f, 0.85f);
		if (K == TEXT("lightning")) return FLinearColor(0.8f, 0.85f, 1.f);
		if (K == TEXT("volcano")) return FLinearColor(0.6f, 0.12f, 0.02f);
		if (K == TEXT("earthquake")) return FLinearColor(0.35f, 0.22f, 0.1f);
		if (K == TEXT("flood") || K == TEXT("tidal_wave") || K == TEXT("tidal wave")) return FLinearColor(0.08f, 0.25f, 0.55f);
		if (K == TEXT("wildfire")) return FLinearColor(0.7f, 0.25f, 0.03f);
		if (K == TEXT("meteor")) return FLinearColor(1.f, 0.7f, 0.3f);
		if (K == TEXT("forest")) return FLinearColor(0.08f, 0.35f, 0.08f);
		if (K == TEXT("swarm")) return FLinearColor(0.05f, 0.04f, 0.03f);
		if (K == TEXT("raise_land") || K == TEXT("raise land")) return FLinearColor(0.45f, 0.38f, 0.28f);
		if (K == TEXT("sink_land") || K == TEXT("sink land")) return FLinearColor(0.15f, 0.2f, 0.3f);
		return FLinearColor(0.5f, 0.3f, 0.5f);   // plate drift and anything new
	}
}

AFNEarthPlay::AFNEarthPlay()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Caps = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Caps"));
	Caps->SetupAttachment(RootComponent);
	Caps->SetUsingAbsoluteLocation(true);
	Caps->SetUsingAbsoluteRotation(true);
	Caps->SetUsingAbsoluteScale(true);
	Caps->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Caps->SetCastShadow(false);
	Caps->NumCustomDataFloats = 10;
	GlobeCaps = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GlobeCaps"));
	GlobeCaps->SetupAttachment(RootComponent);
	GlobeCaps->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GlobeCaps->SetCastShadow(false);
	GlobeCaps->NumCustomDataFloats = 10;
}

AFNEarthPlay* AFNEarthPlay::Get(UWorld* World, bool bCreate)
{
	if (!World) return nullptr;
	if (TActorIterator<AFNEarthPlay> It(World); It) return *It;
	return bCreate ? World->SpawnActor<AFNEarthPlay>() : nullptr;
}

AFNStudio* AFNEarthPlay::FindStudio()
{
	if (!Studio.IsValid())
		if (TActorIterator<AFNStudio> It(GetWorld()); It) Studio = *It;
	return Studio.Get();
}

UStaticMeshComponent* AFNEarthPlay::Shape(const TCHAR* Mesh, const FLinearColor& Color, USceneComponent* Parent)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	if (!Parent) { C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true); }
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, FN7EarthPlay::BaseMatPath))
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, C);
		D->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, D);
	}
	C->RegisterComponent();
	return C;
}

bool AFNEarthPlay::OnBoard(double Lat, double Lon, FVector& OutWorld) const
{
	return Studio.IsValid() && Studio->BoardPointFor(Lat, Lon, OutWorld);
}

// --- asking -----------------------------------------------------------------------------------------------------

void AFNEarthPlay::AskBoard(bool bFull)
{
	UFNNetClient* N = Net.Get();
	AFNStudio* St = FindStudio();
	int32 Q0, R0;
	if (!N || !St || !St->BoardCentreHex(Q0, R0)) return;
	const FString Key = St->BoardCellKey(Q0, R0);
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetStringField(TEXT("cell"), Key);
	A->SetNumberField(TEXT("max_phenomena"), 0);   // the beat's phenomena come with round.outcome
	if (!bFull && BoardBeat >= 0 && Key == BoardKey) A->SetNumberField(TEXT("since"), BoardBeat);
	N->Query(TEXT("game.earth"), A);
	AskedAt = FPlatformTime::Seconds();
	LastCentre = Key;
}

void AFNEarthPlay::AskGlobe()
{
	UFNNetClient* N = Net.Get();
	if (!N) return;
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetNumberField(TEXT("tiles"), 4);
	if (GlobeBeat >= 0) A->SetNumberField(TEXT("since"), GlobeBeat);
	N->Query(TEXT("game.earth"), A);
	bAskedGlobe = true;
}

// --- the beat ---------------------------------------------------------------------------------------------------

void AFNEarthPlay::OnOutcome(const TSharedPtr<FJsonObject>& Earth)
{
	using namespace FN7EarthPlay;
	if (!Earth.IsValid()) return;
	const int32 Beat = (int32)Num(Earth, TEXT("beat"), LastBeat);
	if (LastBeat >= 0 && Beat > LastBeat) BeatLen = FMath::Clamp((float)(FPlatformTime::Seconds() - BeatStart), 2.f, 20.f);
	LastBeat = Beat;
	BeatStart = FPlatformTime::Seconds();
	static const TArray<TSharedPtr<FJsonValue>> None;
	const TArray<TSharedPtr<FJsonValue>>* List = Arr(Earth, TEXT("phenomena"));
	SyncPhenomena(List ? *List : None, Arr(Earth, TEXT("ended")));
	// The board: only what changed since the beat it was drawn at (the whole disc the first time or after a move).
	AskBoard(BoardBeat < 0);
	AskGlobe();
}

void AFNEarthPlay::OnReply(const TSharedPtr<FJsonObject>& D)
{
	using namespace FN7EarthPlay;
	if (const TArray<TSharedPtr<FJsonValue>>* T = Arr(D, TEXT("tops")))
	{
		Tops.Reset();
		for (const TSharedPtr<FJsonValue>& V : *T) Tops.Add(V->AsString());
	}
	if (const TSharedPtr<FJsonObject> B = Obj(D, TEXT("board"))) { ApplyBoard(B); BoardBeat = (int32)Num(D, TEXT("beat"), BoardBeat); }
	if (const TSharedPtr<FJsonObject> T = Obj(D, TEXT("tiles"))) { ApplyTiles(T); GlobeBeat = (int32)Num(D, TEXT("beat"), GlobeBeat); }
}

void AFNEarthPlay::ApplyBoard(const TSharedPtr<FJsonObject>& Board)
{
	using namespace FN7EarthPlay;
	AFNStudio* St = FindStudio();
	if (!St) return;
	// The centre key says which disc this is: "<face>:<level>:<q>:<r>".
	const FString Centre = Str(Board, TEXT("center"));
	TArray<FString> Parts;
	Centre.ParseIntoArray(Parts, TEXT(":"));
	if (Parts.Num() != 4) return;
	const int32 Q0 = FCString::Atoi(*Parts[2]), R0 = FCString::Atoi(*Parts[3]);
	int32 CQ, CR;
	const bool bCurrent = St->BoardCentreHex(CQ, CR) && St->BoardCellKey(Q0, R0) == Centre;   // still the board on the table
	BoardKey = Centre;
	const TArray<uint8> Data = Bytes(Str(Board, TEXT("data")));
	const TArray<uint8> Changed = Bytes(Str(Board, TEXT("changed")));
	const bool bSince = Board->HasField(TEXT("changed"));
	const int32 N = Data.Num() / 9;
	const TArray<FIntPoint>& Order = RingOrder();
	int32 NChanged = 0;
	Moves.Reset();
	for (int32 j = 0; j < N; ++j)
	{
		const int32 Pos = bSince ? (Changed.IsValidIndex(j * 2 + 1) ? Changed[j * 2] | (Changed[j * 2 + 1] << 8) : -1) : j;
		if (!Order.IsValidIndex(Pos)) continue;
		const FIntPoint H = Order[Pos];
		const int32 Q = Q0 + H.X, R = R0 + H.Y;
		const FString Key = FString::Printf(TEXT("%s:%s:%d:%d"), *Parts[0], *Parts[1], Q, R);
		const uint8* B = &Data[j * 9];
		FRec Rec;
		Rec.H16 = B[0]; Rec.Top = B[1]; Rec.Water = B[2]; Rec.Lava = B[3]; Rec.Ash = B[4]; Rec.Fire = B[5]; Rec.Snow = B[6]; Rec.Heat = (int8)B[7]; Rec.Crack = B[8];
		const uint8 Base = Base16.FindOrAdd(Key, Rec.H16);
		float Def; FVector DefW;
		if (bCurrent && !DefaultTop.Contains(Key) && St->BoardHexTop(Q, R, false, Def, DefW)) DefaultTop.Add(Key, Def);
		const FRec* Old = Cur.Find(Key);
		const int32 OldD = Old ? ((int32)Old->H16 - Base) / 16 : 0, NewD = ((int32)Rec.H16 - Base) / 16;
		const bool bChanged = !Old || FMemory::Memcmp(Old, &Rec, sizeof(FRec)) != 0;
		if (bChanged && Old) ++NChanged;
		if (bCurrent && (NewD != OldD || (Old && StateOf(*Old, TopName(Old->Top), OldD) != StateOf(Rec, TopName(Rec.Top), NewD))))
			Moves.Add({ Key, Q, R, OldD * CmPerLevel, NewD * CmPerLevel, (uint8)StateOf(Rec, TopName(Rec.Top), NewD) });
		Cur.Add(Key, Rec);
	}
	MoveStart = FPlatformTime::Seconds();
	bMovePending = Moves.Num() > 0;
	if (bCurrent) RebuildCaps(0.f);
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: beat %d, %d cells changed, %d phenomena drawn (board %s, %d records%s, %d hexes moving)"),
		LastBeat, NChanged, Phens.Num(), *Centre, N, bSince ? TEXT(" since") : TEXT(""), Moves.Num());
}

void AFNEarthPlay::RebuildCaps(float StartT)
{
	using namespace FN7EarthPlay;
	AFNStudio* St = FindStudio();
	if (!St) return;
	if (Caps->GetStaticMesh() == nullptr)
	{
		Caps->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, HexMesh));
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, ColumnMat)) { CapMat = UMaterialInstanceDynamic::Create(M, this); Caps->SetMaterial(0, CapMat); }
	}
	Caps->ClearInstances();
	int32 Q0, R0;
	if (!St->BoardCentreHex(Q0, R0)) return;
	const float S = (float)St->GetActorScale3D().X;
	// The hex size on the table: the distance between two neighbouring hex centres / sqrt(3).
	double La0, Lo0, La1, Lo1;
	FVector W0, W1;
	float R = 4.f;
	if (St->BoardHexLatLon(Q0, R0, La0, Lo0) && St->BoardHexLatLon(Q0 + 1, R0, La1, Lo1) && St->BoardPointFor(La0, Lo0, W0) && St->BoardPointFor(La1, Lo1, W1))
		R = (float)FVector::Dist2D(W0, W1) / 1.7320508f * 0.9f;
	const TArray<FIntPoint>& Order = RingOrder();
	TArray<FString> Parts;
	BoardKey.ParseIntoArray(Parts, TEXT(":"));
	if (Parts.Num() != 4) return;
	TMap<FString, const FMove*> MoveOf;
	for (const FMove& M : Moves) MoveOf.Add(M.Key, &M);
	for (const FIntPoint& H : Order)
	{
		const int32 Q = Q0 + H.X, Rr = R0 + H.Y;
		const FString Key = St->BoardCellKey(Q, Rr);
		const FRec* Rec = Cur.Find(Key);
		if (!Rec) continue;
		FLinearColor C; float Wet, Heat;
		const bool bLook = Look(*Rec, TopName(Rec->Top), C, Wet, Heat);
		const FMove* M = MoveOf.FindRef(Key);
		const bool bRise = M && M->To > M->From && StartT < 1.f;
		if (!bLook && !bRise) continue;
		float TopCm;
		FVector W;
		if (!St->BoardHexTop(Q, Rr, true, TopCm, W)) continue;   // on the tile's own top
		if (!bLook) { C = FLinearColor(0.3f, 0.27f, 0.24f); Wet = 0.f; Heat = 0.f; }   // new rock rising
		const float H0 = bRise ? FMath::Max(CapH, M->To - M->From) : CapH;
		const int32 Idx = Caps->AddInstance(FTransform(FRotator::ZeroRotator, W + FVector(0, 0, 0.15f * S), FVector(R / 50.f, R / 50.f, H0 * S / 100.f)), true);
		// Colour A (before) -> B (now): a hex that just turned blends in over the second.
		const float Data[10] = { C.R * 0.4f, C.G * 0.4f, C.B * 0.4f, 0.f, 0.f, C.R, C.G, C.B, Wet, Heat };
		Caps->SetCustomData(Idx, TArrayView<const float>(Data, 10), false);
	}
	Caps->MarkRenderStateDirty();
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: %d hexes drawn over the board (cap size %.1f cm, %s)"), Caps->GetInstanceCount(), R, StartT < 1.f ? TEXT("blending in") : TEXT("settled"));
	CapStart = FPlatformTime::Seconds() - StartT * MoveTime;
	if (CapMat) CapMat->SetScalarParameterValue(TEXT("BeatT"), StartT);
}

void AFNEarthPlay::CommitMoves()
{
	// After the second's animation: the studio's own tiles take the new heights and states (one rebuild).
	AFNStudio* St = FindStudio();
	bMovePending = false;
	if (!St || Moves.Num() == 0) return;
	for (const FMove& M : Moves)
	{
		const float* Def = DefaultTop.Find(M.Key);
		const bool bHeight = Def && !FMath::IsNearlyZero(M.To);
		St->SetHexOverride(M.Key, bHeight ? FMath::Clamp(*Def + M.To, 0.6f, 24.f) : NAN, (EFNHexState)M.State);
	}
	St->RebuildBoard();
	Moves.Reset();
	RebuildCaps(1.f);
}

// --- the globe --------------------------------------------------------------------------------------------------

void AFNEarthPlay::ApplyTiles(const TSharedPtr<FJsonObject>& Tiles)
{
	using namespace FN7EarthPlay;
	if (const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(Tiles, TEXT("row_cols")))
	{
		GlobeRowCols.Reset();
		for (const TSharedPtr<FJsonValue>& V : *Rows) GlobeRowCols.Add((int32)V->AsNumber());
	}
	const TArray<uint8> Data = Bytes(Str(Tiles, TEXT("data")));
	const TArray<uint8> Changed = Bytes(Str(Tiles, TEXT("changed")));
	const bool bSince = Tiles->HasField(TEXT("changed"));
	for (int32 j = 0; j < Data.Num() / 9; ++j)
	{
		const int32 Idx = bSince ? (Changed.IsValidIndex(j * 2 + 1) ? Changed[j * 2] | (Changed[j * 2 + 1] << 8) : -1) : j;
		if (Idx < 0) continue;
		const uint8* B = &Data[j * 9];
		FRec R;
		R.H16 = B[0]; R.Top = B[1]; R.Water = B[2]; R.Lava = B[3]; R.Ash = B[4]; R.Fire = B[5]; R.Snow = B[6]; R.Heat = (int8)B[7]; R.Crack = B[8];
		GlobeTiles.Add(Idx, R);
	}
	// Draw the tiles that carry something (lava, fire, flood, rift, ash, snow, a swarm) as caps on the globe.
	AFNStudio* St = FindStudio();
	if (!St || GlobeRowCols.Num() == 0) return;
	USceneComponent* BoardRoot = nullptr;
	USceneComponent* Globe = nullptr;
	TInlineComponentArray<USceneComponent*> Comps(St);
	for (USceneComponent* C : Comps) { if (C && C->GetName() == TEXT("BoardRoot")) BoardRoot = C; if (C && C->GetName() == TEXT("Globe")) Globe = C; }
	if (!BoardRoot || !Globe) return;
	if (GlobeCaps->GetAttachParent() != BoardRoot) GlobeCaps->AttachToComponent(BoardRoot, FAttachmentTransformRules::KeepRelativeTransform);
	GlobeCaps->SetRelativeTransform(FTransform::Identity);
	if (GlobeCaps->GetStaticMesh() == nullptr)
	{
		GlobeCaps->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, ColumnMat)) { GlobeMat = UMaterialInstanceDynamic::Create(M, this); GlobeMat->SetScalarParameterValue(TEXT("BeatT"), 1.f); GlobeCaps->SetMaterial(0, GlobeMat); }
	}
	GlobeCaps->ClearInstances();
	const float Rg = (float)Globe->GetRelativeScale3D().X * 50.f + 0.9f;   // just above the globe's tiles
	int32 Row = 0, RowStart = 0, Shown = 0;
	const int32 Deg = 4;
	for (int32 Idx = 0; Row < GlobeRowCols.Num(); ++Idx)
	{
		while (Row < GlobeRowCols.Num() && Idx >= RowStart + GlobeRowCols[Row]) { RowStart += GlobeRowCols[Row]; ++Row; }
		if (Row >= GlobeRowCols.Num()) break;
		const FRec* R = GlobeTiles.Find(Idx);
		if (!R) continue;
		FLinearColor C; float Wet, Heat;
		if (!Look(*R, TopName(R->Top), C, Wet, Heat)) continue;
		const double Lat = 90.0 - (Row + 0.5) * Deg, Lon = -180.0 + (Idx - RowStart + 0.5) * 360.0 / GlobeRowCols[Row];
		const FVector P = GlobePoint(Lon, Lat, Rg);
		const float Side = Rg * FMath::DegreesToRadians((float)Deg) * 0.8f;
		const int32 I = GlobeCaps->AddInstance(FTransform(FRotationMatrix::MakeFromZ(P.GetSafeNormal()).Rotator(), P, FVector(Side / 100.f, Side / 100.f, 0.004f)));
		const float Dt[10] = { C.R, C.G, C.B, Wet, Heat, C.R, C.G, C.B, Wet, Heat };
		GlobeCaps->SetCustomData(I, TArrayView<const float>(Dt, 10), false);
		++Shown;
	}
	GlobeCaps->MarkRenderStateDirty();
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: globe, %d tiles marked of %d"), Shown, GlobeTiles.Num());
}

// --- the phenomena ----------------------------------------------------------------------------------------------

void AFNEarthPlay::SyncPhenomena(const TArray<TSharedPtr<FJsonValue>>& List, const TArray<TSharedPtr<FJsonValue>>* Ended)
{
	using namespace FN7EarthPlay;
	TSet<int32> Seen;
	for (const TSharedPtr<FJsonValue>& V : List)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid()) continue;
		const int32 Id = (int32)Num(O, TEXT("id"), -1);
		if (Id < 0) continue;
		Seen.Add(Id);
		FPhen& P = Phens.FindOrAdd(Id);
		const bool bNew = P.Parts.Num() == 0;
		P.Id = Id;
		P.Kind = Str(O, TEXT("kind")).Replace(TEXT(" "), TEXT("_"));
		LonLat(O, TEXT("at"), P.Lon, P.Lat);
		P.ToLon = P.Lon; P.ToLat = P.Lat;
		if (const TSharedPtr<FJsonObject> Vel = Obj(O, TEXT("velocity"))) LonLat(Vel, TEXT("to"), P.ToLon, P.ToLat);
		P.RadiusKm = (float)Num(O, TEXT("radius_km"), 120.0);
		P.Intensity = (float)Num(O, TEXT("intensity"), 3.0);
		P.FrontKm = (float)Num(O, TEXT("front_km"), -1.0);
		if (bNew) { P.SeenAt = FPlatformTime::Seconds(); BuildPhen(P); }
	}
	TSet<int32> Gone;
	if (Ended) for (const TSharedPtr<FJsonValue>& V : *Ended) if (const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr) Gone.Add((int32)Num(O, TEXT("id"), -1));
	for (auto It = Phens.CreateIterator(); It; ++It)
		if (!Seen.Contains(It.Key()) || Gone.Contains(It.Key()))
		{
			for (UStaticMeshComponent* C : It.Value().Parts) if (C) C->DestroyComponent();
			if (It.Value().Light) It.Value().Light->DestroyComponent();
			if (It.Value().GlobeDot) It.Value().GlobeDot->DestroyComponent();
			It.RemoveCurrent();
		}
}

void AFNEarthPlay::BuildPhen(FPhen& P)
{
	using namespace FN7EarthPlay;
	static const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const FLinearColor C = KindColor(P.Kind);
	const FString& K = P.Kind;
	auto Add = [&](const TCHAR* M, const FLinearColor& Col, int32 Count) { for (int32 i = 0; i < Count; ++i) P.Parts.Add(Shape(M, Col)); };
	if (K == TEXT("storm") || K == TEXT("blizzard")) Add(Sphere, C, 6);
	else if (K == TEXT("volcano")) { Add(Cone, C, 1); Add(Sphere, FLinearColor(0.12f, 0.11f, 0.1f), 4); }
	else if (K == TEXT("earthquake") || K == TEXT("tidal_wave")) Add(Cube, C, 24);
	else if (K == TEXT("flood")) Add(Cyl, C, 1);
	else if (K == TEXT("wildfire")) Add(Cone, C, 6);
	else if (K == TEXT("meteor")) { Add(Sphere, C * 3.f, 1); Add(Cyl, FLinearColor(0.04f, 0.03f, 0.03f), 1); }
	else if (K == TEXT("forest")) Add(Cone, C, 7);
	else if (K == TEXT("swarm")) Add(Sphere, C, 10);
	else if (K == TEXT("lightning")) Add(Cyl, C * 4.f, 1);
	else Add(Sphere, C, 1);
	if (K == TEXT("volcano") || K == TEXT("wildfire") || K == TEXT("meteor") || K == TEXT("lightning"))
	{
		P.Light = NewObject<UPointLightComponent>(this);
		P.Light->SetupAttachment(RootComponent);
		P.Light->SetUsingAbsoluteLocation(true);
		P.Light->SetIntensity(12.f);
		P.Light->SetAttenuationRadius(60.f);
		P.Light->SetLightColor(K == TEXT("lightning") ? FLinearColor(0.7f, 0.8f, 1.f) : FLinearColor(1.f, 0.45f, 0.12f));
		P.Light->SetCastShadows(false);
		P.Light->RegisterComponent();
	}
	// Its mark on the globe, riding the globe's turns.
	if (AFNStudio* St = FindStudio())
	{
		TInlineComponentArray<USceneComponent*> Comps(St);
		for (USceneComponent* Cc : Comps)
			if (Cc && Cc->GetName() == TEXT("BoardRoot"))
			{
				P.GlobeDot = Shape(Sphere, C * 2.f, Cc);
				break;
			}
	}
}

void AFNEarthPlay::PlacePhen(FPhen& P, double Now)
{
	using namespace FN7EarthPlay;
	AFNStudio* St = Studio.Get();
	if (!St) return;
	const float U = FMath::Clamp((float)((Now - BeatStart) / FMath::Max(1.f, BeatLen)), 0.f, 1.f);
	const double Lat = FMath::Lerp(P.Lat, P.ToLat, (double)U), Lon = FMath::Lerp(P.Lon, P.ToLon, (double)U);
	const float T = (float)(Now - P.SeenAt);
	if (P.GlobeDot)
	{
		USceneComponent* Globe = nullptr;
		TInlineComponentArray<USceneComponent*> Comps(St);
		for (USceneComponent* Cc : Comps) if (Cc && Cc->GetName() == TEXT("Globe")) { Globe = Cc; break; }
		const float Rg = Globe ? (float)Globe->GetRelativeScale3D().X * 50.f + 1.5f : 60.f;
		P.GlobeDot->SetRelativeLocation(GlobePoint(Lon, Lat, Rg));
		P.GlobeDot->SetRelativeScale3D(FVector(0.035f + 0.01f * FMath::Sin(T * 4.f)));
	}
	FVector W;
	const bool bOn = OnBoard(Lat, Lon, W);
	for (UStaticMeshComponent* C : P.Parts) if (C) C->SetVisibility(bOn);
	if (P.Light) P.Light->SetVisibility(bOn);
	if (!bOn) return;
	const float S = (float)St->GetActorScale3D().X;
	const float Rcm = FMath::Clamp((float)(P.RadiusKm * 1000.0 * St->BoardCmPerMeter()) * S, 3.f * S, 120.f * S);
	const FString& K = P.Kind;
	const int32 N = P.Parts.Num();
	if (K == TEXT("storm") || K == TEXT("blizzard"))
	{
		for (int32 i = 0; i < N; ++i)
		{
			const float A = i * 2.f * PI / N + T * 0.15f;
			P.Parts[i]->SetWorldLocation(W + FVector(FMath::Cos(A) * Rcm * 0.45f, FMath::Sin(A) * Rcm * 0.45f, (30.f + 2.f * FMath::Sin(T + i)) * S));   // high: the land under it stays in view
			P.Parts[i]->SetWorldScale3D(FVector(Rcm * 0.007f, Rcm * 0.007f, Rcm * 0.003f));
		}
	}
	else if (K == TEXT("volcano"))
	{
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, 2.f * S));
		P.Parts[0]->SetWorldScale3D(FVector(0.06f * S, 0.06f * S, 0.05f * S));
		for (int32 i = 1; i < N; ++i)
		{
			const float Ph = FMath::Fmod(T * 0.5f + i * 0.25f, 1.f);
			P.Parts[i]->SetWorldLocation(W + FVector(0, 0, (6.f + Ph * 22.f) * S));
			P.Parts[i]->SetWorldScale3D(FVector((0.03f + Ph * 0.08f) * S));
		}
	}
	else if (K == TEXT("earthquake") || K == TEXT("tidal_wave"))
	{
		// A ring: the quake ripples out from the epicentre again and again; a tidal wave's front sits at front_km.
		const float Ring = (K == TEXT("tidal_wave") && P.FrontKm > 0.f) ? FMath::Clamp((float)(P.FrontKm * 1000.0 * St->BoardCmPerMeter()) * S, 2.f * S, 120.f * S)
			: Rcm * FMath::Fmod(T / 1.5f, 1.f);
		for (int32 i = 0; i < N; ++i)
		{
			const float A = i * 2.f * PI / N;
			P.Parts[i]->SetWorldLocation(W + FVector(FMath::Cos(A) * Ring, FMath::Sin(A) * Ring, 1.f * S));
			P.Parts[i]->SetWorldScale3D(FVector(0.012f * S, 0.012f * S, 0.006f * S));
		}
	}
	else if (K == TEXT("flood"))
	{
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, 0.9f * S));
		P.Parts[0]->SetWorldScale3D(FVector(Rcm / 50.f, Rcm / 50.f, 0.004f * S));
	}
	else if (K == TEXT("wildfire") || K == TEXT("forest"))
	{
		for (int32 i = 0; i < N; ++i)
		{
			const float A = i * 2.f * PI / N, Rr = Rcm * (0.25f + 0.5f * ((i * 37) % 10) / 10.f);
			const float F = K == TEXT("wildfire") ? 0.8f + 0.3f * FMath::Sin(T * 9.f + i * 1.7f) : 1.f;
			P.Parts[i]->SetWorldLocation(W + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 1.f * S));
			P.Parts[i]->SetWorldScale3D(FVector(0.025f * S, 0.025f * S, 0.04f * S * F));
		}
	}
	else if (K == TEXT("meteor"))
	{
		const float Fall = FMath::Clamp(T / 0.6f, 0.f, 1.f);
		P.Parts[0]->SetWorldLocation(W + FVector(-30.f * (1.f - Fall) * S, 0.f, 40.f * (1.f - Fall) * S));
		P.Parts[0]->SetWorldScale3D(FVector((Fall < 1.f ? 0.05f : 0.f) * S));
		P.Parts[1]->SetWorldLocation(W + FVector(0, 0, 0.5f * S));
		P.Parts[1]->SetWorldScale3D(FVector(Rcm / 70.f, Rcm / 70.f, 0.003f * S));
	}
	else if (K == TEXT("swarm"))
	{
		for (int32 i = 0; i < N; ++i)
		{
			const float A = i * 2.3f + T * (1.2f + 0.1f * i);
			P.Parts[i]->SetWorldLocation(W + FVector(FMath::Cos(A) * Rcm * 0.4f, FMath::Sin(A * 1.3f) * Rcm * 0.4f, (4.f + 2.f * FMath::Sin(A)) * S));
			P.Parts[i]->SetWorldScale3D(FVector(0.012f * S));
		}
	}
	else if (K == TEXT("lightning"))
	{
		const bool bFlash = FMath::Fmod(T, 1.3f) < 0.12f;
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, 25.f * S));
		P.Parts[0]->SetWorldScale3D(bFlash ? FVector(0.008f * S, 0.008f * S, 0.5f * S) : FVector::ZeroVector);
	}
	else if (N > 0)
	{
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, 3.f * S));
		P.Parts[0]->SetWorldScale3D(FVector(0.04f * S));
	}
	if (P.Light)
	{
		P.Light->SetWorldLocation(W + FVector(0, 0, 8.f * S));
		const float Flick = K == TEXT("wildfire") ? 0.7f + 0.3f * FMath::Sin(T * 13.f) : 1.f;
		P.Light->SetIntensity(12.f * Flick * FMath::Clamp(P.Intensity / 5.f, 0.4f, 2.f));
	}
}

// --- a cast's strike --------------------------------------------------------------------------------------------

void AFNEarthPlay::OnStreak(const TSharedPtr<FJsonObject>& Pub)
{
	using namespace FN7EarthPlay;
	if (!FindStudio()) return;
	TArray<TSharedPtr<FJsonObject>> Casts;
	if (const TArray<TSharedPtr<FJsonValue>>* A = Arr(Pub, TEXT("casts")))
		for (const TSharedPtr<FJsonValue>& V : *A) if (V.IsValid() && V->AsObject().IsValid()) Casts.Add(V->AsObject());
	if (Casts.Num() == 0) Casts.Add(Pub);
	for (const TSharedPtr<FJsonObject>& C : Casts)
	{
		double Lon, Lat;
		FVector W;
		if (!LonLat(C, TEXT("at"), Lon, Lat) || !OnBoard(Lat, Lon, W)) continue;
		const FString Power = Str(C, TEXT("power"));
		const float S = (float)Studio->GetActorScale3D().X;
		FFlash F;
		F.At = FPlatformTime::Seconds();
		F.Where = W;
		F.Kind = Power;
		F.Beam = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), KindColor(Power) * 4.f);
		F.Beam->SetWorldLocation(W + FVector(0, 0, 30.f * S));
		F.Beam->SetWorldScale3D(FVector(0.02f * S, 0.02f * S, 0.6f * S));
		F.Light = NewObject<UPointLightComponent>(this);
		F.Light->SetupAttachment(RootComponent);
		F.Light->SetUsingAbsoluteLocation(true);
		F.Light->SetWorldLocation(W + FVector(0, 0, 10.f * S));
		F.Light->SetIntensity(60.f);
		F.Light->SetAttenuationRadius(90.f * S);
		F.Light->SetLightColor(KindColor(Power) * 2.f + FLinearColor(0.3f, 0.3f, 0.3f));
		F.Light->SetCastShadows(false);
		F.Light->RegisterComponent();
		// A mega's fault reading over the strike, shown for two seconds. Mega powers should feel very powerful at huge intersections.
		// volcanos near the no-faultline areas").
		FString Label;
		const FString Tier = Str(C, TEXT("fault_tier"));
		const double Factor = Num(C, TEXT("fault_factor"), 0.0);
		if (const TSharedPtr<FJsonObject> J = Obj(C, TEXT("junction")); J.IsValid() && !Str(J, TEXT("name")).IsEmpty())
			Label = FString::Printf(TEXT("%s x%.1f"), *Str(J, TEXT("name")).ToUpper(), Factor);
		else if (Tier == TEXT("tiny")) Label = TEXT("far from any fault - tiny");
		else if (!Tier.IsEmpty()) Label = FString::Printf(TEXT("%s %d km x%.1f"), *Tier.ToUpper(), (int32)Num(C, TEXT("fault_km"), 0.0), Factor);
		if (!Label.IsEmpty())
		{
			F.Label = NewObject<UTextRenderComponent>(this);
			F.Label->SetupAttachment(RootComponent);
			F.Label->SetUsingAbsoluteLocation(true);
			F.Label->SetUsingAbsoluteRotation(true);
			F.Label->SetHorizontalAlignment(EHTA_Center);
			F.Label->SetWorldSize(4.f * S);
			F.Label->SetTextRenderColor(FColor(255, 214, 120));
			F.Label->SetText(FText::FromString(Label));
			F.Label->SetWorldLocation(W + FVector(0, 0, 34.f * S));
			F.Label->RegisterComponent();
		}
		Flashes.Add(F);
		UE_LOG(LogTemp, Display, TEXT("FN7 earth: strike %s at %.2f, %.2f%s"), *Power, Lat, Lon, Label.IsEmpty() ? TEXT("") : *(TEXT(" (") + Label + TEXT(")")));
	}
}

// --- every frame ------------------------------------------------------------------------------------------------

void AFNEarthPlay::Tick(float Dt)
{
	using namespace FN7EarthPlay;
	Super::Tick(Dt);
	const double Now = FPlatformTime::Seconds();
	AFNStudio* St = FindStudio();
	if (!St) return;
	// The board moved (a pan, a zoom): the new disc, once it has settled.
	if (LastBeat >= 0)
	{
		int32 Q0, R0;
		if (St->BoardCentreHex(Q0, R0))
		{
			const FString Key = St->BoardCellKey(Q0, R0);
			if (Key != LastCentre) { LastCentre = Key; CentreChangedAt = Now; }
			else if (Key != BoardKey && CentreChangedAt > 0.0 && Now - CentreChangedAt > 0.6 && Now - AskedAt > 1.0) { AskBoard(true); CentreChangedAt = 0.0; }
		}
	}
	// The beat's blend and the rising hexes.
	const float U = FMath::Clamp((float)((Now - CapStart) / MoveTime), 0.f, 1.f);
	if (CapMat) CapMat->SetScalarParameterValue(TEXT("BeatT"), U * U * (3.f - 2.f * U));
	if (bMovePending && Now - MoveStart > MoveTime + 0.05) CommitMoves();
	for (TPair<int32, FPhen>& P : Phens) PlacePhen(P.Value, Now);
	for (int32 i = Flashes.Num() - 1; i >= 0; --i)
	{
		FFlash& F = Flashes[i];
		const float T = (float)(Now - F.At);
		const float S = (float)St->GetActorScale3D().X;
		if (F.Beam) F.Beam->SetWorldScale3D(FVector(0.02f * S * FMath::Max(0.f, 1.f - T / 0.6f), 0.02f * S * FMath::Max(0.f, 1.f - T / 0.6f), 0.6f * S));
		if (F.Light) F.Light->SetIntensity(60.f * FMath::Max(0.f, 1.f - T / 0.9f));
		if (F.Label)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				if (PC->PlayerCameraManager) F.Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation() - F.Label->GetComponentLocation()).Rotation());
		}
		if (T > 2.2f)
		{
			if (F.Beam) F.Beam->DestroyComponent();
			if (F.Light) F.Light->DestroyComponent();
			if (F.Label) F.Label->DestroyComponent();
			Flashes.RemoveAt(i);
		}
	}
}
