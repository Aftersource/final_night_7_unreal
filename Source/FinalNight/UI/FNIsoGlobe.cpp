#include "FNIsoGlobe.h"

#include "FNStudio.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

AFNIsoGlobe::AFNIsoGlobe()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

namespace FN7Iso
{
	static constexpr int32 N = 64;   // grid cells per side
	/** The field in units of the globe's radius: a gyroid lattice cut to a spherical shell. Negative inside. */
	static float Field(const FVector3f& P)
	{
		const float K = 2.f * PI * 1.6f;
		const float X = P.X * K, Y = P.Y * K, Z = P.Z * K;
		const float G = FMath::Sin(X) * FMath::Cos(Y) + FMath::Sin(Y) * FMath::Cos(Z) + FMath::Sin(Z) * FMath::Cos(X);
		const float Lattice = FMath::Abs(G) - 0.32f;
		const float Shell = FMath::Abs(P.Size() - 1.22f) - 0.07f;
		return FMath::Max(Lattice, Shell);
	}
}

void AFNIsoGlobe::Build()
{
	using namespace FN7Iso;
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(GetWorld()); It) St = *It;
	if (!St) return;
	UStaticMeshComponent* Globe = nullptr;
	TArray<UStaticMeshComponent*> Ms;
	St->GetComponents<UStaticMeshComponent>(Ms);
	for (UStaticMeshComponent* M : Ms) if (M->GetFName() == AFNStudio::Globe) { Globe = M; break; }
	if (!Globe) return;
	const FBoxSphereBounds B = Globe->Bounds;
	const float R = FMath::Max3((float)B.BoxExtent.X, (float)B.BoxExtent.Y, (float)B.BoxExtent.Z);
	if (R < 1.f) return;

	// surface nets: the field on the grid corners, one vertex per cell the surface crosses (the mean of its edge
	// crossings), one quad per crossed grid edge
	const float Lo = -1.4f, Step = 2.8f / N;
	const int32 V = N + 1;
	TArray<float> F;
	F.SetNumUninitialized(V * V * V);
	auto At = [V](int32 x, int32 y, int32 z) { return (z * V + y) * V + x; };
	auto Pt = [Lo, Step](int32 x, int32 y, int32 z) { return FVector3f(Lo + x * Step, Lo + y * Step, Lo + z * Step); };
	for (int32 z = 0; z < V; ++z) for (int32 y = 0; y < V; ++y) for (int32 x = 0; x < V; ++x) F[At(x, y, z)] = Field(Pt(x, y, z));
	TArray<int32> CellVert;
	CellVert.Init(-1, N * N * N);
	auto Cell = [](int32 x, int32 y, int32 z) { return (z * N + y) * N + x; };
	TArray<FVector3f> Pos, Nrm;
	static const int32 E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	for (int32 z = 0; z < N; ++z) for (int32 y = 0; y < N; ++y) for (int32 x = 0; x < N; ++x)
	{
		float C[8]; FVector3f P[8];
		int32 Neg = 0;
		for (int32 k = 0; k < 8; ++k)
		{
			const int32 dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
			C[k] = F[At(x + dx, y + dy, z + dz)];
			P[k] = Pt(x + dx, y + dy, z + dz);
			Neg += C[k] < 0.f;
		}
		if (Neg == 0 || Neg == 8) continue;
		FVector3f Sum = FVector3f::ZeroVector;
		int32 Cnt = 0;
		for (const auto& Ed : E)
		{
			const float A = C[Ed[0]], Bv = C[Ed[1]];
			if ((A < 0.f) == (Bv < 0.f)) continue;
			const float T = A / (A - Bv);
			Sum += P[Ed[0]] + (P[Ed[1]] - P[Ed[0]]) * T;
			++Cnt;
		}
		const FVector3f Q = Sum / (float)Cnt;
		const float H = Step * 0.5f;
		const FVector3f Gr(Field(Q + FVector3f(H, 0, 0)) - Field(Q - FVector3f(H, 0, 0)), Field(Q + FVector3f(0, H, 0)) - Field(Q - FVector3f(0, H, 0)), Field(Q + FVector3f(0, 0, H)) - Field(Q - FVector3f(0, 0, H)));
		CellVert[Cell(x, y, z)] = Pos.Num();
		Pos.Add(Q * R);
		Nrm.Add(Gr.GetSafeNormal(SMALL_NUMBER, FVector3f::UpVector));
	}
	TArray<int32> Idx;
	auto Quad = [&Idx](int32 a, int32 b, int32 c, int32 d, bool bFlip)
	{
		if (a < 0 || b < 0 || c < 0 || d < 0) return;
		if (bFlip) Idx.Append({ a, c, b, a, d, c }); else Idx.Append({ a, b, c, a, c, d });
	};
	for (int32 z = 1; z < N; ++z) for (int32 y = 1; y < N; ++y) for (int32 x = 0; x < N; ++x)   // x edges
	{
		const float A = F[At(x, y, z)], Bv = F[At(x + 1, y, z)];
		if ((A < 0.f) == (Bv < 0.f)) continue;
		Quad(CellVert[Cell(x, y - 1, z - 1)], CellVert[Cell(x, y, z - 1)], CellVert[Cell(x, y, z)], CellVert[Cell(x, y - 1, z)], A < 0.f);
	}
	for (int32 z = 1; z < N; ++z) for (int32 y = 0; y < N; ++y) for (int32 x = 1; x < N; ++x)   // y edges
	{
		const float A = F[At(x, y, z)], Bv = F[At(x, y + 1, z)];
		if ((A < 0.f) == (Bv < 0.f)) continue;
		Quad(CellVert[Cell(x - 1, y, z - 1)], CellVert[Cell(x - 1, y, z)], CellVert[Cell(x, y, z)], CellVert[Cell(x, y, z - 1)], A < 0.f);
	}
	for (int32 z = 0; z < N; ++z) for (int32 y = 1; y < N; ++y) for (int32 x = 1; x < N; ++x)   // z edges
	{
		const float A = F[At(x, y, z)], Bv = F[At(x, y, z + 1)];
		if ((A < 0.f) == (Bv < 0.f)) continue;
		Quad(CellVert[Cell(x - 1, y - 1, z)], CellVert[Cell(x, y - 1, z)], CellVert[Cell(x, y, z)], CellVert[Cell(x - 1, y, z)], A < 0.f);
	}
	if (Idx.Num() < 3) return;

	FMeshDescription Md;
	FStaticMeshAttributes Ma(Md);
	Ma.Register();
	Ma.GetVertexInstanceUVs().SetNumChannels(1);
	const FPolygonGroupID Grp = Md.CreatePolygonGroup();
	Ma.GetPolygonGroupMaterialSlotNames()[Grp] = FName(TEXT("Gold"));
	TArray<FVertexInstanceID> Vi;
	Vi.Reserve(Pos.Num());
	for (int32 k = 0; k < Pos.Num(); ++k)
	{
		const FVertexID Vx = Md.CreateVertex();
		Ma.GetVertexPositions()[Vx] = Pos[k];
		const FVertexInstanceID Ins = Md.CreateVertexInstance(Vx);
		const FVector3f Nn = Nrm[k];
		Ma.GetVertexInstanceNormals()[Ins] = Nn;
		Ma.GetVertexInstanceTangents()[Ins] = FVector3f::CrossProduct(Nn, FMath::Abs(Nn.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector).GetSafeNormal();
		Ma.GetVertexInstanceBinormalSigns()[Ins] = 1.f;
		Ma.GetVertexInstanceUVs().Set(Ins, 0, FVector2f(0.f, 0.f));
		Vi.Add(Ins);
	}
	for (int32 t = 0; t + 2 < Idx.Num(); t += 3)
		if (Idx[t] != Idx[t + 1] && Idx[t + 1] != Idx[t + 2] && Idx[t] != Idx[t + 2]) Md.CreateTriangle(Grp, { Vi[Idx[t]], Vi[Idx[t + 1]], Vi[Idx[t + 2]] });
	Mesh = NewObject<UStaticMesh>(this, NAME_None, RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, FName(TEXT("Gold"))));
	UStaticMesh::FBuildMeshDescriptionsParams Prm;
	Prm.bBuildSimpleCollision = false;
	Prm.bFastBuild = true;
	Mesh->BuildFromMeshDescriptions({ &Md }, Prm);

	Shell = NewObject<UStaticMeshComponent>(this);
	Shell->SetupAttachment(RootComponent);
	Shell->SetStaticMesh(Mesh);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->SetCastShadow(true);
	if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Basic, Shell);
		D->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.72f, 0.28f));
		Shell->SetMaterial(0, D);
	}
	Shell->RegisterComponent();
	SetActorLocation(B.Origin);
	bBuilt = true;
	UE_LOG(LogTemp, Display, TEXT("FN7 iso globe: %d vertices, %d triangles around the globe (radius %.1f)"), Pos.Num(), Idx.Num() / 3, R);
}

