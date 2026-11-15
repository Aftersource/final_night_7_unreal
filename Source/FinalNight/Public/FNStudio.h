#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/Actor.h"
#include "FNTypes.h"
#include "FNStudio.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
class UPointLightComponent;
class UAudioComponent;
class USoundWaveProcedural;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class USpotLightComponent;

/** Something standing on the globe board. */
struct FFNPiece
{
	FString Name;
	FString Ref;   // the demigod's ref when known (lets a click on it open the right card)
	double Lon = 0.0, Lat = 0.0;
	/** 0 you, 1 another player, 2 villager (AI), 3 fallen, 4 drop spot (day 1) */
	int32 Kind = 1;
	int32 Color = 1;
};

class UTexture2D;

/** One seat at the table (sent by the widget every beat and on room.updated). */
struct FFNSeatInfo
{
	FString Ref, Name;
	int32 Seat = -1;
	bool bHuman = false, bConnected = true, bMine = false, bAlive = true;
	FLinearColor Color = FLinearColor::White;
	UTexture2D* Portrait = nullptr;
	UTexture2D* Diorama = nullptr;
};

/** Sounds the table can make. All of them are synthesised in code (no audio files). */
enum class EFNSfx : uint8 { Click, Boom, Chime, Snuff, Quill, Impact, Swarm, Roar, Whoosh };

/**
 * The Final Night tabletop, rebuilt in Unreal from the Unity client
 * (elationate/finalnight_unity, scene "FinalNight - Client"):
 *  - Player easel        click to begin (StartExperience -> connect)
 *  - Centrex / Globe of Destiny + globe guard (blitzes until you join)  click to join the lobby
 *  - WILL panel          click to join the game, then to write / lock your WILL
 *  - Table easel         welcome message, then "[primer] round: #N"
 *  - 4 player stands     names + colours, markers on the globe
 *  - Episode ribbons     circle the globe; click one to choose it (selection is new: the 2024 build had none)
 * Built from engine basic shapes; it only DISPLAYS state. SFNRootWidget owns the logic.
 * Clickable parts carry the component tag "FN:<Id>".
 */
/** What the earth simulator says a board hex is right now (the scenery hides or swaps its trees on anything but Normal). */
enum class EFNHexState : uint8 { Normal, Burning, Flooded, Lava, Ash, NewLand };

UCLASS()
class FINALNIGHT_API AFNStudio : public AActor
{
	GENERATED_BODY()

public:
	AFNStudio();

	// --- earth-simulator hook (backend 2026-11-14 21:04) ---------------------------------------------------------------
	// Every board height (the hex tiles, the scenery on them, cities, people, the cursor hit test) goes through
	// BoardHeightAt. Today nothing overrides it, so it returns the board's own terraced relief (no visual change); the
	// earth component sets per-hex overrides from the server's voxel field and calls RebuildBoard().
	// CellKey is the server's cell code at the table's current level: "<face>:<people|houses|villages|regions>:<q>:<r>".
	float BoardHeightAt(const FString& CellKey, float DefaultRelief) const;
	EFNHexState HexStateAt(const FString& CellKey) const;
	/** Height on the table in cm (as BuildTable uses: water 0.6, land 2.2..14.1); NaN = keep the relief, only set the state. */
	void SetHexOverride(const FString& CellKey, float HeightCm, EFNHexState State);
	void ClearHexOverrides();
	void RebuildBoard() { RebuildIn = 0.f; }
	/** The cell code of the board hex (Q, R) at the table's current level (on the current face). */
	FString BoardCellKey(int32 Q, int32 R) const;
	virtual void Tick(float DeltaSeconds) override;

	static const FName PlayerEasel;
	static const FName Globe;
	static const FName WillPanel;
	static const FName RibbonA;
	static const FName RibbonB;
	static const FName RibbonC;

	FVector GetSeatLocation() const;
	FRotator GetSeatRotation() const;
	void SetHovered(FName Id);

	// Scene animator (mirrors SceneAnimator in the Unity client)
	void PlayerEaselDown();
	void SetGuardBlitz(bool bBlitz);
	void SetGuardLocked(bool bLocked);
	void SetWillPanelState(const FString& Label, bool bActive);

