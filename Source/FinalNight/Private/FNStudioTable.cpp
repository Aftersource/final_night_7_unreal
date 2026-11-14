// The circle of action (ported from the browser prototype client/globe/main.js buildTable):
// pick a spot on the floating Globe (click it; drag it to spin it), and that patch of the planet is built on the
// table as hex tiles at the right scale: terrain and biomes when you are far out, towns, then single people as
// you zoom in (mouse wheel, or + / -). The edge dissolves into fog; a projector beam joins the spot to the table.
// Every scene is a pure function of its planet position (FNPlanet.h), so going back shows the same ground.
#include "FNStudio.h"
#include "FNPlanet.h"

#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace FNTableScene
{
	static const FVector ActionC(0.f, 70.f, 80.f);   // centre of the circle of action, on the table top (actor space, cm)
	static constexpr float ActionR = 135.f;          // its radius on the table (cm)
	static constexpr float Relief = 14.f;            // tallest hill on the table (cm)
	static constexpr int32 MaxHex = 1400, MaxMarks = 2500, FogLevels = 4;
	static constexpr double MinR = 2.0, MaxR = 3000000.0;   // scene radius limits (planet meters)
	static const FColor BiomeRGB[FNPlanet::NumBiomes] = { FColor(28, 70, 140), FColor(214, 200, 150), FColor(96, 150, 70), FColor(40, 96, 52), FColor(128, 118, 110), FColor(236, 240, 245) };
	static const FLinearColor FogCol = FLinearColor::FromSRGBColor(FColor(143, 166, 191));
	static FString FmtM(double M)
	{
		if (M >= 10000) return FString::Printf(TEXT("%.0f km"), M / 1000);
		if (M >= 1000) return FString::Printf(TEXT("%.1f km"), M / 1000);
		return M < 10 ? FString::Printf(TEXT("%.1f m"), M) : FString::Printf(TEXT("%.0f m"), M);
	}
	static FString FmtN(double N)
	{
		if (N >= 1e9) return FString::Printf(TEXT("%.1f billion"), N / 1e9);
		if (N >= 1e6) return FString::Printf(TEXT("%.1f million"), N / 1e6);
		if (N >= 1e3) return FString::Printf(TEXT("%.1f thousand"), N / 1e3);
		return FString::Printf(TEXT("%.0f"), N);
	}
}
using namespace FNTableScene;

void AFNStudio::PickSpot(double Lat, double Lon, bool bByUser)
{
	PickLat = FMath::Clamp(Lat, -89.9, 89.9);
	PickLon = Lon;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	bUserPicked |= bByUser;
	if (bByUser) { PlaySfx(EFNSfx::Chime, 0.5f); if (OnUserPickedSpot) OnUserPickedSpot(PickLon, PickLat); }
	RebuildIn = 0.f;
}

