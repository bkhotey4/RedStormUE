// 單位：步兵、載具、艦艇、飛行器
#pragma once

#include "CoreMinimal.h"
#include "RSEntity.h"
#include "RSUnit.generated.h"

class ARSBuilding;

struct FRSWaypoint
{
	FVector2D P;
	bool bAttackMove = false;
};

UCLASS()
class REDSTORM_API ARSUnit : public ARSEntity
{
	GENERATED_BODY()
public:
	static constexpr float CARGO_MAX = 700.f;

	FString UKind = TEXT("veh");
	float Speed = 1.5f;
	float FormSpeed = 0.f;
	float DmgSmokeT = 0.f;      // 陣型移動時與最慢單位同速（0＝不限）
	float Radius = 0.35f;
	float Facing = 0.f;
	float Turret = 0.f;
	bool bHasTurret = false;
	TArray<FVector2D> Path;
	FString Order = TEXT("idle");
	TWeakObjectPtr<ARSEntity> Target;
	FVector2D HomePos = FVector2D::ZeroVector;
	FVector2D AmoveDest = FVector2D::ZeroVector;
	FVector2D MoveDest = FVector2D::ZeroVector;
	const FRSWeapon* Weapon = nullptr;
	const FRSWeapon* Weapon2 = nullptr;
	float Cd = 0.f, Cd2 = 0.f;
	int32 Burst = 0;
	float BurstT = 0.f;
	float ScanT = 0.f, Scan2T = 0.f, RepathT = 0.f;
	float StuckT = 0.f;
	FVector2D StuckRef = FVector2D::ZeroVector;
	int32 Repaths = 0;
	bool bMoving = false;
	float AnimT = 0.f;
	float FireT = 0.f;
	float TurnRate = 4.f;
	float Cargo = 0.f;
	FIntPoint OreCell = FIntPoint(-1, -1);
	float MineT = 0.f;
	TWeakObjectPtr<ARSBuilding> Dock;
	float IdleT = 0.f;
	float Alt = 0.f;
	int32 Ammo = 0;
	bool bLanded = false;
	float JamT = 0.f;
	TArray<FRSWaypoint> Waypoints;
	// Godot meta 欄位
	float ParaT = -1.f;                     // 空降中
	TWeakObjectPtr<ARSUnit> CarrierHome;     // 艦載機所屬航母
	bool bFromCarrier = false;
	bool bSwarm = false;
	bool bApproach = false;
	int32 RevealKey = -1;

	void Setup(ARSGame* G, URSPlayer* P, const FString& Id, const FVector2D& At);
	virtual bool IsAir() const override { return UKind == TEXT("air"); }
	bool HasWeapon() const { return Weapon && Weapon->IsValid(); }
	float WeaponRange() const;   // 含高地射程加成
	void Update(float Dt);
	void UpdateVisual(float Dt);

	// 指令
	void QueueMove(const FVector2D& Dest, bool bAttackMove = false);
	bool NextWaypoint();
	void CmdMove(const FVector2D& Dest, bool bAttackMove = false);
	void CmdAttack(ARSEntity* T);
	void CmdStop();
	void CmdCapture(ARSBuilding* B);
	void CmdGarrison(ARSBuilding* B);
	void CmdHarvest(FIntPoint Cell = FIntPoint(-1, -1));
	void CmdDock(ARSBuilding* Ref);
	void CmdDeploy();
	bool CanHit(const FRSWeapon* W, const ARSEntity* T) const;
	virtual void OnAttacked(ARSEntity* Attacker) override;
	virtual FVector Center3D() const override;
	float CruiseAlt() const;

private:
	void UpdateGround(float Dt);
	void LostTarget();
	void Chase(float Dt, ARSEntity* T);
	void AttackStep(float Dt, ARSEntity* T);
	FVector2D AimPoint(const ARSEntity* T) const;
	bool AimAndFire(float Dt, ARSEntity* T, const FRSWeapon* W);
	void FireOnMove(float Dt);
	void UpdateAA(float Dt);
	void FireWeapon(const FRSWeapon* W, ARSEntity* T);
	void LaunchJet(ARSEntity* T);
	bool SetPath(const FVector2D& To);
	void ResolveSolid();
	void CrushInfantry();   // 物理限制：單位外圈不得穿進建築、圍牆、樹、岩石、懸崖與地圖邊界
	bool Follow(float Dt);
	void Separate(float Dt);
	void UpdateHarvester(float Dt);
	ARSEntity* DroneTarget();
	void UpdateAir(float Dt);
	bool FlyTo(float Dt, const FVector2D& P, float Tol);
};
