// 存檔 / 讀檔：Saved/RedStorm/saves/slotN.json
// 做法：記錄開局設定（含地圖種子），讀檔時先用同樣設定重建地圖，再清掉所有單位/建築並從存檔還原。
#pragma once

#include "CoreMinimal.h"

class ARSGame;
class FJsonObject;

struct FRSSlotInfo
{
	bool bValid = false;
	FString Title, Date, MapName;
	float Time = 0.f;
};

namespace RSSave
{
	constexpr int32 SLOTS = 3;
	FString SlotPath(int32 N);
	FRSSlotInfo SlotInfo(int32 N);
	bool SaveGame(ARSGame* Game, int32 N);
	TSharedPtr<FJsonObject> LoadData(int32 N);
	void Apply(ARSGame* Game, const TSharedPtr<FJsonObject>& D);
}