void AFNStudio::BuildTable()
{
	using namespace FNPlanet;
	if (!BoardRoot) return;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MakeSet = [this, Cube](const FName& Name, const FLinearColor& Color) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Cube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BaseMat) { UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C); M->SetVectorParameterValue(TEXT("Color"), Color); C->SetMaterial(0, M); }
		C->RegisterComponent();
		return C;
	};
	if (HexSets.Num() == 0)
	{
		for (int32 B = 0; B < NumBiomes; ++B)
			for (int32 Sh = 0; Sh < 2; ++Sh)
				for (int32 F = 0; F < FogLevels; ++F)
				{
					const FLinearColor Base = FLinearColor::FromSRGBColor(BiomeRGB[B]) * (Sh ? 0.86f : 1.f);
					HexSets.Add(MakeSet(FName(*FString::Printf(TEXT("Hex_%d_%d_%d"), B, Sh, F)), FLinearColor::LerpUsingHSV(Base, FogCol, F / (float)FogLevels)));
				}
		PeopleSet = MakeSet(TEXT("TablePeople"), FLinearColor::FromSRGBColor(FColor(255, 225, 176)));
		TownSet = MakeSet(TEXT("TableTowns"), FLinearColor::FromSRGBColor(FColor(217, 212, 204)));
	}
	for (UInstancedStaticMeshComponent* C : HexSets) C->ClearInstances();
	PeopleSet->ClearInstances();
	TownSet->ClearInstances();

	// the hex lattice on the cube face under the picked spot
	const FVector3d C0 = LatLonToVec(PickLat, PickLon);
	int32 Face; double U, W;
	VecToFaceUV(C0, Face, U, W);
	const double Cx = U * RadiusM, Cy = W * RadiusM, R = PickRadiusM;
	double S = HexSizeFor(R, 22);
	while (PI * FMath::Square(R / S) / 2.6 > MaxHex) S *= 2;   // hard cap on tiles
	const double K = ActionR / R;                                // cm on the table per planet meter
	int32 Q0, R0; PlaneToHex(Cx, Cy, S, Q0, R0);
	const int32 Span = FMath::CeilToInt(R / (S * 1.5)) + 1;
	struct FT { int32 Q, Rr; float X, Y, D; double Elev; EBiome B; double People; };
	TArray<FT> Cells;
	for (int32 Dq = -Span * 2; Dq <= Span * 2; ++Dq)
		for (int32 Dr = -Span; Dr <= Span; ++Dr)
		{
			const int32 Q = Q0 + Dq, Rr = R0 + Dr;
			const FVector2D P = HexToPlane(Q, Rr, S);
			const double Dx = P.X - Cx, Dy = P.Y - Cy, D = FMath::Sqrt(Dx * Dx + Dy * Dy);
			if (D > R) continue;
			const FVector3d V = PlaneToVec(Face, P.X, P.Y);
			double La, Lo; VecToLatLon(V, La, Lo);
			const double E = ElevationM(V, S, La, Lo);
			const EBiome B = BiomeOf(E, La);
			// east = table +X (your right), north = table -Y (away from you)
			Cells.Add({ Q, Rr, (float)(Dx * K), (float)(-Dy * K), (float)(D / R), E, B, PeopleIn(B, 2.6 * S * S, V) });
		}
	double Lo = 1e30, Hi = -1e30, Crowd = 0, People = 0;
	int32 Mix[NumBiomes] = {};
	for (const FT& T : Cells) { const double L = FMath::Max(0.0, T.Elev); Lo = FMath::Min(Lo, L); Hi = FMath::Max(Hi, L); Crowd = FMath::Max(Crowd, T.People); People += T.People; Mix[T.B]++; }
	const double Range = FMath::Max3(Hi - Lo, R * 0.002, 1.0);   // exaggerated: an action-game table, not a survey map
	const float TileW = (float)(S * K) * 0.97f, UnitPerson = (float)(1.7 * K);
	const bool bFine = S <= 6.0;
	int32 NM = 0;
	for (const FT& T : Cells)
	{
		const float H = 0.4f + Relief * (float)((FMath::Max(0.0, T.Elev) - Lo) / Range);
		const int32 Fog = T.D > 0.72f ? FMath::Clamp(1 + (int32)((T.D - 0.72f) / 0.28f * (FogLevels - 1)), 1, FogLevels - 1) : 0;
		const int32 Shade = ((T.Q - T.Rr) % 3 + 3) % 3 == 0 ? 1 : 0;
		UInstancedStaticMeshComponent* Set = HexSets[(T.B * 2 + Shade) * FogLevels + Fog];
		// a hexagon = three boxes (sqrt3*s by s) at 0, 60 and 120 degrees
		const FVector Pos = ActionC + FVector(T.X, T.Y, H * 0.5f);
		for (int32 k = 0; k < 3; ++k)
			Set->AddInstance(FTransform(FRotator(0.f, 60.f * k, 0.f), Pos, FVector(TileW * 1.7320508f / 100.f, TileW / 100.f, H / 100.f)));
		// what lives on this tile: people when tiles are small, towns when they are big. Same tile -> same dice.
		if (T.People < 0.5 || NM >= MaxMarks || Fog >= 2) continue;
		auto Dice = [&T](int32 i) { const double X = FMath::Sin(T.Q * 127.1 + T.Rr * 311.7 + i * 74.7) * 43758.5453; return X - FMath::FloorToDouble(X); };
		const double Rel = T.People / FMath::Max(Crowd, 1e-9);
		const int32 Count = bFine ? FMath::Min((int32)T.People + (Dice(9) < FMath::Fmod(T.People, 1.0) ? 1 : 0), 5) : (Rel > 0.35 ? FMath::CeilToInt(Rel * 3) : 0);
		for (int32 i = 0; i < Count && NM < MaxMarks; ++i, ++NM)
		{
			const float A = (float)Dice(i) * 2.f * PI, Rr = TileW * 0.45f * FMath::Sqrt((float)Dice(i + 20));
			const float Wd = bFine ? FMath::Max(UnitPerson * 0.25f, 0.4f) : TileW * 0.18f;
			const float Ht = bFine ? FMath::Max(UnitPerson, 1.2f) : TileW * (0.3f + 1.2f * (float)Rel) * (1.f - 0.25f * i);
			(bFine ? PeopleSet : TownSet)->AddInstance(FTransform(FRotator::ZeroRotator, ActionC + FVector(T.X + FMath::Cos(A) * Rr, T.Y + FMath::Sin(A) * Rr, H + Ht * 0.5f), FVector(Wd / 100.f, Wd / 100.f, Ht / 100.f)));
		}
	}

	// remember the scene: same cell + same hex size = the same scene
	const FString Cell = CellKey(PickLat, PickLon, LevelForSize(R * 2));
	const int32 Visits = ++SceneVisits.FindOrAdd(Cell + FString::Printf(TEXT("@%g"), S));
	FString Terr;
	for (int32 B = 0; B < NumBiomes; ++B) if (Mix[B] * 100 >= Cells.Num() * 5) Terr += FString::Printf(TEXT("%s%d%% %s"), Terr.IsEmpty() ? TEXT("") : TEXT(", "), Mix[B] * 100 / FMath::Max(1, Cells.Num()), BiomeName(B));
	const FString Info = FString::Printf(TEXT("%.2f%s %.2f%s   radius %s   hexes of %s\n%s\n~%s people   cell %s   seen %dx"),
		FMath::Abs(PickLat), PickLat >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(PickLon), PickLon >= 0 ? TEXT("E") : TEXT("W"),
		*FmtM(R), *FmtM(S), *Terr, *FmtN(People), *Cell, Visits);
	if (TableInfo) TableInfo->SetText(FText::FromString(Info));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s\t%.5f\t%.5f\t%.0f\t%s\t%d tiles\t%s\n"), *FDateTime::Now().ToString(), PickLat, PickLon, R, *Cell, Cells.Num(), *Terr.Replace(TEXT("%"), TEXT("pct"))),
		*(FPaths::ProjectSavedDir() / TEXT("GlobeScenes.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogTemp, Display, TEXT("FNTable: %s | %d tiles, %d marks"), *Info.Replace(TEXT("\n"), TEXT(" | ")), Cells.Num(), NM);

	// the selection circle on the Globe (never smaller than a dot you can see)
	const double Ang = FMath::Max(R / RadiusM, 0.03);
	const FVector Up = FMath::Abs(PickDir.Z) > 0.95f ? FVector(1, 0, 0) : FVector(0, 0, 1);
	const FVector E1 = FVector::CrossProduct(Up, PickDir).GetSafeNormal(), E2 = FVector::CrossProduct(PickDir, E1);
	for (int32 i = 0; i < PickRing.Num(); ++i)
	{
		const double T = 2.0 * PI * i / PickRing.Num();
		const FVector D = (PickDir * FMath::Cos(Ang) + (E1 * FMath::Cos(T) + E2 * FMath::Sin(T)) * FMath::Sin(Ang)).GetSafeNormal();
		PickRing[i]->SetRelativeLocation(D * (BoardR + 4.f));
		PickRing[i]->SetRelativeRotation(FRotationMatrix::MakeFromZ(D).Rotator());
	}
	if (PickPin) { PickPin->SetRelativeLocation(PickDir * (BoardR + 4.f)); }
}

void AFNStudio::UpdateTable(float Dt)
{
	if (!BoardRoot) return;
	// first call: build the fixed props (selection ring + pin on the Globe, beam rays, fog bank, projector, caption)
	if (PickRing.Num() == 0)
	{
		const FLinearColor Glow(0.35f, 1.f, 1.f);
		for (int32 i = 0; i < 36; ++i)
		{
			UStaticMeshComponent* M = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("PickRing%d"), i)), FVector::ZeroVector, FVector(0.012f, 0.012f, 0.05f), Glow, BoardRoot);
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision); M->SetCastShadow(false);
			PickRing.Add(M);
		}
		PickPin = Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("PickPin"), FVector::ZeroVector, FVector(0.035f), Glow, BoardRoot);
		PickPin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (int32 i = 0; i < 12; ++i)
		{
			UStaticMeshComponent* M = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("BeamRay%d"), i)), FVector::ZeroVector, FVector(0.003f), FLinearColor(0.3f, 0.75f, 0.95f));
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision); M->SetCastShadow(false);
			BeamRays.Add(M);
		}
		for (int32 i = 0; i < 0; ++i)   // fog puffs off: opaque blobs read as white plates; the tiles fade into fog instead
		{
			UStaticMeshComponent* M = Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FName(*FString::Printf(TEXT("FogPuff%d"), i)), FVector::ZeroVector, FVector(0.4f, 0.28f, 0.05f), FLinearColor(0.42f, 0.5f, 0.6f));
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision); M->SetCastShadow(false);
			FogPuffs.Add(M);
		}
		Projector = NewObject<USpotLightComponent>(this, TEXT("Projector"));
		Projector->SetupAttachment(RootComponent);
		Projector->SetMobility(EComponentMobility::Movable);
		Projector->SetIntensityUnits(ELightUnits::Candelas);
		Projector->SetIntensity(7.f);
		Projector->SetLightColor(FLinearColor(0.75f, 0.92f, 1.f));
		Projector->SetAttenuationRadius(600.f);
		Projector->RegisterComponent();
		TableInfo = Text(TEXT("TableInfo"), FVector(-105.f, 175.f, 84.f), FRotator(50.f, 63.f, 0.f), 3.6f, FColor(170, 230, 255), FString());
		RebuildIn = 0.f;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC)
	{
		// zoom: mouse wheel or + / -
		double Zoom = 1.0;
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) || PC->WasInputKeyJustPressed(EKeys::Add) || PC->WasInputKeyJustPressed(EKeys::Equals)) Zoom = 1.0 / 1.6;
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) || PC->WasInputKeyJustPressed(EKeys::Subtract) || PC->WasInputKeyJustPressed(EKeys::Hyphen)) Zoom = 1.6;
		if (Zoom != 1.0)
		{
			const double NewR = FMath::Clamp(PickRadiusM * Zoom, MinR, MaxR);
			if (NewR != PickRadiusM) { PickRadiusM = NewR; RebuildIn = 0.15f; PlaySfx(EFNSfx::Click, 0.35f); }
		}
		// the Globe: press on it and drag to spin it; a press-and-release without dragging picks that spot
		float MX = 0.f, MY = 0.f;
		const bool bMouse = PC->GetMousePosition(MX, MY);
		auto GlobeHit = [&](FVector& Local) -> bool
		{
			FHitResult Hit;
			if (!PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit) || Hit.GetComponent() != GlobeMesh) return false;
			Local = BoardRoot->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint).GetSafeNormal();
			return true;
		};
		FVector Local;
		if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton) && bMouse && GlobeHit(Local)) { bGlobeDrag = true; DragLast = FVector2D(MX, MY); DragMoved = 0.f; }
		if (bGlobeDrag && bMouse && PC->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			const FVector2D Delta = FVector2D(MX, MY) - DragLast;
			DragLast = FVector2D(MX, MY);
			DragMoved += Delta.Size();
			if (DragMoved > 6.f && PC->PlayerCameraManager && GlobeMesh)
			{
				// grab-the-surface: the point under the cursor follows the mouse (right = right, down = down), from any camera angle
				const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
				const FVector CamR = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Y), CamU = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Z);
				const FVector Front = -CamRot.Vector();   // the globe's face toward you
				// radians per pixel = 1 / (globe radius on screen in pixels), so the surface stays under the cursor
				const FVector C = GlobeMesh->Bounds.Origin;
				FVector2D S0, S1;
				float RadPerPx = FMath::DegreesToRadians(0.2f);
				if (PC->ProjectWorldLocationToScreen(C, S0) && PC->ProjectWorldLocationToScreen(C + CamU * GlobeMesh->Bounds.SphereRadius, S1) && FVector2D::Distance(S0, S1) > 20.f)
					RadPerPx = 1.f / FVector2D::Distance(S0, S1);
				BoardRoot->AddWorldRotation(FQuat(FVector::CrossProduct(Front, CamR).GetSafeNormal(), Delta.X * RadPerPx));
				BoardRoot->AddWorldRotation(FQuat(FVector::CrossProduct(Front, -CamU).GetSafeNormal(), Delta.Y * RadPerPx));
				ManualUntil = Time + 12.f;
			}
		}
		if (bGlobeDrag && PC->WasInputKeyJustReleased(EKeys::LeftMouseButton))
		{
			bGlobeDrag = false;
			if (DragMoved <= 6.f && GlobeHit(Local))
			{
				ManualUntil = Time + 12.f;   // don't swing the Globe away from what you just picked
				PickSpot(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Local.Z, -1.0, 1.0))), FMath::RadiansToDegrees(FMath::Atan2(-Local.Y, Local.X)), true);
			}
		}
	}

	if (RebuildIn >= 0.f && (RebuildIn -= Dt) <= 0.f) { RebuildIn = -1.f; BuildTable(); }

	// the beam: rays from the picked spot on the Globe down to the rim of the circle of action
	const FVector Apex = BoardToActor(PickDir, BoardR + 4.f);
	for (int32 i = 0; i < BeamRays.Num(); ++i)
	{
		const float A = 2.f * PI * i / BeamRays.Num() + Time * 0.15f;
		const FVector Foot = ActionC + FVector(FMath::Cos(A) * ActionR, FMath::Sin(A) * ActionR, 1.f);
		const FVector D = Foot - Apex;
		BeamRays[i]->SetRelativeLocation((Apex + Foot) * 0.5f);
		BeamRays[i]->SetRelativeRotation(FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
		BeamRays[i]->SetRelativeScale3D(FVector(0.003f, 0.003f, D.Size() / 100.f));
	}
	if (Projector)
	{
		Projector->SetRelativeLocation(Apex + (ActionC - Apex).GetSafeNormal() * 12.f);   // just below the Globe, so it doesn't glare on it
		Projector->SetRelativeRotation((ActionC - Apex).Rotation());
		const float Cone = FMath::RadiansToDegrees(FMath::Atan2(ActionR * 1.05f, FVector::Dist(ActionC, Apex)));
		Projector->SetOuterConeAngle(Cone);
		Projector->SetInnerConeAngle(Cone * 0.8f);
	}
	// the fog bank drifts slowly around the rim
	for (int32 i = 0; i < FogPuffs.Num(); ++i)
	{
		const float A = 2.f * PI * i / FogPuffs.Num() + Time * (i % 2 ? 0.03f : -0.02f);
		const float Rr = ActionR * (1.0f + 0.05f * FMath::Sin(Time * 0.4f + i * 1.7f));
		FogPuffs[i]->SetRelativeLocation(ActionC + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 2.5f + 1.5f * FMath::Sin(Time * 0.5f + i)));
		FogPuffs[i]->SetRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(A), 0.f));
	}
}
