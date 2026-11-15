#include "Features/FNPeopleLife.h"
#include "FNStudio.h"
#include "FNNetClient.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "../../UI/FNMedia.h"

namespace FNPeopleLifeImpl
{
	enum EMesh { MCyl, MCone, MBall, MCube };
	static const TCHAR* MeshPath[4] = { TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("/Engine/BasicShapes/Cone.Cone"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Engine/BasicShapes/Cube.Cube") };
	static const TCHAR* RoleName[] = { TEXT("fisherman"), TEXT("farmer"), TEXT("smith"), TEXT("merchant"), TEXT("guard"), TEXT("warrior"), TEXT("priest"), TEXT("witch"), TEXT("noble"), TEXT("townswoman"), TEXT("child"), TEXT("elder") };
	static const TCHAR* WorkName[] = { TEXT("fishing at the shore"), TEXT("hoeing the field"), TEXT("hammering at the forge"), TEXT("calling out wares"), TEXT("keeping watch"), TEXT("drilling with a spear"), TEXT("praying"), TEXT("stirring a cauldron"), TEXT("surveying the town"), TEXT("going about her day"), TEXT("playing"), TEXT("resting on a cane") };
	static const TCHAR* Her[] = { TEXT("Ana"), TEXT("Mira"), TEXT("Leila"), TEXT("Sofia"), TEXT("Yuki"), TEXT("Amara"), TEXT("Elena"), TEXT("Noor"), TEXT("Ines"), TEXT("Kaia"), TEXT("Rosa"), TEXT("Lin"), TEXT("Zara"), TEXT("Freya"), TEXT("Adaeze"), TEXT("Marta") };
	static const TCHAR* His[] = { TEXT("Tomas"), TEXT("Ravi"), TEXT("Kofi"), TEXT("Mateo"), TEXT("Ivan"), TEXT("Omar"), TEXT("Bao"), TEXT("Diego"), TEXT("Finn"), TEXT("Arjun"), TEXT("Yusuf"), TEXT("Ezra"), TEXT("Kenji"), TEXT("Lars"), TEXT("Emeka"), TEXT("Paulo") };
	static const FColor Skin[4] = { FColor(241, 200, 160), FColor(205, 150, 105), FColor(150, 100, 65), FColor(95, 62, 40) };
	static const FColor Hair[4] = { FColor(30, 22, 18), FColor(95, 60, 30), FColor(230, 200, 110), FColor(170, 60, 25) };
	static const FColor Dress[4] = { FColor(220, 40, 60), FColor(240, 110, 170), FColor(40, 170, 170), FColor(250, 200, 60) };
	static const FColor Wood(110, 72, 40), Steel(175, 180, 190), Gold(240, 195, 50), Black(25, 22, 28);

	double Meters(double La1, double Lo1, double La2, double Lo2)
	{
		const double A = FMath::DegreesToRadians(La1), B = FMath::DegreesToRadians(La2);
		return 6371000.0 * FMath::Acos(FMath::Clamp(FMath::Sin(A) * FMath::Sin(B) + FMath::Cos(A) * FMath::Cos(B) * FMath::Cos(FMath::DegreesToRadians(Lo2 - Lo1)), -1.0, 1.0));
	}
	float Dice(int32 Q, int32 R, int32 Salt) { return FMath::Frac(FMath::Sin((float)(Q * 127.1f + R * 311.7f + Salt * 74.7f)) * 43758.5453f); }
	FColor RoleCol(uint8 R, int32 Var)
	{
		switch (R)
		{
		case 0: return FColor(70, 110, 150);    // fisherman: sea blue
		case 1: return FColor(110, 140, 60);    // farmer: field green
		case 2: return FColor(90, 60, 40);      // smith: leather
		case 3: return FColor(230, 130, 40);    // merchant: orange
		case 4: return FColor(40, 70, 170);     // guard: blue
		case 5: return FColor(170, 30, 30);     // warrior: red
		case 6: return FColor(245, 245, 235);   // priest: white robe
		case 7: return Black;                   // witch
		case 8: return FColor(110, 40, 150);    // noble: purple
		case 9: return Dress[Var % 4];          // townswoman: bright dresses
		case 10: return FColor(80, 200, 120);   // child
		default: return FColor(130, 130, 130);  // elder: grey
		}
	}
}

AFNPeopleLife::AFNPeopleLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AFNPeopleLife::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UFNNetClient* N = Net.Get()) N->OnServerMessage.Remove(MsgHandle);
	Super::EndPlay(Reason);
}

