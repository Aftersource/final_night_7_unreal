// The circle of action (ported from the browser prototype client/globe/main.js buildTable):
// pick a spot on the floating Globe (click it; drag it to spin it), and that patch of the planet is built on the
// table as hex tiles at the right scale: terrain and biomes when you are far out, towns, then single people as
// you zoom in (mouse wheel, or + / -). The edge dissolves into fog; a projector beam joins the spot to the table.
// Every scene is a pure function of its planet position (FNPlanet.h), so going back shows the same ground.
#include "FNStudio.h"
#include "FNPlanet.h"
#include "FNEarthLines.h"

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
	// The board's zoom levels, board-game style: one thing per hexagon, about 500 hexagons on the board (22 from centre to edge).
	// Globe click -> REGIONS -> VILLAGES -> HOUSES -> PEOPLE; right-click goes back up one level.
	struct FLevel { double HexM; const TCHAR* Name; };
	static const FLevel Levels[4] = {
		{ 3.0,      TEXT("PEOPLE: 1 hexagon = 1 character (a house covers 7)") },
		{ 30.0,     TEXT("HOUSES: 1 hexagon = 1 house") },
		{ 3000.0,   TEXT("VILLAGES: 1 hexagon = 1 village (a city covers 2-4)") },
		{ 120000.0, TEXT("REGIONS: 1 hexagon = a region (big cities glow)") } };
	static constexpr int32 NumLevels = 4;
	static constexpr double HexRadius = 22.0;
	static double LevelR(int32 L) { return Levels[L].HexM * HexRadius; }
	static double HexDice(int32 Q, int32 R, int32 i) { const double X = FMath::Sin(Q * 127.1 + R * 311.7 + i * 74.7) * 43758.5453; return X - FMath::FloorToDouble(X); }
	// where people settle, as smooth patterns on the planet so every zoom level agrees (a readable board, not a census):
	static double RegionCity(const FVector3d& V) { return FMath::Max(0.0, FNPlanet::ValueNoise(V.X * 300, V.Y * 300, V.Z * 300, 99)); }   // big-city regions, ~40 km
	static double CityCore(const FVector3d& V) { return FMath::Max(0.0, FNPlanet::ValueNoise(V.X * 2000, V.Y * 2000, V.Z * 2000, 61)) * (0.4 + RegionCity(V)); }  // a city, 2-4 village hexes
	static double VillageSpot(const FVector3d& V) { return FMath::Max(0.0, FNPlanet::ValueNoise(V.X * 4000, V.Y * 4000, V.Z * 4000, 41)); }   // a village, ~1 km
	static bool IsCity(const FVector3d& V) { return CityCore(V) > 0.45; }
	static bool IsVillage(const FVector3d& V) { return VillageSpot(V) > 0.5; }
	static bool Livable(FNPlanet::EBiome B) { return B == FNPlanet::Beach || B == FNPlanet::Grass || B == FNPlanet::Forest; }
	// one house per 30 m hexagon, or none: the same answer on the HOUSES and the PEOPLE level
	static bool HouseAt(int32 Face, int32 Q, int32 R)
	{
		using namespace FNPlanet;
		const FVector2D C = HexToPlane(Q, R, Levels[1].HexM);
		const FVector3d V = PlaneToVec(Face, C.X, C.Y);
		double La, Lo; VecToLatLon(V, La, Lo);
		if (!Livable(BiomeOf(ElevationM(V, Levels[1].HexM, La, Lo), La))) return false;
		return HexDice(Q, R, 5) < (IsCity(V) ? 0.8 : IsVillage(V) ? 0.6 : 0.03);
	}
	// crafted-board ground: water sits low and flat, land rises in terraces like stacked foam layers
	static float GroundH(FNPlanet::EBiome B, double Elev, double Lo, double Range)
	{
		if (B == FNPlanet::Ocean) return 0.6f;
		return 2.2f + 1.7f * FMath::RoundToFloat(7.f * (float)((FMath::Max(0.0, Elev) - Lo) / Range));
	}
	static FString FmtN(double N)
	{
		if (N >= 1e9) return FString::Printf(TEXT("%.1f billion"), N / 1e9);
		if (N >= 1e6) return FString::Printf(TEXT("%.1f million"), N / 1e6);
		if (N >= 1e3) return FString::Printf(TEXT("%.1f thousand"), N / 1e3);
		return FString::Printf(TEXT("%.0f"), N);
	}
}

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
	auto MakeSet = [this, Cube](const FName& Name, const FLinearColor& Color, UStaticMesh* Mesh = nullptr) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, Name);
		C->SetupAttachment(MapPivot);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Mesh ? Mesh : Cube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (BaseMat) { UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C); M->SetVectorParameterValue(TEXT("Color"), Color); C->SetMaterial(0, M); }
		C->RegisterComponent();
		return C;
	};
	if (HexSets.Num() == 0)
	{
		MapPivot = NewObject<USceneComponent>(this, TEXT("MapPivot"));
		MapPivot->SetupAttachment(RootComponent);
		MapPivot->SetMobility(EComponentMobility::Movable);
		MapPivot->RegisterComponent();
		for (int32 B = 0; B < NumBiomes; ++B)
			for (int32 Sh = 0; Sh < 2; ++Sh)
				for (int32 F = 0; F < FNTableScene::FogLevels; ++F)
				{
					const FLinearColor Base = FLinearColor::FromSRGBColor(FNTableScene::BiomeRGB[B]) * (Sh ? 0.86f : 1.f);
					HexSets.Add(MakeSet(FName(*FString::Printf(TEXT("Hex_%d_%d_%d"), B, Sh, F)), FLinearColor::LerpUsingHSV(Base, FNTableScene::FogCol, F / (float)FNTableScene::FogLevels)));
				}
		PeopleSet = MakeSet(TEXT("TablePeople"), FLinearColor::FromSRGBColor(FColor(255, 225, 176)));
		TownSet = MakeSet(TEXT("TableTowns"), FLinearColor::FromSRGBColor(FColor(217, 212, 204)));
		HeadSet = MakeSet(TEXT("TableHeads"), FLinearColor::FromSRGBColor(FColor(120, 80, 50)));    // dark hair: reads as a person
		RoofSet = MakeSet(TEXT("TableRoofs"), FLinearColor::FromSRGBColor(FColor(178, 70, 45)));    // terracotta: reads as a house
		TowerSet = MakeSet(TEXT("TableTowers"), FLinearColor::FromSRGBColor(FColor(150, 170, 195)));  // glassy blue-grey: buildings
		CoastSet = MakeSet(TEXT("TableCoast"), FLinearColor(0.95f, 0.97f, 1.f));
		PlateSet = MakeSet(TEXT("TablePlates"), FLinearColor(1.f, 0.35f, 0.05f));
		CitySet = MakeSet(TEXT("TableCities"), FLinearColor(1.f, 0.8f, 0.2f));
		// crafted-terrain look: earth/stone sides under the coloured top layer, plus trees, rocks and peaks
		for (int32 F = 0; F < FNTableScene::FogLevels; ++F)
		{
			EarthSets.Add(MakeSet(FName(*FString::Printf(TEXT("Earth_%d"), F)), FLinearColor::LerpUsingHSV(FLinearColor::FromSRGBColor(FColor(112, 80, 52)), FNTableScene::FogCol, F / (float)FNTableScene::FogLevels)));
			StoneSets.Add(MakeSet(FName(*FString::Printf(TEXT("Stone_%d"), F)), FLinearColor::LerpUsingHSV(FLinearColor::FromSRGBColor(FColor(92, 88, 84)), FNTableScene::FogCol, F / (float)FNTableScene::FogLevels)));
		}
		UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		TrunkSet = MakeSet(TEXT("TableTrunks"), FLinearColor::FromSRGBColor(FColor(92, 62, 38)), Cyl);
		PineSet = MakeSet(TEXT("TablePines"), FLinearColor::FromSRGBColor(FColor(30, 88, 48)), Cone);
		LeafSet = MakeSet(TEXT("TableLeaves"), FLinearColor::FromSRGBColor(FColor(88, 150, 58)), Ball);
		RockSet = MakeSet(TEXT("TableRocks"), FLinearColor::FromSRGBColor(FColor(140, 136, 130)), Ball);
		PeakSet = MakeSet(TEXT("TablePeaks"), FLinearColor::FromSRGBColor(FColor(118, 110, 104)), Cone);
		SnowSet = MakeSet(TEXT("TableSnow"), FLinearColor::FromSRGBColor(FColor(245, 248, 252)), Cone);
	}
	for (UInstancedStaticMeshComponent* C : HexSets) C->ClearInstances();
	for (UInstancedStaticMeshComponent* C : EarthSets) C->ClearInstances();
	for (UInstancedStaticMeshComponent* C : StoneSets) C->ClearInstances();
	for (UInstancedStaticMeshComponent* C : { TrunkSet.Get(), PineSet.Get(), LeafSet.Get(), RockSet.Get(), PeakSet.Get(), SnowSet.Get() }) C->ClearInstances();
	for (UInstancedStaticMeshComponent* C : { PeopleSet.Get(), TownSet.Get(), HeadSet.Get(), RoofSet.Get(), TowerSet.Get() }) C->ClearInstances();
	PeopleSpots.Reset(); HouseSpots.Reset();

	// the hex lattice on the cube face under the picked spot
	const FVector3d C0 = LatLonToVec(PickLat, PickLon);
	int32 Face; double U, W;
	VecToFaceUV(C0, Face, U, W);
	// FN7 (Mac session, view-fix go-ahead): while the hand pans, the board stays on the cube face it was on (its plane
	// runs on past the face's edge), so crossing a seam never swaps the lattice under the hand (Chris asked that this hold while dragging
	// around it's glitching, snapping around"; her drags crossed the seam at ~42N). Released by a zoom or a centring.
	if (bReliefHold && TableFace >= 0 && Face != TableFace)
	{
		const FVector3d N = FaceUVToVec(TableFace, 0.0, 0.0);   // the held face's normal
		const double D = FVector3d::DotProduct(C0, N);
		if (D > 0.25)
		{
			const FVector3d Ue = FaceUVToVec(TableFace, 1.0, 0.0) - N, We = FaceUVToVec(TableFace, 0.0, 1.0) - N;
			Face = TableFace;
			U = FVector3d::DotProduct(C0, Ue) / D;
			W = FVector3d::DotProduct(C0, We) / D;
		}
	}
	MapLevel = FMath::Clamp(MapLevel, 0, FNTableScene::NumLevels - 1);
	PickRadiusM = FNTableScene::LevelR(MapLevel);
	const double Cx = U * RadiusM, Cy = W * RadiusM, R = PickRadiusM;
	const double S = FNTableScene::Levels[MapLevel].HexM;
	const double K = FNTableScene::ActionR / R;                                // cm on the table per planet meter
	int32 Q0, R0; PlaneToHex(Cx, Cy, S, Q0, R0);
	const int32 Span = FMath::CeilToInt(R / (S * 1.5)) + 1;
	struct FT { int32 Q, Rr; float X, Y, D; double Elev; EBiome B; double People; FVector3d V; FVector2D P; };
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
			Cells.Add({ Q, Rr, (float)(Dx * K), (float)(-Dy * K), (float)(D / R), E, B, PeopleIn(B, 2.6 * S * S, V), V, P });
		}
	double Lo = 1e30, Hi = -1e30, Crowd = 0, People = 0;
	int32 Mix[NumBiomes] = {};
	for (const FT& T : Cells) { const double L = FMath::Max(0.0, T.Elev); Lo = FMath::Min(Lo, L); Hi = FMath::Max(Hi, L); Crowd = FMath::Max(Crowd, T.People); People += T.People; Mix[T.B]++; }
	double Range = FMath::Max3(Hi - Lo, R * 0.002, 1.0);   // exaggerated: an action-game table, not a survey map
	// FN7 (Mac session, view-fix go-ahead): while the hand pans the board, the relief keeps the scale it had when the hand
	// took hold; re-normalising it to each new window made every hill jump at every re-centre (Chris asked that this hold while dragging
	// around it's glitching, snapping around"). A zoom or a centring clears the hold.
	if (bReliefHold && TableRange > 0.0) { Lo = TableLo; Range = TableRange; }
	TableFace = Face; TableCx = Cx; TableCy = Cy; TableK = K; TableS = S; TableLo = Lo; TableRange = Range;
	const float TileW = (float)(S * K) * 0.97f;
	const float HexW = (float)(S * K) * 1.7320508f;   // flat-to-flat width of one hexagon on the table (cm)
	int32 NM = 0;
	auto House = [&](const FVector& Base, float Wd, float Yaw)   // walls + pitched roof
	{
		const float Wall = Wd * 0.6f;
		TownSet->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Base + FVector(0, 0, Wall * 0.5f), FVector(Wd / 100.f, Wd * 0.8f / 100.f, Wall / 100.f)));
		RoofSet->AddInstance(FTransform(FRotator(0.f, Yaw, 45.f), Base + FVector(0, 0, Wall), FVector(Wd * 1.05f / 100.f, Wd * 0.58f / 100.f, Wd * 0.58f / 100.f)));
	};
	auto Tower = [&](const FVector& Base, float Wd, float Ht, float Yaw)
	{
		TowerSet->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Base + FVector(0, 0, Ht * 0.5f), FVector(Wd / 100.f, Wd / 100.f, Ht / 100.f)));
	};
	for (const FT& T : Cells)
	{
		const float H = BoardHeightAtHex(T.Q, T.Rr, FNTableScene::GroundH(T.B, T.Elev, Lo, Range));   // the earth hook (no override today)
		const int32 Fog = T.D > 0.72f ? FMath::Clamp(1 + (int32)((T.D - 0.72f) / 0.28f * (FNTableScene::FogLevels - 1)), 1, FNTableScene::FogLevels - 1) : 0;
		const int32 Shade = ((T.Q - T.Rr) % 3 + 3) % 3 == 0 ? 1 : 0;
		UInstancedStaticMeshComponent* Set = HexSets[(T.B * 2 + Shade) * FNTableScene::FogLevels + Fog];
		// a hexagon = three boxes (sqrt3*s by s) at 0, 60 and 120 degrees
		// land = earth-brown sides with a coloured top layer (like painted craft terrain); water = one blue slab
		const float Cap = T.B == Ocean ? H : FMath::Min(H, 0.7f);
		UInstancedStaticMeshComponent* Body = T.B == Ocean ? Set : (T.B == Rock || T.B == Snow ? StoneSets[Fog].Get() : EarthSets[Fog].Get());
		for (int32 k = 0; k < 3; ++k)
		{
			const FRotator Rk(0.f, 60.f * k, 0.f);
			Set->AddInstance(FTransform(Rk, FNTableScene::ActionC + FVector(T.X, T.Y, H - Cap * 0.5f), FVector(TileW * 1.7320508f / 100.f, TileW / 100.f, Cap / 100.f)));
			if (Cap < H) Body->AddInstance(FTransform(Rk, FNTableScene::ActionC + FVector(T.X, T.Y, (H - Cap) * 0.5f), FVector(TileW * 1.7320508f / 100.f, TileW / 100.f, (H - Cap) / 100.f)));
		}
		const FVector Top = FNTableScene::ActionC + FVector(T.X, T.Y, H);
		// nature on the hexagon: trees, bushes, rocks, peaks. Same hexagon -> same scenery, every visit.
		auto Nature = [&]()
		{
			if (Fog >= 3) return;
			const float Hw = HexW;
			auto Dice = [&](int32 i) { return (float)FNTableScene::HexDice(T.Q, T.Rr, 100 + i); };
			auto Spot = [&](int32 i, float Rad) { const float A = 2.f * PI * Dice(i); const float Rr = Rad * Hw * Dice(i + 50); return Top + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.f); };
			auto Pine = [&](const FVector& At, float Sz)
			{
				TrunkSet->AddInstance(FTransform(FRotator::ZeroRotator, At + FVector(0, 0, Sz * 0.12f), FVector(Sz * 0.08f / 100.f, Sz * 0.08f / 100.f, Sz * 0.24f / 100.f)));
				PineSet->AddInstance(FTransform(FRotator::ZeroRotator, At + FVector(0, 0, Sz * 0.2f), FVector(Sz * 0.45f / 100.f, Sz * 0.45f / 100.f, Sz * 0.85f / 100.f)));
			};
			auto Leafy = [&](const FVector& At, float Sz)
			{
				TrunkSet->AddInstance(FTransform(FRotator::ZeroRotator, At + FVector(0, 0, Sz * 0.2f), FVector(Sz * 0.09f / 100.f, Sz * 0.09f / 100.f, Sz * 0.4f / 100.f)));
				LeafSet->AddInstance(FTransform(FRotator::ZeroRotator, At + FVector(0, 0, Sz * 0.55f), FVector(Sz * 0.55f / 100.f)));
			};
			auto Stone = [&](const FVector& At, float Sz, int32 i) { RockSet->AddInstance(FTransform(FRotator(0.f, 360.f * Dice(i), 0.f), At + FVector(0, 0, Sz * 0.15f), FVector(Sz / 100.f, Sz * 0.75f / 100.f, Sz * 0.5f / 100.f))); };
			// true scale where a tree is visible (PEOPLE 3 m and HOUSES 30 m hexes); farther out a single tree is
			// smaller than a speck, so a forest hexagon becomes one solid canopy block (board-game forest tile)
			const float CmPerM = Hw / (1.7320508f * (float)S);
			const bool bTreesVisible = MapLevel <= 1;
			auto Tree = [&](const FVector& At, float Meters, int32 i) { const float Sz = Meters * CmPerM; if (Dice(i + 20) < 0.6f) Pine(At, Sz); else Leafy(At, Sz * 0.8f); };
			auto Canopy = [&]()   // seven packed, squashed crowns: reads as dense forest from above
			{
				for (int32 i = 0; i < 7; ++i)
				{
					const float A = PI / 3.f * i, Rr = i == 6 ? 0.f : Hw * 0.27f;
					LeafSet->AddInstance(FTransform(FRotator::ZeroRotator, Top + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Hw * 0.08f), FVector(Hw * 0.36f / 100.f, Hw * 0.36f / 100.f, Hw * (0.18f + 0.08f * Dice(i)) / 100.f)));
				}
			};
			switch (T.B)
			{
			case Forest:
				if (MapLevel == 0) Tree(Spot(0, 0.15f), 9.f + 6.f * Dice(10), 0);                                  // one tree, crown wider than its hexagon
				else if (bTreesVisible) for (int32 i = 0; i < 7; ++i) Tree(Spot(i, 0.42f), 8.f + 8.f * Dice(i + 10), i); // a thick wood of small trees
				else Canopy();
				break;
			case Grass:
				if (MapLevel == 0) { if (Dice(1) < 0.12f) Tree(Spot(2, 0.2f), 7.f + 5.f * Dice(11), 2); else if (Dice(1) < 0.3f) Stone(Spot(3, 0.3f), 0.8f * CmPerM, 4); }
				else if (bTreesVisible) { if (Dice(1) < 0.3f) for (int32 i = 0; i < 2; ++i) Tree(Spot(i, 0.4f), 7.f + 6.f * Dice(i + 10), i); }
				else if (Dice(1) < 0.15f) LeafSet->AddInstance(FTransform(FRotator::ZeroRotator, Top + FVector(0, 0, Hw * 0.05f), FVector(Hw * 0.3f / 100.f, Hw * 0.3f / 100.f, Hw * 0.12f / 100.f)));   // a copse
				break;
			case Beach:  if (Dice(1) < 0.25f) Stone(Spot(3, 0.3f), FMath::Min(Hw * 0.1f, 1.f * CmPerM), 4); break;
			case Rock:
				if (bTreesVisible)   // up close a mountain is bigger than the board: boulders at true size instead
				{
					for (int32 i = 0; i < 2; ++i) Stone(Spot(i, 0.3f), FMath::Min(Hw * 0.9f, (1.5f + 3.f * Dice(i + 30)) * CmPerM), i);
				}
				else if (Dice(1) < 0.45f)   // a mountain: grey cone with a snowy tip
				{
					const float Ht = Hw * (0.9f + 0.6f * Dice(5));
					PeakSet->AddInstance(FTransform(FRotator(0.f, 360.f * Dice(6), 0.f), Top, FVector(Hw * 0.95f / 100.f, Hw * 0.95f / 100.f, Ht / 100.f)));
					SnowSet->AddInstance(FTransform(FRotator::ZeroRotator, Top + FVector(0, 0, Ht * 0.68f), FVector(Hw * 0.31f / 100.f, Hw * 0.31f / 100.f, Ht * 0.33f / 100.f)));
				}
				else for (int32 i = 0; i < 2; ++i) Stone(Spot(i, 0.25f), Hw * (0.14f + 0.1f * Dice(i + 30)), i);
				break;
			case Snow:   if (Dice(1) < 0.5f) SnowSet->AddInstance(FTransform(FRotator::ZeroRotator, Top, FVector(Hw * 0.8f / 100.f, Hw * 0.8f / 100.f, Hw * 0.8f / 100.f))); break;
			default: break;
			}
		};
		const int32 NM0 = NM;
		// scenery goes on at the end of this hexagon's turn, only if nothing else was placed on it
		const bool bNatureOk = HexStateAtHex(T.Q, T.Rr) == EFNHexState::Normal;   // burning / flooded / lava / ash / new land: no trees (for now)
		struct FAfter { TFunction<void()> F; bool bOn = true; ~FAfter() { if (bOn) F(); } } After{ [&]() { if (NM == NM0 && bNatureOk) Nature(); } };
		// what stands on this hexagon: at most ONE thing (board-game rule). Same hexagon -> same answer, every visit.
		if (NM >= FNTableScene::MaxMarks || Fog >= 2 || !FNTableScene::Livable(T.B)) continue;
		{   // board life: burned / ruined / lava hexagons keep no houses or people (a flood leaves them standing, half sunk)
			const EFNHexState Hs = HexStateAtHex(T.Q, T.Rr);
			if (Hs == EFNHexState::Burning || Hs == EFNHexState::Ash || Hs == EFNHexState::Lava) continue;
		}
		const FVector Mid = FNTableScene::ActionC + FVector(T.X, T.Y, H);
		const float Yaw = (float)FNTableScene::HexDice(T.Q, T.Rr, 40) * 90.f;
		if (MapLevel == 0)        // PEOPLE: one character per hexagon; a house covers a hexagon and its 6 neighbours
		{
			int32 Q2, R2; PlaneToHex(T.P.X, T.P.Y, FNTableScene::Levels[1].HexM, Q2, R2);
			if (FNTableScene::HouseAt(Face, Q2, R2))
			{
				const FVector2D C2 = HexToPlane(Q2, R2, FNTableScene::Levels[1].HexM);
				int32 Qc, Rc; PlaneToHex(C2.X, C2.Y, S, Qc, Rc);
				const int32 Dq = T.Q - Qc, Dr = T.Rr - Rc;
				if ((FMath::Abs(Dq) + FMath::Abs(Dr) + FMath::Abs(Dq + Dr)) / 2 <= 1)
				{
					if (Dq == 0 && Dr == 0) { House(Mid, HexW * 2.6f, Yaw); ++NM; HouseSpots.Add(Mid); }   // drawn once, from its middle hexagon
					After.bOn = false;   // no trees inside the walls either
					continue;   // nobody stands inside the walls
				}
			}
			const double Busy = FNTableScene::IsCity(T.V) ? 0.3 : FNTableScene::IsVillage(T.V) ? 0.2 : 0.02;
			if (FNTableScene::HexDice(T.Q, T.Rr, 9) < Busy)
			{
				const float Ht = HexW * 0.9f, Wd = HexW * 0.3f;   // a game token that fills its hexagon
				if (bShowStaticPeople)
				{
					PeopleSet->AddInstance(FTransform(FRotator::ZeroRotator, Mid + FVector(0, 0, Ht * 0.41f), FVector(Wd / 100.f, Wd / 100.f, Ht * 0.82f / 100.f)));
					HeadSet->AddInstance(FTransform(FRotator::ZeroRotator, Mid + FVector(0, 0, Ht * 0.82f + Wd * 0.55f), FVector(Wd * 1.1f / 100.f)));
				}
				PeopleSpots.Add({ Mid, FIntPoint(T.Q, T.Rr), (uint8)(FNTableScene::IsCity(T.V) ? 2 : FNTableScene::IsVillage(T.V) ? 1 : 0) });   // the living people (Features/FNPeopleLife) start here
				++NM;
			}
		}
		else if (MapLevel == 1)   // HOUSES: one house per hexagon, or none
		{
			if (FNTableScene::HouseAt(Face, T.Q, T.Rr)) { House(Mid, HexW * 0.62f, Yaw); ++NM; HouseSpots.Add(Mid); }
		}
		else if (MapLevel == 2)   // VILLAGES: one village per hexagon; a city is a few neighbouring hexagons of towers
		{
			if (FNTableScene::IsCity(T.V))
			{
				for (int32 i = 0; i < 3; ++i)
				{
					const float A = 2.f * PI * i / 3.f + Yaw, Rr = HexW * 0.2f;
					Tower(Mid + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.f), HexW * 0.2f, HexW * (0.8f + 0.5f * (float)FNTableScene::HexDice(T.Q, T.Rr, i)), Yaw);
				}
				++NM;
			}
			else if (FNTableScene::IsVillage(T.V))
			{
				for (int32 i = 0; i < 3; ++i)
				{
					const float A = 2.f * PI * i / 3.f + Yaw, Rr = HexW * 0.2f;
					House(Mid + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.f), HexW * 0.22f, Yaw + 30.f * i);
				}
				++NM;
			}
		}
		else                      // REGIONS: where the big cities are, as glowing towers
		{
			const double C = FNTableScene::RegionCity(T.V);
			if (C > 0.6) { Tower(Mid, HexW * 0.3f, HexW * (0.6f + 2.f * (float)C), Yaw); ++NM; }
		}
	}

	// Real Earth outlines over the hexes (same data as the Globe): coasts white, plate faults orange, big cities gold
	const float LineZ = 3.4f;   // just above the lowest land terrace: coasts sit on the shore instead of floating
	auto ToTable = [&](int32 Lat100, int32 Lon100, FVector2D& Out) -> bool
	{
		int32 F; double Uu, Ww;
		VecToFaceUV(LatLonToVec(Lat100 / 100.0, Lon100 / 100.0), F, Uu, Ww);
		Out = FVector2D((Uu * RadiusM - Cx) * K, -(Ww * RadiusM - Cy) * K);
		return F == Face;
	};
	auto DrawLines = [&](const int16* V, int32 Num, float Thick, UInstancedStaticMeshComponent* Set)
	{
		bool bHave = false; FVector2D A;
		for (int32 i = 0; i < Num;)
		{
			if (V[i] == 32767) { bHave = false; ++i; continue; }
			FVector2D B; const bool bOk = ToTable(V[i], V[i + 1], B);
			i += 2;
			if (bOk && bHave)
			{
				// clip the segment A-B to the circle of action
				const FVector2D D = B - A;
				const double a = D.Dot(D), b = 2 * A.Dot(D), c = A.Dot(A) - FNTableScene::ActionR * FNTableScene::ActionR, Disc = b * b - 4 * a * c;
				if (a > 1e-6 && Disc > 0)
				{
					const double Sq = FMath::Sqrt(Disc), T0 = FMath::Max(0.0, (-b - Sq) / (2 * a)), T1 = FMath::Min(1.0, (-b + Sq) / (2 * a));
					if (T1 > T0)
					{
						const FVector2D P0 = A + D * T0, P1 = A + D * T1, M = (P0 + P1) * 0.5;
						const float L = (float)(P1 - P0).Size();
						Set->AddInstance(FTransform(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), 0.f), FNTableScene::ActionC + FVector(M.X, M.Y, LineZ), FVector(L / 100.f + 0.002f, Thick, Thick)));
					}
				}
			}
			A = B; bHave = bOk;
		}
	};
	CoastSet->ClearInstances(); PlateSet->ClearInstances(); CitySet->ClearInstances();
	DrawLines(FNEarthCoast, UE_ARRAY_COUNT(FNEarthCoast), 0.007f, CoastSet);
	DrawLines(FNEarthPlates, UE_ARRAY_COUNT(FNEarthPlates), 0.01f, PlateSet);
	for (int32 i = 0; i + 2 < UE_ARRAY_COUNT(FNEarthCities); i += 3)
	{
		FVector2D P;
		if (!ToTable(FNEarthCities[i], FNEarthCities[i + 1], P) || P.SquaredLength() > FNTableScene::ActionR * FNTableScene::ActionR) continue;
		const float Sz = 0.03f + 0.03f * FMath::Min(1.f, FNEarthCities[i + 2] / 20000.f);   // bigger dot for bigger city
		const double La = FNEarthCities[i] / 100.0, Lo2 = FNEarthCities[i + 1] / 100.0, E = ElevationM(LatLonToVec(La, Lo2), S, La, Lo2);
		int32 CQ, CR; PlaneToHex(Cx + P.X / K, Cy - P.Y / K, S, CQ, CR);
		const float CityH = BoardHeightAtHex(CQ, CR, FNTableScene::GroundH(BiomeOf(E, La), E, Lo, Range));
		CitySet->AddInstance(FTransform(FRotator::ZeroRotator, FNTableScene::ActionC + FVector(P.X, P.Y, CityH + Sz * 50.f), FVector(Sz, Sz, Sz)));
	}

	// remember the scene: same cell + same hex size = the same scene
	const FString Cell = CellKey(PickLat, PickLon, LevelForSize(R * 2));
	const int32 Visits = ++SceneVisits.FindOrAdd(Cell + FString::Printf(TEXT("@%g"), S));
	FString Terr;
	for (int32 B = 0; B < NumBiomes; ++B) if (Mix[B] * 100 >= Cells.Num() * 5) Terr += FString::Printf(TEXT("%s%d%% %s"), Terr.IsEmpty() ? TEXT("") : TEXT(", "), Mix[B] * 100 / FMath::Max(1, Cells.Num()), BiomeName(B));
	const FString Info = FString::Printf(TEXT("ZOOM %d of %d   %s\nmap %s across   click: closer   right-click: farther   Home: back to you\n%.2f%s %.2f%s   hexes of %s\n%s\n~%s people   cell %s   seen %dx"),
		FNTableScene::NumLevels - MapLevel, FNTableScene::NumLevels, FNTableScene::Levels[MapLevel].Name, *FNTableScene::FmtM(R * 2),
		FMath::Abs(PickLat), PickLat >= 0 ? TEXT("N") : TEXT("S"), FMath::Abs(PickLon), PickLon >= 0 ? TEXT("E") : TEXT("W"),
		*FNTableScene::FmtM(S), *Terr, *FNTableScene::FmtN(People), *Cell, Visits);
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
	PlaceTableHeroes();
}

