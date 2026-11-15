#include "Features/FNInventoryShelf.h"
#include "Features/FNItemIcon.h"
#include "FNNetClient.h"
#include "../../UI/FNTableGods.h"
#include "../../UI/FNMedia.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

namespace FNShelfImpl
{
	static const FLinearColor Wood(0.16f, 0.10f, 0.06f), Grey(0.35f, 0.35f, 0.35f), Gold(1.f, 0.78f, 0.25f);
	static constexpr int32 MaxOnShelf = 8;

	static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { FString S; if (O.IsValid()) O->TryGetStringField(K, S); return S; }

	static void Tint(UStaticMeshComponent* C, const FLinearColor& Col)
	{
		if (!C) return;
		UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
		if (!M)
		{
			static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			M = UMaterialInstanceDynamic::Create(Base, C);
			C->SetMaterial(0, M);
		}
		M->SetVectorParameterValue(TEXT("Color"), Col);
	}

	/** TextRender has no wrapping: break at spaces near Width characters. */
	static FString Wrap(const FString& S, int32 Width, int32 MaxLines)
	{
		FString Out, Line;
		int32 Lines = 1;
		TArray<FString> Words;
		S.ParseIntoArray(Words, TEXT(" "));
		for (const FString& Wd : Words)
		{
			if (Line.Len() + Wd.Len() + 1 > Width && !Line.IsEmpty())
			{
				if (++Lines > MaxLines) { Out += Line + TEXT("..."); return Out; }
				Out += Line + TEXT("\n");
				Line.Reset();
			}
			Line += (Line.IsEmpty() ? TEXT("") : TEXT(" ")) + Wd;
		}
		return Out + Line;
	}

	/** The god a pedestal shows: its name plate's first line (AFNGodPedestal::SetGod writes "Player\nHUMAN"). */
	static FString PedestalPlayer(const AActor* Ped)
	{
		TArray<UTextRenderComponent*> Texts;
		Ped->GetComponents<UTextRenderComponent>(Texts);
		for (const UTextRenderComponent* T : Texts)
		{
			FString S = T->Text.ToString(), First;
			if (!S.Split(TEXT("\n"), &First, nullptr)) First = S;
			if (!First.IsEmpty()) return First;
		}
		return FString();
	}
}

AFNInventoryShelf::AFNInventoryShelf()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AFNInventoryShelf::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

void AFNInventoryShelf::Walk(const TSharedPtr<FJsonObject>& O, int32 Depth, const FString& InDg)
{
	using namespace FNShelfImpl;
	if (!O.IsValid() || Depth > 8) return;
	const FString Ref = Str(O, TEXT("ref"));
	const FString Dg = Ref.StartsWith(TEXT("dg:")) ? Ref : InDg;   // an item listed inside a demigod (look.loadout) is that demigod's
	if (Ref.StartsWith(TEXT("dg:")))
	{
		const FString P = Str(O, TEXT("player"));
		if (!P.IsEmpty()) DgPlayer.Add(Ref, P);
	}
	else if (Ref.StartsWith(TEXT("it:")))
	{
		FItem& It = Items.FindOrAdd(Ref);
		It.Ref = Ref;
		// only what this copy says: a loadout entry may be shorter than the full item
		auto Take = [&](const TCHAR* K, FString& Into) { FString S; if (O->TryGetStringField(K, S) && !S.IsEmpty()) Into = S; };
		Take(TEXT("name"), It.Name); Take(TEXT("description"), It.Desc); Take(TEXT("icon"), It.Icon);
		Take(TEXT("state"), It.State); Take(TEXT("holder"), It.Holder); Take(TEXT("holder_name"), It.HolderName);
		if (It.Holder.IsEmpty() && !Dg.IsEmpty()) It.Holder = Dg;
		const TSharedPtr<FJsonObject>* Img = nullptr;
		if (O->TryGetObjectField(TEXT("image"), Img) && Img && Str(*Img, TEXT("status")) == TEXT("ready"))
		{
			It.Hash = Str(*Img, TEXT("hash"));
			It.Url = Str(*Img, TEXT("url"));
			Fetch(It);
		}
	}
	// look.changed.gained: the pop-in glint
	const TSharedPtr<FJsonObject>* Changed = nullptr;
	if (O->TryGetObjectField(TEXT("changed"), Changed) && Changed)
	{
		const TSharedPtr<FJsonObject>* G = nullptr;
		if ((*Changed)->TryGetObjectField(TEXT("gained"), G) && G)
		{
			const FString R = Str(*G, TEXT("ref"));
			if (R.StartsWith(TEXT("it:"))) { Walk(*G, Depth + 1, Dg); Items.FindOrAdd(R).Glint = 2.5f; }
		}
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : O->Values)
	{
		if (!KV.Value.IsValid()) continue;
		if (KV.Value->Type == EJson::Object) Walk(KV.Value->AsObject(), Depth + 1, Dg);
		else if (KV.Value->Type == EJson::Array)
			for (const TSharedPtr<FJsonValue>& V : KV.Value->AsArray())
				if (V.IsValid() && V->Type == EJson::Object) Walk(V->AsObject(), Depth + 1, Dg);
	}
}

