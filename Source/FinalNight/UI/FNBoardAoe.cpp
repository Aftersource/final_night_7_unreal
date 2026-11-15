// AFNStudio's two style-kit hooks (declared in FNStudio.h): which board hex is under a viewport pixel, and an
// area-of-effect overlay of lit hexes on the board around it. They reuse the table's own scene state (TableFace,
// TableCx/Cy, TableK, TableS, TableLo/Range, PickRadiusM) and the FNPlanet hex math, so a lit hex sits exactly
// on a board hex at every zoom level.
#include "FNStudio.h"
#include "FNPlanet.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace FN7Board
{
	// The circle of action on the table, as FNTableScene defines it in FNStudioTable.cpp (actor space, cm).
	static const FVector ActionC(0.f, 70.f, 80.f);
	static constexpr float ActionR = 135.f;
	static constexpr float Relief = 14.f;
	static constexpr int32 MaxLit = 700;

	// The board's ground height, as FNTableScene::GroundH in FNStudioTable.cpp (keep in step with it): water low and
	// flat at 0.6 cm, land in 1.7 cm terraces from 2.2 cm. Water = below sea level here (its BiomeOf is file-local there).
	static float GroundTop(double Elev, double Lo, double Range)
	{
		if (Elev < 0.0) return 0.6f;
		return 2.2f + 1.7f * FMath::RoundToFloat(7.f * (float)((FMath::Max(0.0, Elev) - Lo) / FMath::Max(Range, 1e-6)));
	}

	struct FOverlay
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Area;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Target;
	};
	static TMap<TWeakObjectPtr<AFNStudio>, FOverlay> Overlays;
}

bool AFNStudio::HexAtScreen(const FVector2D& ViewportPixel, double& OutLat, double& OutLon, FVector& OutWorldCenter, double& OutHexM) const
{
	using namespace FNPlanet;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || TableFace < 0 || TableK <= 0.0) return false;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(ViewportPixel.X, ViewportPixel.Y, O, D)) return false;
	O = GetActorTransform().InverseTransformPosition(O);
	D = GetActorTransform().InverseTransformVectorNoScale(D);
	if (D.Z > -1e-3f) return false;

	// Meet the ray with the top of the hex it crosses (tiles stand up to Relief cm tall): settle in a few steps.
	float Top = 0.f;
	FVector Hit = FVector::ZeroVector;
	FVector2D C = FVector2D::ZeroVector;
	double La = 0.0, Lo = 0.0;
	for (int32 It = 0; It < 6; ++It)
	{
		Hit = O + D * ((FN7Board::ActionC.Z + Top - O.Z) / D.Z) - FN7Board::ActionC;
		const double Px = TableCx + Hit.X / TableK, Py = TableCy - Hit.Y / TableK;
		int32 Q, R;
		PlaneToHex(Px, Py, TableS, Q, R);
		C = HexToPlane(Q, R, TableS);
		const FVector3d V = PlaneToVec(TableFace, C.X, C.Y);
		VecToLatLon(V, La, Lo);
		Top = BoardHeightAtHex(Q, R, FN7Board::GroundTop(ElevationM(V, TableS, La, Lo), TableLo, TableRange));   // the earth's overrides too
	}
	if (Hit.X * Hit.X + Hit.Y * Hit.Y > FN7Board::ActionR * FN7Board::ActionR) return false;

	OutLat = La;
	OutLon = Lo;
	OutHexM = TableS;
	const FVector Local = FN7Board::ActionC + FVector((float)((C.X - TableCx) * TableK), (float)(-(C.Y - TableCy) * TableK), Top);
	OutWorldCenter = GetActorTransform().TransformPosition(Local);
	return true;
}

void AFNStudio::ShowAreaOfEffect(bool bShow, const FVector2D& ViewportPixel, double RadiusKm)
{
	using namespace FNPlanet;

	FN7Board::FOverlay& Ov = FN7Board::Overlays.FindOrAdd(this);
	auto MakeSet = [this](const TCHAR* Name, const FLinearColor& Color) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, FName(Name));
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
			M->SetVectorParameterValue(TEXT("Color"), Color);
			C->SetMaterial(0, M);
		}
		C->RegisterComponent();
		return C;
	};
	if (!Ov.Area.IsValid()) Ov.Area = MakeSet(TEXT("FN7AoeArea"), FLinearColor::FromSRGBColor(FColor(255, 128, 40)));
	if (!Ov.Target.IsValid()) Ov.Target = MakeSet(TEXT("FN7AoeTarget"), FLinearColor::FromSRGBColor(FColor(255, 214, 90)));
	UInstancedStaticMeshComponent* Area = Ov.Area.Get();
	UInstancedStaticMeshComponent* Target = Ov.Target.Get();
	Area->ClearInstances();
	Target->ClearInstances();

	double La, Lo, HexM;
	FVector World;
	if (!bShow || TableFace < 0 || !HexAtScreen(ViewportPixel, La, Lo, World, HexM)) return;

	// The centre hex on the table's face plane.
	int32 Face;
	double U, W;
	VecToFaceUV(LatLonToVec(La, Lo), Face, U, W);
	if (Face != TableFace) return;
	int32 Q0, R0;
	PlaneToHex(U * RadiusM, W * RadiusM, TableS, Q0, R0);
	const FVector2D C0 = HexToPlane(Q0, R0, TableS);

	const double Reach = FMath::Max(RadiusKm * 1000.0, 0.0);
	const int32 Span = FMath::Clamp(FMath::CeilToInt(Reach / (TableS * 1.5)) + 1, 1, 40);
	const float TileW = (float)(TableS * TableK) * 0.97f * 0.88f;   // a little inside the board hex, so its edge shows
	int32 Lit = 0;
	for (int32 Dq = -Span * 2; Dq <= Span * 2 && Lit < FN7Board::MaxLit; ++Dq)
		for (int32 Dr = -Span; Dr <= Span && Lit < FN7Board::MaxLit; ++Dr)
		{
			const FVector2D P = HexToPlane(Q0 + Dq, R0 + Dr, TableS);
			const bool bCentre = Dq == 0 && Dr == 0;
			if (!bCentre && FVector2D::Distance(P, C0) > Reach) continue;
			if (FVector2D::Distance(P, FVector2D(TableCx, TableCy)) > PickRadiusM) continue;   // only hexes on the board
			const FVector3d V = PlaneToVec(TableFace, P.X, P.Y);
			double PLa, PLo;
			VecToLatLon(V, PLa, PLo);
			const float Top = BoardHeightAtHex(Q0 + Dq, R0 + Dr, FN7Board::GroundTop(ElevationM(V, TableS, PLa, PLo), TableLo, TableRange));
			const FVector Pos = FN7Board::ActionC + FVector((float)((P.X - TableCx) * TableK), (float)(-(P.Y - TableCy) * TableK), Top + (bCentre ? 0.9f : 0.45f));
			const float Thick = bCentre ? 1.6f : 0.7f;
			UInstancedStaticMeshComponent* Set = bCentre ? Target : Area;
			for (int32 k = 0; k < 3; ++k)
				Set->AddInstance(FTransform(FRotator(0.f, 60.f * k, 0.f), Pos, FVector(TileW * 1.7320508f / 100.f, TileW / 100.f, Thick / 100.f)));
			++Lit;
		}
}