void AFNStudio::PlaceTableHeroes()
{
	using namespace FNPlanet;
	if (TableFace < 0 || TableK <= 0.0) return;
	int32 N = 0;
	for (const FFNPiece& P : WorldPieces)
	{
		if (P.Kind == 4) continue;   // day-1 drop spots aren't people
		const FVector3d V = LatLonToVec(P.Lat, P.Lon);
		int32 F; double U, W;
		VecToFaceUV(V, F, U, W);
		if (F != TableFace) continue;
		const float X = (float)((U * RadiusM - TableCx) * TableK), Y = (float)(-(W * RadiusM - TableCy) * TableK);
		if (X * X + Y * Y > FNTableScene::ActionR * FNTableScene::ActionR) continue;
		const double E = ElevationM(V, TableS, P.Lat, P.Lon);
		int32 PQ, PR; PlaneToHex(U * RadiusM, W * RadiusM, TableS, PQ, PR);
		const float Ground = BoardHeightAtHex(PQ, PR, FNTableScene::GroundH(BiomeOf(E, P.Lat), E, TableLo, TableRange));
		// true scale: a 1.7 m person; never smaller than a piece you can see (and click) from the chair
		const float Ht = FMath::Max((float)(1.7 * TableK), 14.f), Wd = FMath::Max(Ht * 0.25f, 4.f);
		if (!HeroMarks.IsValidIndex(N))
		{
			const FName Id(*FString::Printf(TEXT("Hero%d"), N));
			UStaticMeshComponent* M = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("HeroMark%d"), N)), FVector::ZeroVector, FVector(0.05f), FLinearColor::White);
			M->SetCollisionEnabled(ECollisionEnabled::QueryOnly);   // clickable
			Tag(M, Id);                                              // hover bump + "FN:HeroN" for the click trace
			HeroMarks.Add(M);
			UStaticMeshComponent* B = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("HeroBeam%d"), N)), FVector::ZeroVector, FVector(0.05f), FLinearColor::White);
			B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);   // the beam is clickable too: an easy target from far out
			B->ComponentTags.Add(FName(*(FString(TEXT("FN:")) + Id.ToString())));
			B->SetCastShadow(false);
			HeroBeams.Add(B);
			UStaticMeshComponent* R = Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FName(*FString::Printf(TEXT("HeroRing%d"), N)), FVector::ZeroVector, FVector(0.05f), FLinearColor::White);
			R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			R->SetCastShadow(false);
			HeroRings.Add(R);
			HeroLabels.Add(Text(FName(*FString::Printf(TEXT("HeroLabel%d"), N)), FVector::ZeroVector, FRotator(0.f, 90.f, 0.f), 6.f, FColor::White, FString()));
		}
		// gold for you (white got lost among the towns), the player colour for rivals, dark grey for the fallen
		const FLinearColor Col = P.Kind == 0 ? FLinearColor(1.f, 0.78f, 0.15f) : P.Kind == 3 ? FLinearColor(0.15f, 0.15f, 0.15f) : PlayerColor(P.Color);
		const FVector Foot = FNTableScene::ActionC + FVector(X, Y, Ground);
		const float BeamH = 55.f;
		auto Paint = [&Col](UStaticMeshComponent* C, float Mul) { if (UMaterialInstanceDynamic* Mi = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0))) Mi->SetVectorParameterValue(TEXT("Color"), Col * Mul); };
		UStaticMeshComponent* M = HeroMarks[N];
		Paint(M, 1.f);
		M->SetRelativeLocation(Foot + FVector(0, 0, Ht * 0.5f));
		M->SetRelativeScale3D(FVector(Wd / 100.f, Wd / 100.f, Ht / 100.f));
		BaseScale.Add(FName(*FString::Printf(TEXT("Hero%d"), N)), M->GetRelativeScale3D());   // Tick's hover bump scales from this
		M->SetVisibility(true);
		UStaticMeshComponent* B = HeroBeams[N];
		Paint(B, 1.6f);
		B->SetRelativeLocation(Foot + FVector(0, 0, Ht + BeamH * 0.5f));
		B->SetRelativeScale3D(FVector(0.015f, 0.015f, BeamH / 100.f));
		B->SetVisibility(P.Kind != 3);
		UStaticMeshComponent* R = HeroRings[N];
		Paint(R, 1.2f);
		R->SetRelativeLocation(Foot + FVector(0, 0, 0.6f));
		R->SetRelativeScale3D(FVector(Wd * 3.f / 100.f, Wd * 3.f / 100.f, 0.006f));
		R->SetVisibility(true);
		UTextRenderComponent* L = HeroLabels[N];
		L->SetRelativeLocation(Foot + FVector(0, 0, Ht + (P.Kind != 3 ? BeamH : 0.f) + 6.f));
		L->SetWorldSize(FMath::Clamp(Ht * 0.12f, 6.f, 14.f));
		L->SetTextRenderColor(Col.ToFColor(true));
		L->SetText(FText::FromString(P.Kind == 0 ? P.Name + TEXT(" (you)") : P.Kind == 3 ? P.Name + TEXT(" (fallen)") : P.Name));
		L->SetVisibility(true);
		HeroShown.SetNum(N + 1);
		HeroShown[N] = P;
		++N;
	}
	HeroShown.SetNum(N);
	for (int32 i = N; i < HeroMarks.Num(); ++i)
	{
		HeroMarks[i]->SetVisibility(false); HeroBeams[i]->SetVisibility(false); HeroRings[i]->SetVisibility(false); HeroLabels[i]->SetVisibility(false);
		HeroMarks[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision); HeroBeams[i]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	for (int32 i = 0; i < N; ++i) { HeroMarks[i]->SetCollisionEnabled(ECollisionEnabled::QueryOnly); HeroBeams[i]->SetCollisionEnabled(ECollisionEnabled::QueryOnly); }
}

