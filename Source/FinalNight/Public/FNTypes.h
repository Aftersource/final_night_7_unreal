#pragma once

#include "CoreMinimal.h"

// Plain data models that mirror the Final Night server protocol.

struct FFNPlayer
{
	FString Uuid;
	FString Name;
	int32 Score = 0;
	FString Location;      // short description
	FString LocationLong;  // full description
	FString Latitude;
	FString Longitude;
	TArray<FString> Wills; // WILLsPerRound
};

struct FFNFoundItem
{
	FString Item;
	float Ratio = 0.f;     // share of the 5 inference passes that found it
	TArray<FString> Descriptions;
};

struct FFNEpisode
{
	FString EpisodeUuid;
	FString RoundOutcome;
	FString LocalExperience;
	FString EpisodeDescription;
	bool bUpside = true;   // came from selectableUpEpisodes vs selectableDownEpisodes
};

/** One "take" = one round, as recorded for the Director's Cut reel. */
struct FFNTake
{
	int32 Round = 0;
	FString Will;
	FString Primer;
	TArray<FFNFoundItem> Items;
	TArray<FFNEpisode> Episodes;
	int32 ChosenEpisode = INDEX_NONE;
};
