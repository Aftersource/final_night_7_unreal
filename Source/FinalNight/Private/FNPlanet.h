// Planet math for the globe table (ported from the browser prototype client/globe/cells.js).
// One stable address for every spot on the planet at every zoom, and terrain that is a pure function of
// position, so the same spot always shows the same ground and people, and revisits match.
//  - Index: cube-sphere quadtree (like Google S2): 6 faces, each level splits a cell in 4; level 30 ~ 1 cm.
//  - Hexes: a hex lattice laid on the cube face's flat plane, in meters; (face, size, q, r) is a hex's identity.
//  - The coastline follows the real-Earth land mask the Globe uses (FNLand); noise adds hills and detail.
#pragma once
#include "CoreMinimal.h"
#include "FNLandMask.h"

namespace FNPlanet
{
	static constexpr double RadiusM = 12742000.0;     // "Super Earth": twice Earth's radius
	static constexpr double Population = 14e12;       // ~14 trillion people
	static constexpr double LandFraction = 0.35;
	static constexpr int32 MaxLevel = 30;

	// browser convention: y is up (north), x/z the equator
	inline FVector3d LatLonToVec(double Lat, double Lon)
	{
		const double La = FMath::DegreesToRadians(Lat), Lo = FMath::DegreesToRadians(Lon);
		return FVector3d(FMath::Cos(La) * FMath::Cos(Lo), FMath::Sin(La), -FMath::Cos(La) * FMath::Sin(Lo));
	}
	inline void VecToLatLon(const FVector3d& V, double& Lat, double& Lon)
	{
		const double N = V.Size();
		Lat = FMath::RadiansToDegrees(FMath::Asin(V.Y / N));
		Lon = FMath::RadiansToDegrees(FMath::Atan2(-V.Z, V.X));
	}
	inline void VecToFaceUV(const FVector3d& V, int32& Face, double& U, double& W)
	{
		const double Ax = FMath::Abs(V.X), Ay = FMath::Abs(V.Y), Az = FMath::Abs(V.Z);
		if (Ax >= Ay && Ax >= Az) { if (V.X > 0) { Face = 0; U = -V.Z / Ax; W = V.Y / Ax; } else { Face = 1; U = V.Z / Ax; W = V.Y / Ax; } return; }
		if (Ay >= Az) { if (V.Y > 0) { Face = 2; U = V.X / Ay; W = -V.Z / Ay; } else { Face = 3; U = V.X / Ay; W = V.Z / Ay; } return; }
		if (V.Z > 0) { Face = 4; U = V.X / Az; W = V.Y / Az; } else { Face = 5; U = -V.X / Az; W = V.Y / Az; }
	}
	inline FVector3d FaceUVToVec(int32 Face, double U, double W)
	{
		switch (Face)
		{
		case 0: return FVector3d(1, W, -U);
		case 1: return FVector3d(-1, W, U);
		case 2: return FVector3d(U, 1, -W);
		case 3: return FVector3d(U, -1, W);
		case 4: return FVector3d(U, W, 1);
		default: return FVector3d(-U, W, -1);
		}
	}
	inline FString CellKey(double Lat, double Lon, int32 Level)
	{
		int32 Face; double U, W;
		VecToFaceUV(LatLonToVec(Lat, Lon), Face, U, W);
		double S = (U + 1) / 2, T = (W + 1) / 2;
		FString Key = FString::Printf(TEXT("%d/"), Face);
		for (int32 i = 0; i < Level; ++i)
		{
			S *= 2; T *= 2;
			const int32 Bs = FMath::Min(1, FMath::FloorToInt(S)), Bt = FMath::Min(1, FMath::FloorToInt(T));
			Key.AppendInt(Bs + 2 * Bt); S -= Bs; T -= Bt;
		}
		return Key;
	}
	inline int32 LevelForSize(double M) { return FMath::Clamp(FMath::RoundToInt(FMath::Log2(2 * RadiusM / M)), 0, MaxLevel); }

	// hex lattice on a face plane (pointy-top axial coords, size = centre-to-corner in face meters)
	inline double HexSizeFor(double SceneRadiusM, double Across) { return FMath::Pow(2.0, FMath::RoundToDouble(FMath::Log2(FMath::Max(0.01, SceneRadiusM / Across)))); }
	inline FVector2D HexToPlane(int32 Q, int32 R, double S) { return FVector2D(S * FMath::Sqrt(3.0) * (Q + R / 2.0), S * 1.5 * R); }
	inline void PlaneToHex(double X, double Y, double S, int32& Q, int32& R)
	{
		const double Qf = (FMath::Sqrt(3.0) / 3 * X - Y / 3) / S, Rf = (2.0 / 3 * Y) / S, Sf = -Qf - Rf;
		int32 Qi = FMath::RoundToInt(Qf), Ri = FMath::RoundToInt(Rf); const int32 Si = FMath::RoundToInt(Sf);
		const double Dq = FMath::Abs(Qi - Qf), Dr = FMath::Abs(Ri - Rf), Ds = FMath::Abs(Si - Sf);
		if (Dq > Dr && Dq > Ds) Qi = -Ri - Si; else if (Dr > Ds) Ri = -Qi - Si;
		Q = Qi; R = Ri;
	}
	inline FVector3d PlaneToVec(int32 Face, double X, double Y) { return FaceUVToVec(Face, X / RadiusM, Y / RadiusM).GetSafeNormal(); }

