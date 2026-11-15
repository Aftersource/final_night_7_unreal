#include "Features/FNItemIcon.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"

namespace FNItemIconImpl
{
	enum class EShape : uint8 { Cube, Cylinder, Cone, Sphere };

	/** One part, in units of the model's height (1 = SizeCm): centre, size (x, y, z), rotation (pitch, yaw, roll), shade. */
	struct FPart
	{
		EShape Shape;
		FVector Pos;
		FVector Dim;
		FRotator Rot;
		float Shade;
	};

	static UStaticMesh* MeshFor(EShape S)
	{
		static UStaticMesh* Meshes[4] = { nullptr, nullptr, nullptr, nullptr };
		static const TCHAR* Paths[4] = {
			TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
			TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("/Engine/BasicShapes/Sphere.Sphere") };
		const int32 I = (int32)S;
		if (!Meshes[I]) Meshes[I] = LoadObject<UStaticMesh>(nullptr, Paths[I]);
		return Meshes[I];
	}

	static UMaterialInterface* BaseMaterial()
	{
		static UMaterialInterface* M = nullptr;
		if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		return M;
	}

	static FPart P(EShape S, float X, float Y, float Z, float W, float D, float H, float Pitch = 0.f, float Yaw = 0.f, float RollDeg = 0.f, float Shade = 1.f)
	{
		return FPart{ S, FVector(X, Y, Z), FVector(W, D, H), FRotator(Pitch, Yaw, RollDeg), Shade };
	}