	void ShowEaselText(const FString& Header, const FString& Body);
	/** Stands show who is near you (name + distance/direction). */
	void ShowNearby(const TArray<FFNPlayer>& Nearby);
	/** Globe: where you are, the safe zone ring, and (day 1) the battle bus drop cities. */
	void ShowSelfOnGlobe(double Lon, double Lat, bool bAlive);
	void ShowZoneOnGlobe(double Lon, double Lat, double RadiusKm);
	/** A lightning bolt strikes the Globe at this point (about half a second). */
	void FlashAt(double Lon, double Lat);
	/** Style-kit hooks (implemented in UI/FNBoardAoe.cpp; the only lines the style kit adds to this file): the board hex
	 *  under a viewport pixel, and an area-of-effect overlay of lit hexes around it, for dragging an Entropy power. */
	bool HexAtScreen(const FVector2D& ViewportPixel, double& OutLat, double& OutLon, FVector& OutWorldCenter, double& OutHexM) const;
	void ShowAreaOfEffect(bool bShow, const FVector2D& ViewportPixel, double RadiusKm);
	/** Style-kit hooks for the gods, the demigod figures and the power ring (implemented in UI/FNBoardAoe.cpp): the world
	 *  point on the board for a lat/lon (false when it is off the board), the board's rim, and hiding the classic seat
	 *  silhouettes and minifigs while the kit's gods stand at the table. */
	bool BoardPointFor(double Lat, double Lon, FVector& OutWorld) const;
	bool GetBoardRim(FVector& OutCenter, double& OutRadiusWorld, FRotator& OutTableRot) const;
	void HideClassicSeats();
	/** The kit's WILL scroll replaces the table's 3D WILL bar: hide it (and keep it from catching clicks). */
	void HideWillBlock();
	/** Point the table board at a lat/lon at a zoom level (0 people, 1 houses, 2 villages, 3 regions; -1 = one closer). */
	void FocusBoard(double Lat, double Lon, int32 Level);
	/** Same level and within about one hexagon of the current centre: a focus there changes nothing (no rebuild). */
	bool IsSameBoardSpot(double Lat, double Lon, int32 Level) const;
	/** FN7 god's hand (Mac session, UI/FNBoardAoe.cpp): drag the board and the land slides with the cursor; it re-centres
	 *  (rebuilds) every two hexes, seamlessly, and the globe turns along. */
	/** FN7 earth playback (Mac session, UI/FNEarthPlay.cpp): the board's centre hex, a hex's place on the planet and the
	 *  table's scale (cm per planet metre). */
	bool BoardCentreHex(int32& OutQ, int32& OutR) const;
	bool BoardHexLatLon(int32 Q, int32 R, double& OutLat, double& OutLon) const;
	/** The exact top of a board tile (cm, as BuildTable stacks it; with or without the earth's override) and its centre. */
	bool BoardHexTop(int32 Q, int32 R, bool bWithEarth, float& OutTopCm, FVector& OutWorld) const;
	double BoardCmPerMeter() const { return TableK; }
	bool IsBoardPanning() const { return bBoardPan; }
	bool BeginBoardPan(const FVector2D& ViewportPixel);
	void PanBoardTo(const FVector2D& ViewportPixel);
	void EndBoardPan();
	/** FN7 hand of god, the planet (docs/design/hand_of_god): the point under the hand stays under the hand
	 *  (FindBetweenNormals from the press-start quaternion), a fling coasts (tau 0.6 s), a press catches it. */
	bool GlobeDirAt(const FVector2D& ViewportPixel, FVector& OutDirWorld, bool bSilhouette) const;
	bool BeginGlobeGrab(const FVector2D& ViewportPixel);
	void GlobeGrabTo(const FVector2D& ViewportPixel);
	void EndGlobeGrab();
	/** A press while the planet or the land coasts: stops it where it is. True if something was coasting. */
	bool CatchHand();
	/** Seconds since the last fling (the FLING cursor shows for 0.25 s). */
	double SinceFling() const { return FPlatformTime::Seconds() - LastFlingAt; }
	/** The plate (fault) lines on the globe and the board: 0 hidden .. 1 glowing. The kit fades them in only while a mega
	 *  is in hand (FFNPowerDragDrop::OnMegaArmed). */
	void SetFaultLinesAlpha(float Alpha);
	/** Double-click a demigod: the table map centred on it at its CURRENT zoom, and the globe turned to face it. */
	void CenterBoardAndGlobe(double Lat, double Lon);
	/** The view to come back to (the kit's duel glide returns here after the fight): the table's centre and whether you
	 *  had picked it. Bodies in UI/FNBoardAoe.cpp (Mac session, backend's go-ahead). */
	void GetViewSpot(double& OutLat, double& OutLon, bool& bOutPicked) const;
	/** Back to a spot saved with GetViewSpot: the same animated slide at the current zoom; following your demigod again
	 *  when you hadn't picked one. */
	void RestoreViewSpot(double Lat, double Lon, bool bPicked);
	/** View changes animate (Chris asked for zoom to expand and contract smoothly): call every frame (the kit's table tick). */
	void TickViewAnim(float Dt);
	/** After a rebuild centred on a point: start the board from how it looked (OldLocal drawn, scale From) and ease it in. */
	void BeginViewAnim(bool bHaveOld, const FVector& OldLocal, const FVector& NewLocal, float ScaleFrom);
	/** Keep the globe facing a point (re-asserted by the HUD while a centring holds). */
	void SetGlobeFocus(double Lat, double Lon);
	/** The table's zoom level now: 0 people (3 m hexes), 1 houses (30 m), 2 villages (3 km), 3 regions (120 km). */
	int32 GetMapLevel() const { return MapLevel; }
	/** Gate renders only (no match needed): the table at a lat/lon and level. */
	void PreviewFocus(double Lat, double Lon, int32 Level);
	/** RESET VIEW: the board back to the regions level centred on your demigod (as at the start of a match). */
	void ResetView();
	/** The point on the floating GLOBE under a viewport pixel (lat/lon on the globe, and the world point), or false;
	 *  and a ring on the globe around a lat/lon at a radius in km (a MEGA power's area while it hovers over the globe). */
	bool GlobePointAtScreen(const FVector2D& ViewportPixel, double& OutLat, double& OutLon, FVector& OutWorld) const;
	void ShowMegaOnGlobe(bool bShow, double Lat, double Lon, double RadiusKm);
	/** Called when YOU click a spot on the Globe (Lon, Lat); the widget casts an armed Entropy power there. */
	TFunction<void(double /*Lon*/, double /*Lat*/)> OnUserPickedSpot;
	void ShowDropSpots(const TArray<FFNPiece>& Spots);
	/** The board: every piece we know about (you, players, villagers, the fallen). */
	void ShowWorld(const TArray<FFNPiece>& Pieces);
	/** Your little settlement grows with the things you build, find and keep. */
	void ShowBuilt(const TArray<FString>& Things);
	/** 4 seats round the table: mine faces the camera, the others clockwise. Other seats show a silhouette
	 *  (gold halo = human, cyan orbiting cubes = AI); every seat has a minifig (or its diorama standee) on the table. */
	void ShowSeats(const TArray<FFNSeatInfo>& Seats);
	/** The table map, web-map style (during a match): zoom in on the spot under the cursor, or out. False if the cursor isn't over the map. */
	bool MapClick(bool bZoomIn);
	/** Centre the table map back on your demigod and follow it again. */
	void MapHome();
	/** A click on a demigod (its marker on the table map, or a seat's minifig/silhouette): centres the map on it and
	 *  fires OnDemigodClicked so the widget can pull up that demigod's card. False if Id isn't a demigod. */
	bool ClickDemigod(FName Id);
	TFunction<void(const FString& /*Ref*/, const FString& /*Name*/)> OnDemigodClicked;
	void ShowRibbons(const TArray<FFNEpisode>& Choices);
	void ChooseRibbon(int32 Index);
	void ClearRibbons();