void AFNPeopleLife::OnMessage(const FString& Type, TSharedPtr<FJsonObject> D)
{
	using namespace FNPeopleLifeImpl;
	if (Type == TEXT("game.people") && D.IsValid()) { ReadPeople(D); return; }
	if (Type != TEXT("round.outcome") || !D.IsValid()) return;
	const TSharedPtr<FJsonObject>* Pub = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
	if (!D->TryGetObjectField(TEXT("public"), Pub) || !Pub || !(*Pub)->TryGetArrayField(TEXT("events"), Ev) || !Ev) return;
	for (const TSharedPtr<FJsonValue>& V : *Ev)
	{
		const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
		if (!E.IsValid() || !E->TryGetArrayField(TEXT("at"), At) || !At || At->Num() < 2) continue;
		const FString K = E->GetStringField(TEXT("kind")).ToLower(), S = (K + TEXT(" ") + E->GetStringField(TEXT("text"))).ToLower();
		FAlarm A;
		A.Lon = (*At)[0]->AsNumber(); A.Lat = (*At)[1]->AsNumber();
		double Mag = 5.0; E->TryGetNumberField(TEXT("magnitude"), Mag);
		A.Kind = 0;
		if (K.Contains(TEXT("riot")) || K.Contains(TEXT("loot"))) A.Kind = 2;
		else if (K == TEXT("host_raised") || K == TEXT("general_appointed") || K.Contains(TEXT("celebrat")) || K.Contains(TEXT("festival"))) A.Kind = 3;
		else for (const TCHAR* W : { TEXT("fire"), TEXT("burn"), TEXT("flood"), TEXT("storm"), TEXT("quake"), TEXT("meteor"), TEXT("lava"), TEXT("battle"), TEXT("plague"), TEXT("tsunami"), TEXT("hurricane"), TEXT("raid") }) if (S.Contains(W)) A.Kind = 1;
		if (A.Kind == 0) continue;
		A.RadiusM = FMath::Clamp(Mag, 1.0, 10.0) * (A.Kind == 1 ? 8000.0 : 4000.0);
		A.Until = Time + (A.Kind == 1 ? 25.f : 18.f);
		if (A.Kind == 1) A.What = S.Contains(TEXT("flood")) || S.Contains(TEXT("tsunami")) ? TEXT("the flood") : S.Contains(TEXT("fire")) || S.Contains(TEXT("burn")) || S.Contains(TEXT("lava")) ? TEXT("the fire") : S.Contains(TEXT("battle")) || S.Contains(TEXT("raid")) ? TEXT("the fighting") : TEXT("the storm");
		else A.What = A.Kind == 2 ? TEXT("the riot") : TEXT("the army");
		Alarms.Add(A);
	}
	if (Alarms.Num() > 32) Alarms.RemoveAt(0, Alarms.Num() - 32);
}

void AFNPeopleLife::ReadPeople(const TSharedPtr<FJsonObject>& D)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!D->TryGetArrayField(TEXT("people"), Arr))
	{
		const TSharedPtr<FJsonObject>* R = nullptr;
		if (!D->TryGetObjectField(TEXT("result"), R) || !R || !(*R)->TryGetArrayField(TEXT("people"), Arr)) return;
	}
	if (!Arr) return;
	// the server's roles onto ours (the library has 16; the board draws 12)
	static const TPair<const TCHAR*, uint8> Map[] = {
		{ TEXT("fisher"), Fisherman }, { TEXT("sailor"), Fisherman }, { TEXT("farmer"), Farmer }, { TEXT("smith"), Smith },
		{ TEXT("merchant"), Merchant }, { TEXT("trader"), Merchant }, { TEXT("guard"), Guard }, { TEXT("soldier"), Guard },
		{ TEXT("warrior"), Warrior }, { TEXT("hunter"), Warrior }, { TEXT("priest"), Priest }, { TEXT("healer"), Priest },
		{ TEXT("witch"), Witch }, { TEXT("noble"), Noble }, { TEXT("beauty"), Woman }, { TEXT("woman"), Woman },
		{ TEXT("child"), Child }, { TEXT("kid"), Child }, { TEXT("elder"), Elder }, { TEXT("beggar"), Elder } };
	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> P = V.IsValid() ? V->AsObject() : nullptr;
		if (!P.IsValid()) continue;
		FString RoleS;
		P->TryGetStringField(TEXT("role"), RoleS);
		RoleS = RoleS.ToLower();
		const TSharedPtr<FJsonObject>* Sp = nullptr;
		if (RoleS.IsEmpty() || !P->TryGetObjectField(TEXT("sprite"), Sp) || !Sp) continue;
		FString Hash, Url, Status;
		(*Sp)->TryGetStringField(TEXT("hash"), Hash);
		(*Sp)->TryGetStringField(TEXT("url"), Url);
		(*Sp)->TryGetStringField(TEXT("status"), Status);
		if (Hash.IsEmpty() || Url.IsEmpty() || (!Status.IsEmpty() && Status != TEXT("ready") && Status != TEXT("done"))) continue;
		int32 RoleIdx = -1;
		for (const TPair<const TCHAR*, uint8>& M : Map) if (RoleIdx < 0 && RoleS.Contains(M.Key)) RoleIdx = M.Value;
		if (RoleIdx < 0) continue;
		TArray<FString>& L = RoleSprites.FindOrAdd((uint8)RoleIdx);
		if (!L.Contains(Hash)) { L.Add(Hash); SpriteUrl.Add(Hash, Url); ++Added; FetchSprite(Hash); }
	}
	if (Added > 0) UE_LOG(LogTemp, Display, TEXT("FNPeople: %d new people pictures from game.people (%d roles have pictures)"), Added, RoleSprites.Num());
}

void AFNPeopleLife::FetchSprite(const FString& Hash)
{
	if (SpriteTex.Contains(Hash) || SpriteLoading.Contains(Hash)) return;
	TArray<uint8> Png;
	if (FN7Media::LoadCached(Hash, Png))
	{
		if (UTexture2D* T = FN7Media::TextureFromPng(Png)) SpriteTex.Add(Hash, T);
		return;
	}
	UFNNetClient* N = Net.Get();
	const FString* Url = SpriteUrl.Find(Hash);
	if (!N || !Url) return;
	SpriteLoading.Add(Hash);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(N->MediaUrl(*Url));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.f);
	TWeakObjectPtr<AFNPeopleLife> Wk(this);
	const FString H = Hash;
	Req->OnProcessRequestComplete().BindLambda([Wk, H](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		if (!Wk.IsValid()) return;
		Wk->SpriteLoading.Remove(H);
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) return;
		const TArray<uint8> Data = Resp->GetContent();
		if (UTexture2D* T = FN7Media::TextureFromPng(Data)) { FN7Media::SaveCached(H, Data); Wk->SpriteTex.Add(H, T); }
	});
	Req->ProcessRequest();
}

UStaticMeshComponent* AFNPeopleLife::CardAt(int32 Index)
{
	while (CardPool.Num() <= Index)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->RegisterComponent();
		CardPool.Add(C);
	}
	return CardPool[Index];
}

