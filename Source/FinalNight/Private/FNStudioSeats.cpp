// The four seats round the table (Chris's must-have): each OTHER seat has an abstract silhouette behind the table
// with an aura that says who sits there (HUMAN = warm gold halo that breathes, AI = cyan cubes orbiting the head,
// flickering). In front of every seat, on the table, stands that demigod's minifig in its colour, or its diorama
// image on a standee once one arrives. The widget calls ShowSeats every beat and on room.updated; Tick animates.
#include "FNStudio.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace FNSeats
{
	static constexpr int32 NumSeats = 4, HaloBeads = 14, AiCubes = 6;
	static constexpr float BodyR = 330.f;   // silhouettes stand just past the table edge (table radius 240 cm)
	static constexpr float FigR = 222.f;    // minifigs stand near the table rim
	static const FVector ImageUp = -FVector::UpVector;   // the plane's texture V runs along +Y, top of the image at -Y
	static const FLinearColor Shadow(0.006f, 0.006f, 0.009f);
	static const FLinearColor HumanAura(1.f, 0.72f, 0.25f);
	static const FLinearColor AiAura(0.2f, 0.85f, 1.f);
	static const FLinearColor SeatBrass(0.55f, 0.38f, 0.12f);
	static const FLinearColor Fallen(0.12f, 0.12f, 0.12f);
	static const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* Sph = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* Cub = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* Pln = TEXT("/Engine/BasicShapes/Plane.Plane");
	// slot 0 = mine (the camera at +Y), then clockwise seen from above. Not an even 90 degrees: the camera only sees
	// the far half of the table, and straight across is hidden by the Globe and the easel; the right edge has the WILL panel.
	static float SlotAngle(int32 Slot) { static const float A[NumSeats] = { 90.f, 200.f, 235.f, 305.f }; return A[Slot]; }
	// minifigs: mine sits left of centre (the WILL panel covers the right), the others in front of their silhouette
	static float FigAngle(int32 Slot) { return Slot == 0 ? 110.f : SlotAngle(Slot); }
	static FVector Dir(float Deg) { const float A = FMath::DegreesToRadians(Deg); return FVector(FMath::Cos(A), FMath::Sin(A), 0.f); }
	static void SetCol(UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (UMaterialInstanceDynamic* M = C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr) M->SetVectorParameterValue(TEXT("Color"), Col);
	}
}
using namespace FNSeats;

void AFNStudio::BuildSeats()
{
	auto Quiet = [](UStaticMeshComponent* C) { C->SetCollisionEnabled(ECollisionEnabled::NoCollision); return C; };
	auto Keep = [this](auto* C) { SeatParts.Add(C); return C; };
	SeatSlots.SetNum(NumSeats);
	for (int32 k = 0; k < NumSeats; ++k)
	{
		FSeatSlot& S = SeatSlots[k];
		const float A = SlotAngle(k);
		const FString P = FString::Printf(TEXT("Seat%d"), k);
		auto N = [&P](const TCHAR* What) { return FName(*(P + What)); };
		// clicking the minifig or the silhouette picks that demigod (AFNStudio::ClickDemigod, id "SeatN")
		auto Clickable = [&P](UStaticMeshComponent* C) { C->SetCollisionEnabled(ECollisionEnabled::QueryOnly); C->ComponentTags.Add(FName(*(TEXT("FN:") + P))); return C; };

		if (k > 0)   // my own seat is the camera: no silhouette there
		{
			S.Body = Keep(NewObject<USceneComponent>(this, N(TEXT("Body"))));
			S.Body->SetupAttachment(RootComponent);
			S.Body->SetMobility(EComponentMobility::Movable);
			S.Body->SetRelativeLocation(Dir(A) * BodyR);
			S.Body->SetRelativeRotation(FRotator(0.f, A + 180.f, 0.f));   // local +X looks at the table
			S.Body->RegisterComponent();
			Keep(Clickable(Shape(Cyl, N(TEXT("Torso")), FVector(0, 0, 128), FVector(0.55f, 0.36f, 0.95f), Shadow, S.Body)));
			Keep(Quiet(Box(N(TEXT("Shoulders")), FVector::ZeroVector, FVector(36, 64, 16), Shadow)));
			SeatParts.Last()->AttachToComponent(S.Body, FAttachmentTransformRules::KeepRelativeTransform);
			SeatParts.Last()->SetRelativeLocation(FVector(0, 0, 170));
			Keep(Clickable(Shape(Sph, N(TEXT("Head")), FVector(0, 0, 196), FVector(0.34f), Shadow, S.Body)));
			S.Face = Clickable(Shape(Pln, N(TEXT("Face")), FVector(20, 0, 196), FVector(0.36f), FLinearColor::White, S.Body));
			S.Face->SetVisibility(false);
			Keep(S.Face);
			for (int32 i = 0; i < HaloBeads + AiCubes; ++i)
			{
				const bool bCube = i >= HaloBeads;
				UStaticMeshComponent* B = Quiet(Shape(bCube ? Cub : Sph, FName(*FString::Printf(TEXT("%sAura%d"), *P, i)), FVector(0, 0, 230), FVector(bCube ? 0.07f : 0.05f), bCube ? AiAura : HumanAura, S.Body));
				B->SetCastShadow(false);
				B->SetVisibility(false);
				S.Aura.Add(B);
				Keep(B);
			}
			S.Glow = Light(N(TEXT("Glow")), Dir(A) * (BodyR + 40.f) + FVector(0, 0, 150), 0.f, HumanAura, 230.f);
			Keep(S.Glow);
			S.Label = Text(N(TEXT("Label")), Dir(A) * BodyR + FVector(0, 0, 250), FRotator::ZeroRotator, 9.f, FColor(225, 218, 205), FString());
			Keep(S.Label);
			S.Body->SetVisibility(false, true);
			S.Label->SetVisibility(false);
		}

		S.Fig = Keep(NewObject<USceneComponent>(this, N(TEXT("Fig"))));
		S.Fig->SetupAttachment(RootComponent);
		S.Fig->SetMobility(EComponentMobility::Movable);
		S.Fig->SetRelativeLocation(Dir(FigAngle(k)) * FigR + FVector(0, 0, 80));   // table top is at 80 cm
		S.Fig->RegisterComponent();
		S.FigBase = Keep(Quiet(Shape(Cyl, N(TEXT("FigBase")), FVector(0, 0, 1.25f), FVector(0.13f, 0.13f, 0.025f), SeatBrass, S.Fig)));
		S.FigBody = Keep(Clickable(Shape(Cyl, N(TEXT("FigBody")), FVector(0, 0, 10.5f), FVector(0.07f, 0.07f, 0.16f), FLinearColor::White, S.Fig)));
		S.FigHead = Keep(Clickable(Shape(Sph, N(TEXT("FigHead")), FVector(0, 0, 22.5f), FVector(0.08f), FLinearColor::White, S.Fig)));
		S.Standee = Keep(Clickable(Shape(Pln, N(TEXT("Standee")), FVector(0, 0, 15.5f), FVector(0.2f, 0.26f, 1.f), FLinearColor::White, S.Fig)));
		S.FigLabel = Keep(Text(N(TEXT("FigLabel")), FVector(0, 0, 33.f), FRotator::ZeroRotator, 3.5f, FColor(225, 218, 205), FString(), S.Fig));
		S.Fig->SetVisibility(false, true);
	}
}