bool AFNStudio::BoardPointFor(double Lat, double Lon, FVector& OutWorld) const
{
	using namespace FNPlanet;
	if (TableFace < 0 || TableK <= 0.0) return false;
	const FVector3d V = LatLonToVec(Lat, Lon);
	int32 Face;
	double U, W;
	VecToFaceUV(V, Face, U, W);
	if (Face != TableFace) return false;
	const double Px = U * RadiusM, Py = W * RadiusM;
	if (FVector2D::Distance(FVector2D(Px, Py), FVector2D(TableCx, TableCy)) > PickRadiusM) return false;
	int32 HQ, HR;
	PlaneToHex(Px, Py, TableS, HQ, HR);
	const float Top = BoardHeightAtHex(HQ, HR, FN7Board::GroundTop(ElevationM(V, TableS, Lat, Lon), TableLo, TableRange));   // risen / sunk by the earth
	FVector Local = FN7Board::ActionC + FVector((float)((Px - TableCx) * TableK), (float)(-(Py - TableCy) * TableK), Top);
	if (MapPivot) Local = MapPivot->GetRelativeTransform().TransformPosition(Local);   // ride the zoom animation
	OutWorld = GetActorTransform().TransformPosition(Local);
	return true;
}

bool AFNStudio::GetBoardRim(FVector& OutCenter, double& OutRadiusWorld, FRotator& OutTableRot) const
{
	OutCenter = GetActorTransform().TransformPosition(FN7Board::ActionC);
	OutRadiusWorld = FN7Board::ActionR * GetActorScale3D().X;
	OutTableRot = GetActorRotation();
	return true;
}

void AFNStudio::HideClassicSeats()
{
	// Hidden-in-game, not invisible: UpdateSeats flips the beads' visibility every tick, and fighting it with
	// SetVisibility each frame rebuilt their render state twice a frame. bHiddenInGame wins and nobody touches it.
	// The studio's own demigod markers on the board (an aura pillar, a light column, a base ring, a label) stand at the
	// same lat/lon as the kit's raised diorama cards and cut right through them (Chris noted poles in front of the
	// portraits"): while the kit's pieces stand, the card is the only thing at a demigod's spot.
	auto Hide = [](UPrimitiveComponent* C)
	{
		if (!C) return;
		if (!C->bHiddenInGame) C->SetHiddenInGame(true);
		if (C->GetCollisionEnabled() != ECollisionEnabled::NoCollision) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	};
	for (UStaticMeshComponent* C : HeroMarks) Hide(C);
	for (UStaticMeshComponent* C : HeroBeams) Hide(C);
	for (UStaticMeshComponent* C : HeroRings) Hide(C);
	for (UTextRenderComponent* C : HeroLabels) Hide(C);
	for (FSeatSlot& S : SeatSlots)
	{
		if (S.Fig) S.Fig->SetHiddenInGame(true, true);
		if (S.Body) S.Body->SetHiddenInGame(true, true);
		for (UStaticMeshComponent* B : S.Aura) if (B) B->SetHiddenInGame(true);
		if (S.Label) S.Label->SetHiddenInGame(true);
		if (S.Glow) S.Glow->SetHiddenInGame(true);
	}
}

bool AFNStudio::GlobePointAtScreen(const FVector2D& ViewportPixel, double& OutLat, double& OutLon, FVector& OutWorld) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !BoardRoot) return false;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(ViewportPixel.X, ViewportPixel.Y, O, D)) return false;
	// The globe: a sphere of BoardR (board-root space) around BoardRoot, spinning with it.
	const FTransform Xf = BoardRoot->GetComponentTransform();
	const FVector C = Xf.GetLocation();
	const double R = BoardR * Xf.GetScale3D().GetMax();
	const FVector OC = O - C;
	const double B = FVector::DotProduct(OC, D);
	const double Disc = B * B - (OC.SizeSquared() - R * R);
	if (Disc < 0.0) return false;
	const double T = -B - FMath::Sqrt(Disc);
	if (T <= 0.0) return false;
	OutWorld = O + D * T;
	const FVector L = Xf.InverseTransformPosition(OutWorld).GetSafeNormal();
	// The inverse of LonLatToGlobe: (cosLa cosLo, -cosLa sinLo, sinLa).
	OutLat = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(L.Z, -1.0, 1.0)));
	OutLon = FMath::RadiansToDegrees(FMath::Atan2(-L.Y, L.X));
	return true;
}