bool AFNStudio::ClickDemigod(FName Id)
{
	const FString S = Id.ToString();
	FString Ref, Name;
	if (S.StartsWith(TEXT("Hero")))
	{
		const int32 i = FCString::Atoi(*S.RightChop(4));
		if (!HeroShown.IsValidIndex(i)) return false;
		Ref = HeroShown[i].Ref; Name = HeroShown[i].Name;
	}
	else if (S.StartsWith(TEXT("Seat")))
	{
		const int32 k = FCString::Atoi(*S.RightChop(4));
		if (!SeatSlots.IsValidIndex(k) || !SeatSlots[k].bUsed) return false;
		Ref = SeatSlots[k].Info.Ref; Name = SeatSlots[k].Info.Name;
	}
	else return false;
	UE_LOG(LogTemp, Display, TEXT("FNPick: demigod %s ref=%s"), *Name, *Ref);
	PlaySfx(EFNSfx::Chime, 0.5f);
	CenterMapOn(Ref, Name);
	if (OnDemigodClicked) OnDemigodClicked(Ref, Name);
	return true;
}

void AFNStudio::CenterMapOn(const FString& Ref, const FString& Name)
{
	for (const FFNPiece& P : WorldPieces)
		if (P.Kind != 4 && ((!Ref.IsEmpty() && P.Ref == Ref) || P.Name == Name))
		{
			PickLat = FMath::Clamp(P.Lat, -89.9, 89.9);
			PickLon = P.Lon;
			PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
			bUserPicked = true;
			RebuildIn = 0.f;
			return;
		}
}