	static FLinearColor PlayerColor(int32 Index);

	// Atmosphere: sound, the day's crisis played out on the Globe, and the build-up while the day is written
	void PlaySfx(EFNSfx Sfx, float Gain = 1.f, float DelaySeconds = 0.f);
	void PlayHazard(const FString& Hazard);
	void SetFatesDeciding(bool bOn);
	FRotator GetShake() const;
	/** The DM's d20 tumbles onto the table and lands on D20; the line under it shows the bonus and verdict. */
	void RollDice(int32 D20, int32 Mod, const FString& Verdict);
	/** Duel dice: any number of sides (the odds are 1 in N with no ceiling); you need Need or higher. */
	void RollDuelDice(int32 Roll, int32 Sides, int32 Need, bool bSuccess, const FString& Label);
	/** End of match: the Globe flies toward you and replays each day where it happened, one line at a time. */
	void PlayFinalCut(const TArray<FString>& Lines, const TArray<FVector2D>& Spots, const FString& Closing);
	/** 0 = quiet table, 1 = war drums (rises over the days). */
	void SetMusicLevel(float Level) { MusicLevel = FMath::Clamp(Level, 0.f, 1.f); }

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* Box(const FName& Name, const FVector& Loc, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator);
	UStaticMeshComponent* Shape(const TCHAR* MeshPath, const FName& Name, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, USceneComponent* Parent = nullptr);
	UTextRenderComponent* Text(const FName& Name, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color, const FString& Str, USceneComponent* Parent = nullptr);
	UPointLightComponent* Light(const FName& Name, const FVector& Loc, float Intensity, const FLinearColor& Color, float Radius);
	void Spot(const FName& Name, const FVector& Loc, const FRotator& Rot, float Intensity, const FLinearColor& Color);
	void Tag(UStaticMeshComponent* C, FName Id);
	static FString Wrap(const FString& S, int32 Width, int32 MaxLines);
	static FVector LonLatToGlobe(double Lon, double Lat, float R);

	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TObjectPtr<USceneComponent> GlobePivot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> GlobeMesh;
	UPROPERTY() TObjectPtr<USceneComponent> GuardPivot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GuardShards;
	UPROPERTY() TObjectPtr<UPointLightComponent> GlobeLight;
	UPROPERTY() TObjectPtr<USceneComponent> PlayerEaselPivot;
	UPROPERTY() TObjectPtr<UTextRenderComponent> EaselHeader;
	UPROPERTY() TObjectPtr<UTextRenderComponent> EaselBody;
	UPROPERTY() TObjectPtr<UTextRenderComponent> WillLabel;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> StandTexts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ZoneMarkers;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SpotMarkers;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> SelfMarker;
	UPROPERTY() TObjectPtr<USceneComponent> RibbonPivot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Ribbons;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> RibbonTexts;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Candles;
	UPROPERTY() TMap<FName, TObjectPtr<UStaticMeshComponent>> Interactables;