void AFNStudio::ShowMegaOnGlobe(bool bShow, double Lat, double Lon, double RadiusKm)
{
	static TMap<TWeakObjectPtr<AFNStudio>, TWeakObjectPtr<UInstancedStaticMeshComponent>> Rings;
	TWeakObjectPtr<UInstancedStaticMeshComponent>& Slot = Rings.FindOrAdd(this);
	if (!Slot.IsValid() && BoardRoot)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, TEXT("FN7MegaGlobe"));
		C->SetupAttachment(BoardRoot);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		if (BaseMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(FColor(255, 150, 40)) * 2.f);
			C->SetMaterial(0, M);
		}
		C->RegisterComponent();
		Slot = C;
	}
	UInstancedStaticMeshComponent* Ring = Slot.Get();
	if (!Ring) return;
	Ring->ClearInstances();
	if (!bShow) return;
	// A dotted fence at angular radius km / earth radius around the target, plus a bright cross on it (board-root space).
	const FVector Cn = LonLatToGlobe(Lon, Lat, 1.f).GetSafeNormal();
	const FVector Up = FMath::Abs(Cn.Z) > 0.95f ? FVector(1, 0, 0) : FVector(0, 0, 1);
	const FVector E1 = FVector::CrossProduct(Up, Cn).GetSafeNormal();
	const FVector E2 = FVector::CrossProduct(Cn, E1);
	const double Ang = FMath::Clamp(FMath::Max(RadiusKm, 300.0) / 6371.0, 0.02, PI * 0.95);
	const int32 N = 72;
	for (int32 i = 0; i < N; ++i)
	{
		const double T = 2.0 * PI * i / N;
		const FVector Dv = (Cn * FMath::Cos(Ang) + (E1 * FMath::Cos(T) + E2 * FMath::Sin(T)) * FMath::Sin(Ang)).GetSafeNormal();
		Ring->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Dv).Rotator(), Dv * (BoardR + 2.5f), FVector(0.022f, 0.022f, 0.05f)));
	}
	for (int32 k = 0; k < 2; ++k)
	{
		const FVector Axis = k == 0 ? E1 : E2;
		for (int32 j = -3; j <= 3; ++j)
		{
			const FVector Dv = (Cn + Axis * (0.018 * j)).GetSafeNormal();
			Ring->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Dv).Rotator(), Dv * (BoardR + 3.f), FVector(0.03f, 0.03f, 0.07f)));
		}
	}
}

void AFNStudio::HideWillBlock()
{
	if (const TObjectPtr<UStaticMeshComponent>* WP = Interactables.Find(WillPanel))
		if (UStaticMeshComponent* C = WP->Get())
		{
			C->SetHiddenInGame(true);
			if (C->GetCollisionEnabled() != ECollisionEnabled::NoCollision) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	if (WillLabel) WillLabel->SetHiddenInGame(true);
}

void AFNStudio::FocusBoard(double Lat, double Lon, int32 Level)
{
	bReliefHold = false;   // a jump to a place: its own relief scale
	// The table's zoom levels as FNTableScene defines them in FNStudioTable.cpp (hex size in m, 22 hexes to the edge).
	static const double LevelHexM[4] = { 3.0, 30.0, 3000.0, 120000.0 };
	static constexpr double HexesToEdge = 22.0;
	if (!bHasFocus) return;   // only during a match
	const int32 NewLevel = Level < 0 ? FMath::Max(MapLevel - 1, 0) : FMath::Clamp(Level, 0, 3);   // -1: one level closer than now
	if (IsSameBoardSpot(Lat, Lon, NewLevel)) return;   // nothing changes: no rebuild (the wheel's storm)
	FVector OldW;
	const bool bOld = BoardPointFor(Lat, Lon, OldW);
	const FVector OldLocal = GetActorTransform().InverseTransformPosition(OldW);
	const double OldR = PickRadiusM;
	const bool bLevelChange = NewLevel != MapLevel;
	MapLevel = NewLevel;
	PickRadiusM = LevelHexM[MapLevel] * HexesToEdge;
	PickLat = FMath::Clamp(Lat, -89.9, 89.9);
	PickLon = Lon;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	bUserPicked = true;
	ZoomAnimT = -1.f;
	if (bLevelChange) BeginZoomGhost();   // the old level stays on screen through the switch
	if (MapPivot) { MapPivot->SetRelativeScale3D(FVector::OneVector); MapPivot->SetRelativeLocation(FVector::ZeroVector); }
	NoteRebuild(TEXT("FocusBoard"));
	BuildTable();
	RebuildIn = -1.f;
	FVector NewW;
	const FVector NewLocal = BoardPointFor(Lat, Lon, NewW) ? GetActorTransform().InverseTransformPosition(NewW) : FN7Board::ActionC;
	BeginViewAnim(bOld, OldLocal, NewLocal, (float)(PickRadiusM / FMath::Max(OldR, 1.0)));
}

void AFNStudio::SetFaultLinesAlpha(float Alpha)
{
	// The studio's own plate lines (the globe's "EarthPlates" and the board's PlateSet): hidden unless a mega is in hand.
	static TMap<TWeakObjectPtr<AFNStudio>, TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>> Found;
	TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>& Sets = Found.FindOrAdd(this);
	bool bStale = Sets.Num() == 0;
	for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& W : Sets) bStale |= !W.IsValid();
	if (bStale)
	{
		Sets.Reset();
		TArray<UInstancedStaticMeshComponent*> All;
		GetComponents<UInstancedStaticMeshComponent>(All);
		for (UInstancedStaticMeshComponent* C : All)
			if (C && (C == PlateSet || C->GetName().Contains(TEXT("Plates")))) Sets.Add(C);
	}
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& W : Sets)
	{
		UInstancedStaticMeshComponent* C = W.Get();
		if (!C) continue;
		C->SetVisibility(A > 0.02f);
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
			M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.35f, 0.05f) * (0.35f + 2.2f * A));
	}
}