bool AFNStudio::MapPointUnderCursor(double& Px, double& Py) const
{
	using namespace FNPlanet;
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || TableFace < 0 || TableK <= 0.0) return false;
	FVector O, D;
	if (!PC->DeprojectMousePositionToWorld(O, D)) return false;
	O = GetActorTransform().InverseTransformPosition(O);
	D = GetActorTransform().InverseTransformVectorNoScale(D);
	if (D.Z > -1e-3f) return false;
	// the tiles stand up to 14 cm tall and the camera looks down at a shallow angle, so meet the ray with the actual
	// top of the hex it crosses (a flat plane put the click up to a quarter of the map off): settle in a few steps
	float Top = 0.f;
	FVector Hit;
	for (int32 It = 0; It < 6; ++It)
	{
		Hit = O + D * ((FNTableScene::ActionC.Z + Top - O.Z) / D.Z) - FNTableScene::ActionC;
		Px = TableCx + Hit.X / TableK;
		Py = TableCy - Hit.Y / TableK;
		int32 Q, Rr; PlaneToHex(Px, Py, TableS, Q, Rr);
		const FVector2D C = HexToPlane(Q, Rr, TableS);
		const FVector3d V = PlaneToVec(TableFace, C.X, C.Y);
		double La, Lo; VecToLatLon(V, La, Lo);
		const double E = ElevationM(V, TableS, La, Lo);
		Top = BoardHeightAtHex(Q, Rr, FNTableScene::GroundH(BiomeOf(E, La), E, TableLo, TableRange));
	}
	return Hit.X * Hit.X + Hit.Y * Hit.Y <= FNTableScene::ActionR * FNTableScene::ActionR;
}