	UPROPERTY() TObjectPtr<UTextRenderComponent> PlayerEaselTextComp;
	UPROPERTY() TObjectPtr<USceneComponent> BoardRoot;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> TileSets;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Trees;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Peaks;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> PieceRoots;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PieceParts;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> PieceLabels;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> HutRoots;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> HutLabels;
	UPROPERTY() TObjectPtr<UTextRenderComponent> ZoneLabel;
	UPROPERTY() TObjectPtr<USceneComponent> DicePivot;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DiceNumber;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DiceLine;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> AudioWave;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioComp;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FxBits;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FxMats;
	UPROPERTY() TObjectPtr<UPointLightComponent> FlashLight;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Halos;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BoltMesh;
	UPROPERTY() TObjectPtr<UPointLightComponent> BoltLight;
	float BoltT = -1.f;
	FVector BoltDir = FVector::UpVector;

	// --- the circle of action: pick a spot on the Globe, that patch of the planet is built on the table ---
	void PickSpot(double Lat, double Lon, bool bByUser);
	void BuildTable();
	void UpdateTable(float Dt);
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> HexSets;   // biome x shade x fog level
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PeopleSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> TownSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> HeadSet;    // heads on the people
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RoofSet;    // roofs on the houses
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> TowerSet;   // buildings / towers
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CoastSet;   // real coastlines
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PlateSet;   // tectonic plate edges
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CitySet;    // real cities over 1 million
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> EarthSets;   // brown terrain sides, per fog level
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> StoneSets;   // grey rock sides, per fog level
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> TrunkSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PineSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> LeafSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RockSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PeakSet;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SnowSet;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PickRing;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PickPin;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BeamRays;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FogPuffs;
	UPROPERTY() TObjectPtr<USpotLightComponent> Projector;
	UPROPERTY() TObjectPtr<UTextRenderComponent> TableInfo;
	UPROPERTY() TObjectPtr<USceneComponent> MapPivot;   // parent of the map's tiles and pieces, scaled for the zoom animation
	float ZoomAnimT = -1.f, ZoomAnimS0 = 1.f;
	FVector ZoomAnchor = FVector::ZeroVector;
	// FN7 zoom ghost (Mac session, UI/FNBoardAoe.cpp, under the view-fix go-ahead; Chris noted the zoom ghosts keep jumping
	// into view"): at a level switch the old level's instanced tiles and scenery stay on screen and move with the same
	// zoom around the cursor (expanding and flattening going in, contracting going out) while the new level grows in.
	void BeginZoomGhost();
	void TickZoomGhost(float NewScale, float Ease);
	void EndZoomGhost();
	void TickGhostTransform(float G, const FVector& Loc, float Flat);
	static void NoteRebuild(const TCHAR* Why);   // "FN7 board rebuilds: N in the last minute"
	UPROPERTY() TObjectPtr<USceneComponent> GhostPivot;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> ZoomGhosts;
	TArray<TArray<FVector>> GhostPos;     // per ghost set: each instance's position (pivot space)
	TArray<TArray<uint8>> GhostHidden;    // per ghost set: dropped once it leaves the table
	TArray<TArray<float>> GhostRadius;    // per ghost set: each instance's half-width
	FTransform GhostBase;
	bool bZoomGhost = false;
	bool bBoardPan = false;
	bool bReliefHold = false;   // FN7 hand: the relief scale is held while panning (FNStudioTable.cpp BuildTable)
	bool bGlobeGrab = false, bGlobeCoast = false, bBoardCoast = false;
	FVector GrabA = FVector::ZeroVector;
	FQuat GrabStartQuat = FQuat::Identity;
	TArray<TPair<double, FQuat>> GrabHist;
	TArray<TPair<double, FVector>> PanHist;
	FVector GlobeOmega = FVector::ZeroVector;   // world axis x rad/s
	FVector BoardVel = FVector::ZeroVector;     // studio cm/s
	FVector PanCommitted = FVector::ZeroVector;
	double LastFlingAt = -10.0, LastGlobeRecentre = 0.0;
	FTSTicker::FDelegateHandle HandTicker;
	void StartHandTicker();
	bool TickHand(float Dt);
	void SettleGlobe(bool bFinal);
	FVector PanFromHit = FVector::ZeroVector;   // studio-local table point the hand took hold of
	bool TablePointAt(const FVector2D& ViewportPixel, FVector& OutLocal) const;
	void CommitBoardPan(const FVector& Offset);
	double PickLat = 46.5, PickLon = 9.0, PickRadiusM = 300000.0;
	// the scene now on the table (set by BuildTable): cube face, centre on that face's plane (m), cm per m, hex size, relief range
	int32 TableFace = -1;
	double TableCx = 0.0, TableCy = 0.0, TableK = 0.0, TableS = 1.0, TableLo = 0.0, TableRange = 1.0;
	struct FHexOverride { bool bHeight = false; float HeightCm = 0.f; EFNHexState State = EFNHexState::Normal; };
	TMap<FString, FHexOverride> HexOverrides;   // the earth simulator's per-hex heights and states (empty today)
	/** The relief at board hex (Q, R): the override if any, else Default. Fast path: no key built while nothing overrides. */
	float BoardHeightAtHex(int32 Q, int32 R, float Default) const { return HexOverrides.Num() == 0 ? Default : BoardHeightAt(BoardCellKey(Q, R), Default); }
	EFNHexState HexStateAtHex(int32 Q, int32 R) const { return HexOverrides.Num() == 0 ? EFNHexState::Normal : HexStateAt(BoardCellKey(Q, R)); }
	void PlaceTableHeroes();   // demigods standing on the table map at true scale
	bool MapPointUnderCursor(double& Px, double& Py) const;   // the face-plane point (m) under the mouse, on top of the hexes
	void ZoomMapAt(double Px, double Py, int32 Step);          // one zoom level (-1 closer), keeping that point under the cursor
	float NextZoomAt = 0.f;                                     // earliest time the next zoom step may happen
	int32 MapLevel = 3;                                         // 0 people, 1 houses, 2 villages, 3 regions (FNStudioTable.cpp)
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HeroMarks;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> HeroLabels;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HeroBeams;   // a coloured light column over each demigod
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HeroRings;   // a coloured disc at its feet
	TArray<FFNPiece> HeroShown;                                          // which piece each marker shows now
	void CenterMapOn(const FString& Ref, const FString& Name);
	FVector PickDir = FVector::ZeroVector;
	bool bUserPicked = false;
	float RebuildIn = -1.f;
	// spinning the Globe by dragging it
	bool bGlobeDrag = false;
	FVector2D DragLast = FVector2D::ZeroVector;
	float DragMoved = 0.f, ManualUntil = -1.f;
	// every scene shown, by planet cell (also appended to Saved/GlobeScenes.txt)
	TMap<FString, int32> SceneVisits;