void AFNStudio::ResetView()
{
	// The regions level (1 of 4), then home onto your own demigod the way a match starts.
	MapLevel = 3;
	PickRadiusM = 120000.0 * 22.0;
	ZoomAnimT = -1.f;
	if (MapPivot) { MapPivot->SetRelativeScale3D(FVector::OneVector); MapPivot->SetRelativeLocation(FVector::ZeroVector); }
	bUserPicked = false;
	bGlobeDrag = false;
	if (bHasFocus) MapHome();
	BuildTable();
	RebuildIn = -1.f;
}

void AFNStudio::CenterBoardAndGlobe(double Lat, double Lon)
{
	bReliefHold = false;   // a jump to a place: its own relief scale
	// The zoom level stays; only the centre moves (Chris noted that double clicking is not centring the globe map and table map).
	if (IsSameBoardSpot(Lat, Lon, MapLevel) && bUserPicked)
	{
		FocusDir = BoardDir(Lon, FMath::Clamp(Lat, -89.9, 89.9)).GetSafeNormal();   // the globe still turns to it
		bHasFocus = true;
		return;
	}
	FVector OldW;
	const bool bOld = BoardPointFor(Lat, Lon, OldW);
	const FVector OldLocal = GetActorTransform().InverseTransformPosition(OldW);
	PickLat = FMath::Clamp(Lat, -89.9, 89.9);
	PickLon = Lon;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	bUserPicked = true;
	FocusDir = PickDir;          // the globe turns this point toward you (UpdateGlobe's slerp)
	bHasFocus = true;            // also for a viewer or before your demigod is placed (else the globe only idles)
	ManualUntil = -1.f;          // even if you spun it by hand a moment ago
	ZoomAnimT = -1.f;
	if (MapPivot) { MapPivot->SetRelativeScale3D(FVector::OneVector); MapPivot->SetRelativeLocation(FVector::ZeroVector); }
	NoteRebuild(TEXT("CenterBoardAndGlobe"));
	BuildTable();
	RebuildIn = -1.f;
	FVector NewW;
	const FVector NewLocal = BoardPointFor(Lat, Lon, NewW) ? GetActorTransform().InverseTransformPosition(NewW) : FN7Board::ActionC;
	BeginViewAnim(bOld, OldLocal, NewLocal, 1.f);   // same zoom: it slides the demigod to the centre
}

void AFNStudio::GetViewSpot(double& OutLat, double& OutLon, bool& bOutPicked) const
{
	OutLat = PickLat;
	OutLon = PickLon;
	bOutPicked = bUserPicked;
}

void AFNStudio::RestoreViewSpot(double Lat, double Lon, bool bPicked)
{
	CenterBoardAndGlobe(Lat, Lon);   // the animated slide, zoom kept
	bUserPicked = bPicked;           // not picked before: the table follows your demigod again
}

void AFNStudio::SetGlobeFocus(double Lat, double Lon)
{
	FocusDir = BoardDir(Lon, FMath::Clamp(Lat, -89.9, 89.9)).GetSafeNormal();
	bHasFocus = true;
}

void AFNStudio::PreviewFocus(double Lat, double Lon, int32 Level)
{
	static const double LevelHexM[4] = { 3.0, 30.0, 3000.0, 120000.0 };
	MapLevel = FMath::Clamp(Level, 0, 3);
	PickRadiusM = LevelHexM[MapLevel] * 22.0;
	PickLat = FMath::Clamp(Lat, -89.9, 89.9);
	PickLon = Lon;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	bUserPicked = true;
	BuildTable();
	RebuildIn = -1.f;
}

namespace FN7View
{
	struct FAnim { float T = -1.f, Dur = 0.45f, S0 = 1.f; FVector From = FVector::ZeroVector, To = FVector::ZeroVector; };
	static TMap<TWeakObjectPtr<AFNStudio>, FAnim> Anims;
}

bool AFNStudio::BoardCentreHex(int32& OutQ, int32& OutR) const
{
	if (TableFace < 0 || TableK <= 0.0) return false;
	FNPlanet::PlaneToHex(TableCx, TableCy, TableS, OutQ, OutR);
	return true;
}

bool AFNStudio::BoardHexLatLon(int32 Q, int32 R, double& OutLat, double& OutLon) const
{
	using namespace FNPlanet;
	if (TableFace < 0 || TableK <= 0.0) return false;
	const FVector2D P = HexToPlane(Q, R, TableS);
	VecToLatLon(PlaneToVec(TableFace, P.X, P.Y), OutLat, OutLon);
	return true;
}

bool AFNStudio::TablePointAt(const FVector2D& ViewportPixel, FVector& OutLocal) const
{
	// Where the cursor's ray meets the table plane (a little above the ground: the hand holds the land's surface).
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector O, D;
	if (!PC || !PC->DeprojectScreenPositionToWorld(ViewportPixel.X, ViewportPixel.Y, O, D)) return false;
	O = GetActorTransform().InverseTransformPosition(O);
	D = GetActorTransform().InverseTransformVectorNoScale(D);
	if (D.Z > -1e-3f) return false;
	const float Z = FN7Board::ActionC.Z + 4.f;
	OutLocal = O + D * ((Z - O.Z) / D.Z);
	return true;
}

