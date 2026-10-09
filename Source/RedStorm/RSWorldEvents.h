// 混合戰爭隨機時事事件（遭遇戰）：制裁、駭客、輿論、油價、軍援、罷工……以 RSN 快訊播報
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RSDefs.h"
#include "RSWorldEvents.generated.h"

class ARSGame;
class URSPlayer;

struct FRSActiveEvent
{
	FString Id, Name;
	float Until = 0.f;
	TWeakObjectPtr<URSPlayer> Target;
	bool bGlobal = false;
};

UCLASS()
class REDSTORM_API URSWorldEvents : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	float NextT = 150.f;
	TArray<FRSActiveEvent> Active;
	FString Banner;
	float BannerT = 0.f;
	TMap<int32, float> CostMod;
	TMap<int32, float> SpeedMod;
	TArray<FString> Hist;

	void Setup(ARSGame* G);
	bool Enabled() const;
	void Update(float Dt);
	float CostMult(const URSPlayer* P) const;
	float SpeedMult(const URSPlayer* P) const;
	void Show(const FString& Txt);
	void FireRandom();
	void Fire(const FRSEventDef& Ev, URSPlayer* Target);
	void EndEvent(const FRSActiveEvent& E);
	float OilBonus(const URSPlayer* P) const;
	TArray<FString> StatusLines() const;
};
