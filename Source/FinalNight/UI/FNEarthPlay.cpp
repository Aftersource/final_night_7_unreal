#include "FNEarthPlay.h"
#include "FNHudPrefs.h"
#include "FNSoundPass.h"

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
	static double Km(double La1, double Lo1, double La2, double Lo2)
	{
		const double P = PI / 180.0, A = FMath::Sin((La2 - La1) * P / 2), B = FMath::Sin((Lo2 - Lo1) * P / 2);
		return 2.0 * 6371.0 * FMath::Asin(FMath::Min(1.0, FMath::Sqrt(A * A + FMath::Cos(La1 * P) * FMath::Cos(La2 * P) * B * B)));
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

	/** One name per kind: the server's power slugs and phenomenon kinds differ for two of them. */
	static FString Canon(const FString& Raw)
	{
		const FString K = Raw.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (K == TEXT("storm_clouds")) return TEXT("storm");
		if (K == TEXT("grow_forest")) return TEXT("forest");
		return K;
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
	if (LastBeat >= 0 && Beat == LastBeat + 1) BeatLen = FMath::Clamp((float)(FPlatformTime::Seconds() - BeatStart), 2.f, 20.f);
	LastBeat = Beat;
	const FString Hash = Str(Earth, TEXT("hash"));
	if (!Hash.IsEmpty())
	{
		OutcomeHash.Add(Beat, Hash);
		for (auto It = OutcomeHash.CreateIterator(); It; ++It) if (It.Key() < Beat - 64) It.RemoveCurrent();
	}
	if (bHeld) return;   // a scrub is on the table: the live beat waits for BackToLive
	if (DrawnBeat < 0 || Beat != DrawnBeat + 1) { JumpTo(Beat); return; }   // a jump (first beat, reconnect, replay seek)
	BeatStart = FPlatformTime::Seconds();
	static const TArray<TSharedPtr<FJsonValue>> None;
	const TArray<TSharedPtr<FJsonValue>>* List = Arr(Earth, TEXT("phenomena"));
	SyncPhenomena(List ? *List : None, Arr(Earth, TEXT("ended")));
	DrawnBeat = Beat;
	DrawnHash = Hash;
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
	const int32 AnsBeat = (int32)Num(D, TEXT("beat"), -1);
	if (const TSharedPtr<FJsonObject> B = Obj(D, TEXT("board")))
	{
		// Never under a moving hand: a board answer that lands mid-drag waits for the release (Tick).
		if (AFNStudio* St = FindStudio(); St && St->IsBoardPanning()) { PendingBoard = D; return; }
		const FString Centre = Str(B, TEXT("center"));
		const bool bFull = !B->HasField(TEXT("changed"));
		if (bFull && AnsBeat >= 0)
		{
			const FString CKey = FString::Printf(TEXT("%s@%d"), *Centre, AnsBeat);
			if (!BoardCache.Contains(CKey)) { CacheOrder.Add(CKey); while (CacheOrder.Num() > 32) BoardCache.Remove(CacheOrder[0]), CacheOrder.RemoveAt(0); }
			BoardCache.Add(CKey, D);
		}
		if (bFull && AnsBeat == FirstBeat && BaseAsked.Contains(Centre) && !BaseKnown.Contains(Centre)) ApplyBaseline(B);
		else if (bFull && bJumpBoard && AnsBeat == JumpBeat) ApplyJumpBoard(D);
		else
		{
			ApplyBoard(B);
			BoardBeat = AnsBeat >= 0 ? AnsBeat : BoardBeat;
		}
		// A place seen for the first time after the first beat: its heights are measured from the first beat.
		if (bFull && AnsBeat > FirstBeat && !BaseKnown.Contains(Centre) && !BaseAsked.Contains(Centre) && Net.IsValid())
		{
			BaseAsked.Add(Centre);
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("cell"), Centre);
			A->SetNumberField(TEXT("beat"), FirstBeat);
			A->SetNumberField(TEXT("max_phenomena"), 0);
			Net->Query(TEXT("game.earth"), A);
		}
		else if (bFull && AnsBeat >= 0 && AnsBeat <= FirstBeat) BaseKnown.Add(Centre);
	}
	if (const TSharedPtr<FJsonObject> T = Obj(D, TEXT("tiles")))
	{
		if (bJumpTiles && AnsBeat == JumpBeat && !T->HasField(TEXT("changed"))) { GlobeTiles.Reset(); bJumpTiles = false; }
		ApplyTiles(T);
		GlobeBeat = AnsBeat >= 0 ? AnsBeat : GlobeBeat;
	}
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
		const FRec* Old = Cur.Find(Key);
		const int32 OldD = Old ? ((int32)Old->H16 - Base) / 16 : 0, NewD = ((int32)Rec.H16 - Base) / 16;
		const bool bChanged = !Old || FMemory::Memcmp(Old, &Rec, sizeof(FRec)) != 0;
		if (bChanged && Old) ++NChanged;
		if (bCurrent && (NewD != OldD || (Old && StateOf(*Old, TopName(Old->Top), OldD) != StateOf(Rec, TopName(Rec.Top), NewD))))
		{
			float Def; FVector DefW;   // the board's own tile top, only for hexes that move (cheap on a full answer)
			if (!DefaultTop.Contains(Key) && St->BoardHexTop(Q, R, false, Def, DefW)) DefaultTop.Add(Key, Def);
			Moves.Add({ Key, Q, R, OldD * CmPerLevel, NewD * CmPerLevel, (uint8)StateOf(Rec, TopName(Rec.Top), NewD) });
		}
		Cur.Add(Key, Rec);
	}
	MoveStart = FPlatformTime::Seconds();
	bMovePending = Moves.Num() > 0;
	CapBlend = MoveTime;
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
	CapStart = FPlatformTime::Seconds() - StartT * FMath::Max(0.05f, CapBlend);
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
		Committed.Add(M.Key, bHeight ? M.To : NAN);   // what a heave settles back to
		CommittedState.Add(M.Key, M.State);
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
		P.Kind = Canon(Str(O, TEXT("kind")));
		LonLat(O, TEXT("at"), P.Lon, P.Lat);
		P.ToLon = P.Lon; P.ToLat = P.Lat;
		if (const TSharedPtr<FJsonObject> Vel = Obj(O, TEXT("velocity"))) LonLat(Vel, TEXT("to"), P.ToLon, P.ToLat);
		P.RadiusKm = (float)Num(O, TEXT("radius_km"), 120.0);
		P.Intensity = (float)Num(O, TEXT("intensity"), 3.0);
		P.FrontKm = (float)Num(O, TEXT("front_km"), -1.0);
		if (bNew)
		{
			P.SeenAt = FPlatformTime::Seconds();
			BuildPhen(P);
			if (P.Kind == TEXT("earthquake")) StartHeave(P.Lat, P.Lon, 2.4f, FMath::Clamp(P.RadiusKm, 20.f, 800.f), 2.f);
		}
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
	else if (K == TEXT("wildfire")) Add(Cone, FLinearColor(0.95f, 0.16f, 0.01f), 9);   // saturated: an over-bright colour washes to pale yellow
	else if (K == TEXT("meteor")) { Add(Sphere, C * 3.f, 1); Add(Cyl, FLinearColor(0.04f, 0.03f, 0.03f), 1); Add(Cyl, FLinearColor(1.f, 0.4f, 0.08f) * 3.f, 1); Add(Sphere, FLinearColor(0.15f, 0.13f, 0.12f), 3); }
	else if (K == TEXT("forest")) Add(Cone, FLinearColor(0.02f, 0.16f, 0.04f), 10);
	else if (K == TEXT("swarm")) Add(Sphere, FLinearColor(0.03f, 0.025f, 0.02f), 26);
	else if (K == TEXT("lightning")) Add(Cyl, C * 4.f, 1);
	else if (K == TEXT("plate_drift")) Add(Cube, C, 12);
	else if (K == TEXT("raise_land")) Add(Cone, C, 1);
	else if (K == TEXT("sink_land")) Add(Cyl, C, 1);
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
		// Sized to the phenomenon's reach on the table (Sz), never smaller than reads from the seat.
		const float Sz = FMath::Clamp(Rcm * 0.18f, 8.f * S, 16.f * S);   // fire and forest stand over the board's trees
		for (int32 i = 0; i < N; ++i)
		{
			const float A = i * 2.f * PI / N + 0.4f * ((i * 53) % 7), Rr = Rcm * (0.15f + 0.55f * ((i * 37) % 10) / 10.f);
			const float F = K == TEXT("wildfire") ? 0.75f + 0.35f * FMath::Sin(T * 9.f + i * 1.7f) : 1.f;
			const float Wd = Sz * (K == TEXT("wildfire") ? 0.32f : 0.38f), Ht = Sz * (K == TEXT("wildfire") ? 0.7f : 0.95f) * F;
			P.Parts[i]->SetWorldLocation(W + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.8f * S + Ht * 0.5f));
			P.Parts[i]->SetWorldScale3D(FVector(Wd / 100.f, Wd / 100.f, Ht / 100.f));
		}
	}
	else if (K == TEXT("meteor"))
	{
		// The fall (a fireball out of the sky), then the crater: a dark bowl with a glowing rim and smoke rising.
		const float Fall = FMath::Clamp(T / 0.6f, 0.f, 1.f), Cr = FMath::Max(Rcm * 0.45f, 4.f * S);
		P.Parts[0]->SetWorldLocation(W + FVector(-30.f * (1.f - Fall) * S, 0.f, 40.f * (1.f - Fall) * S));
		P.Parts[0]->SetWorldScale3D(FVector((Fall < 1.f ? 0.06f : 0.f) * S));
		P.Parts[1]->SetWorldLocation(W + FVector(0, 0, 0.8f * S));
		P.Parts[1]->SetWorldScale3D(FVector(Cr / 50.f, Cr / 50.f, 0.004f * S));
		P.Parts[2]->SetWorldLocation(W + FVector(0, 0, 0.7f * S));
		P.Parts[2]->SetWorldScale3D(FVector(Cr * 1.15f / 50.f, Cr * 1.15f / 50.f, 0.003f * S));
		for (int32 i = 3; i < N; ++i)
		{
			const float Ph = FMath::Fmod(T * 0.4f + i * 0.33f, 1.f);
			P.Parts[i]->SetWorldLocation(W + FVector(0, 0, (3.f + Ph * 18.f) * S));
			P.Parts[i]->SetWorldScale3D(FVector((0.04f + Ph * 0.1f) * S));
		}
	}
	else if (K == TEXT("swarm"))
	{
		// A dark cloud of beasts boiling low over the land.
		const float Sz = FMath::Clamp(Rcm * 0.12f, 6.f * S, 10.f * S);
		for (int32 i = 0; i < N; ++i)
		{
			const float A = i * 2.3f + T * (1.2f + 0.07f * i), Rr = Rcm * (0.15f + 0.35f * ((i * 29) % 10) / 10.f);
			P.Parts[i]->SetWorldLocation(W + FVector(FMath::Cos(A) * Rr, FMath::Sin(A * 1.3f) * Rr, (2.5f + 1.5f * FMath::Sin(A * 0.7f)) * S));
			P.Parts[i]->SetWorldScale3D(FVector(Sz * (0.18f + 0.06f * ((i * 7) % 3)) / 50.f));
		}
	}
	else if (K == TEXT("lightning"))
	{
		const bool bFlash = FN7HudPrefs::ReduceFlashing() || FMath::Fmod(T, 0.9f) < 0.25f;   // reduce flashing: a steady bolt
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, 25.f * S));
		P.Parts[0]->SetWorldScale3D(bFlash ? FVector(0.015f * S, 0.015f * S, 0.5f * S) : FVector::ZeroVector);
	}
	else if (K == TEXT("plate_drift"))
	{
		// A fault line across the drift: dark slabs in a row, creeping along the plate's way.
		FVector To;
		const double ToLat = P.ToLat, ToLon = P.ToLon;
		FVector Dir = (OnBoard(ToLat, ToLon, To) && FVector::Dist2D(To, W) > 0.1f) ? (To - W).GetSafeNormal2D() : FVector(1, 0, 0);
		const FVector Across(-Dir.Y, Dir.X, 0.f);
		for (int32 i = 0; i < N; ++i)
		{
			const float A = (i - (N - 1) * 0.5f) / (N * 0.5f);
			const float Side = (i % 2 ? 1.f : -1.f) * (0.5f + 0.5f * FMath::Sin(T * 1.5f)) * 0.6f * S;
			P.Parts[i]->SetWorldLocation(W + Across * (A * Rcm * 0.8f) + Dir * Side + FVector(0, 0, 0.9f * S));
			P.Parts[i]->SetWorldRotation(Across.Rotation());
			P.Parts[i]->SetWorldScale3D(FVector(Rcm * 0.06f / 100.f + 0.01f * S, 0.012f * S, 0.006f * S));
		}
	}
	else if (K == TEXT("raise_land"))
	{
		// New land pushing up: a mound that grows out of the board over a second and a half.
		const float G = FMath::Clamp(T / 1.5f, 0.f, 1.f), Hc = 6.f * S * G;
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, Hc * 0.5f));
		P.Parts[0]->SetWorldScale3D(FVector(Rcm * 0.5f / 50.f, Rcm * 0.5f / 50.f, FMath::Max(Hc, 0.01f) / 100.f));
	}
	else if (K == TEXT("sink_land"))
	{
		// The land giving way: a dark hollow that opens over a second and a half.
		const float G = FMath::Clamp(T / 1.5f, 0.f, 1.f);
		P.Parts[0]->SetWorldLocation(W + FVector(0, 0, 0.7f * S));
		P.Parts[0]->SetWorldScale3D(FVector(Rcm * 0.5f * G / 50.f, Rcm * 0.5f * G / 50.f, 0.004f * S));
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
		const FString Power = Canon(Str(C, TEXT("power")));
		const float S = (float)Studio->GetActorScale3D().X;
		FFlash F;
		F.At = FPlatformTime::Seconds();
		F.Where = W;
		F.Kind = Power;
		F.bMega = Str(C, TEXT("tier")) == TEXT("mega");
		// The sound of it: a mini a soft breath, a mega a deep rush with the studio's impact under it.
		FN7Sound::Play(GetWorld(), F.bMega ? FN7Sound::ESnd::MegaWhoosh : FN7Sound::ESnd::MiniWhoosh, F.bMega ? 0.55f : 0.35f);
		if (F.bMega) Studio->PlaySfx(EFNSfx::Impact, 0.35f, 0.9f);
		// A column of light out of the sky onto the point, and a shock ring that runs out over the land.
		F.Beam = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), KindColor(Power) * 5.f + FLinearColor(0.4f, 0.4f, 0.4f));
		F.Beam->SetWorldLocation(W + FVector(0, 0, 45.f * S));
		F.Beam->SetWorldScale3D(FVector((F.bMega ? 0.07f : 0.035f) * S, (F.bMega ? 0.07f : 0.035f) * S, 0.9f * S));
		F.Ring = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), KindColor(Power) * 3.f + FLinearColor(0.3f, 0.3f, 0.3f));
		F.Ring->SetWorldLocation(W + FVector(0, 0, 0.6f * S));
		F.Ring->SetWorldScale3D(FVector(0.01f * S, 0.01f * S, 0.004f * S));
		F.Light = NewObject<UPointLightComponent>(this);
		F.Light->SetupAttachment(RootComponent);
		F.Light->SetUsingAbsoluteLocation(true);
		F.Light->SetWorldLocation(W + FVector(0, 0, 10.f * S));
		F.Light->SetIntensity(F.bMega ? 220.f : 90.f);
		F.Light->SetAttenuationRadius(120.f * S);
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
			F.Label->SetWorldSize((F.bMega ? 6.f : 4.f) * S);
			F.Label->SetTextRenderColor(FColor(255, 214, 120));
			F.Label->SetText(FText::FromString(Label));
			F.Label->SetWorldLocation(W + FVector(0, 0, 34.f * S));
			F.Label->RegisterComponent();
		}
		Flashes.Add(F);
		UE_LOG(LogTemp, Display, TEXT("FN7 earth: strike %s %s at %.2f, %.2f%s"), *Power, F.bMega ? TEXT("mega") : TEXT("mini"), Lat, Lon, Label.IsEmpty() ? TEXT("") : *(TEXT(" (") + Label + TEXT(")")));
		// The earth answers at once: a quake heaves the land, raise / sink land jolt it.
		if (Power == TEXT("earthquake")) StartHeave(Lat, Lon, F.bMega ? 3.2f : 2.2f, F.bMega ? 650.f : 30.f, 2.2f);
		else if (Power == TEXT("raise_land") || Power == TEXT("sink_land")) StartHeave(Lat, Lon, F.bMega ? 1.6f : 1.1f, F.bMega ? 380.f : 18.f, 1.3f);
	}
}