bool AFNStudio::BeginBoardPan(const FVector2D& ViewportPixel)
{
	FVector Hit;
	if (!MapPivot || TableK <= 0.0 || !TablePointAt(ViewportPixel, Hit)) return false;
	CatchHand();
	// Any zoom or slide in flight ends where it is going: the hand holds the board as it now stands.
	ZoomAnimT = -1.f;
	if (FN7View::FAnim* A = FN7View::Anims.Find(this)) A->T = -1.f;
	EndZoomGhost();
	MapPivot->SetRelativeScale3D(FVector::OneVector);
	MapPivot->SetRelativeLocation(FVector::ZeroVector);
	PanFromHit = Hit;
	PanCommitted = FVector::ZeroVector;
	PanHist.Reset();
	PanHist.Add({ FPlatformTime::Seconds(), FVector::ZeroVector });
	bBoardPan = true;
	UE_LOG(LogTemp, Display, TEXT("FN7 hand: grab board at %.4f, %.4f"), PickLat, PickLon);
	return true;
}

void AFNStudio::CommitBoardPan(const FVector& Offset)
{
	// The land moved Offset (studio cm) with the hand: centre the board where that puts it, rebuilt on the same planet
	// grid (PlaneToHex), so every tile lands where the slide left it; the globe turns to keep it at its front.
	using namespace FNPlanet;
	const double Cx = TableCx - Offset.X / TableK, Cy = TableCy + Offset.Y / TableK;
	double La, Lo;
	VecToLatLon(PlaneToVec(TableFace, Cx, Cy), La, Lo);
	PickLat = FMath::Clamp(La, -89.9, 89.9);
	PickLon = Lo;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	bUserPicked = true;
	if (bHasFocus) FocusDir = PickDir;
	if (MapPivot) MapPivot->SetRelativeLocation(FVector::ZeroVector);
	PanCommitted += Offset;
	bReliefHold = true;   // the same hills at the same heights: the re-centre is seamless
	NoteRebuild(TEXT("hand"));
	BuildTable();
	RebuildIn = -1.f;
	UE_LOG(LogTemp, Display, TEXT("FN7 hand: pan to %.4f, %.4f"), PickLat, PickLon);
}

void AFNStudio::PanBoardTo(const FVector2D& ViewportPixel)
{
	FVector Hit;
	if (!bBoardPan || !MapPivot || !TablePointAt(ViewportPixel, Hit)) return;
	FVector Off = Hit - PanFromHit;
	Off.Z = 0.f;
	MapPivot->SetRelativeLocation(Off);   // the land slides with the hand, this frame (re-centred every 1.5 hexes, seamlessly)
	if (Off.Size2D() > FMath::Max((float)(1.5 * TableS * TableK), 6.f))
	{
		CommitBoardPan(Off);
		PanFromHit = Hit;
	}
	const double Now = FPlatformTime::Seconds();
	PanHist.Add({ Now, PanCommitted + MapPivot->GetRelativeLocation() });
	while (PanHist.Num() > 2 && Now - PanHist[0].Key > 0.5) PanHist.RemoveAt(0);
}

void AFNStudio::EndBoardPan()
{
	if (!bBoardPan) return;
	bBoardPan = false;
	// Regular panning (Chris): the board stays where the hand lets go, no coasting.
	const FVector Off = MapPivot ? MapPivot->GetRelativeLocation() : FVector::ZeroVector;
	if (Off.Size2D() > 0.05f) CommitBoardPan(Off);
	UE_LOG(LogTemp, Display, TEXT("FN7 hand: lets go of the board at %.4f, %.4f"), PickLat, PickLon);
}

// --- the planet -------------------------------------------------------------------------------------------------

bool AFNStudio::GlobeDirAt(const FVector2D& ViewportPixel, FVector& OutDirWorld, bool bSilhouette) const
{
	// The cursor's ray against the globe sphere; off the edge, the nearest point of its silhouette.
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !GlobeMesh) return false;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(ViewportPixel.X, ViewportPixel.Y, O, D)) return false;
	const FVector C = GlobeMesh->GetComponentLocation();
	const double R = GlobeMesh->Bounds.SphereRadius * 1.02;
	const FVector Oc = O - C;
	const double B = FVector::DotProduct(Oc, D), Cc = Oc.SizeSquared() - R * R, Disc = B * B - Cc;
	if (Disc >= 0.0 && -B - FMath::Sqrt(Disc) > 0.0)
	{
		OutDirWorld = (O + D * (-B - FMath::Sqrt(Disc)) - C).GetSafeNormal();
		return true;
	}
	if (!bSilhouette) return false;
	OutDirWorld = (O + D * FMath::Max(0.0, -B) - C).GetSafeNormal();
	return !OutDirWorld.IsNearlyZero();
}

bool AFNStudio::BeginGlobeGrab(const FVector2D& ViewportPixel)
{
	FVector A;
	if (!BoardRoot || !GlobeDirAt(ViewportPixel, A, false)) return false;
	CatchHand();
	GrabA = A;
	GrabStartQuat = BoardRoot->GetComponentQuat();
	GrabHist.Reset();
	GrabHist.Add({ FPlatformTime::Seconds(), GrabStartQuat });
	bGlobeGrab = true;
	ManualUntil = Time + 1.0e6f;   // the studio's own turning leaves it to the hand
	const FVector L = GrabStartQuat.UnrotateVector(A);
	UE_LOG(LogTemp, Display, TEXT("FN7 hand: grab globe at %.3f, %.3f"), FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(L.Z, -1.0, 1.0))), FMath::RadiansToDegrees(FMath::Atan2(-L.Y, L.X)));
	return true;
}

