#include "Features/FNDayNight.h"
#include "FNStudio.h"
#include "Components/LightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Dom/JsonObject.h"

namespace FNDayNightImpl
{
	// Roles: 1 = the sun (BoardKey), 2 = the moon, 3 = a fill light (TabletLight)
	static int32 RoleOf(const FName& N)
	{
		if (N == FName(TEXT("BoardKey"))) return 1;
		if (N == FName(TEXT("Moon"))) return 2;
		if (N == FName(TEXT("TabletLight")) || N == FName(TEXT("Projector"))) return 3;
		return 0;
	}
	static FLinearColor TintOf(const FString& Phase)
	{
		if (Phase == TEXT("dawn"))  return FLinearColor(1.f, 0.72f, 0.62f);
		if (Phase == TEXT("dusk"))  return FLinearColor(1.f, 0.55f, 0.32f);
		if (Phase == TEXT("night")) return FLinearColor(0.45f, 0.55f, 1.f);
		return FLinearColor(1.f, 0.97f, 0.92f);   // day
	}
	static float LightOf(const FString& Phase)
	{
		if (Phase == TEXT("dawn") || Phase == TEXT("dusk")) return 0.5f;
		if (Phase == TEXT("night")) return 0.15f;
		return 1.f;
	}
}

AFNDayNight::AFNDayNight()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("NightGrade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
	Grade->Priority = -1.f;     // under any volume the kit or the studio adds
	Grade->BlendWeight = 0.f;   // nothing until the server says what time it is
}

void AFNDayNight::Bind(AFNStudio* InStudio)
{
	Studio = InStudio;
	FindLamps();
}

void AFNDayNight::FindLamps()
{
	// Lamps can appear later (the table's Projector is made when the map is built): keep the ones we have, add new ones.
	Lamps.RemoveAll([](const FLamp& P) { return !P.Comp.IsValid(); });
	AFNStudio* St = Studio.Get();
	if (!St) return;
	TArray<ULightComponent*> All;
	St->GetComponents<ULightComponent>(All);
	for (ULightComponent* L : All)
	{
		const int32 LampRole = L ? FNDayNightImpl::RoleOf(L->GetFName()) : 0;
		if (!LampRole || Lamps.ContainsByPredicate([L](const FLamp& P) { return P.Comp.Get() == L; })) continue;
		FLamp& P = Lamps.AddDefaulted_GetRef();
		P.Comp = L; P.BaseIntensity = L->Intensity; P.BaseColor = L->GetLightColor(); P.Role = LampRole;
	}
}

void AFNDayNight::Apply(const TSharedPtr<FJsonObject>& Payload)
{
	if (!Payload.IsValid()) return;
	TSharedPtr<FJsonObject> T = Payload;
	const TSharedPtr<FJsonObject>* Inner = nullptr;
	if (Payload->TryGetObjectField(TEXT("time_of_day"), Inner) && Inner && Inner->IsValid()) T = *Inner;
	FString P;
	if (!T->TryGetStringField(TEXT("phase"), P)) return;
	double L = FNDayNightImpl::LightOf(P), D = Day;
	T->TryGetNumberField(TEXT("light"), L);
	T->TryGetNumberField(TEXT("day"), D);
	Day = (int32)D;
	SetTarget(P, (float)L);
}

void AFNDayNight::ApplyEvent(const TSharedPtr<FJsonObject>& Event)
{
	FString Kind;
	if (Event.IsValid() && Event->TryGetStringField(TEXT("event"), Kind) && Kind == TEXT("time.phase")) Apply(Event);
}

void AFNDayNight::SetTarget(const FString& InPhase, float InLight)
{
	InLight = FMath::Clamp(InLight, 0.f, 1.f);
	if (InPhase == Phase && FMath::IsNearlyEqual(InLight, ToLight, 0.01f)) return;
	FindLamps();
	// Start the blend from wherever the look is right now.
	const float A = FMath::SmoothStep(0.f, 1.f, Mix);
	FromLight = FMath::Lerp(FromLight, ToLight, A);
	FromTint = FLinearColor::LerpUsingHSV(FromTint, ToTint, A);
	ToLight = InLight;
	ToTint = FNDayNightImpl::TintOf(InPhase);
	Phase = InPhase;
	Light = InLight;
	Mix = 0.f;
	UE_LOG(LogTemp, Display, TEXT("FN7 day/night: day %d %s light %.2f (%d lamps)"), Day, *Phase, Light, Lamps.Num());
}

void AFNDayNight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Mix >= 1.f) return;
	Mix = FMath::Min(1.f, Mix + DeltaSeconds / 2.f);
	const float A = FMath::SmoothStep(0.f, 1.f, Mix);
	Push(FMath::Lerp(FromLight, ToLight, A), FLinearColor::LerpUsingHSV(FromTint, ToTint, A));
}

void AFNDayNight::Push(float L, const FLinearColor& Tint)
{
	if (Grade)
	{
		// Day: no grade. Night (light 0.15): a cool tint, about one stop darker, two-thirds saturation.
		const float Night = 1.f - L;
		FPostProcessSettings& P = Grade->Settings;
		P.bOverride_SceneColorTint = true;
		P.SceneColorTint = FLinearColor::LerpUsingHSV(FLinearColor::White, Tint, 0.6f);
		P.bOverride_ColorSaturation = true;
		P.ColorSaturation = FVector4(1.f - 0.35f * Night, 1.f - 0.35f * Night, 1.f - 0.35f * Night, 1.f);
		P.bOverride_ColorGain = true;
		const float Gain = 1.f - 0.5f * Night;
		P.ColorGain = FVector4(Gain, Gain, Gain, 1.f);
		Grade->BlendWeight = 1.f;
	}
	for (const FLamp& P : Lamps)
	{
		ULightComponent* C = P.Comp.Get();
		if (!C) continue;
		float I = P.BaseIntensity;
		FLinearColor Col = P.BaseColor;
		switch (P.Role)
		{
		case 1: I *= 0.3f + 0.7f * L; Col = P.BaseColor * Tint; break;                // the sun dims and warms or cools
		case 2: I *= 1.f + 2.5f * (1.f - L); break;                                   // the moon comes up at night
		case 3: I *= 0.55f + 0.45f * L; Col = FLinearColor::LerpUsingHSV(P.BaseColor, P.BaseColor * Tint, 0.5f); break;
		default: break;
		}
		C->SetIntensity(I);
		C->SetLightColor(Col);
	}
}
