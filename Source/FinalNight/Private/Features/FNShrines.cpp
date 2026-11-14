#include "Features/FNShrines.h"
#include "Features/FNCellGeo.h"
#include "FNStudio.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"

namespace FNShrinesImpl
{
	static const FLinearColor Stone(0.42f, 0.4f, 0.38f);
	static const FLinearColor Fallen(0.3f, 0.3f, 0.32f);

	static FString JStr(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O.IsValid()) O->TryGetStringField(K, S); return S; }
	static double JNum(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def) { double N = Def; if (O.IsValid()) O->TryGetNumberField(K, N); return N; }
	static FString JKey(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		double N = 0.0;
		if (O.IsValid() && O->TryGetNumberField(K, N)) return FString::FromInt((int32)N);
		return JStr(O, K);
	}
	static bool ParseAura(const FString& Hex, FLinearColor& Out)
	{
		if (Hex.Len() < 7 || Hex[0] != TEXT('#')) return false;
		Out = FLinearColor(FColor::FromHex(Hex));
		return true;
	}
	// 0 worshippers -> 0 figures, ~100 -> 4, ~1,000 -> 6, ~10,000 -> 8 ... up to 24
	static int32 FolkFor(int32 W) { return W <= 0 ? 0 : FMath::Clamp(FMath::RoundToInt(FMath::LogX(10.0, (double)W) * 2.0), 1, 24); }
}

AFNShrines::AFNShrines()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFNShrines::Bind(AFNStudio* InStudio)
{
	Studio = InStudio;
	if (!BaseMat) BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UStaticMeshComponent* AFNShrines::Piece(const TCHAR* Mesh)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	return C;
}

void AFNShrines::Paint(UPrimitiveComponent* C, const FLinearColor& Color)
{
	if (!C || !BaseMat) return;
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, C);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	C->SetMaterial(0, M);
}

void AFNShrines::Repaint(FShrine& S)
{
	using namespace FNShrinesImpl;
	const FLinearColor C = S.bAlive ? S.Color : Fallen;
	Paint(S.Plinth, S.bAlive ? Stone : Fallen * 0.8f);
	Paint(S.Spire, C);
	Paint(S.Tip, C * 1.3f);
	Paint(S.Folk, C * 0.75f);
	if (S.Label) S.Label->SetTextRenderColor(C.ToFColor(true));
}

AFNShrines::FShrine& AFNShrines::Upsert(const FString& Key, const TSharedPtr<FJsonObject>& Src)
{
	using namespace FNShrinesImpl;
	FShrine* Found = Shrines.Find(Key);
	if (!Found)
	{
		FShrine& S = Shrines.Add(Key);
		S.Rise = 0.f;
		S.Plinth = Piece(TEXT("/Engine/BasicShapes/Cube.Cube"));
		S.Spire = Piece(TEXT("/Engine/BasicShapes/Cube.Cube"));
		S.Tip = Piece(TEXT("/Engine/BasicShapes/Cone.Cone"));
		S.Folk = NewObject<UInstancedStaticMeshComponent>(this);
		S.Folk->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		S.Folk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S.Folk->SetCastShadow(false);
		S.Folk->SetMobility(EComponentMobility::Movable);
		S.Folk->SetupAttachment(RootComponent);
		S.Folk->RegisterComponent();
		S.Label = NewObject<UTextRenderComponent>(this);
		S.Label->SetHorizontalAlignment(EHTA_Center);
		S.Label->SetVerticalAlignment(EVRTA_TextBottom);
		S.Label->SetCastShadow(false);
		S.Label->SetMobility(EComponentMobility::Movable);
		S.Label->SetupAttachment(RootComponent);
		S.Label->RegisterComponent();
		Found = &S;
	}
	FShrine& S = *Found;
	const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
	if (Src.IsValid() && Src->TryGetArrayField(TEXT("at"), At) && At && At->Num() >= 2) { S.Lon = (*At)[0]->AsNumber(); S.Lat = (*At)[1]->AsNumber(); }
	else FNCellGeo::CellToLonLat(JStr(Src, TEXT("cell")), S.Lon, S.Lat);
	S.Level = FMath::Clamp((int32)JNum(Src, TEXT("level"), S.Level), 1, 9);
	S.Worshippers = (int32)JNum(Src, TEXT("worshippers"), S.Worshippers);
	const FString NewOwner = JStr(Src, TEXT("owner"));
	if (!NewOwner.IsEmpty()) S.Owner = NewOwner;
	const FString Power = JStr(Src, TEXT("power"));
	if (!Power.IsEmpty()) S.Power = Power;
	bool bAlive = S.bAlive;
	if (Src.IsValid() && Src->TryGetBoolField(TEXT("owner_alive"), bAlive)) S.bAlive = bAlive;
	if (const FLinearColor* A = Auras.Find(S.Owner)) S.Color = *A;
	if (S.Label) S.Label->SetText(FText::FromString(S.Power.ToUpper()));
	Repaint(S);
	return S;
}