void AFNStudio::GlobeGrabTo(const FVector2D& ViewportPixel)
{
	FVector B;
	if (!bGlobeGrab || !BoardRoot || !GlobeDirAt(ViewportPixel, B, true)) return;
	// Always from the press-start quaternion: the grabbed point is exactly under the hand, no drift.
	const FQuat Q = FQuat::FindBetweenNormals(GrabA, B) * GrabStartQuat;
	BoardRoot->SetWorldRotation(Q.GetNormalized());
	const double Now = FPlatformTime::Seconds();
	GrabHist.Add({ Now, BoardRoot->GetComponentQuat() });
	while (GrabHist.Num() > 2 && Now - GrabHist[0].Key > 0.5) GrabHist.RemoveAt(0);
	// The board follows the front of the planet when the hand lets go (SettleGlobe(true)), never mid-drag: a board
	// rebuild is a frame's stall, and stalls under the hand read as lag (Chris noted it feels very laggy).
}

void AFNStudio::EndGlobeGrab()
{
	if (!bGlobeGrab || !BoardRoot) return;
	bGlobeGrab = false;
	{
		// The rule, checked: how far the grabbed point of the planet ended from under the hand (FindBetweenNormals: ~0).
		const FVector Local = GrabStartQuat.UnrotateVector(GrabA);
		const FVector Now3 = BoardRoot->GetComponentQuat().RotateVector(Local);
		const FVector Last = GrabHist.Num() ? GrabHist.Last().Value.RotateVector(Local) : Now3;
		UE_LOG(LogTemp, Display, TEXT("FN7 hand: release; the grabbed point is %.2f deg from where the hand left it"), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Now3, Last), -1.0, 1.0))));
	}
	const double Now = FPlatformTime::Seconds();
	GlobeOmega = FVector::ZeroVector;
	const FQuat QNow = BoardRoot->GetComponentQuat();
	// The oldest sample in the last 150 ms; at a low frame rate (a stall), the newest one up to half a second old.
	int32 Pick = INDEX_NONE;
	for (int32 i = 0; i < GrabHist.Num(); ++i) if (Now - GrabHist[i].Key <= 0.15 && Now - GrabHist[i].Key > 0.02) { Pick = i; break; }
	if (Pick == INDEX_NONE) for (int32 i = GrabHist.Num() - 1; i >= 0; --i) if (Now - GrabHist[i].Key > 0.02 && Now - GrabHist[i].Key <= 0.5) { Pick = i; break; }
	if (Pick != INDEX_NONE)
		{
			const TPair<double, FQuat>& H = GrabHist[Pick];
			FQuat Dq = QNow * H.Value.Inverse();
			Dq.Normalize();
			if (Dq.W < 0.f) Dq = FQuat(-Dq.X, -Dq.Y, -Dq.Z, -Dq.W);
			FVector Axis; float Angle;
			Dq.ToAxisAndAngle(Axis, Angle);
			GlobeOmega = Axis * (Angle / (float)(Now - H.Key));
		}
	if (GlobeOmega.Size() > 0.3f)
	{
		bGlobeCoast = true;
		LastFlingAt = Now;
		UE_LOG(LogTemp, Display, TEXT("FN7 hand: fling w=%.2f rad/s"), GlobeOmega.Size());
		StartHandTicker();
		return;
	}
	SettleGlobe(true);
}

void AFNStudio::SettleGlobe(bool bFinal)
{
	// The front of the planet (the point facing the camera) becomes the board's centre, at the zoom you're at.
	LastGlobeRecentre = FPlatformTime::Seconds();
	if (!BoardRoot || !GlobeMesh) return;
	FVector Cam = GlobeMesh->GetComponentLocation() + FVector(0.f, 350.f, 65.f);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector L = BoardRoot->GetComponentQuat().UnrotateVector((Cam - GlobeMesh->GetComponentLocation()).GetSafeNormal());
	const double La = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(L.Z, -1.0, 1.0))), Lo = FMath::RadiansToDegrees(FMath::Atan2(-L.Y, L.X));
	if (bHasFocus)
	{
		CenterBoardAndGlobe(La, Lo);   // a no-op inside the same hex (IsSameBoardSpot): no rebuild storm
		FocusDir = L;                  // hold it exactly where the hand left it
	}
	if (bFinal) ManualUntil = bHasFocus ? Time : Time + 20.f;   // focused: the slerp holds this spot; else idle a while
}

bool AFNStudio::CatchHand()
{
	const bool bWas = bGlobeCoast || bBoardCoast;
	if (bGlobeCoast) { bGlobeCoast = false; SettleGlobe(true); }
	if (bBoardCoast)
	{
		bBoardCoast = false;
		const FVector Off = MapPivot ? MapPivot->GetRelativeLocation() : FVector::ZeroVector;
		if (Off.Size2D() > 0.05f) CommitBoardPan(Off);
	}
	if (bWas) UE_LOG(LogTemp, Display, TEXT("FN7 hand: caught"));
	return bWas;
}

void AFNStudio::StartHandTicker()
{
	if (HandTicker.IsValid()) return;
	TWeakObjectPtr<AFNStudio> W(this);
	HandTicker = FTSTicker::GetCoreTicker().AddTicker(TEXT("FN7Hand"), 0.f, [W](float Dt)
	{
		AFNStudio* S = W.Get();
		if (!S) return false;
		const bool bMore = S->TickHand(FMath::Min(Dt, 0.1f));
		if (!bMore) S->HandTicker.Reset();
		return bMore;
	});
}

