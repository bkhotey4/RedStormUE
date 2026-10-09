// 戰役任務（基礎戰役 + 混合戰爭戰役）
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RSMission.generated.h"

class ARSGame;
class URSPlayer;
class ARSBuilding;

struct FRSBaseItem
{
	FString Id;
	FIntPoint Off;
};

UCLASS()
class REDSTORM_API URSMission : public UObject
{
	GENERATED_BODY()
public:
	FString Id;
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	UPROPERTY() TObjectPtr<URSPlayer> Enemy = nullptr;
	int32 SpyHits = 0;
	bool bSanctionFired = false;
	float WaveT = 0.f;
	int32 Waves = 0;

	void Setup(ARSGame* G, const FString& Mid);
	void OnSpy(ARSBuilding* B) { SpyHits++; }
	void OnPower(const FString& Kind) { if (Kind == TEXT("sanction")) bSanctionFired = true; }
	// "win" / "lose" / ""
	FString Check();

private:
	void SetAI(bool bNoBuild, bool bPassive);
	void SpawnMcv(URSPlayer* P);
	void BuildBase(URSPlayer* P, const TArray<FRSBaseItem>& List);
	void Landing();
	int32 CountBld(URSPlayer* P) const;
};