void AFNStudio::ZoomMapAt(double Px, double Py, int32 Step)
{
	// one level per click (-1 = closer). The planet point under the cursor stays under the cursor, everything else
	// scales around it: click the same hexagon again and again and you dive straight down into it.
	// one level per gesture: a mouse wheel sends several notches in a few milliseconds, which raced through every level
	if (Time < NextZoomAt) return;
	const int32 NewLevel = FMath::Clamp(MapLevel + Step, 0, FNTableScene::NumLevels - 1);
	if (NewLevel == MapLevel) return;
	NextZoomAt = Time + 0.4f;
	const double NewR = FNTableScene::LevelR(NewLevel);
	const double F = NewR / PickRadiusM;
	MapLevel = NewLevel;
	const double Cx = Px + (TableCx - Px) * F, Cy = Py + (TableCy - Py) * F;
	double La, Lo;
	FNPlanet::VecToLatLon(FNPlanet::PlaneToVec(TableFace, Cx, Cy), La, Lo);
	PickLat = FMath::Clamp(La, -89.9, 89.9);
	PickLon = Lo;
	PickDir = BoardDir(PickLon, PickLat).GetSafeNormal();
	PickRadiusM = NewR;
	bUserPicked = true;
	// the zoom flows: the new level starts at the size the old view showed it and eases to full size around the anchor
	ZoomAnchor = FNTableScene::ActionC + FVector((float)((Px - TableCx) * TableK), (float)(-(Py - TableCy) * TableK), 0.f);
	// FN7 (Mac session, view-fix go-ahead): a level step is 10-100x, and starting the new level at its true size made it
	// grow out of a speck at the cursor (Chris said popping in from the centre looks wrong). The visible zoom of a step
	// is ~1.8x either way: the new level starts nearly filling the board and settles; the old one swells and sinks.
	ZoomAnimS0 = F < 1.0 ? FMath::Max((float)F, 0.55f) : FMath::Min((float)F, 1.8f);
	ZoomAnimT = 0.f;
	BeginZoomGhost();   // FN7 (Mac session, view-fix go-ahead): the old level stays on screen through the switch (UI/FNBoardAoe.cpp)
	bReliefHold = false;   // FN7: a new level gets its own relief scale
	NoteRebuild(TEXT("ZoomMapAt"));
	BuildTable();   // now, not next frame: a fast second click must land on the new scene
	RebuildIn = -1.f;
}