bool AFNStudio::TickHand(float Dt)
{
	if (bGlobeCoast && BoardRoot)
	{
		const float W = GlobeOmega.Size();
		BoardRoot->AddWorldRotation(FQuat(GlobeOmega / FMath::Max(W, 1e-6f), W * Dt));
		GlobeOmega *= FMath::Exp(-Dt / 0.6f);
		if (GlobeOmega.Size() < 0.05f) { bGlobeCoast = false; SettleGlobe(true); UE_LOG(LogTemp, Display, TEXT("FN7 hand: globe settles")); }
	}
	if (bBoardCoast && MapPivot)
	{
		FVector Off = MapPivot->GetRelativeLocation() + BoardVel * Dt;
		Off.Z = 0.f;
		MapPivot->SetRelativeLocation(Off);
		if (Off.Size2D() > FMath::Max((float)(1.5 * TableS * TableK), 6.f)) CommitBoardPan(Off);
		BoardVel *= FMath::Exp(-Dt / 0.5f);
		if (BoardVel.Size2D() < 3.f)
		{
			bBoardCoast = false;
			const FVector Rest = MapPivot->GetRelativeLocation();
			if (Rest.Size2D() > 0.05f) CommitBoardPan(Rest);
			UE_LOG(LogTemp, Display, TEXT("FN7 hand: board settles at %.4f, %.4f"), PickLat, PickLon);
		}
	}
	return bGlobeCoast || bBoardCoast;
}

void AFNStudio::NoteRebuild(const TCHAR* Why)
{
	// How often the board rebuilds (Benito's bridge found a storm: 203 in a minute from the wheel near a demigod).
	static double WindowStart = 0.0;
	static int32 Count = 0;
	++Count;
	const double Now = FPlatformTime::Seconds();
	if (WindowStart <= 0.0) WindowStart = Now;
	if (Now - WindowStart >= 60.0)
	{
		UE_LOG(LogTemp, Display, TEXT("FN7 board rebuilds: %d in the last minute (last: %s)"), Count, Why);
		Count = 0;
		WindowStart = Now;
	}
}

bool AFNStudio::IsSameBoardSpot(double Lat, double Lon, int32 Level) const
{
	// Same level and within about one hexagon of the current centre: nothing to rebuild.
	static const double LevelHexM[4] = { 3.0, 30.0, 3000.0, 120000.0 };
	if (Level != MapLevel) return false;
	const double L1 = FMath::DegreesToRadians(PickLat), L2 = FMath::DegreesToRadians(Lat), DO = FMath::DegreesToRadians(Lon - PickLon);
	const double M = FMath::Acos(FMath::Clamp(FMath::Sin(L1) * FMath::Sin(L2) + FMath::Cos(L1) * FMath::Cos(L2) * FMath::Cos(DO), -1.0, 1.0)) * 6371000.0;
	return M < LevelHexM[FMath::Clamp(MapLevel, 0, 3)];
}

void AFNStudio::BeginZoomGhost()
{
	// A copy of what the board shows now (every instanced set under MapPivot: tiles, trees, towns...), on a pivot of its
	// own, so a level switch never empties or swaps the board in one frame.
	if (!MapPivot) return;
	if (!GhostPivot)
	{
		GhostPivot = NewObject<USceneComponent>(this, TEXT("ZoomGhostPivot"));
		GhostPivot->SetupAttachment(RootComponent);
		GhostPivot->SetMobility(EComponentMobility::Movable);
		GhostPivot->RegisterComponent();
	}
	GhostBase = MapPivot->GetRelativeTransform();
	GhostPivot->SetRelativeTransform(GhostBase);
	int32 Used = 0;
	GhostPos.Reset();
	GhostHidden.Reset();
	GhostRadius.Reset();
	TArray<USceneComponent*> Kids;
	MapPivot->GetChildrenComponents(false, Kids);
	for (USceneComponent* K : Kids)
	{
		UInstancedStaticMeshComponent* Src = Cast<UInstancedStaticMeshComponent>(K);
		if (!Src || !Src->IsVisible() || Src->GetInstanceCount() == 0) continue;
		// Only the ground (Hex_*, Earth_*, Stone_*, coast): blown-up trees and towns read as blobs, not as a zoom.
		const FString N = Src->GetName();
		if (!(N.StartsWith(TEXT("Hex_")) || N.StartsWith(TEXT("Earth_")) || N.StartsWith(TEXT("Stone_")) || N == TEXT("TableCoast"))) continue;
		UInstancedStaticMeshComponent* G = ZoomGhosts.IsValidIndex(Used) ? ZoomGhosts[Used].Get() : nullptr;
		if (!G)
		{
			G = NewObject<UInstancedStaticMeshComponent>(this);
			G->SetupAttachment(GhostPivot);
			G->SetMobility(EComponentMobility::Movable);
			G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			G->SetCastShadow(false);
			G->RegisterComponent();
			ZoomGhosts.Add(G);
		}
		G->ClearInstances();
		G->SetStaticMesh(Src->GetStaticMesh());
		for (int32 m = 0; m < Src->GetNumMaterials(); ++m) G->SetMaterial(m, Src->GetMaterial(m));
		G->SetRelativeTransform(Src->GetRelativeTransform());
		TArray<FTransform> X;
		TArray<FVector>& P = GhostPos.AddDefaulted_GetRef();
		TArray<float>& Rad = GhostRadius.AddDefaulted_GetRef();
		X.Reserve(Src->GetInstanceCount());
		P.Reserve(Src->GetInstanceCount());
		for (int32 i = 0; i < Src->GetInstanceCount(); ++i)
		{
			FTransform T;
			Src->GetInstanceTransform(i, T, false);
			X.Add(T);
			P.Add(Src->GetRelativeTransform().TransformPosition(T.GetLocation()));
			Rad.Add(50.f * (float)FMath::Max(T.GetScale3D().X, T.GetScale3D().Y));   // a unit cube's half-width, scaled
		}
		G->AddInstances(X, false, false);
		G->SetVisibility(true);
		GhostHidden.AddZeroed_GetRef().AddZeroed(X.Num());
		++Used;
	}
	for (int32 i = Used; i < ZoomGhosts.Num(); ++i) { ZoomGhosts[i]->ClearInstances(); ZoomGhosts[i]->SetVisibility(false); }
	bZoomGhost = Used > 0;
}