void AFNInventoryShelf::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	Walk(D, 0);
}

void AFNInventoryShelf::Fetch(const FItem& It)
{
	if (It.Hash.IsEmpty() || Tex.Contains(It.Hash) || Downloading.Contains(It.Hash)) return;
	TArray<uint8> Png;
	if (FN7Media::LoadCached(It.Hash, Png))
	{
		if (UTexture2D* T = FN7Media::TextureFromPng(Png)) { Tex.Add(It.Hash, T); TexGrey.Add(It.Hash, FN7Media::TextureFromPng(Png, true)); }
		return;
	}
	UFNNetClient* N = Net.Get();
	if (!N || It.Url.IsEmpty()) return;
	Downloading.Add(It.Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(It.Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakObjectPtr<AFNInventoryShelf> W(this);
	const FString Hash = It.Hash, Ref = It.Ref;
	Req->OnProcessRequestComplete().BindLambda([W, Hash, Ref](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (!W.IsValid()) return;
		W->Downloading.Remove(Hash);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { UE_LOG(LogTemp, Display, TEXT("FNShelf: picture for %s failed (HTTP %d)"), *Ref, Resp.IsValid() ? Resp->GetResponseCode() : 0); return; }
		const TArray<uint8> Data = Resp->GetContent();
		if (UTexture2D* T = FN7Media::TextureFromPng(Data))
		{
			FN7Media::SaveCached(Hash, Data);
			W->Tex.Add(Hash, T);
			W->TexGrey.Add(Hash, FN7Media::TextureFromPng(Data, true));
			UE_LOG(LogTemp, Display, TEXT("FNShelf: picture for %s ready"), *Ref);
		}
	});
	Req->ProcessRequest();
}

UStaticMeshComponent* AFNInventoryShelf::Piece(USceneComponent* Parent, const TCHAR* Mesh)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->RegisterComponent();
	Keep.Add(C);
	return C;
}

AFNInventoryShelf::FSlot& AFNInventoryShelf::SlotFor(const FString& Ref)
{
	using namespace FNShelfImpl;
	if (FSlot* S = Slots.Find(Ref)) return *S;
	FSlot S;
	S.Root = NewObject<USceneComponent>(this);
	S.Root->SetupAttachment(RootComponent);
	S.Root->SetMobility(EComponentMobility::Movable);
	S.Root->RegisterComponent();
	Keep.Add(S.Root);
	S.Plinth = Piece(S.Root, TEXT("/Engine/BasicShapes/Cube.Cube"));
	S.Plinth->SetRelativeLocation(FVector(0, 0, -4.f));
	S.Plinth->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.08f));
	Tint(S.Plinth, Wood);
	S.Card = Piece(S.Root, TEXT("/Engine/BasicShapes/Cube.Cube"));   // a thin card: the picture on both faces
	S.Card->SetVisibility(false);
	S.Back = Piece(S.Root, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Tint(S.Back, FLinearColor(0.012f, 0.01f, 0.014f));
	S.Back->SetVisibility(false);
	S.Spark = Piece(S.Root, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Tint(S.Spark, Gold * 3.f);
	S.Spark->SetVisibility(false);
	S.Label = NewObject<UTextRenderComponent>(this);
	S.Label->SetupAttachment(S.Root);
	S.Label->SetHorizontalAlignment(EHTA_Center);
	S.Label->SetVerticalAlignment(EVRTA_TextTop);
	S.Label->SetCastShadow(false);
	S.Label->RegisterComponent();
	Keep.Add(S.Label);
	return Slots.Add(Ref, S);
}

void AFNInventoryShelf::Tick(float DeltaSeconds)
{
	using namespace FNShelfImpl;
	Super::Tick(DeltaSeconds);
	UWorld* W = GetWorld();
	if (!W) return;
	if (!Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNInventoryShelf::OnMessage); break; }
	APlayerController* PC = W->GetFirstPlayerController();
	const FVector Cam = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FRotator CamRot = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;

	// the gods at the table, by name
	TMap<FString, AActor*> Peds;
	for (TActorIterator<AFNGodPedestal> It(W); It; ++It)
		if (!It->IsHidden())
		{
			const FString P = PedestalPlayer(*It);
			if (!P.IsEmpty()) Peds.Add(P, *It);
		}

	// click a statue: its items come up; click again: they go back
	// the game's Slate layer takes the mouse first, so read the button from Slate itself (a press = down now, up last frame)
	const bool bDown = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	const bool bClick = bDown && !bMouseWasDown;
	bMouseWasDown = bDown;
	if (PC && bClick)
	{
		float Mx = 0, My = 0;
		FString Hit;
		if (PC->GetMousePosition(Mx, My))
			for (const TPair<FString, FVector>& KV : Anchors)
			{
				FVector2D Px;
				if (PC->ProjectWorldLocationToScreen(KV.Value, Px) && FVector2D::Distance(Px, FVector2D(Mx, My)) < 80.f) Hit = KV.Key;
			}
		Open = !Open.IsEmpty() ? FString() : Hit;
		if (!Open.IsEmpty()) UE_LOG(LogTemp, Display, TEXT("FNShelf: %s's items open"), *Open);
	}

	// each player's items, in the order first seen
	TMap<FString, TArray<FItem*>> ByPlayer;
	for (TPair<FString, FItem>& KV : Items)
	{
		FItem& It = KV.Value;
		It.Glint = FMath::Max(0.f, It.Glint - DeltaSeconds);
		const FString* P = DgPlayer.Find(It.Holder);
		if (P && It.State != TEXT("transferred")) ByPlayer.FindOrAdd(*P).Add(&It);
	}
	TSet<FString> Shown;
	Anchors.Reset();
	for (const TPair<FString, AActor*>& KV : Peds) Anchors.Emplace(KV.Key, KV.Value->GetActorLocation() + FVector(0, 0, 150.f));
	int32 OnShelves = 0;
	for (TPair<FString, TArray<FItem*>>& KV : ByPlayer)
	{
		AActor** Ped = Peds.Find(KV.Key);
		// you have no statue (it's your seat): your shelf floats low at the left of your view, over the table's near rim
		const FVector Fw = CamRot.Vector().GetSafeNormal2D(), Rt = FVector::CrossProduct(FVector::UpVector, Fw).GetSafeNormal();
		const FVector Mine = Cam + Fw * 150.f - Rt * 42.f - FVector(0, 0, 36.f);
		if (!Ped && !PC) continue;
		const bool bOpen = KV.Key == Open;
		const FTransform PT = Ped ? (*Ped)->GetActorTransform() : FTransform::Identity;
		const int32 N = bOpen ? KV.Value.Num() : FMath::Min(KV.Value.Num(), MaxOnShelf);
		for (int32 i = 0; i < N; ++i)
		{
			FItem& It = *KV.Value[i];
			FSlot& S = SlotFor(It.Ref);
			Shown.Add(It.Ref);
			++OnShelves;
			const bool bGrey = It.State == TEXT("destroyed");
			const bool bStolen = It.State == TEXT("stolen");
			float Size = 22.f;
			FVector Pos;
			if (bOpen)
			{
				// cards in a grid floating before the camera, 4 to a row
				const int32 Cols = FMath::Min(4, N), Row = i / 4, Col = i % 4;
				const FRotationMatrix M(CamRot);
				Size = 14.f;
				Pos = Cam + M.GetUnitAxis(EAxis::X) * 170.f + M.GetUnitAxis(EAxis::Y) * ((Col - (Cols - 1) * 0.5f) * 30.f) + M.GetUnitAxis(EAxis::Z) * (4.f - Row * 36.f);
			}
			else
			{
				// around the statue's flanks (pedestal +X faces the table): left, right, then further back, so the table sees them
				const int32 Row = i / 4, Col = i % 4;
				const float Side = (i % 2) ? -1.f : 1.f;
				const float A = FMath::DegreesToRadians(Side * (80.f + 28.f * (i / 2)));
				if (Ped) Size = 32.f;
				Pos = Ped ? PT.TransformPosition(FVector(FMath::Cos(A) * 85.f, FMath::Sin(A) * 85.f, 80.f))
					: Mine + Rt * ((Col - 1.5f) * 22.f) - FVector(0, 0, Row * 30.f);
			}
			const float Pop = It.Glint > 0.f ? 1.f + 0.6f * FMath::Clamp(It.Glint - 1.5f, 0.f, 1.f) : 1.f;
			S.Root->SetWorldLocation(Pos);
			if (!bOpen) Anchors.Emplace(KV.Key, Pos + FVector(0, 0, 10.f));
			S.Root->SetWorldRotation(FRotator(0.f, (Cam - Pos).GetSafeNormal2D().Rotation().Yaw, 0.f));
			S.Root->SetWorldScale3D(FVector(Pop * (Ped || bOpen ? 1.f : 0.5f)));
			S.Plinth->SetVisibility(!bOpen);
			S.Back->SetVisibility(bOpen);
			if (bOpen)
			{
				S.Back->SetRelativeLocation(FVector(-3.f, 0, Size * 0.3f));
				S.Back->SetRelativeScale3D(FVector(0.01f, Size * 1.9f / 100.f, Size * 2.5f / 100.f));
			}
			// the picture, else the little model
			const TObjectPtr<UTexture2D>* TP = (bGrey ? TexGrey : Tex).Find(It.Hash);
			UTexture2D* T = TP ? TP->Get() : nullptr;
			const bool bCard = T != nullptr;
			if (bCard && (S.CardHash != It.Hash || S.bGrey != bGrey))
			{
				static UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Unlit, S.Card);
				M->SetTextureParameterValue(TEXT("SlateUI"), T);
				S.Card->SetMaterial(0, M);
				S.CardHash = It.Hash;
			}
			S.Card->SetVisibility(bCard);
			if (bCard)
			{
				S.Card->SetRelativeLocation(FVector(0, 0, Size * 0.5f + 1.f));
				S.Card->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
				S.Card->SetRelativeScale3D(FVector(Size, Size * 0.03f, Size) / 100.f);
			}
			const FString Icon = It.Icon.IsEmpty() ? FString(TEXT("thing")) : It.Icon;
			if (!bCard && S.ModelIcon != Icon)
			{
				for (UStaticMeshComponent* C : S.Model) if (C) C->DestroyComponent();
				S.Model = FNItemIcon::Build(S.Root, Icon, Gold, Size * 0.9f);
				S.ModelIcon = Icon;
				S.bGrey = !bGrey;   // force the tint below
			}
			for (UStaticMeshComponent* C : S.Model) if (C) C->SetVisibility(!bCard);
			if (S.bGrey != bGrey) { FNItemIcon::SetTint(S.Model, bGrey ? Grey : Gold); S.bGrey = bGrey; }
			// glint: a gold flash that swells and fades
			S.Spark->SetVisibility(It.Glint > 0.f);
			if (It.Glint > 0.f)
			{
				const float G = It.Glint / 2.5f;
				S.Spark->SetRelativeLocation(FVector(0, 0, Size * 0.6f));
				S.Spark->SetRelativeScale3D(FVector(Size * (1.6f - G) / 100.f));
				Tint(S.Spark, Gold * (3.f * G));
			}
			// the name (and in the open view, what it is)
			FString Text = Wrap(It.Name.IsEmpty() ? It.Ref : It.Name, bOpen ? 22 : 16, 2);
			if (bStolen) Text += TEXT("\nSTOLEN");
			else if (bGrey) Text += TEXT("\ndestroyed");
			if (bOpen && !It.Desc.IsEmpty()) Text += TEXT("\n") + Wrap(It.Desc, 30, 4);
			S.Label->SetText(FText::FromString(Text));
			S.Label->SetWorldSize(bOpen ? 1.7f : 3.6f);
			S.Label->SetTextRenderColor(bStolen ? FColor(235, 60, 50) : bGrey ? FColor(150, 150, 150) : FColor(245, 225, 170));
			S.Label->SetRelativeLocation(FVector(bOpen ? 1.f : 0.f, 0, bOpen ? -1.f : -10.f));
			S.Label->SetVisibility(true);
		}
	}
	for (TPair<FString, FSlot>& KV : Slots)
		if (!Shown.Contains(KV.Key) && KV.Value.Root->IsVisible()) KV.Value.Root->SetVisibility(false, true);
		else if (Shown.Contains(KV.Key) && !KV.Value.Root->IsVisible()) KV.Value.Root->SetVisibility(true, false);

	const FString Log = FString::Printf(TEXT("%d items known, %d on shelves, %d gods at the table, %d holders known%s"), Items.Num(), OnShelves, Peds.Num(), DgPlayer.Num(), Open.IsEmpty() ? TEXT("") : *(TEXT(", open: ") + Open));
	if (Log != LastLog)
	{
		LastLog = Log;
		TArray<FString> PedNames, Who;
		Peds.GetKeys(PedNames);
		for (const TPair<FString, FItem>& KV : Items) { const FString* P = DgPlayer.Find(KV.Value.Holder); Who.Add(FString::Printf(TEXT("%s \"%s\" %s held by %s (%s)"), *KV.Key, *KV.Value.Name, *KV.Value.State, *KV.Value.Holder, P ? **P : TEXT("player unknown"))); }
		UE_LOG(LogTemp, Display, TEXT("FNShelf: %s; gods: %s; %s"), *Log, *FString::Join(PedNames, TEXT(", ")), *FString::Join(Who, TEXT("; ")));
	}
}

bool UFNInventoryShelfSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNInventoryShelfSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNInventoryShelf>(AFNInventoryShelf::StaticClass(), FTransform::Identity, P);
}