void AFNIsoGlobe::Tick(float Dt)
{
	Super::Tick(Dt);
	// Once. The effect cannot persist: turn it off after a 10-second smooth fadeout, and keep it off after that.
	// at most 10 s on screen, shrinking away over the last 1.5 s (sooner if you join), then gone for good, never shown again
	static bool bShownOnce = false;
	if (!bBuilt)
	{
		if (bShownOnce) { Destroy(); return; }
		Build();
		if (!bBuilt) return;
		bShownOnce = true;
	}
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(GetWorld()); It) St = *It;
	Age += Dt;
	if (FadeT < 0.f && (Age >= 8.5f || !St || !St->IsGuardBlitz())) { FadeT = 0.f; UE_LOG(LogTemp, Display, TEXT("FN7 iso globe: fading out (%.1f s on screen)"), Age); }
	float Sc = 1.f;
	if (FadeT >= 0.f)
	{
		FadeT += Dt;
		const float E = FMath::Clamp(FadeT / 1.5f, 0.f, 1.f);
		Sc = 1.f - E * E * (3.f - 2.f * E);
		if (E >= 1.f)
		{
			UE_LOG(LogTemp, Display, TEXT("FN7 iso globe: gone (shown once)"));
			Destroy();
			return;
		}
	}
	Spin += Dt * 6.f;
	SetActorRotation(FRotator(0.f, Spin, 0.f));
	SetActorScale3D(FVector(FMath::Max(Sc, 0.001f)));
}

bool UFNIsoGlobeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNIsoGlobeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNIsoGlobe>(AFNIsoGlobe::StaticClass(), FTransform::Identity, P);
}
