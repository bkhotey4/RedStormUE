// 電腦 AI：建設基地、採礦、生產、進攻、防守、騷擾、超級武器
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RSAI.generated.h"

class ARSGame;
class URSPlayer;
class ARSUnit;
class ARSBuilding;
class ARSEntity;

UCLASS()
class REDSTORM_API URSAI : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	UPROPERTY() TObjectPtr<URSPlayer> P = nullptr;
	int32 Diff = 1;
	FString Pre = TEXT("a_");
	float ThinkT = 0.f;
	TArray<TWeakObjectPtr<ARSUnit>> AttackGroup;
	float NextAttack = 240.f;
	int32 AttackSize = 8;
	int32 Wave = 0;
	TSet<FString> ReadyPowers;
	float LastDefend = -100.f;
	float EngT = 120.f;
	float RetargetT = 0.f;
	int32 DeployTries = 0;
	float GarrisonT = 60.f;
	bool bNoBuild = false;
	bool bPassive = false;
	bool bDroneFocus = false;
	float WallT = 150.f;
	int32 WaterOk = -1;
	float RaidT = 240.f;
	TArray<TWeakObjectPtr<ARSUnit>> RaidGroup;

	void Setup(ARSGame* G, URSPlayer* Pl, int32 D);
	void Update(float Dt);
	void OnUnitBuilt(ARSUnit* U);
	void OnAttacked(ARSEntity* Victim, ARSEntity* Attacker);
	void PowerReady(const FString& Kind) { ReadyPowers.Add(Kind); }
	FVector2D BasePos() const;

private:
	bool NavalOk();
	TArray<URSPlayer*> EnemyPlayers() const;
	FVector2D EnemyDir() const;
	FVector2D RallyPos() const { return BasePos() + EnemyDir() * 6.f; }
	void ManageMcv();
	FString RoleId(const FString& Role) const;
	void ManageBuildings();
	void PlaceReady();
	FIntPoint FindSpot(const FString& Id) const;
	void ManageWalls();
	bool MarginFree(FIntPoint TL, FIntPoint Sz) const;
	void ManageProduction();
	int32 NavalCount() const;
	FString Pick(const TArray<TPair<FString, int32>>& Opts) const;
	ARSBuilding* NearestOil() const;
	bool IsCombat(const ARSUnit* U) const;
	void ManageArmy();
	void ManageRaid();
	void TryGarrison(const TArray<ARSUnit*>& Home);
	void LaunchAttack(const TArray<ARSUnit*>& Home);
	ARSEntity* PickTarget(const FVector2D& From, bool bPreferDef) const;
	bool EnemyHasDrones() const;
	void ManageSuper();
	void ManageRepair();
};
