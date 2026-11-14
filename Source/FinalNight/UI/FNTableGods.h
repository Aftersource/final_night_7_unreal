#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FNStoryDecorable.h"
#include "FNTableGods.generated.h"

class AFNStudio;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;

/** One demigod as the table shows it: its diorama (full-fidelity image) and where it stands on the board. */
struct FFNTableDemigod
{
	FString Ref;
	FString Name;
	FString Side;                 // "left" / "right"
	bool bAlive = true;
	bool bHasPos = false;
	double Lat = 0.0, Lon = 0.0;
	UTexture2D* Diorama = nullptr;      // the generated diorama_image
	UTexture2D* DioramaGrey = nullptr;  // the same, desaturated, for the fallen
};

/** One player (a god) at the table: its seat, name, aura and its demigods. */
struct FFNTableGod
{
	int32 Seat = -1;
	FString Player;
	FLinearColor Aura = FLinearColor::White;
	bool bHuman = false;
	bool bConnected = true;
	bool bAlive = true;
	bool bMine = false;
	TArray<FFNTableDemigod> Demigods;
};

/**
 * A player's god on a stone pedestal past the table's rim: a faceted figure with a SHAPE of its own (hooded, horned,
 * crowned, winged, masked or antlered, picked stably from the name), two glowing hands, and an aura in the player's
 * colour: a steady breathing halo for a human, a flickering ring of orbiting cubes for the AI, dimmed while away,
 * grey once out. A name plate turns to the camera.
 */
UCLASS()
class FINALNIGHT_API AFNGodPedestal : public AActor
{
	GENERATED_BODY()
public:
	AFNGodPedestal();
	virtual void Tick(float DeltaSeconds) override;
	void SetGod(const FFNTableGod& God);

private:
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Parts;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	TArray<UStaticMeshComponent*> Robe;        // tinted dark with the aura
	TArray<UStaticMeshComponent*> Feature;     // the shape's parts
	TArray<UStaticMeshComponent*> Hands;
	TArray<UStaticMeshComponent*> Beads;       // human halo
	TArray<UStaticMeshComponent*> Cubes;       // AI ring
	UPointLightComponent* Glow = nullptr;
	UTextRenderComponent* Plate = nullptr;
	FFNTableGod Info;
	int32 Kind = -1;
	float Time = 0.f;
	float Phase = 0.f;
	UStaticMeshComponent* Part(const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color);
	void BuildBody();
	void BuildFeature(int32 InKind);
};

/**
 * A demigod's DIORAMA BOX: a small hex plinth with an upright, framed card showing its generated diorama at full
 * fidelity, turned to the camera, framed in the player's aura colour, with an L/R notch on the plinth. Fallen: the
 * card tips over onto the plinth and greys. Until the image is ready: a dim silhouette card. The same box, smaller,
 * stands on the board where the demigod is. Benito's piece-story feature dresses it through IFNStoryDecorable.
 */
UCLASS()
class FINALNIGHT_API AFNDemigodFigure : public AActor, public IFNStoryDecorable
{
	GENERATED_BODY()
public:
	AFNDemigodFigure();
	virtual void Tick(float DeltaSeconds) override;
	void SetDemigod(const FFNTableDemigod& D, const FLinearColor& Aura, bool bOwnerAlive);
	const FString& GetRef() const { return Ref; }
	virtual void ApplyStory(const TSharedPtr<FJsonObject>& Story) override;

private:
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Parts;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<UMaterialInterface> ImageMat;
	UPROPERTY() TObjectPtr<UTexture2D> ShownTex;
	USceneComponent* Card = nullptr;
	UStaticMeshComponent* Image = nullptr;
	UStaticMeshComponent* Matte = nullptr;
	UStaticMeshComponent* Back = nullptr;
	UStaticMeshComponent* Notch = nullptr;
	UTextRenderComponent* NotchText = nullptr;
	TArray<UStaticMeshComponent*> Plinth;
	TArray<UStaticMeshComponent*> StoryBits;
	FString Ref;
	FString Side;
	FLinearColor Aura = FLinearColor::White;
	bool bAlive = true;
	float Tip = 0.f;
	float Time = 0.f;
	UStaticMeshComponent* Part(USceneComponent* Parent, const TCHAR* Mesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot, const FLinearColor& Color);
};

/**
 * The table's gods and dioramas: spawns a pedestal per rival player at the seat slots around the far rim
 * (slot angles 90/200/235/305: slot 0 is you at the camera, the others (seat - yours) mod 4, clockwise), two diorama
 * boxes per player between its pedestal and the rim (yours at the near rim), and a small diorama box on the board at
 * each demigod's position. While it is up, the classic seat silhouettes and minifigs are hidden.
 */
UCLASS()
class FINALNIGHT_API AFNTableGods : public AActor
{
	GENERATED_BODY()
public:
	AFNTableGods();
	virtual void Tick(float DeltaSeconds) override;
	static AFNTableGods* Get(AFNStudio* Studio, bool bCreate);
	void SetGods(const TArray<FFNTableGod>& Gods, int32 MySeat);
	void SetShown(bool bShow);
	/** The figure for a demigod ref (the one by its god's pedestal), for the piece-story feature. */
	AFNDemigodFigure* FindFigure(const FString& Ref) const;

private:
	TWeakObjectPtr<AFNStudio> Studio;
	UPROPERTY() TMap<int32, TObjectPtr<AFNGodPedestal>> Pedestals;   // by slot
	UPROPERTY() TMap<FString, TObjectPtr<AFNDemigodFigure>> Figures;  // by ref
	UPROPERTY() TMap<FString, TObjectPtr<AFNDemigodFigure>> OnBoard;  // by ref
	TMap<FString, FFNTableDemigod> BoardInfo;
	TMap<FString, FLinearColor> BoardAura;
	bool bShown = true;
	bool bHasGods = false;
	static float SlotAngle(int32 Slot);
	FVector Local(float Deg, float Radius, float Z) const;
};
