#pragma once

#include "CoreMinimal.h"

/**
 * The server's hex cell "<face>:<level>:<q>:<r>" back to lon/lat (docs/CLIENT_API.md, "cell": a gnomonic cube face,
 * plane metres X = U*R, Y = W*R, pointy-top axial hexes of 3 m / 30 m / 3 km / 120 km). This is the exact inverse of the
 * server's hexgrid.rs, checked against its cross-check table (Paris, New York, Sydney, Tokyo round-trip).
 */
namespace FNCellGeo
{
	inline double LevelSizeM(const FString& Level)
	{
		if (Level == TEXT("people")) return 3.0;
		if (Level == TEXT("houses")) return 30.0;
		if (Level == TEXT("villages") || Level == TEXT("village")) return 3000.0;
		if (Level == TEXT("regions") || Level == TEXT("region")) return 120000.0;
		return 0.0;
	}

	inline bool CellToLonLat(const FString& Cell, double& OutLon, double& OutLat)
	{
		TArray<FString> P;
		Cell.ParseIntoArray(P, TEXT(":"));
		if (P.Num() != 4) return false;
		const int32 Face = FCString::Atoi(*P[0]);
		const double S = LevelSizeM(P[1]);
		if (S <= 0.0 || Face < 0 || Face > 5) return false;
		const double Q = FCString::Atod(*P[2]), R = FCString::Atod(*P[3]);
		const double Rm = 6371000.0;
		const double U = S * FMath::Sqrt(3.0) * (Q + R / 2.0) / Rm, W = S * 1.5 * R / Rm;
		double X = 0, Y = 0, Z = 0;
		switch (Face)
		{
		case 0: X = 1;  Y = W; Z = -U; break;
		case 1: X = -1; Y = W; Z = U; break;
		case 2: X = U;  Y = 1; Z = -W; break;
		case 3: X = U;  Y = -1; Z = W; break;
		case 4: X = U;  Y = W; Z = 1; break;
		default: X = -U; Y = W; Z = -1; break;
		}
		const double N = FMath::Sqrt(X * X + Y * Y + Z * Z);
		X /= N; Y /= N; Z /= N;
		OutLat = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Y, -1.0, 1.0)));
		OutLon = FMath::RadiansToDegrees(FMath::Atan2(-Z, X));
		return true;
	}
}