bool AFNStudio::MapClick(bool bZoomIn)
{
	double Px, Py;
	if (!MapPointUnderCursor(Px, Py)) return false;
	double La, Lo;
	FNPlanet::VecToLatLon(FNPlanet::PlaneToVec(TableFace, Px, Py), La, Lo);
	UE_LOG(LogTemp, Display, TEXT("FNZoom: %s at %.6f, %.6f (map was %.0f m across)"), bZoomIn ? TEXT("in") : TEXT("out"), La, Lo, PickRadiusM * 2);
	ZoomMapAt(Px, Py, bZoomIn ? -1 : 1);
	PlaySfx(EFNSfx::Whoosh, 0.35f);
	return bHasFocus;   // before the match starts, the click also still moves the menus along (start, hero cards)
}

void AFNStudio::MapHome()
{
	if (!bHasFocus) return;
	bUserPicked = false;
	PickSpot(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FocusDir.Z, -1.0, 1.0))), FMath::RadiansToDegrees(FMath::Atan2(-FocusDir.Y, FocusDir.X)), false);
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
		Projector->SetCastShadows(false);   // perf: only the BoardKey spot casts shadows (see AFNStudio::Light)
		Projector->RegisterComponent();
		// floats over the left of the table, facing the chair (it used to lie on the table, under the HUD's bottom-left box)
		TableInfo = Text(TEXT("TableInfo"), FVector(-105.f, 60.f, 140.f), FRotator(0.f, 62.f, 0.f), 3.8f, FColor(170, 230, 255), FString());
		// the zoom / cell readout is a developer aid: hidden unless Config/FN7DevInfo.txt exists (the turn bar sits there now)
		TableInfo->SetVisibility(FPaths::FileExists(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("FN7DevInfo.txt"))));
		RebuildIn = 0.f;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC)
	{
		// zoom: mouse wheel or + / -, one level per notch, around the cursor when it's over the map (else the map centre)
		int32 Step = 0;
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) || PC->WasInputKeyJustPressed(EKeys::Add) || PC->WasInputKeyJustPressed(EKeys::Equals)) Step = -1;
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) || PC->WasInputKeyJustPressed(EKeys::Subtract) || PC->WasInputKeyJustPressed(EKeys::Hyphen)) Step = 1;
		double Px, Py;
		if (Step != 0 && TableFace >= 0)
		{
			if (!MapPointUnderCursor(Px, Py)) { Px = TableCx; Py = TableCy; }
			ZoomMapAt(Px, Py, Step);   // FN7 (Mac session): no click per wheel step (the zoom has its whoosh; Chris heard clicks)
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
				MapLevel = FNTableScene::NumLevels - 1;    // click 1 of the dive: the region around that spot
				PickSpot(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Local.Z, -1.0, 1.0))), FMath::RadiansToDegrees(FMath::Atan2(-Local.Y, Local.X)), true);
			}
		}
	}

	if (RebuildIn >= 0.f && (RebuildIn -= Dt) <= 0.f) { RebuildIn = -1.f; BuildTable(); }
	if (ZoomAnimT >= 0.f && MapPivot)
	{
		ZoomAnimT = FMath::Min(1.f, ZoomAnimT + Dt / 0.45f);
		const float E = 1.f - FMath::Cube(1.f - ZoomAnimT);                 // ease out
		const float Sc = FMath::Exp(FMath::Loge(ZoomAnimS0) * (1.f - E));     // even speed in log scale, like a web map
		// FN7 (Mac session, view-fix go-ahead): the new level's tiles rise from flat (height E) instead of scaling their
		// heights with the zoom (needles going in, towers going out), and the old level's ghost moves with it.
		const float Rise = 0.55f + 0.45f * E;   // settles up a little, never rises out of a flat table
		MapPivot->SetRelativeScale3D(FVector(Sc, Sc, Rise));
		MapPivot->SetRelativeLocation(FVector(ZoomAnchor.X * (1.f - Sc), ZoomAnchor.Y * (1.f - Sc), ZoomAnchor.Z * (1.f - Rise)));   // the anchor stays still
		TickZoomGhost(Sc, E);
		if (ZoomAnimT >= 1.f) { ZoomAnimT = -1.f; MapPivot->SetRelativeScale3D(FVector::OneVector); MapPivot->SetRelativeLocation(FVector::ZeroVector); EndZoomGhost(); }
	}

	// the beam: rays from the picked spot on the Globe down to the rim of the circle of action
	const FVector Apex = BoardToActor(PickDir, BoardR + 4.f);
	for (int32 i = 0; i < BeamRays.Num(); ++i)
	{
		const float A = 2.f * PI * i / BeamRays.Num() + Time * 0.15f;
		const FVector Foot = FNTableScene::ActionC + FVector(FMath::Cos(A) * FNTableScene::ActionR, FMath::Sin(A) * FNTableScene::ActionR, 1.f);
		const FVector D = Foot - Apex;
		BeamRays[i]->SetRelativeLocation((Apex + Foot) * 0.5f);
		BeamRays[i]->SetRelativeRotation(FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
		BeamRays[i]->SetRelativeScale3D(FVector(0.003f, 0.003f, D.Size() / 100.f));
	}
	if (Projector)
	{
		Projector->SetRelativeLocation(Apex + (FNTableScene::ActionC - Apex).GetSafeNormal() * 12.f);   // just below the Globe, so it doesn't glare on it
		Projector->SetRelativeRotation((FNTableScene::ActionC - Apex).Rotation());
		const float Cone = FMath::RadiansToDegrees(FMath::Atan2(FNTableScene::ActionR * 1.05f, FVector::Dist(FNTableScene::ActionC, Apex)));
		Projector->SetOuterConeAngle(Cone);
		Projector->SetInnerConeAngle(Cone * 0.8f);
	}
	// the fog bank drifts slowly around the rim
	for (int32 i = 0; i < FogPuffs.Num(); ++i)
	{
		const float A = 2.f * PI * i / FogPuffs.Num() + Time * (i % 2 ? 0.03f : -0.02f);
		const float Rr = FNTableScene::ActionR * (1.0f + 0.05f * FMath::Sin(Time * 0.4f + i * 1.7f));
		FogPuffs[i]->SetRelativeLocation(FNTableScene::ActionC + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 2.5f + 1.5f * FMath::Sin(Time * 0.5f + i)));
		FogPuffs[i]->SetRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(A), 0.f));
	}
}