	// --- the seats (FNStudioSeats.cpp) ---
	struct FSeatSlot
	{
		USceneComponent* Body = nullptr;          // the silhouette behind the table (other seats only)
		UStaticMeshComponent* Face = nullptr;     // portrait plane over the head
		TArray<UStaticMeshComponent*> Aura;       // halo beads (human) or orbiting cubes (AI)
		UPointLightComponent* Glow = nullptr;
		UTextRenderComponent* Label = nullptr;
		USceneComponent* Fig = nullptr;           // the minifig on the table
		UStaticMeshComponent* FigBody = nullptr;
		UStaticMeshComponent* FigHead = nullptr;
		UStaticMeshComponent* FigBase = nullptr;
		UStaticMeshComponent* Standee = nullptr;  // diorama image
		UTextRenderComponent* FigLabel = nullptr;
		FFNSeatInfo Info;
		bool bUsed = false;
	};
	void BuildSeats();
	void UpdateSeats(float Dt);
	TArray<FSeatSlot> SeatSlots;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> SeatParts;   // keeps every seat component alive for GC
	UPROPERTY() TObjectPtr<UMaterialInterface> ImageMat;          // engine pass-through material with a texture slot

	// --- sound (a tiny software synth feeding one procedural voice) ---
	struct FVoice { const TArray<float>* Samples = nullptr; int32 Pos = 0; int32 Delay = 0; float Gain = 1.f; };
	void BuildSounds();
	void PumpAudio();
	TMap<uint8, TArray<float>> SfxBank;
	TArray<FVoice> Voices;
	FRandomStream Rng;
	int64 SampleClock = 0;
	float Lp1 = 0.f, Lp2 = 0.f, Lp3 = 0.f, Lp4 = 0.f, CrackleEnv = 0.f;
	float FatesAmt = 0.f, FatesTarget = 0.f;