	// ---- deterministic terrain ----
	inline double Hash3(int32 X, int32 Y, int32 Z, int32 Seed)
	{
		uint32 H = ((uint32)X * 374761393u) ^ ((uint32)Y * 668265263u) ^ ((uint32)Z * 2147483647u) ^ ((uint32)Seed * 1274126177u);
		H = (H ^ (H >> 13)) * 1274126177u;
		return (double)(H ^ (H >> 16)) / 4294967296.0;
	}
	inline double ValueNoise(double X, double Y, double Z, int32 Seed)
	{
		const double Xi = FMath::FloorToDouble(X), Yi = FMath::FloorToDouble(Y), Zi = FMath::FloorToDouble(Z);
		auto Fade = [](double T) { return T * T * (3 - 2 * T); };
		const double Xf = Fade(X - Xi), Yf = Fade(Y - Yi), Zf = Fade(Z - Zi);
		const int32 Ix = (int32)Xi, Iy = (int32)Yi, Iz = (int32)Zi;
		auto H = [&](int32 i, int32 j, int32 k) { return Hash3(Ix + i, Iy + j, Iz + k, Seed); };
		auto L = [](double A, double B, double T) { return A + (B - A) * T; };
		return L(L(L(H(0, 0, 0), H(1, 0, 0), Xf), L(H(0, 1, 0), H(1, 1, 0), Xf), Yf),
			L(L(H(0, 0, 1), H(1, 0, 1), Xf), L(H(0, 1, 1), H(1, 1, 1), Xf), Yf), Zf) * 2 - 1;
	}
	/** 0 = open sea, 1 = inland, from the same land mask the Globe is painted with (smoothed over ~1.5 deg). */
	inline double Landness(double Lat, double Lon)
	{
		int32 N = 0;
		for (int32 i = -1; i <= 1; ++i) for (int32 j = -1; j <= 1; ++j)
			N += FNLand::IsLand(Lon + i * 0.75, FMath::Clamp(Lat + j * 0.75, -89.9, 89.9)) ? 1 : 0;
		return N / 9.0;
	}
	/** Elevation in meters; octaves stop at FinestM so a coarse and a close view of the same spot agree. */
	inline double ElevationM(const FVector3d& V, double FinestM, double Lat, double Lon)
	{
		// octaves sum to at most ~2x Amp, so land sits mostly 100..2500 m (was 2600: most land came out above the snow line)
		double E = 0, Wl = 4000000.0, Amp = 900.0; int32 i = 0;
		while (Wl >= FinestM && i < 26)
		{
			const double F = RadiusM / Wl;
			E += Amp * ValueNoise(V.X * F + i * 17.1, V.Y * F - i * 3.7, V.Z * F + i * 9.3, 7 + i);
			Wl /= 2; Amp *= 0.5; ++i;
		}
		return E + FMath::Lerp(-1500.0, 500.0, Landness(Lat, Lon));
	}
	/** Height (m) above which ground is snow: ~4800 m at the equator, sea level at 70 deg. */
	inline double SnowLineM(double Lat) { return 4800.0 * FMath::Max(0.0, 1.0 - FMath::Abs(Lat) / 70.0); }
	enum EBiome : uint8 { Ocean, Beach, Grass, Forest, Rock, Snow, NumBiomes };
	inline EBiome BiomeOf(double Elev, double Lat)
	{
		if (Elev < 0) return Ocean;
		if (Elev < 40) return Beach;
		const double Line = SnowLineM(Lat);
		if (Elev > Line) return Snow;
		if (Elev > FMath::Min(2000.0, Line - 600.0)) return Rock;
		return Elev > 500 ? Forest : Grass;
	}
	inline const TCHAR* BiomeName(int32 B) { static const TCHAR* N[NumBiomes] = { TEXT("ocean"), TEXT("beach"), TEXT("grass"), TEXT("forest"), TEXT("rock"), TEXT("snow") }; return N[B]; }
	inline double PeopleIn(EBiome B, double AreaM2, const FVector3d& V)
	{
		static const double Factor[NumBiomes] = { 0, 1.2, 1.6, 0.6, 0.25, 0.05 };
		const double Mean = Population / (4 * PI * RadiusM * RadiusM * LandFraction);
		const double City = 0.2 + 3 * FMath::Square(FMath::Max(0.0, ValueNoise(V.X * 300, V.Y * 300, V.Z * 300, 99)));
		return Mean * Factor[B] * City * AreaM2;
	}
}