void AFNStudio::ShowSeats(const TArray<FFNSeatInfo>& Seats)
{
	if (SeatSlots.Num() == 0) BuildSeats();
	int32 MySeat = -1;
	for (const FFNSeatInfo& I : Seats) if (I.bMine) { MySeat = I.Seat; break; }

	TArray<bool> Taken; Taken.Init(false, NumSeats);
	TArray<int32> SlotOf; SlotOf.Init(-1, Seats.Num());
	for (int32 i = 0; i < Seats.Num(); ++i)   // seat numbers decide the slot when we know them
	{
		const FFNSeatInfo& I = Seats[i];
		int32 Slot = I.bMine ? 0 : (I.Seat >= 0 && MySeat >= 0 ? ((I.Seat - MySeat) % NumSeats + NumSeats) % NumSeats : -1);
		if (Slot >= 0 && !Taken[Slot]) { Taken[Slot] = true; SlotOf[i] = Slot; }
	}
	for (int32 i = 0; i < Seats.Num(); ++i)   // the rest take the next free chair, clockwise
	{
		if (SlotOf[i] >= 0) continue;
		for (int32 k = 1; k < NumSeats; ++k) if (!Taken[k]) { Taken[k] = true; SlotOf[i] = k; break; }
		if (SlotOf[i] < 0) UE_LOG(LogTemp, Warning, TEXT("FNSeats: more than %d seats, %s has no chair"), NumSeats, *Seats[i].Name);
	}

	FString Summary;
	for (FSeatSlot& S : SeatSlots) S.bUsed = false;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		if (SlotOf[i] < 0) continue;
		FSeatSlot& S = SeatSlots[SlotOf[i]];
		const FFNSeatInfo& I = Seats[i];
		S.Info = I;
		S.bUsed = true;
		Summary += FString::Printf(TEXT(" [%d seat%d %s %s%s%s%s]"), SlotOf[i], I.Seat, *I.Name, I.bHuman ? TEXT("human") : TEXT("AI"),
			I.bMine ? TEXT(" mine") : TEXT(""), I.bConnected ? TEXT("") : TEXT(" away"), I.bAlive ? TEXT("") : TEXT(" fallen"));

		const FLinearColor Col = I.bAlive ? I.Color : Fallen;
		const FString Tag = !I.bAlive ? TEXT("fallen") : !I.bConnected ? TEXT("away") : I.bHuman ? TEXT("HUMAN") : TEXT("AI");
		// the minifig, or the diorama image on its standee
		UTexture2D* Img = ImageMat ? I.Diorama : nullptr;
		S.Fig->SetVisibility(true, true);
		S.FigBody->SetVisibility(!Img);
		S.FigHead->SetVisibility(!Img);
		S.Standee->SetVisibility(Img != nullptr);
		if (Img)
		{
			UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(S.Standee->GetMaterial(0));
			if (!M || M->Parent != ImageMat) { M = UMaterialInstanceDynamic::Create(ImageMat, S.Standee); S.Standee->SetMaterial(0, M); }
			M->SetTextureParameterValue(TEXT("SlateUI"), Img);
		}
		SetCol(S.FigBody, Col);
		SetCol(S.FigHead, Col * 1.15f);
		SetCol(S.FigBase, I.bMine ? FLinearColor(0.9f, 0.9f, 0.85f) : SeatBrass);   // mine stands on a white base
		S.Fig->SetRelativeRotation(FRotator(0.f, 0.f, I.bAlive ? 0.f : 80.f));    // the fallen lie on the table
		S.FigLabel->SetText(FText::FromString(I.bMine ? I.Name + TEXT(" (you)") : I.Name));
		S.FigLabel->SetTextRenderColor(Col.ToFColor(true));

		if (!S.Body) continue;
		S.Body->SetVisibility(true, true);
		UTexture2D* Face = ImageMat ? I.Portrait : nullptr;
		S.Face->SetVisibility(Face != nullptr);
		if (Face)
		{
			UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(S.Face->GetMaterial(0));
			if (!M || M->Parent != ImageMat) { M = UMaterialInstanceDynamic::Create(ImageMat, S.Face); S.Face->SetMaterial(0, M); }
			M->SetTextureParameterValue(TEXT("SlateUI"), Face);
		}
		S.Label->SetVisibility(true);
		S.Label->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *I.Name, *Tag)));
		S.Label->SetTextRenderColor((I.bAlive ? (I.bHuman ? HumanAura : AiAura) : Fallen * 4.f).ToFColor(true));
		S.Glow->SetLightColor(I.bHuman ? HumanAura : AiAura);
	}
	for (FSeatSlot& S : SeatSlots)   // empty chairs: nothing there
	{
		if (S.bUsed) continue;
		S.Fig->SetVisibility(false, true);
		if (S.Body) { S.Body->SetVisibility(false, true); S.Label->SetVisibility(false); S.Glow->SetIntensity(0.f); }
	}
	static FString LastSummary;
	if (Summary != LastSummary) { LastSummary = Summary; UE_LOG(LogTemp, Display, TEXT("FNSeats:%s"), *Summary); }
}

