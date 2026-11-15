#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class SBox;
class STreeCanvas;

/**
 * Tree of consequence (for someone like a judge asking what the data structure is ... a beautifully
 * rendered exact tree of how precisely our memory hierarchical system works, recall for various granular facts, density
 * of detail"). The server's game.memory_tree answer drawn as it is (CLIENT_API "Tree of consequence"):
 *  - the tiers world -> region -> settlement -> 3 km -> 30 m -> 3 m (+ a demigod's story), the society branch
 *    (district -> crowd -> cell -> person) beside the hexes; node size = facts_total (log), glow = recency, the fill =
 *    density of detail (log10 facts per km2, one ramp for every tier), edges = the roll-ups (thickness = facts);
 *  - click a node: its card (counts, density, kinds, samples, its latest roll-up);
 *  - RECALL PATHS: pick one, it lights its nodes from the world down and plays its chain
 *    (the key -> the stored die -> the memory entry -> the roll-ups -> the prompt lines that use it now);
 *  - the totals strip.
 * One look (the star map). Esc or CLOSE closes it; it never touches the board.
 */
class FINALNIGHT_API SFNMemoryTree : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFNMemoryTree) {}
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** The game.memory_tree answer. */
	void SetData(const TSharedPtr<FJsonObject>& D);
	void SetWaiting(const FString& What) { Waiting = What; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& K) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }

	struct FNode
	{
		TSharedPtr<FJsonObject> J;
		FString Id, Name, Kind, Store;
		int32 Level = 0;
		double Facts = 0.0, LastBeat = 0.0, Density = 0.0;
		bool bOnPath = false, bCollapsed = false;
		int32 Parent = -1;
		FVector2D At = FVector2D::ZeroVector;   // 0..1 in the canvas
	};

private:
	FSimpleDelegate OnClose;
	TSharedPtr<FJsonObject> Data;
	TArray<FNode> Nodes;
	TArray<FString> LevelNames;
	double Beat = 0.0;
	int32 Selected = -1;
	int32 Path = -1;
	double PathAt = 0.0;
	FString Waiting = TEXT("Asking the server for the memory...");
	TSharedPtr<SBox> CardBox, PathsBox, ChainBox;
	TSharedPtr<STreeCanvas> Canvas;

	void Flatten(const TSharedPtr<FJsonObject>& N, int32 Parent);
	void Layout();
	void Select(int32 i);
	void PickPath(int32 p);
	TSharedRef<SWidget> MakeCard() const;
	TSharedRef<SWidget> MakePaths();
	TSharedRef<SWidget> MakeChain() const;
	FText HeaderLine() const;
	FText TotalsLine() const;
	friend class STreeCanvas;
};
