// 玩家資料：資金、電力、生產佇列、特殊武器、外交關係
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RSPlayer.generated.h"

class ARSGame;
struct FRSDef;

struct FRSQueueItem
{
	FString Id;
	float Progress = 0.f;
	float Spent = 0.f;
};

struct FRSStats
{
	int32 Built = 0, Killed = 0, Lost = 0, BldLost = 0;
	double Harvested = 0.0;
};

UCLASS()
class REDSTORM_API URSPlayer : public UObject
{
	GENERATED_BODY()
public:
	int32 Index = 0;
	FString Name;
	FString Faction = TEXT("allies");
	FString Country;
	FLinearColor Color = FLinearColor::White;
	int32 Team = 0;
	bool bAI = false;
	bool bNeutral = false;
	bool bAlive = true;
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	double Money = 10000.0;
	float PowerProd = 0.f, PowerUse = 0.f;
	bool bLowPower = false;
	TMap<FString, TArray<FRSQueueItem>> Queues;
	TMap<FString, FString> Ready;
	TMap<FString, float> Powers;
	float SanctionedUntil = -100.f, EnergyCutUntil = -100.f, SabotageUntil = -100.f, NoTreatyUntil = -100.f;
	float RadarJamUntil = -100.f;   // 網路作戰：雷達被駭（小地圖失效）
	bool bVetBonus = false;
	TMap<int32, float> Relations;
	TSet<int32> Allies;
	TMap<int32, float> Ceasefire;
	TMap<int32, FString> Proposal;
	FIntPoint StartCell = FIntPoint::ZeroValue;
	float IncomeMult = 1.f, BuildMult = 1.f;
	TMap<FString, TWeakObjectPtr<class ARSBuilding>> Primary;
	FRSStats Stats;
	float FundsWarned = 0.f;
	float LastUnderAttack = -100.f;
	bool bPowerDirty = true;
	bool bWasLow = false;
	int32 AllyHits = 0;

	void InitQueues();
	bool IsEnemyOf(const URSPlayer* Other) const;
	float Relation(const URSPlayer* Other) const;
	void AddRelation(const URSPlayer* Other, float D);
	bool HasRole(const FString& Role) const;
	int32 CountRole(const FString& Role) const;
	int32 CountType(const FString& Id) const;
	static FString ProducerRole(const FRSDef& Def);
	static FString QueueCat(const FRSDef& Def);
	TArray<FString> TechFactions(const FString& Role) const;
	FString MissingReq(const FString& Id) const;
	bool Available(const FString& Id) const;
	bool CanBuild(const FString& Id) const;
	bool Enqueue(const FString& Id);
	float CostOf(const FString& Id) const;
	FString CountryName() const;
	bool Cancel(const FString& Id);
	float ProgressOf(const FString& Id) const;
	int32 QueuedCount(const FString& Id) const;
	void RecalcPower();
	void Update(float Dt);
	bool Sanctioned() const;
	float IncomeFactor() const { return IncomeMult * (Sanctioned() ? 0.4f : 1.f); }
	TArray<FString> PowerList() const;
	FString GetReady(const FString& Cat) const { const FString* R = Ready.Find(Cat); return R ? *R : FString(); }
	int32 NumUnits() const;
	int32 NumBuildings() const;
};
