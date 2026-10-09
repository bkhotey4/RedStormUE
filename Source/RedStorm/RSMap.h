// 地圖：生成、地形資料、礦石（對應 Godot map.gd 的資料部分；3D 呈現在 RSTerrain）
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "RSDefs.h"

class ARSGame;

// 簡易可種子化的梯度雜訊 + fBm（取代 Godot 的 FastNoiseLite）
class FRSNoise
{
public:
	void Init(int32 Seed, float Frequency, int32 Octaves = 1);
	float Get2D(float X, float Y) const;
	float Get1D(float X) const { return Get2D(X, 0.37f); }
private:
	float Perlin(float X, float Y) const;
	uint8 Perm[512];
	float Freq = 0.05f;
	int32 Oct = 1;
	FVector2D Offset = FVector2D::ZeroVector;
};

struct FRSTree { FIntPoint C; int32 Var = 0; FString Kind; };
struct FRSRock { FIntPoint C; int32 Var = 0; };
struct FRSCivSpot { FString Id; FIntPoint TL; int32 Var = 0; };

class REDSTORM_API FRSMap
{
public:
	static constexpr uint8 T_BASE = 0;
	static constexpr uint8 T_DIRT = 1;
	static constexpr uint8 T_ALT = 2;
	static constexpr uint8 T_WATER = 3;
	static constexpr uint8 T_ROCK = 4;
	static constexpr uint8 T_ROAD = 5;
	static constexpr int32 ORE_MAX = 500;

	int32 W = 72, H = 72;
	FString Theme = TEXT("temperate");
	FString Layout = TEXT("plains");
	TArray<uint8> Terrain, Gem, TreeAt, Bridge, Elev, Cliff;
	TArray<int32> Ore;
	TArray<float> VH;
	TArray<FRSTree> Trees;
	TArray<FRSRock> Rocks;
	TArray<FIntPoint> Starts;
	TArray<FIntPoint> Oils;
	TArray<FRSCivSpot> Civs;
	TSet<int32> OreCells;
	bool bOreDirty = true;
	float RegrowT = 0.f;
	ARSGame* Game = nullptr;
	FRandomStream Rng;

	int32 Idx(int32 X, int32 Y) const { return Y * W + X; }
	bool Inb(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < W && Y < H; }
	bool Passable(int32 X, int32 Y) const;
	bool Buildable(int32 X, int32 Y) const { return Passable(X, Y); }
	int32 ElevAt(const FVector2D& P) const;
	float VtxH(int32 VX, int32 VY) const;
	float HeightAt(const FVector2D& P) const;
	bool IsRamp(int32 X, int32 Y) const;
	bool IsWater(int32 X, int32 Y) const { return Inb(X, Y) && Terrain[Idx(X, Y)] == T_WATER; }

	void Generate(const FRSMapPreset& Preset, int32 Players, int32 Seed);

	// 預覽 / 小地圖（BGRA，W*H）
	TArray<FColor> MakePreview(bool bShowStarts) const;
	FLinearColor BaseColor(int32 I) const;

	void Update(float Dt);
	int32 TakeOre(int32 X, int32 Y, int32 Amt);
	bool HasOre(int32 X, int32 Y) const { return Inb(X, Y) && Ore[Idx(X, Y)] > 0; }
	FIntPoint FindOreNear(const FVector2D& From, float MaxD = 40.f) const;
	void ClearOre(int32 X, int32 Y);
	bool NearType(int32 X, int32 Y, uint8 T, int32 R) const;

private:
	void BuildVH();
	void MakePlateaus();
	void MakeRoads();
	bool CivAt(int32 X, int32 Y) const;
	void MakeTowns();
	void CarveRiver(const FRSNoise& HN);
	TArray<uint8> DilateMask(uint8 T, int32 R) const;
	TArray<uint8> CircleMask(const TArray<FIntPoint>& Centers, float R, FVector2D Off = FVector2D::ZeroVector) const;
	bool NearStart(int32 X, int32 Y, float R) const;
	bool NearOil(int32 X, int32 Y) const;
	void ClearArea(FIntPoint C, float R);
	void OreBlob(FVector2D C, float R, bool bGem);
	void EnsureConnectivity();
	TArray<uint8> Flood(FIntPoint A) const;
	void CarveLine(FIntPoint A, FIntPoint B);
};
