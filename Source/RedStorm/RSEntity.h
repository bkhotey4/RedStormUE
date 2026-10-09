// 所有單位與建築的共同基底
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RSDefs.h"
#include "RSEntity.generated.h"

class ARSGame;
class URSPlayer;
class UStaticMeshComponent;
class UPointLightComponent;

UCLASS(Abstract)
class REDSTORM_API ARSEntity : public AActor
{
	GENERATED_BODY()
public:
	ARSEntity();

	int32 EntId = 0;
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	UPROPERTY() TObjectPtr<URSPlayer> Player = nullptr;
	const FRSDef* Def = nullptr;
	FString TypeId;
	bool bIsBuilding = false;
	float Hp = 100.f, MaxHp = 100.f;
	FVector2D Pos = FVector2D::ZeroVector;
	bool bDead = false;
	bool bSelected = false;
	FString Armor = TEXT("heavy");
	float Sight = 5.f;
	float LastHit = -100.f;
	TWeakObjectPtr<ARSEntity> LastAttacker;
	int32 Kills = 0;
	int32 Rank = 0;
	float FlashT = 0.f;
	bool bShown = true;            // 對人類玩家可見（已探索）
	float DeathTime = -1.f;

	// 視覺元件（由 RSVisuals 建立）
	UPROPERTY() TObjectPtr<USceneComponent> Root = nullptr;
	UPROPERTY() TObjectPtr<USceneComponent> Body = nullptr;
	UPROPERTY() TObjectPtr<USceneComponent> TurretPivot = nullptr;
	UPROPERTY() TObjectPtr<USceneComponent> Spinner = nullptr;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> TeamParts;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Limbs;
	UPROPERTY() TObjectPtr<UPointLightComponent> NightLight = nullptr;   // 建築夜間照明   // 步兵雙腿（走路動畫）
	TArray<float> TeamShade;       // 每個隊伍色零件的明暗（<0 變暗，>0 變亮）
	float VisualTop = 50.f;        // 頭頂高度（血條位置）
	float PickRadius = 40.f;       // 點選半徑（uu）

	FLinearColor TC() const;
	virtual bool IsAir() const { return false; }
	bool IsEnemy(const ARSEntity* O) const;
	virtual float DistTo(const FVector2D& P) const { return FVector2D::Distance(P, Pos); }
	virtual FVector2D ClosestPoint(const FVector2D& P) const { return Pos; }
	float ArmorMult() const;
	float DmgMult() const;
	void AddKill();
	void ApplyDamage(float Amount, ARSEntity* Attacker, const FString& Wh);
	virtual void OnAttacked(ARSEntity* Attacker) {}
	void Heal(float Amount) { Hp = FMath::Min(MaxHp, Hp + Amount); }
	virtual void Die();
	virtual float GroundZ() const;
	virtual FVector Center3D() const;     // 世界座標中心（含高度）
	void RefreshTeamColor();
	void SetShown(bool b);
};

inline bool RSAlive(const ARSEntity* E) { return E && IsValid(E) && !E->bDead; }
template <typename T>
inline bool RSAlive(const TWeakObjectPtr<T>& W) { return W.IsValid() && !W->bDead; }