	// --- typewriter on the tablet ---
	FString TypeFull;
	float TypeShown = 0.f;
	int32 TypeLastQuill = 0;

	// --- crisis effects ---
	int32 FxKind = 0;      // 0 none, 1 meteors, 2 locusts, 3 dinosaurs, 4 other
	float FxT = 0.f, FxDur = 0.f;
	TArray<FVector> FxPos, FxVel, FxParam;
	float FlashAmt = 0.f, ShakeAmt = 0.f, LastImpact = -10.f;
	int32 StompsDone = 0;
	void UpdateFx(float Dt);
	void ResetFxBit(int32 i);

	// --- the globe board (a Polytopia-style tile world) ---
	void BuildBoard();
	void PaintTiles();
	void PlacePiece(int32 Index, const FFNPiece& P);
	void UpdateLabels();
	FVector BoardDir(double Lon, double Lat) const { return LonLatToGlobe(Lon, Lat, 1.f); }
	FVector BoardToActor(const FVector& LocalDir, float Radius) const;
	struct FTile { FVector Dir; FVector East; uint8 Biome; uint8 Shade; float H; float W; float D; };
	TArray<FTile> Tiles;
	TArray<FVector> PieceDirs;
	TArray<int32> PieceKinds;
	TArray<FFNPiece> WorldPieces, DropPieces;
	FVector ZoneDir = FVector::ZeroVector;
	double ZoneAng = PI;
	FVector FocusDir = FVector::ZeroVector;
	bool bHasFocus = false;
	FVector FxCenter = FVector(0.f, -10.f, 172.f);
	static constexpr float BoardR = 52.f;

	// dice
	float DiceT = -1.f;
	FVector DiceSpin = FVector::ZeroVector;
	int32 DiceSounds = 0;
	bool bDiceGood = true;
	void UpdateDice(float Dt);
	// the Final Cut replay
	TArray<FString> CutLines;
	TArray<FVector2D> CutSpots;
	FString CutClosing;
	float CutT = -1.f, CutZoom = 0.f;
	int32 CutIdx = -1;
	// music
	float MusicLevel = 0.f;

	TMap<FName, FVector> BaseScale;
	FName Hovered;
	float Time = 0.f;
	float NextBlitz = 3.72f;
	float BlitzT = -1.f;
	bool bBlitz = true;
	bool bGuardLocked = false;
	bool bWillActive = false;
	float EaselDownT = -1.f;
};