// --- time travel ------------------------------------------------------------------------------------------------

void AFNEarthPlay::JumpTo(int32 Beat)
{
	// Not the next beat: no deltas. The board, the globe's tiles and the field (phenomena + hash) as they stood at Beat.
	using namespace FN7EarthPlay;
	AFNStudio* St = FindStudio();
	UFNNetClient* N = Net.Get();
	if (!St || !N || Beat < 0) return;
	JumpFrom = DrawnBeat;
	JumpBeat = Beat;
	bJumpBoard = bJumpTiles = bJumpField = true;
	int32 Q0, R0;
	if (St->BoardCentreHex(Q0, R0))
	{
		const FString Centre = St->BoardCellKey(Q0, R0);
		if (const TSharedPtr<FJsonObject>* Hit = BoardCache.Find(FString::Printf(TEXT("%s@%d"), *Centre, Beat))) ApplyJumpBoard(*Hit);   // instant
		else
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("cell"), Centre);
			A->SetNumberField(TEXT("beat"), Beat);
			A->SetNumberField(TEXT("max_phenomena"), 0);
			N->Query(TEXT("game.earth"), A);
			AskedAt = FPlatformTime::Seconds();
			LastCentre = Centre;
		}
	}
	TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
	T->SetNumberField(TEXT("tiles"), 4);
	T->SetNumberField(TEXT("beat"), Beat);
	N->Query(TEXT("game.earth"), T);
	TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
	F->SetNumberField(TEXT("beat"), Beat);
	N->Query(TEXT("game.earth_at"), F);
	DrawnBeat = Beat;
}

