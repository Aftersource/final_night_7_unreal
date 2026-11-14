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
		Top = 0.4f + FN7Board::Relief * (float)((FMath::Max(0.0, ElevationM(V, TableS, La, Lo)) - TableLo) / TableRange);
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
			const float Top = 0.4f + FN7Board::Relief * (float)((FMath::Max(0.0, ElevationM(V, TableS, PLa, PLo)) - TableLo) / TableRange);
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
	const float Top = 0.4f + FN7Board::Relief * (float)((FMath::Max(0.0, ElevationM(V, TableS, Lat, Lon)) - TableLo) / TableRange);
	const FVector Local = FN7Board::ActionC + FVector((float)((Px - TableCx) * TableK), (float)(-(Py - TableCy) * TableK), Top);
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
	// Every frame while the kit's gods stand at the table (UpdateSeats turns the aura beads back on each tick).
	for (FSeatSlot& S : SeatSlots)
	{
		if (S.Fig) S.Fig->SetVisibility(false, true);
		if (S.Body) S.Body->SetVisibility(false, true);
		for (UStaticMeshComponent* B : S.Aura) if (B) B->SetVisibility(false);
		if (S.Label) S.Label->SetVisibility(false);
		if (S.Glow) S.Glow->SetIntensity(0.f);
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
			C->SetVisibility(false);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	if (WillLabel) WillLabel->SetVisibility(false);
}

void AFNStudio::FocusBoard(double Lat, double Lon, int32 Level)
{
	// The table's zoom levels as FNTableScene defines them in FNStudioTable.cpp (hex size in m, 22 hexes to the edge).
	static const double LevelHexM[4] = { 3.0, 30.0, 3000.0, 120000.0 };
	static constexpr double HexesToEdge = 22.0;
	if (!bHasFocus) return;   // only during a match
	MapLevel = FMath::Clamp(Level, 0, 3);
	PickRadiusM = LevelHexM[MapLevel] * HexesToEdge;
	PickLat = FMath::Clamp(Lat, -89.9, 89.9);
	PickLon = Lon;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	bUserPicked = true;
	BuildTable();
	RebuildIn = -1.f;
}