UInstancedStaticMeshComponent* AFNPeopleLife::Set(int32 Mesh, const FColor& C)
{
	using namespace FNPeopleLifeImpl;
	const uint32 Key = (uint32)Mesh << 24 | (uint32)C.R << 16 | (uint32)C.G << 8 | C.B;
	if (UInstancedStaticMeshComponent** Found = Sets.Find(Key)) return *Found;
	// the flat UI material does not render on instanced meshes (grey); the basic shape material does
	static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
	I->SetupAttachment(RootComponent);
	I->SetMobility(EComponentMobility::Movable);
	I->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath[Mesh]));
	I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	I->SetCastShadow(false);
	if (Base)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, I);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(C));
		I->SetMaterial(0, M);
	}
	I->RegisterComponent();
	Keep.Add(I);
	Sets.Add(Key, I);
	return I;
}

void AFNPeopleLife::Add(int32 Mesh, const FColor& C, const FTransform& T) { Frame.FindOrAdd(Set(Mesh, C)).Add(T); }

void AFNPeopleLife::Stick(const FVector& A, const FVector& B, float Thick, const FColor& C)
{
	const FVector D = B - A;
	const float L = D.Size();
	if (L < 1e-3f) return;
	Add(FNPeopleLifeImpl::MCyl, C, FTransform(FQuat::FindBetweenNormals(FVector::UpVector, D / L), (A + B) * 0.5f, FVector(Thick, Thick, L) / 100.f));
}

void AFNPeopleLife::Decide(FPerson& P, const TArray<FVector2D>& Spots, const TArray<FVector2D>& Houses)
{
	// a person's day: work a while, then walk somewhere (a house, a neighbour to chat with, the shore, home), repeat
	const int32 Q = P.Home.X, R = P.Home.Y;
	const float U = FMath::FRand();
	if (P.State == Walk) { P.State = P.ChatWith.X != MAX_int32 ? Chat : Work; P.Left = P.State == Chat ? FMath::FRandRange(3.f, 6.f) : FMath::FRandRange(4.f, 9.f); return; }
	if (P.State == Chat) P.ChatWith = FIntPoint(MAX_int32, 0);
	P.State = Walk;
	auto Near = [&](const TArray<FVector2D>& L, float Max, FVector2D& Out)
	{
		TArray<FVector2D> C;
		for (const FVector2D& V : L) if (V.Size() <= Max && V.Size() > 0.6f) C.Add(V);
		if (C.Num() == 0) return false;
		Out = C[FMath::RandRange(0, C.Num() - 1)];
		return true;
	};
	FVector2D T;
	if (P.Role == Fisherman && P.bShore) { P.To = P.Off.Equals(P.Shore, 0.2f) ? FVector2D::ZeroVector : P.Shore; return; }
	if (P.Role == Farmer && U < 0.6f) { P.To = FVector2D(FMath::FRandRange(-1.2f, 1.2f), FMath::FRandRange(-1.2f, 1.2f)); return; }   // along its own field
	if (P.Role == Guard) { P.To = P.Off.Size() > 1.f ? FVector2D::ZeroVector : FVector2D(FMath::Cos(Q * 1.7f + R), FMath::Sin(Q * 1.7f + R)) * 4.f; return; }   // a beat back and forth
	if (P.Role == Child) { P.To = FVector2D(FMath::FRandRange(-2.5f, 2.5f), FMath::FRandRange(-2.5f, 2.5f)); return; }
	if (U < 0.45f && Near(Houses, 6.f, T)) { P.To = T + FVector2D(FMath::FRandRange(-0.6f, 0.6f), 1.4f); return; }   // to a house's door
	if (U < 0.8f && Near(Spots, 5.f, T))
	{
		P.To = T - T.GetSafeNormal() * 0.45f;   // stops in front of a neighbour
		P.ChatWith = FIntPoint(MAX_int32 - 1, 0);   // who: resolved by the caller (the one standing nearest there)
		return;
	}
	P.To = FVector2D(FMath::FRandRange(-0.5f, 0.5f), FMath::FRandRange(-0.5f, 0.5f));   // home
}