void AFNStudio::TickZoomGhost(float NewScale, float Ease)
{
	if (!bZoomGhost || !GhostPivot) return;
	// The old level is the same map at the old zoom: it scales by NewScale / S0 around the anchor (S0 -> 1 on the new
	// board = 1 -> 1/S0 on the old), so the two move as one zoom; it flattens into the table as the new level rises.
	const float G = NewScale / FMath::Max(0.001f, ZoomAnimS0);
	// Going in it flattens by half-way (it is growing); going out it settles with the new level.
	const float Flat = FMath::Clamp(1.f - Ease * (G > 1.f ? 1.6f : 1.35f), 0.f, 1.f);
	TickGhostTransform(G, FVector(ZoomAnchor.X * (1.f - G), ZoomAnchor.Y * (1.f - G), ZoomAnchor.Z * (1.f - Flat)), Flat);
}

void AFNStudio::TickGhostTransform(float G, const FVector& Loc, float Flat)
{
	if (!bZoomGhost || !GhostPivot) return;
	if (Flat < 0.04f) { EndZoomGhost(); return; }   // sunk into the table: gone, no flat plates left under the new level
	GhostPivot->SetRelativeScale3D(GhostBase.GetScale3D() * FVector(G, G, FMath::Max(0.01f, Flat)));
	GhostPivot->SetRelativeLocation(GhostBase.GetLocation() + Loc);
	// What leaves the table's disc drops out (going in, the old tiles grow past the rim and would cover the room).
	const FVector C = FN7Board::ActionC;
	const float R2 = FMath::Square(FN7Board::ActionR * 1.02f);
	for (int32 s = 0; s < ZoomGhosts.Num() && s < GhostPos.Num(); ++s)
	{
		bool bChanged = false;
		for (int32 i = 0; i < GhostPos[s].Num(); ++i)
		{
			if (GhostHidden[s][i]) continue;
			const FVector& P = GhostPos[s][i];
			const float X = Loc.X + P.X * G, Y = Loc.Y + P.Y * G;
			const float Reach = FMath::Sqrt(FMath::Square(X - C.X) + FMath::Square(Y - C.Y)) + GhostRadius[s][i] * G;   // its far edge
			if (Reach * Reach <= R2) continue;
			FTransform T;
			if (ZoomGhosts[s]->GetInstanceTransform(i, T, false))
			{
				T.SetScale3D(FVector::ZeroVector);
				ZoomGhosts[s]->UpdateInstanceTransform(i, T, false, false, true);
				bChanged = true;
			}
			GhostHidden[s][i] = 1;
		}
		if (bChanged) ZoomGhosts[s]->MarkRenderStateDirty();
	}
}

void AFNStudio::EndZoomGhost()
{
	for (UInstancedStaticMeshComponent* G : ZoomGhosts) if (G) { G->ClearInstances(); G->SetVisibility(false); }
	GhostPos.Reset();
	GhostHidden.Reset();
	GhostRadius.Reset();
	bZoomGhost = false;
}


void AFNStudio::BeginViewAnim(bool bHaveOld, const FVector& OldLocal, const FVector& NewLocal, float ScaleFrom)
{
	// The new scene starts looking like the old one (the point drawn where it was, at the old scale) and eases into
	// place: zooming expands or contracts, re-centring slides. MapPivot: drawn = Scale * X + Location (studio local).
	if (!MapPivot) return;
	FN7View::FAnim& A = FN7View::Anims.FindOrAdd(this);
	A.T = 0.f;
	A.Dur = 0.45f;
	A.S0 = ScaleFrom < 1.f ? FMath::Max(ScaleFrom, 0.55f) : FMath::Min(ScaleFrom, 1.8f);   // ~1.8x of visible zoom per step, never a speck (see ZoomMapAt)
	A.To = NewLocal;
	A.From = bHaveOld ? OldLocal : NewLocal;
	MapPivot->SetRelativeScale3D(FVector(A.S0));
	MapPivot->SetRelativeLocation(A.From - A.To * A.S0);
}

void AFNStudio::TickViewAnim(float Dt)
{
	FN7View::FAnim* A = FN7View::Anims.Find(this);
	if (!A || A->T < 0.f || !MapPivot) return;
	if (ZoomAnimT >= 0.f) { A->T = -1.f; return; }   // the studio's own zoom took over
	A->T += Dt;
	const float U = FMath::Clamp(A->T / A->Dur, 0.f, 1.f);
	const float E = 1.f - FMath::Cube(1.f - U);                          // ease out
	const float Sc = FMath::Exp(FMath::Loge(A->S0) * (1.f - E));         // even speed in log scale, like a web map
	const FVector P = FMath::Lerp(A->From, A->To, E);                    // the point glides from where it was to its place
	const bool bLevel = !FMath::IsNearlyEqual(A->S0, 1.f, 0.01f);
	const float Rise = bLevel ? 0.55f + 0.45f * E : 1.f;                 // a new level settles up a little (as ZoomMapAt's)
	MapPivot->SetRelativeScale3D(FVector(Sc, Sc, Rise));
	MapPivot->SetRelativeLocation(FVector(P.X - A->To.X * Sc, P.Y - A->To.Y * Sc, P.Z - A->To.Z * Rise));
	if (bZoomGhost)
	{
		// The old level moves with the same transform at the old zoom (scale Sc / S0), flattening away.
		const float G = Sc / FMath::Max(0.001f, A->S0);
		const float Flat = FMath::Clamp(1.f - E * (G > 1.f ? 1.6f : 1.35f), 0.f, 1.f);
		TickGhostTransform(G, FVector(P.X - A->From.X * G, P.Y - A->From.Y * G, A->From.Z * (1.f - Flat)), Flat);
	}
	if (U >= 1.f)
	{
		A->T = -1.f;
		MapPivot->SetRelativeScale3D(FVector::OneVector);
		MapPivot->SetRelativeLocation(FVector::ZeroVector);
		EndZoomGhost();
	}
}