	/** The parts for an icon kind. Heights run 0..1 (the model stands on Z = 0). */
	static TArray<FPart> PartsFor(const FString& Icon)
	{
		using E = EShape;
		const FString K = Icon.ToLower();
		if (K == TEXT("sword"))
			return { P(E::Cube, 0, 0, 0.62f, 0.07f, 0.02f, 0.62f, 0, 0, 0, 1.15f),     // blade
			         P(E::Cone, 0, 0, 0.97f, 0.07f, 0.02f, 0.08f, 0, 0, 0, 1.15f),     // point
			         P(E::Cube, 0, 0, 0.30f, 0.28f, 0.05f, 0.04f, 0, 0, 0, 0.7f),      // crossguard
			         P(E::Cylinder, 0, 0, 0.17f, 0.05f, 0.05f, 0.22f, 0, 0, 0, 0.45f), // grip
			         P(E::Sphere, 0, 0, 0.04f, 0.08f, 0.08f, 0.08f, 0, 0, 0, 0.7f) };  // pommel
		if (K == TEXT("ranged"))
			return { P(E::Cylinder, 0.06f, 0, 0.78f, 0.04f, 0.04f, 0.42f, 0, 0, -25.f, 0.6f),   // upper limb
			         P(E::Cylinder, 0.06f, 0, 0.22f, 0.04f, 0.04f, 0.42f, 0, 0, 25.f, 0.6f),    // lower limb
			         P(E::Cylinder, 0.13f, 0, 0.50f, 0.05f, 0.05f, 0.16f, 0, 0, 0, 0.45f),      // grip
			         P(E::Cylinder, -0.03f, 0, 0.50f, 0.01f, 0.01f, 0.96f, 0, 0, 0, 1.3f) };    // string
		if (K == TEXT("armor"))
			return { P(E::Sphere, 0, 0, 0.55f, 0.55f, 0.30f, 0.62f, 0, 0, 0, 1.0f),     // chest plate
			         P(E::Sphere, -0.30f, 0, 0.80f, 0.22f, 0.22f, 0.16f, 0, 0, 0, 0.8f), // pauldron L
			         P(E::Sphere, 0.30f, 0, 0.80f, 0.22f, 0.22f, 0.16f, 0, 0, 0, 0.8f),  // pauldron R
			         P(E::Cube, 0, 0, 0.16f, 0.42f, 0.24f, 0.10f, 0, 0, 0, 0.55f) };     // belt
		if (K == TEXT("banner_army"))
			return { P(E::Cylinder, -0.18f, 0, 0.50f, 0.04f, 0.04f, 1.0f, 0, 0, 0, 0.45f), // pole
			         P(E::Cube, 0.05f, 0, 0.74f, 0.42f, 0.02f, 0.34f, 0, 0, 0, 1.1f),      // flag
			         P(E::Cone, -0.18f, 0, 1.02f, 0.07f, 0.07f, 0.06f, 0, 0, 0, 0.8f) };   // finial
		if (K == TEXT("vehicle"))
			return { P(E::Cube, 0, 0, 0.42f, 0.80f, 0.42f, 0.32f, 0, 0, 0, 1.0f),         // body
			         P(E::Cube, -0.1f, 0, 0.66f, 0.40f, 0.36f, 0.18f, 0, 0, 0, 0.8f),      // cab
			         P(E::Cylinder, -0.28f, -0.24f, 0.14f, 0.26f, 0.26f, 0.08f, 90.f, 0, 0, 0.4f),
			         P(E::Cylinder, 0.28f, -0.24f, 0.14f, 0.26f, 0.26f, 0.08f, 90.f, 0, 0, 0.4f),
			         P(E::Cylinder, -0.28f, 0.24f, 0.14f, 0.26f, 0.26f, 0.08f, 90.f, 0, 0, 0.4f),
			         P(E::Cylinder, 0.28f, 0.24f, 0.14f, 0.26f, 0.26f, 0.08f, 90.f, 0, 0, 0.4f) };
		if (K == TEXT("beast"))
			return { P(E::Sphere, 0, 0, 0.50f, 0.70f, 0.38f, 0.36f, 0, 0, 0, 1.0f),        // body
			         P(E::Sphere, 0.42f, 0, 0.70f, 0.26f, 0.22f, 0.24f, 0, 0, 0, 1.0f),     // head
			         P(E::Cone, 0.56f, 0.06f, 0.86f, 0.06f, 0.06f, 0.12f, 0, 0, 0, 0.7f),   // ear / horn
			         P(E::Cone, 0.56f, -0.06f, 0.86f, 0.06f, 0.06f, 0.12f, 0, 0, 0, 0.7f),
			         P(E::Cylinder, -0.22f, -0.12f, 0.17f, 0.08f, 0.08f, 0.34f, 0, 0, 0, 0.75f),
			         P(E::Cylinder, -0.22f, 0.12f, 0.17f, 0.08f, 0.08f, 0.34f, 0, 0, 0, 0.75f),
			         P(E::Cylinder, 0.22f, -0.12f, 0.17f, 0.08f, 0.08f, 0.34f, 0, 0, 0, 0.75f),
			         P(E::Cylinder, 0.22f, 0.12f, 0.17f, 0.08f, 0.08f, 0.34f, 0, 0, 0, 0.75f),
			         P(E::Cone, -0.42f, 0, 0.56f, 0.05f, 0.05f, 0.22f, 60.f, 0, 0, 0.75f) }; // tail
		if (K == TEXT("structure"))
			return { P(E::Cylinder, 0, 0, 0.36f, 0.42f, 0.42f, 0.72f, 0, 0, 0, 1.0f),      // tower
			         P(E::Cone, 0, 0, 0.86f, 0.52f, 0.52f, 0.28f, 0, 0, 0, 0.6f),          // roof
			         P(E::Cube, 0, -0.21f, 0.14f, 0.12f, 0.02f, 0.2f, 0, 0, 0, 0.3f) };    // door
		if (K == TEXT("relic"))
			return { P(E::Cylinder, 0, 0, 0.18f, 0.44f, 0.44f, 0.36f, 0, 0, 0, 0.55f),     // pedestal
			         P(E::Cube, 0, 0, 0.58f, 0.30f, 0.30f, 0.30f, 45.f, 45.f, 0, 1.3f),    // gem
			         P(E::Sphere, 0, 0, 0.58f, 0.46f, 0.46f, 0.46f, 0, 0, 0, 0.25f) };     // halo (dim)
		if (K == TEXT("artifact"))
			return { P(E::Sphere, 0, 0, 0.55f, 0.42f, 0.42f, 0.42f, 0, 0, 0, 1.25f),       // orb
			         P(E::Cylinder, 0, 0, 0.55f, 0.66f, 0.66f, 0.04f, 70.f, 0, 0, 0.7f),   // ring
			         P(E::Cylinder, 0, 0, 0.10f, 0.30f, 0.30f, 0.20f, 0, 0, 0, 0.5f) };    // stand
		if (K == TEXT("phenomenon"))
			return { P(E::Sphere, 0, 0, 0.22f, 0.40f, 0.40f, 0.40f, 0, 0, 0, 0.8f),
			         P(E::Sphere, 0.16f, 0.06f, 0.52f, 0.30f, 0.30f, 0.30f, 0, 0, 0, 1.0f),
			         P(E::Sphere, -0.10f, -0.06f, 0.80f, 0.22f, 0.22f, 0.22f, 0, 0, 0, 1.25f),
			         P(E::Cone, 0, 0, 0.50f, 0.60f, 0.60f, 1.0f, 180.f, 0, 0, 0.3f) };     // funnel
		// thing (and anything unknown): a crate with bands
		return { P(E::Cube, 0, 0, 0.30f, 0.60f, 0.60f, 0.60f, 0, 0, 0, 1.0f),
		         P(E::Cube, 0, 0, 0.30f, 0.62f, 0.62f, 0.06f, 0, 0, 0, 0.5f),
		         P(E::Cube, 0, 0, 0.30f, 0.06f, 0.62f, 0.62f, 0, 0, 0, 0.5f) };
	}