void AFNEarthPlay::ApplyJumpBoard(const TSharedPtr<FJsonObject>& D)
{
	// A jump's board: the whole disc replaces what was drawn, heights at once (no rise), caps cross-fading in 0.3 s.
	using namespace FN7EarthPlay;
	bJumpBoard = false;
	const TSharedPtr<FJsonObject> B = Obj(D, TEXT("board"));
	if (!B.IsValid()) return;
	ApplyBoard(B);
	BoardBeat = (int32)Num(D, TEXT("beat"), JumpBeat);
	if (bMovePending) { bMovePending = false; HeightsNow(); }
	CapBlend = 0.3f;
	RebuildCaps(0.f);
}

void AFNEarthPlay::ApplyBaseline(const TSharedPtr<FJsonObject>& Board)
{
	// The first beat's heights for this disc become its baseline; what is drawn now is re-measured from them.
	using namespace FN7EarthPlay;
	AFNStudio* St = FindStudio();
	const FString Centre = Str(Board, TEXT("center"));
	BaseKnown.Add(Centre);
	TArray<FString> Parts;
	Centre.ParseIntoArray(Parts, TEXT(":"));
	if (!St || Parts.Num() != 4) return;
	const int32 Q0 = FCString::Atoi(*Parts[2]), R0 = FCString::Atoi(*Parts[3]);
	const TArray<uint8> Data = Bytes(Str(Board, TEXT("data")));
	const TArray<FIntPoint>& Order = RingOrder();
	int32 Moved = 0;
	for (int32 j = 0; j < Data.Num() / 9 && Order.IsValidIndex(j); ++j)
	{
		const FString Key = FString::Printf(TEXT("%s:%s:%d:%d"), *Parts[0], *Parts[1], Q0 + Order[j].X, R0 + Order[j].Y);
		uint8& Base = Base16.FindOrAdd(Key, Data[j * 9]);
		if (Base != Data[j * 9]) { Base = Data[j * 9]; ++Moved; }
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: first-beat baseline for %s (%d hexes differ from first sight)"), *Centre, Moved);
	if (Moved > 0) HeightsNow();
}

void AFNEarthPlay::HeightsNow()
{
	// Every drawn hex of the board on the table at its height and state from Cur and its baseline, in one rebuild.
	using namespace FN7EarthPlay;
	AFNStudio* St = FindStudio();
	int32 Q0, R0;
	if (!St || !St->BoardCentreHex(Q0, R0)) return;
	for (const FIntPoint& H : RingOrder())
	{
		const int32 Q = Q0 + H.X, R = R0 + H.Y;
		const FString Key = St->BoardCellKey(Q, R);
		const FRec* Rec = Cur.Find(Key);
		const uint8* Base = Base16.Find(Key);
		if (!Rec || !Base) continue;
		const int32 Dl = ((int32)Rec->H16 - *Base) / 16;
		const uint8 State = (uint8)StateOf(*Rec, TopName(Rec->Top), Dl);
		float Def; FVector W;
		if (Dl != 0 && !DefaultTop.Contains(Key) && St->BoardHexTop(Q, R, false, Def, W)) DefaultTop.Add(Key, Def);
		const float* Dp = DefaultTop.Find(Key);
		const bool bHeight = Dl != 0 && Dp;
		St->SetHexOverride(Key, bHeight ? FMath::Clamp(*Dp + Dl * CmPerLevel, 0.6f, 24.f) : NAN, (EFNHexState)State);
		Committed.Add(Key, bHeight ? Dl * CmPerLevel : NAN);
		CommittedState.Add(Key, State);
	}
	Moves.Reset();
	St->RebuildBoard();
}

void AFNEarthPlay::OnFieldAt(const TSharedPtr<FJsonObject>& D)
{
	// The field at a beat: its phenomena (stable ids, snapped, no tween across a jump) and its hash, checked.
	using namespace FN7EarthPlay;
	const int32 Beat = (int32)Num(D, TEXT("beat"), -1);
	if (!bJumpField || Beat != JumpBeat) return;
	bJumpField = false;
	ClearPhenomena();
	static const TArray<TSharedPtr<FJsonValue>> None;
	const TArray<TSharedPtr<FJsonValue>>* List = Arr(D, TEXT("phenomena"));
	SyncPhenomena(List ? *List : None, nullptr);
	for (TPair<int32, FPhen>& P : Phens) { P.Value.ToLat = P.Value.Lat; P.Value.ToLon = P.Value.Lon; }   // a jump snaps
	BeatStart = FPlatformTime::Seconds();
	const FString Hash = Str(D, TEXT("hash"));
	DrawnHash = Hash;
	const FString* Want = OutcomeHash.Find(Beat);
	if (Want && !Hash.IsEmpty() && *Want != Hash)
	{
		UE_LOG(LogTemp, Warning, TEXT("FN7 earth: hash mismatch at %d (field %s, outcome %s)%s"), Beat, *Hash.Left(12), *Want->Left(12), bHashRetried ? TEXT("") : TEXT(": refetching"));
		if (!bHashRetried) { bHashRetried = true; BoardCache.Empty(); CacheOrder.Reset(); JumpTo(Beat); }
		return;
	}
	bHashRetried = false;
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: jump to beat %d (from %d): board full, %d phenomena, hash %s"), Beat, JumpFrom, Phens.Num(), Want ? TEXT("ok") : TEXT("unverified (no outcome for that beat seen)"));
}

void AFNEarthPlay::OnSnapshot(const TSharedPtr<FJsonObject>& PresentEarth)
{
	using namespace FN7EarthPlay;
	if (!PresentEarth.IsValid()) return;
	FirstBeat = (int32)Num(PresentEarth, TEXT("first_beat"), FirstBeat);
	const int32 Beat = (int32)Num(PresentEarth, TEXT("beat"), -1);
	const FString Hash = Str(PresentEarth, TEXT("hash"));
	if (Beat < 0) return;
	if (!Hash.IsEmpty()) OutcomeHash.Add(Beat, Hash);
	LastBeat = FMath::Max(LastBeat, Beat);
	if (!bHeld && Beat != DrawnBeat) JumpTo(Beat);
}

void AFNEarthPlay::ScrubTo(int32 Beat)
{
	bHeld = true;
	JumpTo(Beat);
}

void AFNEarthPlay::BackToLive()
{
	bHeld = false;
	if (LastBeat >= 0 && LastBeat != DrawnBeat) JumpTo(LastBeat);
}

void AFNEarthPlay::ClearPhenomena()
{
	for (TPair<int32, FPhen>& P : Phens)
	{
		for (UStaticMeshComponent* C : P.Value.Parts) if (C) C->DestroyComponent();
		if (P.Value.Light) P.Value.Light->DestroyComponent();
		if (P.Value.GlobeDot) P.Value.GlobeDot->DestroyComponent();
	}
	Phens.Reset();
}

void AFNEarthPlay::ResetMatch()
{
	// A new match: a new earth. Nothing of the last one stays on the table.
	AFNStudio* St = FindStudio();
	if (St)
	{
		EndHeave(St, false);
		St->ClearHexOverrides();
		St->RebuildBoard();
	}
	ClearPhenomena();
	Cur.Reset(); Base16.Reset(); DefaultTop.Reset(); Committed.Reset(); CommittedState.Reset();
	Moves.Reset(); bMovePending = false;
	GlobeTiles.Reset();
	if (Caps) Caps->ClearInstances();
	if (GlobeCaps) GlobeCaps->ClearInstances();
	BoardCache.Empty(); CacheOrder.Reset(); BaseKnown.Reset(); BaseAsked.Reset(); OutcomeHash.Reset();
	DrawnBeat = LastBeat = BoardBeat = GlobeBeat = JumpBeat = -1;
	bJumpBoard = bJumpTiles = bJumpField = bHeld = false;
	FirstBeat = 1;
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: new match, the table's earth cleared"));
}

// --- the Populous earth-heave -----------------------------------------------------------------------------------

void AFNEarthPlay::StartHeave(double Lat, double Lon, float AmpCm, float RadiusKm, float Dur)
{
	using namespace FN7EarthPlay;
	AFNStudio* St = FindStudio();
	int32 Q0, R0;
	if (!St || !St->BoardCentreHex(Q0, R0)) return;
	FN7Sound::Play(GetWorld(), FN7Sound::ESnd::Rumble, 0.45f);   // the ground heaving
	const double Now = FPlatformTime::Seconds();
	for (const FHeave& H : Heaves) if (Now - H.Start < 1.5 && Km(H.Lat, H.Lon, Lat, Lon) < RadiusKm * 0.3) return;   // the same quake twice
	const FString Board = St->BoardCellKey(Q0, R0);
	if (Board != HeaveBoard) { EndHeave(St, false); HeaveBoard = Board; }
	Heaves.Add({ Lat, Lon, Now, AmpCm, RadiusKm, Dur });
	int32 Added = 0;
	for (const FIntPoint& H : RingOrder())
	{
		const int32 Q = Q0 + H.X, R = R0 + H.Y;
		double La, Lo;
		if (!St->BoardHexLatLon(Q, R, La, Lo) || Km(La, Lo, Lat, Lon) > RadiusKm) continue;
		const FString Key = St->BoardCellKey(Q, R);
		if (HeaveHexes.Contains(Key)) continue;
		float Def; FVector W;
		if (!St->BoardHexTop(Q, R, false, Def, W)) continue;
		HeaveHexes.Add(Key, { Key, La, Lo, Def });
		++Added;
	}
	UE_LOG(LogTemp, Display, TEXT("FN7 earth: the land heaves at %.2f, %.2f (%d hexes, %.0f km, %.1f cm)"), Lat, Lon, Added, RadiusKm, AmpCm);
}

void AFNEarthPlay::TickHeave(double Now, AFNStudio* St)
{
	using namespace FN7EarthPlay;
	if (Heaves.Num() == 0) return;
	int32 Q0, R0;
	// Never under a held hand or during a zoom, and only on the board it started on: it settles at once instead.
	if (St->IsHandHolding() || St->IsZoomAnimating() || !St->BoardCentreHex(Q0, R0) || St->BoardCellKey(Q0, R0) != HeaveBoard) { EndHeave(St, false); return; }
	Heaves.RemoveAll([Now](const FHeave& H) { return Now - H.Start > H.Dur; });
	if (Heaves.Num() == 0) { EndHeave(St, true); return; }
	for (const TPair<FString, FHeaveHex>& It : HeaveHexes)
	{
		const FHeaveHex& X = It.Value;
		float Off = 0.f;
		for (const FHeave& H : Heaves)
		{
			const float D = (float)Km(X.Lat, X.Lon, H.Lat, H.Lon);
			if (D > H.RadiusKm) continue;
			const float T = (float)(Now - H.Start) - 0.55f * D / H.RadiusKm;   // the wave runs out over ~half a second
			if (T <= 0.f) continue;
			const float Fall = 0.35f + 0.65f * (1.f - D / H.RadiusKm);
			Off += H.Amp * Fall * FMath::Exp(-2.6f * T) * FMath::Sin(2.f * PI * 2.1f * T);   // up first, then down, settling
		}
		const float* C = Committed.Find(X.Key);
		const float Base = X.Def + ((C && !FMath::IsNaN(*C)) ? *C : 0.f);
		const uint8* State = CommittedState.Find(X.Key);
		St->SetHexOverride(X.Key, FMath::Clamp(Base + Off, 0.6f, 24.f), State ? (EFNHexState)*State : EFNHexState::Normal);
	}
	St->RebuildBoard();
}

void AFNEarthPlay::EndHeave(AFNStudio* St, bool bRebuild)
{
	// Settle every heaved hex back to what the earth committed (or to the board's own relief).
	for (const TPair<FString, FHeaveHex>& It : HeaveHexes)
	{
		const float* C = Committed.Find(It.Key);
		const uint8* State = CommittedState.Find(It.Key);
		const bool bHeight = C && !FMath::IsNaN(*C) && !FMath::IsNearlyZero(*C);
		St->SetHexOverride(It.Key, bHeight ? FMath::Clamp(It.Value.Def + *C, 0.6f, 24.f) : NAN, State ? (EFNHexState)*State : EFNHexState::Normal);
	}
	if (bRebuild && HeaveHexes.Num() > 0) St->RebuildBoard();   // (a held hand's own re-centre picks it up otherwise)
	HeaveHexes.Reset();
	Heaves.Reset();
}

// --- every frame ------------------------------------------------------------------------------------------------

void AFNEarthPlay::Tick(float Dt)
{
	using namespace FN7EarthPlay;
	Super::Tick(Dt);
	const double Now = FPlatformTime::Seconds();
	AFNStudio* St = FindStudio();
	if (!St) return;
	// Nothing under a moving hand: answers wait, and the board is asked for only once it rests.
	if (St->IsBoardPanning()) { CentreChangedAt = Now; return; }
	if (PendingBoard.IsValid()) { const TSharedPtr<FJsonObject> D = PendingBoard; PendingBoard.Reset(); OnReply(D); }
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
	const float U = FMath::Clamp((float)((Now - CapStart) / FMath::Max(0.05f, CapBlend)), 0.f, 1.f);
	if (CapMat) CapMat->SetScalarParameterValue(TEXT("BeatT"), U * U * (3.f - 2.f * U));
	if (bMovePending && Now - MoveStart > MoveTime + 0.05) CommitMoves();   // (never during a drag: see above)
	TickHeave(Now, St);
	for (TPair<int32, FPhen>& P : Phens) PlacePhen(P.Value, Now);
	for (int32 i = Flashes.Num() - 1; i >= 0; --i)
	{
		FFlash& F = Flashes[i];
		const float T = (float)(Now - F.At);
		const float S = (float)St->GetActorScale3D().X;
		const float Wd = (F.bMega ? 0.07f : 0.035f) * S * FMath::Max(0.f, 1.f - T / 0.8f);
		if (F.Beam) F.Beam->SetWorldScale3D(FVector(Wd, Wd, 0.9f * S));
		if (F.Ring)
		{
			const float Ru = FMath::Clamp(T / 0.9f, 0.f, 1.f), Rad = (F.bMega ? 28.f : 14.f) * S * FMath::Sqrt(Ru);
			F.Ring->SetWorldScale3D(T < 1.1f ? FVector(Rad / 50.f, Rad / 50.f, 0.004f * S * (1.f - Ru) + 0.0005f) : FVector::ZeroVector);
		}
		if (F.Light) F.Light->SetIntensity(FN7HudPrefs::ReduceFlashing() ? 0.f : (F.bMega ? 220.f : 90.f) * FMath::Max(0.f, 1.f - T / 1.0f));
		if (F.Label)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				if (PC->PlayerCameraManager) F.Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation() - F.Label->GetComponentLocation()).Rotation());
		}
		if (T > 2.2f)
		{
			if (F.Beam) F.Beam->DestroyComponent();
			if (F.Ring) F.Ring->DestroyComponent();
			if (F.Light) F.Light->DestroyComponent();
			if (F.Label) F.Label->DestroyComponent();
			Flashes.RemoveAt(i);
		}
	}
}
