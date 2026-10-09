// 格狀 A* 尋路（對角線只在兩側皆無障礙時允許；八方向距離啟發式；可回傳最接近的部分路徑）
#pragma once

#include "CoreMinimal.h"

class REDSTORM_API FRSGridPath
{
public:
	void Init(int32 InW, int32 InH);
	bool IsSolid(int32 X, int32 Y) const { return X < 0 || Y < 0 || X >= W || Y >= H || Solid[Y * W + X] != 0; }
	void SetSolid(int32 X, int32 Y, bool b) { if (X >= 0 && Y >= 0 && X < W && Y < H) Solid[Y * W + X] = b ? 1 : 0; }
	// 回傳包含起點與終點的格子路徑；找不到時回傳最接近終點的部分路徑（bPartial）
	TArray<FIntPoint> FindPath(FIntPoint A, FIntPoint B, bool bPartial = true);

private:
	int32 W = 0, H = 0;
	TArray<uint8> Solid;
	TArray<float> G;
	TArray<int32> Parent;
	TArray<uint32> Stamp;
	TArray<uint8> Closed;
	uint32 CurStamp = 1;
};