// --- earth-simulator hook --------------------------------------------------------------------------------------------

bool AFNStudio::BoardHexTop(int32 Q, int32 R, bool bWithEarth, float& OutTopCm, FVector& OutWorld) const
{
	// FN7 (Mac session, earth playback): the exact top of a board tile, as BuildTable stacks it (GroundH by biome, then
	// the earth hook when bWithEarth), and its centre in the world, so overlays sit ON the tile, never inside it.
	using namespace FNPlanet;
	if (TableFace < 0 || TableK <= 0.0) return false;
	const FVector2D P = HexToPlane(Q, R, TableS);
	if (FVector2D::Distance(P, FVector2D(TableCx, TableCy)) > PickRadiusM) return false;
	const FVector3d V = PlaneToVec(TableFace, P.X, P.Y);
	double La, Lo;
	VecToLatLon(V, La, Lo);
	const double E = ElevationM(V, TableS, La, Lo);
	const float Def = FNTableScene::GroundH(BiomeOf(E, La), E, TableLo, TableRange);
	OutTopCm = bWithEarth ? BoardHeightAtHex(Q, R, Def) : Def;
	FVector Local = FNTableScene::ActionC + FVector((float)((P.X - TableCx) * TableK), (float)(-(P.Y - TableCy) * TableK), OutTopCm);
	if (MapPivot) Local = MapPivot->GetRelativeTransform().TransformPosition(Local);
	OutWorld = GetActorTransform().TransformPosition(Local);
	return true;
}