void AFNShrines::Apply(const TSharedPtr<FJsonObject>& Payload)
{
	using namespace FNShrinesImpl;
	if (!Payload.IsValid()) return;
	const TArray<TSharedPtr<FJsonValue>>* Dg = nullptr;
	if (Payload->TryGetArrayField(TEXT("demigods"), Dg) && Dg)
		for (const TSharedPtr<FJsonValue>& V : *Dg)
		{
			const TSharedPtr<FJsonObject> D = V.IsValid() ? V->AsObject() : nullptr;
			FLinearColor C;
			if (ParseAura(JStr(D, TEXT("aura")), C)) Auras.Add(JStr(D, TEXT("ref")), C);
		}
	const TArray<TSharedPtr<FJsonValue>>* Sh = nullptr;
	if (Payload->TryGetArrayField(TEXT("shrines"), Sh) && Sh)
		for (const TSharedPtr<FJsonValue>& V : *Sh)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			const bool bNew = !Shrines.Contains(JKey(O, TEXT("id")));
			FShrine& S = Upsert(JKey(O, TEXT("id")), O);
			if (bNew) S.Rise = 1.f;   // already standing when we first saw it
		}
}

void AFNShrines::ApplyEvent(const TSharedPtr<FJsonObject>& E)
{
	using namespace FNShrinesImpl;
	const FString Kind = JStr(E, TEXT("event"));
	if (Kind != TEXT("shrine.built") && Kind != TEXT("shrine.level")) return;
	const FString Key = JKey(E, TEXT("shrine"));
	if (Kind == TEXT("shrine.level") && !Shrines.Contains(Key)) return;   // its place comes with the next game.globe
	const bool bNew = !Shrines.Contains(Key);
	FShrine& S = Upsert(Key, E);
	S.Rise = bNew ? 0.f : FMath::Min(S.Rise, 0.6f);   // built: rises from the ground; levelled: grows again
	if (AFNStudio* St = Studio.Get()) St->FlashAt(S.Lon, S.Lat);
}

void AFNShrines::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AFNStudio* St = Studio.Get();
	if (!St || Shrines.Num() == 0) return;
	FVector RimC; double RimR = 100.0; FRotator Rot;
	St->GetBoardRim(RimC, RimR, Rot);
	const FVector Up = St->GetActorUpVector(), Fwd = St->GetActorForwardVector(), Right = St->GetActorRightVector();
	const float Size = (float)RimR * 0.06f;
	const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector CamLoc = Cam ? Cam->GetCameraLocation() : RimC + Up * 500.f;
	const FQuat Q = Rot.Quaternion();
	for (TPair<FString, FShrine>& KV : Shrines)
	{
		FShrine& S = KV.Value;
		S.Age += DeltaSeconds;
		if (S.Rise < 1.f) S.Rise = FMath::Min(1.f, S.Rise + DeltaSeconds / 1.4f);
		FVector W;
		const bool bOn = St->BoardPointFor(S.Lat, S.Lon, W);
		for (UPrimitiveComponent* C : { (UPrimitiveComponent*)S.Plinth, (UPrimitiveComponent*)S.Spire, (UPrimitiveComponent*)S.Tip, (UPrimitiveComponent*)S.Folk, (UPrimitiveComponent*)S.Label })
			if (C) C->SetVisibility(bOn);
		if (!bOn) continue;
		const float R = FMath::SmoothStep(0.f, 1.f, S.Rise);
		const float PW = Size * 0.32f, PH = Size * 0.08f;
		const float SW = Size * 0.12f, SH = Size * (0.45f + 0.18f * S.Level) * R;
		S.Plinth->SetWorldLocationAndRotation(W + Up * (PH * 0.5f), Q);
		S.Plinth->SetWorldScale3D(FVector(PW, PW, PH) / 100.f);
		S.Spire->SetWorldLocationAndRotation(W + Up * (PH + SH * 0.5f), Q);
		S.Spire->SetWorldScale3D(FVector(SW, SW, FMath::Max(SH, 0.01f)) / 100.f);
		S.Tip->SetWorldLocationAndRotation(W + Up * (PH + SH + SW * 0.45f), Q);
		S.Tip->SetWorldScale3D(FVector(SW * 1.3f, SW * 1.3f, SW * 0.9f) * FMath::Max(R, 0.01f) / 100.f);
		const FVector LabelAt = W + Up * (PH + SH + SW * 1.2f);
		S.Label->SetWorldLocation(LabelAt);
		S.Label->SetWorldRotation(FRotationMatrix::MakeFromXZ(CamLoc - LabelAt, Up).Rotator());
		S.Label->SetWorldSize(Size * 0.28f * FMath::Max(R, 0.01f));
		// the worshippers walk slowly around it, bobbing
		const int32 N = FMath::RoundToInt(FNShrinesImpl::FolkFor(S.Worshippers) * R);
		TArray<FTransform> Folk;
		for (int32 i = 0; i < N; ++i)
		{
			const float Ring = Size * (0.32f + 0.12f * (i % 2));
			const float A = 2.f * PI * i / FMath::Max(1, N) + S.Age * (i % 2 ? -0.25f : 0.35f);
			const float Bob = Size * 0.03f * FMath::Abs(FMath::Sin(S.Age * 3.f + i));
			const FVector P = W + (Fwd * FMath::Cos(A) + Right * FMath::Sin(A)) * Ring + Up * (Size * 0.05f + Bob);
			Folk.Add(FTransform(Q, P, FVector(Size * 0.05f, Size * 0.05f, Size * 0.09f) / 100.f));
		}
		S.Folk->ClearInstances();
		if (Folk.Num()) S.Folk->AddInstances(Folk, false, true);
	}
}