void AFNStudio::UpdateSeats(float Dt)
{
	if (SeatSlots.Num() == 0) return;
	FVector Cam = GetActorLocation() + FVector(0.f, 340.f, 190.f);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
	auto FaceCam = [&Cam](USceneComponent* C, bool bPlane)
	{
		if (!C || !C->IsVisible()) return;
		FVector To = Cam - C->GetComponentLocation(); To.Z = 0.f;
		if (!To.Normalize()) return;
		// text faces its +X; a plane faces its +Z. The image's up runs along the plane's Y (with X up, every picture lay on its side)
		C->SetWorldRotation(bPlane ? FRotationMatrix::MakeFromZY(To, ImageUp).Rotator() : To.Rotation());
	};

	for (int32 k = 0; k < SeatSlots.Num(); ++k)
	{
		FSeatSlot& S = SeatSlots[k];
		if (!S.bUsed) continue;
		FaceCam(S.FigLabel, false);
		FaceCam(S.Standee, true);
		if (!S.Body) continue;
		FaceCam(S.Label, false);
		FaceCam(S.Face, true);
		const FFNSeatInfo& I = S.Info;
		const bool bShow = I.bAlive;
		const float Away = I.bConnected ? 1.f : 0.15f;
		const float T = Time + k * 1.3f;
		// human: a slow breathing glow; AI: a fast, clipped flicker
		const float Pulse = I.bHuman ? 0.75f + 0.25f * FMath::Sin(T * 1.6f) : (FMath::Sin(T * 9.f) > -0.2f ? 1.f : 0.55f);
		S.Glow->SetIntensity(bShow ? 30.f * Pulse * Away : 0.f);
		for (int32 i = 0; i < S.Aura.Num(); ++i)
		{
			UStaticMeshComponent* B = S.Aura[i];
			const bool bCube = i >= HaloBeads;
			const bool bOn = bShow && bCube != I.bHuman;
			B->SetVisibility(bOn);
			if (!bOn) continue;
			if (!bCube)   // a level ring above the head
			{
				const float A = 2.f * PI * i / HaloBeads + T * 0.4f;
				B->SetRelativeLocation(FVector(FMath::Cos(A) * 22.f, FMath::Sin(A) * 22.f, 226.f + 1.5f * FMath::Sin(T * 1.6f)));
			}
			else          // cubes orbiting the head on a tilted path
			{
				const float A = 2.f * PI * (i - HaloBeads) / AiCubes + T * 2.2f;
				B->SetRelativeLocation(FVector(FMath::Cos(A) * 30.f, FMath::Sin(A) * 30.f, 196.f + FMath::Sin(A * 2.f) * 12.f));
				B->SetRelativeRotation(FRotator(T * 90.f, T * 120.f, 0.f));
			}
		}
	}
}