bool AFNStudio::GetPeopleSpots(TArray<FFNPeopleSpot>& OutPeople, TArray<FVector>& OutHouses) const
{
	if (!PeopleSet) return false;
	const FTransform X = PeopleSet->GetComponentTransform();   // spots are kept in the sets' space: the board may slide/scale after a build
	OutPeople = PeopleSpots;
	for (FFNPeopleSpot& P : OutPeople) P.Pos = X.TransformPosition(P.Pos);
	OutHouses.Reset();
	for (const FVector& H : HouseSpots) OutHouses.Add(X.TransformPosition(H));
	return true;
}

FString AFNStudio::BoardCellKey(int32 Q, int32 R) const
{
	static const TCHAR* LevelNames[FNTableScene::NumLevels] = { TEXT("people"), TEXT("houses"), TEXT("villages"), TEXT("regions") };
	return FString::Printf(TEXT("%d:%s:%d:%d"), TableFace, LevelNames[FMath::Clamp(MapLevel, 0, FNTableScene::NumLevels - 1)], Q, R);
}

float AFNStudio::BoardHeightAt(const FString& CellKey, float DefaultRelief) const
{
	const FHexOverride* O = HexOverrides.Find(CellKey);
	return O && O->bHeight ? O->HeightCm : DefaultRelief;
}

EFNHexState AFNStudio::HexStateAt(const FString& CellKey) const
{
	const FHexOverride* O = HexOverrides.Find(CellKey);
	return O ? O->State : EFNHexState::Normal;
}

void AFNStudio::SetHexOverride(const FString& CellKey, float HeightCm, EFNHexState State)
{
	FHexOverride& O = HexOverrides.FindOrAdd(CellKey);
	O.bHeight = !FMath::IsNaN(HeightCm);
	O.HeightCm = O.bHeight ? HeightCm : 0.f;
	O.State = State;
}

void AFNStudio::ClearHexOverrides()
{
	if (HexOverrides.Num() == 0) return;
	HexOverrides.Reset();
	RebuildBoard();
}