	static void Paint(UStaticMeshComponent* C, const FLinearColor& Tint, float Shade)
	{
		UMaterialInterface* Base = BaseMaterial();
		if (!C || !Base) return;
		UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!M || M->Parent != Base) { M = UMaterialInstanceDynamic::Create(Base, C); C->SetMaterial(0, M); }
		FLinearColor Col = Tint * Shade;
		Col.A = 1.f;
		M->SetVectorParameterValue(TEXT("Color"), Col);
	}
}

TArray<UStaticMeshComponent*> FNItemIcon::Build(USceneComponent* Parent, const FString& Icon, const FLinearColor& Tint, float SizeCm)
{
	using namespace FNItemIconImpl;
	TArray<UStaticMeshComponent*> Out;
	if (!Parent || !Parent->GetOwner()) return Out;
	AActor* Host = Parent->GetOwner();
	const float S = FMath::Max(1.f, SizeCm);
	for (const FPart& Pt : PartsFor(Icon))
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Host);
		C->SetStaticMesh(MeshFor(Pt.Shape));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Parent);
		// The basic shapes are 100 cm across with their pivot at the centre.
		C->SetRelativeLocation(Pt.Pos * S);
		C->SetRelativeRotation(Pt.Rot);
		C->SetRelativeScale3D(Pt.Dim * (S / 100.f));
		C->RegisterComponent();
		Paint(C, Tint, Pt.Shade);
		C->ComponentTags.Add(*FString::Printf(TEXT("FNItemIconShade=%.2f"), Pt.Shade));
		Out.Add(C);
	}
	return Out;
}

void FNItemIcon::SetTint(const TArray<UStaticMeshComponent*>& Parts, const FLinearColor& Tint)
{
	using namespace FNItemIconImpl;
	for (UStaticMeshComponent* C : Parts)
	{
		if (!C) continue;
		float Shade = 1.f;
		for (const FName& T : C->ComponentTags)
		{
			const FString TS = T.ToString();
			if (TS.StartsWith(TEXT("FNItemIconShade="))) Shade = FCString::Atof(*TS.Mid(16));
		}
		Paint(C, Tint, Shade);
	}
}