void AFNPeopleLife::Tick(float DeltaSeconds)
{
	using namespace FNPeopleLifeImpl;
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	SinceDraw += DeltaSeconds;
	UWorld* W = GetWorld();
	if (W && !Net.IsValid())
		for (TObjectIterator<UFNNetClient> It; It; ++It)
			if (!It->HasAnyFlags(RF_ClassDefaultObject)) { Net = *It; MsgHandle = It->OnServerMessage.AddUObject(this, &AFNPeopleLife::OnMessage); break; }
	if (!W || SinceDraw < 1.f / 30.f) return;
	const float Dt = SinceDraw;
	SinceDraw = 0.f;
	AFNStudio* St = nullptr;
	if (TActorIterator<AFNStudio> It(W); It) St = *It;
	// dev aid (goes with FN7DevAlarm.txt): drop Config/FN7DevLondon.txt in to jump the board to London's streets once
	if (St && FPaths::FileExists(FPaths::ProjectConfigDir() / TEXT("FN7DevLondon.txt")))
	{
		IFileManager::Get().Delete(*(FPaths::ProjectConfigDir() / TEXT("FN7DevLondon.txt")));
		St->FocusBoard(51.513, -0.09, 0);
	}
	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& It : Frame) It.Value.Reset();
	TArray<FFNPeopleSpot> Spots;
	TArray<FVector> HouseW;
	int32 Q0 = 0, R0 = 0; float Tp; FVector C0, C1;
	const bool bOn = St && St->GetMapLevel() == 0 && St->GetPeopleSpots(Spots, HouseW) && Spots.Num() > 0
		&& St->BoardCentreHex(Q0, R0) && St->BoardHexTop(Q0, R0, true, Tp, C0) && St->BoardHexTop(Q0 + 1, R0, true, Tp, C1);
	if (!bOn)
	{
		if (Hover) Hover->SetVisibility(false);
		for (UInstancedStaticMeshComponent* I : Keep) if (I->GetInstanceCount() > 0) I->ClearInstances();
		for (UStaticMeshComponent* Cd : CardPool) if (Cd && Cd->IsVisible()) Cd->SetVisibility(false);
		return;
	}
	const float D = FMath::Max(FVector::Dist2D(C0, C1), 0.5f);
	// the people pictures for the place under the board (asked again when the board moves, at most every 8 s)
	SinceAsk += Dt;
	if (UFNNetClient* Nq = Net.Get())
	{
		const FString Cell = St->BoardCellKey(Q0, R0);
		if (SinceAsk > 8.f && (Cell != AskedCell || RoleSprites.Num() < 6))
		{
			SinceAsk = 0.f;
			AskedCell = Cell;
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("cell"), Cell);
			A->SetNumberField(TEXT("limit"), 300);
			Nq->Query(TEXT("game.people"), A);
		}
	}
	int32 CardsUsed = 0;

	// who lives here: one person per spot, the same person whenever this hexagon is on the board
	TMap<FIntPoint, FVector> HomeW;
	for (TPair<FIntPoint, FPerson>& It : People) It.Value.bSeen = false;
	for (const FFNPeopleSpot& S : Spots)
	{
		HomeW.Add(S.Hex, S.Pos);
		FPerson* P = People.Find(S.Hex);
		if (!P)
		{
			FPerson N;
			N.Home = S.Hex;
			const int32 Q = S.Hex.X, R = S.Hex.Y;
			// near the water? (a sea hexagon within 3)
			for (int32 k = 1; k <= 3 && !N.bShore; ++k)
				for (int32 a = 0; a < 6 && !N.bShore; ++a)
				{
					static const FIntPoint Dir[6] = { {1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}, {1,-1} };
					float Top; FVector Wp;
					if (St->BoardHexTop(Q + Dir[a].X * k, R + Dir[a].Y * k, true, Top, Wp) && Top < 1.5f)
					{
						N.bShore = true;
						N.Shore = FVector2D(Wp - S.Pos) / D * ((k - 0.6f) / k);   // its edge, still on land
					}
				}
			const float U = Dice(Q, R, 3);
			// the trades of the place: a city's market folk, a village's farmers, the wild's witches and warriors
			static const uint8 City[] = { Merchant, Merchant, Merchant, Guard, Guard, Woman, Woman, Woman, Noble, Priest, Smith, Smith, Child, Child, Elder, Warrior };
			static const uint8 Village[] = { Farmer, Farmer, Farmer, Farmer, Woman, Woman, Woman, Child, Child, Smith, Elder, Elder, Priest, Witch, Merchant, Warrior };
			static const uint8 Wild[] = { Farmer, Farmer, Farmer, Farmer, Farmer, Witch, Witch, Warrior, Warrior, Warrior, Elder, Woman, Woman, Child, Fisherman, Farmer };
			const uint8* Pick = S.Place == 2 ? City : S.Place == 1 ? Village : Wild;
			N.Role = Pick[FMath::Min((int32)(U * 16.f), 15)];
			if (N.bShore && Dice(Q, R, 5) < 0.6f) N.Role = Fisherman;
			N.bFemale = N.Role == Woman || N.Role == Witch || ((N.Role == Noble || N.Role == Elder || N.Role == Child || N.Role == Merchant || N.Role == Priest || N.Role == Farmer) && Dice(Q, R, 6) < 0.4f);
			const int32 Ni = (int32)(Dice(Q, R, 7) * 16.f) % 16;
			N.Name = N.Role == Witch ? FString(TEXT("Old ")) + Her[Ni] : N.Role == Elder ? FString(TEXT("Grandpa ")) + His[Ni] : FString(N.bFemale ? Her[Ni] : His[Ni]);
			if (N.Role == Elder && N.bFemale) N.Name = FString(TEXT("Granny ")) + Her[Ni];
			N.Phase = Dice(Q, R, 8) * 10.f;
			N.Left = Dice(Q, R, 9) * 6.f;
			N.Yaw = Dice(Q, R, 10) * 360.f;
			N.Speed = N.Role == Child ? 1.8f : N.Role == Elder ? 0.4f : 0.7f + 0.3f * Dice(Q, R, 11);
			P = &People.Add(S.Hex, N);
		}
		P->bSeen = true;
	}
	for (auto It = People.CreateIterator(); It; ++It) if (!It.Value().bSeen) It.RemoveCurrent();

	// what is happening here now: the newest alarm that reaches this board (lat/lon -> board through the hexes' own map)
	Alarms.RemoveAll([&](const FAlarm& A) { return A.Until < Time; });
	// dev aid: with Config/FN7DevAlarm.txt present, a fire, a riot, then an army near the board centre in turn
	static float DevNext = 8.f; static int32 DevK = 0;
	if (Time > DevNext && FPaths::FileExists(FPaths::ProjectConfigDir() / TEXT("FN7DevAlarm.txt")))
	{
		double La, Lo;
		if (St->BoardHexLatLon(Q0 + 6, R0 - 3, La, Lo))
		{
			FAlarm A; A.Lat = La; A.Lon = Lo; A.RadiusM = 20000.0; A.Kind = (uint8)(1 + DevK++ % 3); A.Until = Time + 15.f;
			A.What = A.Kind == 1 ? TEXT("the fire") : A.Kind == 2 ? TEXT("the riot") : TEXT("the army");
			Alarms.Add(A);
		}
		DevNext = Time + 22.f;
	}
	const FAlarm* Now = nullptr;
	FVector2D EvRel = FVector2D::ZeroVector;   // the event, in hexagon widths from the board centre
	{
		double La0, Lo0, La1, Lo1, La2, Lo2;
		FVector W2; float Tq;
		if (St->BoardHexLatLon(Q0, R0, La0, Lo0) && St->BoardHexLatLon(Q0 + 1, R0, La1, Lo1) && St->BoardHexLatLon(Q0, R0 + 1, La2, Lo2) && St->BoardHexTop(Q0, R0 + 1, true, Tq, W2))
			for (int32 i = Alarms.Num() - 1; i >= 0 && !Now; --i)
			{
				const FAlarm& A = Alarms[i];
				if (Meters(La0, Lo0, A.Lat, A.Lon) > A.RadiusM + 200.0) continue;
				// solve the 2x2 map (lat, lon) -> board (x, y) from the centre hexagon and its two neighbours
				const FVector2D Ex(C1 - C0), Ey(W2 - C0);
				const double a = La1 - La0, b = La2 - La0, c = Lo1 - Lo0, d = Lo2 - Lo0, Det = a * d - b * c;
				if (FMath::Abs(Det) < 1e-14) continue;
				const double dLa = A.Lat - La0, dLo = A.Lon - Lo0;
				const double u = (d * dLa - b * dLo) / Det, v = (-c * dLa + a * dLo) / Det;   // in neighbour steps
				EvRel = (Ex * (float)u + Ey * (float)v) / D;
				Now = &A;
			}
	}
	for (TPair<FIntPoint, FPerson>& It : People)
	{
		FPerson& P = It.Value;
		const uint8 Want = Now ? Now->Kind : 0;
		if (Want == P.React) continue;
		P.React = Want; P.bHide = false;
		const FVector* Hw = HomeW.Find(P.Home);
		if (!Hw) continue;
		const FVector2D Ev = EvRel - FVector2D(*Hw - C0) / D;   // the event, from this one's home
		if (Want == 1)
		{
			// run for the nearest house (and stay inside), else away from it
			float Best = 6.f; FVector2D Door = P.Off - Ev.GetSafeNormal() * 6.f;
			for (const FVector& H : HouseW) { const FVector2D Hv = FVector2D(H - *Hw) / D; const float Dd = FVector2D::Distance(Hv, P.Off); if (Dd < Best) { Best = Dd; Door = Hv; } }
			P.ReactTo = Door;
			P.bHide = Best < 6.f;   // hides on arrival
		}
		else if (Want >= 2) P.ReactTo = P.Off + (Ev - P.Off).GetSafeNormal() * FMath::Clamp((Ev - P.Off).Size() - 1.5f, 0.f, 5.f) + FVector2D(FMath::FRandRange(-0.8f, 0.8f), FMath::FRandRange(-0.8f, 0.8f));
		else { P.State = Walk; P.To = FVector2D(FMath::FRandRange(-0.5f, 0.5f), FMath::FRandRange(-0.5f, 0.5f)); P.Left = 1.f; }   // it passed: home again
	}
	const FString ReactLog = Now ? FString::Printf(TEXT("%s near (%.1f hexes): %s"), *Now->What, EvRel.Size(), Now->Kind == 1 ? TEXT("people run for shelter") : Now->Kind == 2 ? TEXT("people crowd it") : TEXT("people cheer")) : FString(TEXT("calm"));
	if (ReactLog != LastReact) { LastReact = ReactLog; UE_LOG(LogTemp, Display, TEXT("FNPeople: %s"), *ReactLog); }

	// live: work, walk, chat
	for (TPair<FIntPoint, FPerson>& It : People)
	{
		FPerson& P = It.Value;
		if (P.React)
		{
			const FVector2D Step = P.ReactTo - P.Off;
			const float Len = Step.Size(), Mv = (P.React == 1 ? 2.4f : 1.4f) * FMath::Max(P.Speed, 0.6f) * Dt;
			if (Len > Mv) { P.Off += Step / Len * Mv; P.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Step.Y, Step.X)); }
			else P.Off = P.ReactTo;
			continue;
		}
		P.Left -= Dt;
		if (P.State == Walk)
		{
			const FVector2D Step = P.To - P.Off;
			const float Len = Step.Size(), Mv = P.Speed * Dt;
			if (Len <= Mv) { P.Off = P.To; P.Left = 0.f; }
			else { P.Off += Step / Len * Mv; P.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Step.Y, Step.X)); P.Left = 1.f; }
		}
		if (P.Left <= 0.f)
		{
			const FVector& Hw = HomeW[P.Home];
			TArray<FVector2D> Rel, HRel;
			if (P.State != Walk)
			{
				for (const FFNPeopleSpot& S : Spots) { const FVector2D V = FVector2D(S.Pos - Hw) / D; if (V.Size() < 6.f) Rel.Add(V); }
				for (const FVector& H : HouseW) { const FVector2D V = FVector2D(H - Hw) / D; if (V.Size() < 7.f) HRel.Add(V); }
			}
			Decide(P, Rel, HRel);
			// chatting: face the nearest neighbour where it stopped
			if (P.ChatWith.X == MAX_int32 - 1)
			{
				float Best = 1e9f; FIntPoint Who(MAX_int32, 0);
				for (const TPair<FIntPoint, FPerson>& O : People)
				{
					if (O.Key == P.Home) continue;
					const FVector* Ow = HomeW.Find(O.Key);
					if (!Ow) continue;
					const float Dd = FVector2D::Distance(FVector2D(*Ow - Hw) / D + O.Value.Off, P.To);
					if (Dd < Best) { Best = Dd; Who = O.Key; }
				}
				P.ChatWith = Best < 1.5f ? Who : FIntPoint(MAX_int32, 0);
			}
		}
	}

	// draw: a readable figure per trade, and the hovered one's name
	FVector Cam = FVector::ZeroVector;
	FVector2D Mouse(-1e6f, -1e6f);
	APlayerController* PC = W->GetFirstPlayerController();
	if (PC) { if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation(); float Mx, My; if (PC->GetMousePosition(Mx, My)) Mouse = FVector2D(Mx, My); }
	const FPerson* Hovered = nullptr; FVector HoverAt = FVector::ZeroVector; float HoverBest = 28.f;
	TMap<uint8, int32> RoleCount; int32 Walking = 0, Chatting = 0;
	const float G = D * 1.9f;   // figures bigger than their hexagon: they must read from the chair
	for (const TPair<FIntPoint, FPerson>& It : People)
	{
		const FPerson& P = It.Value;
		const FVector Hw = HomeW[P.Home];
		const float A = Time * 6.f + P.Phase;
		const bool bArrived = P.React && P.Off.Equals(P.ReactTo, 0.05f);
		if (P.React == 1 && P.bHide && bArrived) continue;   // sheltering inside
		const bool bWork = !P.React && P.State == Work, bWalk = (!P.React && P.State == Walk) || (P.React && !bArrived);
		RoleCount.FindOrAdd(P.Role)++; Walking += bWalk; Chatting += P.State == Chat;
		float Yaw = P.Yaw;
		if (P.State == Chat && P.ChatWith.X != MAX_int32)
			if (const FPerson* O = People.Find(P.ChatWith)) { const FVector* Ow = HomeW.Find(P.ChatWith); if (Ow) { const FVector2D Dv = FVector2D(*Ow - Hw) / D + O->Off - P.Off; Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dv.Y, Dv.X)); } }
		if (bWork && P.Role == Fisherman && P.bShore) Yaw = FMath::RadiansToDegrees(FMath::Atan2(P.Shore.Y, P.Shore.X));
		if (bWork && (P.Role == Guard || P.Role == Merchant || P.Role == Noble || P.Role == Woman)) Yaw += 35.f * FMath::Sin(Time * 0.7f + P.Phase);
		const FVector Fwd(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.f);
		const FVector Rt(-Fwd.Y, Fwd.X, 0.f), Up = FVector::UpVector;
		const float H = G * (P.Role == Child ? 0.7f : P.Role == Elder ? 0.95f : 1.1f), Wd = G * (P.Role == Child ? 0.22f : 0.3f);
		float Lift = bWalk ? FMath::Abs(FMath::Sin(A * (P.Role == Child ? 2.f : 1.3f))) * G * 0.06f : 0.f;
		if (bWork && P.Role == Child) Lift = FMath::Abs(FMath::Sin(A * 1.6f)) * G * 0.25f;
		if (!P.React && P.State == Chat) Lift = FMath::Abs(FMath::Sin(A * 0.9f)) * G * 0.02f;
		if (P.React >= 2 && bArrived) Lift = FMath::Abs(FMath::Sin(A * (P.React == 2 ? 1.4f : 1.1f))) * G * 0.18f;   // jeering / cheering, jumping
		if (P.React == 1 && bWalk) Lift = FMath::Abs(FMath::Sin(A * 2.2f)) * G * 0.08f;   // running
		const FVector Base = Hw + FVector(P.Off.X * D, P.Off.Y * D, Lift);
		const int32 V = (int32)(Dice(P.Home.X, P.Home.Y, 12) * 4.f);
		// a picture of this kind of person from the server's library: a standing card turned to the camera
		if (const TArray<FString>* Looks = RoleSprites.Find(P.Role))
		{
			const FString& Hash = (*Looks)[FMath::Min((int32)(Dice(P.Home.X, P.Home.Y, 21) * Looks->Num()), Looks->Num() - 1)];
			if (const TObjectPtr<UTexture2D>* Tx = SpriteTex.Find(Hash))
			{
				UStaticMeshComponent* Card = CardAt(CardsUsed++);
				UMaterialInstanceDynamic* M = SpriteMat.FindRef(Hash);
				if (!M)
				{
					static UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
					M = UMaterialInstanceDynamic::Create(Unlit, this);
					M->SetTextureParameterValue(TEXT("SlateUI"), Tx->Get());
					SpriteMat.Add(Hash, M);
				}
				if (Card->GetMaterial(0) != M) Card->SetMaterial(0, M);
				const float Ch = H * 1.35f, Cw = Ch * 0.62f;
				const FVector CardAtW = Base + FVector::UpVector * Ch * 0.5f;
				Card->SetWorldLocation(CardAtW);
				Card->SetWorldRotation(FRotator(0.f, (Cam - CardAtW).GetSafeNormal2D().Rotation().Yaw + 90.f, 0.f));
				Card->SetWorldScale3D(FVector(Cw, Ch * 0.02f, Ch) / 100.f);
				Card->SetVisibility(true);
				FVector2D Spx;
				if (PC && PC->ProjectWorldLocationToScreen(CardAtW + FVector::UpVector * Ch * 0.3f, Spx))
				{
					const float Dpx = FVector2D::Distance(Spx, Mouse);
					if (Dpx < HoverBest) { HoverBest = Dpx; Hovered = &P; HoverAt = CardAtW + FVector::UpVector * Ch * 0.3f; }
				}
				continue;
			}
		}
		const FColor Body = RoleCol(P.Role, V);
		const FColor SkinC = P.Role == Witch ? FColor(150, 175, 110) : Skin[V];
		const bool bDress = P.bFemale || P.Role == Priest || P.Role == Noble || P.Role == Witch;
		const float BodyH = H * 0.7f;
		const FVector Lean = P.Role == Elder || P.Role == Witch ? Fwd * G * 0.08f : FVector::ZeroVector;
		if (bDress) Add(MCone, Body, FTransform(FRotator::ZeroRotator, Base + Up * BodyH * 0.5f, FVector(Wd * 1.6f, Wd * 1.6f, BodyH) / 100.f));
		else
		{
			Add(MCyl, Body, FTransform(FRotator::ZeroRotator, Base + Up * (BodyH * 0.62f), FVector(Wd, Wd, BodyH * 0.76f) / 100.f));
			const float Swing = bWalk ? FMath::Sin(A * 1.3f) * G * 0.08f : 0.f;   // two legs, striding when walking
			Stick(Base + Rt * Wd * 0.25f + Fwd * Swing, Base + Rt * Wd * 0.25f + Up * BodyH * 0.3f, Wd * 0.3f, FColor(60, 50, 45));
			Stick(Base - Rt * Wd * 0.25f - Fwd * Swing, Base - Rt * Wd * 0.25f + Up * BodyH * 0.3f, Wd * 0.3f, FColor(60, 50, 45));
		}
		const FVector Head = Base + Up * (BodyH + Wd * 0.45f) + Lean;
		Add(MBall, SkinC, FTransform(FRotator::ZeroRotator, Head, FVector(Wd * 0.9f) / 100.f));
		if (P.bFemale && P.Role != Witch) Add(MBall, Hair[(V + P.Home.X) & 3], FTransform(FRotator::ZeroRotator, Head - Fwd * Wd * 0.18f + Up * Wd * 0.08f - Up * (P.Role == Woman ? Wd * 0.25f : 0.f), FVector(Wd * 0.95f, Wd * 0.95f, Wd * (P.Role == Woman ? 1.4f : 0.95f)) / 100.f));
		const FVector Hand = Base + Up * BodyH * 0.6f + Rt * Wd * 0.75f + Fwd * Wd * 0.3f;
		switch (P.Role)
		{
		case Fisherman:
		{
			Add(MCyl, FColor(225, 200, 120), FTransform(FRotator::ZeroRotator, Head + Up * Wd * 0.4f, FVector(Wd * 1.7f, Wd * 1.7f, Wd * 0.08f) / 100.f));   // straw hat
			const float Cast = bWork ? 0.5f + 0.5f * FMath::Sin(Time * 1.2f + P.Phase) : 0.1f;
			const FVector Tip = Hand + (Fwd * (0.4f + Cast * 0.8f) + Up * (1.f - Cast * 0.5f)).GetSafeNormal() * G * 1.3f;
			Stick(Hand, Tip, G * 0.03f, Wood);
			if (bWork) Stick(Tip, Tip + Fwd * G * 0.25f - Up * (Tip.Z - Base.Z + G * 0.02f), G * 0.01f, FColor(230, 230, 230));   // the line into the water
			break;
		}
		case Farmer:
		{
			if (!P.bFemale) Add(MCyl, FColor(225, 200, 120), FTransform(FRotator::ZeroRotator, Head + Up * Wd * 0.4f, FVector(Wd * 1.6f, Wd * 1.6f, Wd * 0.08f) / 100.f));
			const float Chop = bWork ? FMath::Abs(FMath::Sin(A * 0.6f)) : 0.3f;
			const FVector End = Hand + (Fwd * 0.9f + Up * (Chop - 0.4f)).GetSafeNormal() * G * 0.6f;
			Stick(Hand - (End - Hand) * 0.3f, End, G * 0.025f, Wood);
			Add(MCube, Steel, FTransform(FRotator(0, Yaw, 0), End, FVector(G * 0.08f, G * 0.14f, G * 0.04f) / 100.f));
			break;
		}
		case Smith:
		{
			const float Hit = bWork ? FMath::Abs(FMath::Sin(A * 0.8f)) : 0.5f;
			const FVector End = Hand + (Fwd * 0.6f + Up * (Hit * 1.2f - 0.3f)).GetSafeNormal() * G * 0.35f;
			Stick(Hand, End, G * 0.03f, Wood);
			Add(MCube, Black, FTransform(FRotator(0, Yaw, 0), End, FVector(G * 0.12f, G * 0.07f, G * 0.07f) / 100.f));
			if (bWork) Add(MCube, FColor(60, 60, 66), FTransform(FRotator(0, Yaw, 0), Base + Fwd * G * 0.45f + Up * G * 0.12f, FVector(G * 0.25f, G * 0.14f, G * 0.24f) / 100.f));   // the anvil
			break;
		}
		case Merchant:
			Add(MBall, FColor(150, 110, 60), FTransform(FRotator::ZeroRotator, Base + Up * BodyH * 0.7f - Fwd * Wd * 0.9f, FVector(Wd * 1.0f, Wd * 0.9f, Wd * 1.1f) / 100.f));   // a sack on the back
			if (bWork) Add(MCube, FColor(170, 120, 70), FTransform(FRotator(0, Yaw, 0), Base + Fwd * G * 0.5f + Up * G * 0.2f, FVector(G * 0.5f, G * 0.3f, G * 0.08f) / 100.f));   // a stall
			break;
		case Guard: case Warrior:
		{
			Add(MBall, Steel, FTransform(FRotator::ZeroRotator, Head + Up * Wd * 0.2f, FVector(Wd * 1.0f, Wd * 1.0f, Wd * 0.7f) / 100.f));   // helmet
			const float Drill = P.Role == Warrior && bWork ? FMath::Sin(A * 0.9f) * 0.6f : 0.f;
			const FVector Sp = Hand + Fwd * Drill * G * 0.3f;
			Stick(Sp - Up * G * 0.45f, Sp + (Up + Fwd * Drill).GetSafeNormal() * G * 1.0f, G * 0.025f, Wood);   // spear
			Add(MCone, Steel, FTransform(FQuat::FindBetweenNormals(Up, (Up + Fwd * Drill).GetSafeNormal()), Sp + (Up + Fwd * Drill).GetSafeNormal() * G * 1.05f, FVector(G * 0.06f, G * 0.06f, G * 0.14f) / 100.f));
			Add(MCyl, P.Role == Guard ? FColor(220, 200, 60) : FColor(120, 20, 20), FTransform(FQuat::FindBetweenNormals(Up, -Rt), Base + Up * BodyH * 0.55f - Rt * Wd * 0.75f, FVector(Wd * 1.3f, Wd * 1.3f, G * 0.04f) / 100.f));   // shield
			break;
		}
		case Priest:
		{
			const FVector Top = Hand + Up * (G * (bWork ? 0.75f + 0.1f * FMath::Sin(A * 0.4f) : 0.6f));
			Stick(Hand - Up * G * 0.5f, Top, G * 0.025f, Gold);
			Add(MBall, Gold, FTransform(FRotator::ZeroRotator, Top, FVector(G * 0.09f) / 100.f));
			break;
		}
		case Witch:
		{
			Add(MCyl, Black, FTransform(FRotator::ZeroRotator, Head + Up * Wd * 0.35f, FVector(Wd * 1.8f, Wd * 1.8f, Wd * 0.07f) / 100.f));   // brim
			Add(MCone, Black, FTransform(FRotator(0.f, 0.f, 12.f), Head + Up * (Wd * 0.35f + H * 0.2f), FVector(Wd * 0.95f, Wd * 0.95f, H * 0.42f) / 100.f));   // the pointed hat
			Add(MBall, FColor(150, 175, 110), FTransform(FRotator::ZeroRotator, Head + Fwd * Wd * 0.5f, FVector(Wd * 0.25f, Wd * 0.25f, Wd * 0.4f) / 100.f));   // the nose
			if (bWork)
			{
				const FVector Pot = Base + Fwd * G * 0.45f + Up * G * 0.12f;
				Add(MBall, Black, FTransform(FRotator::ZeroRotator, Pot, FVector(G * 0.3f, G * 0.3f, G * 0.24f) / 100.f));   // cauldron
				Add(MBall, FColor(120, 230, 80), FTransform(FRotator::ZeroRotator, Pot + Up * G * (0.1f + 0.04f * FMath::Sin(A)), FVector(G * 0.22f, G * 0.22f, G * 0.05f) / 100.f));   // the brew
				Stick(Hand, Pot + FVector(FMath::Cos(A * 0.5f), FMath::Sin(A * 0.5f), 0.f) * G * 0.08f + Up * G * 0.05f, G * 0.02f, Wood);
			}
			break;
		}
		case Noble:
			Add(MCyl, Gold, FTransform(FRotator::ZeroRotator, Head + Up * Wd * 0.45f, FVector(Wd * 0.75f, Wd * 0.75f, Wd * 0.3f) / 100.f));   // crown
			Add(MCone, FColor(200, 30, 40), FTransform(FRotator::ZeroRotator, Base + Up * BodyH * 0.45f - Fwd * Wd * 0.4f, FVector(Wd * 1.9f, Wd * 1.5f, BodyH * 0.9f) / 100.f));   // cape
			break;
		case Elder:
			Stick(Hand + Fwd * G * 0.1f - Up * BodyH * 0.6f, Hand + Fwd * G * 0.1f + Up * G * 0.05f, G * 0.025f, Wood);   // cane
			break;
		default: break;
		}
		// is the mouse on this one?
		FVector2D Sp;
		if (PC && PC->ProjectWorldLocationToScreen(Head, Sp))
		{
			const float Dpx = FVector2D::Distance(Sp, Mouse);
			if (Dpx < HoverBest) { HoverBest = Dpx; Hovered = &P; HoverAt = Head; }
		}
	}
	for (int32 c = CardsUsed; c < CardPool.Num(); ++c) if (CardPool[c] && CardPool[c]->IsVisible()) CardPool[c]->SetVisibility(false);
	for (TPair<UInstancedStaticMeshComponent*, TArray<FTransform>>& It : Frame)
	{
		UInstancedStaticMeshComponent* I = It.Key;
		const TArray<FTransform>& T = It.Value;
		if (T.Num() == 0) { if (I->GetInstanceCount() > 0) I->ClearInstances(); continue; }
		if (I->GetInstanceCount() != T.Num()) { I->ClearInstances(); I->AddInstances(T, false, true); }
		else I->BatchUpdateInstancesTransforms(0, T, true, true, true);
	}
	if (!Hover)
	{
		Hover = NewObject<UTextRenderComponent>(this);
		Hover->SetupAttachment(RootComponent);
		Hover->SetHorizontalAlignment(EHTA_Center);
		Hover->SetVerticalAlignment(EVRTA_TextBottom);
		Hover->SetTextRenderColor(FColor(255, 240, 200));
		Hover->SetCastShadow(false);
		Hover->RegisterComponent();
	}
	Hover->SetVisibility(Hovered != nullptr);
	if (Hovered)
	{
		const FString ReactDo = Hovered->React == 1 ? (Hovered->bHide ? FString(TEXT("running for shelter from ")) : FString(TEXT("fleeing "))) + (Now ? Now->What : FString(TEXT("danger"))) : Hovered->React == 2 ? FString(TEXT("jeering at the riot")) : FString(TEXT("cheering the army"));
		const TCHAR* Doing = Hovered->React ? *ReactDo : Hovered->State == Work ? WorkName[Hovered->Role] : Hovered->State == Chat ? TEXT("chatting") : Hovered->Role == Fisherman ? TEXT("walking to the shore") : TEXT("out and about");
		Hover->SetText(FText::FromString(FString::Printf(TEXT("%s, the %s\n%s"), *Hovered->Name, RoleName[Hovered->Role], Doing)));
		Hover->SetWorldSize(G * 0.4f);
		const FVector At = HoverAt + FVector(0, 0, G * 0.55f);
		Hover->SetWorldLocation(At);
		Hover->SetWorldRotation((Cam - At).GetSafeNormal2D().Rotation());
	}
	FString Log = FString::Printf(TEXT("%d living people (%d walking, %d chatting):"), People.Num(), Walking, Chatting);
	for (int32 r = 0; r < NumRoles; ++r) if (const int32* N = RoleCount.Find((uint8)r)) Log += FString::Printf(TEXT(" %s %d"), RoleName[r], *N);
	const FString Short = FString::Printf(TEXT("%d"), People.Num());
	if (Short != LastLog) { LastLog = Short; UE_LOG(LogTemp, Display, TEXT("FNPeople: %s"), *Log); }
}

bool UFNPeopleLifeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFNPeopleLifeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AFNPeopleLife>(AFNPeopleLife::StaticClass(), FTransform::Identity, P);
}
