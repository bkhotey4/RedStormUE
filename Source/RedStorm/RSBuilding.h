// 建築物
#pragma once

#include "CoreMinimal.h"
#include "RSEntity.h"
#include "RSBuilding.generated.h"

struct FRSOccupant
{
	FString Id;
	float Hp = 100.f;
	int32 Rank = 0;
	int32 Kills = 0;
	const FRSWeapon* W = nullptr;
	float Cd = 0.f;
};

UCLASS()
class REDSTORM_API ARSBuilding : public ARSEntity
{
	GENERATED_BODY()
public:
	FIntPoint Cell = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint(2, 2);
	float BuildP = 0.f;
	bool bSelling = false;
	float SellT = 0.f;
	bool bRepairing = false;
	float AnimT = 0.f;
	const FRSWeapon* Weapon = nullptr;
	float Cd = 0.f;
	float Turret = 0.f;
	TWeakObjectPtr<ARSEntity> Target;
	float ScanT = 0.f;
	FVector2D Rally = FVector2D(-1, -1);
	float FxT = 0.f;
	float OilT = 0.f;
	float UnloadingT = 0.f;
	float CaptureFlash = 0.f;
	int32 VarFrame = 0;
	TArray<FRSOccupant> Occupants;
	float DustT = 0.f;
	float JamUntil = -1.f;
	bool bRevealed = false;
	float LastVisualP = -1.f;

	void Setup(ARSGame* G, URSPlayer* P, const FString& Id, FIntPoint TL, bool bInstant);
	bool Built() const { return BuildP >= 1.f && !bSelling; }
	bool HasWeapon() const { return Weapon && Weapon->IsValid(); }
	virtual float DistTo(const FVector2D& P) const override { return FVector2D::Distance(P, ClosestPoint(P)); }
	virtual FVector2D ClosestPoint(const FVector2D& P) const override;
	FVector2D DockPos() const { return FVector2D(Cell.X + Size.X * 0.5f, Cell.Y + Size.Y + 0.55f); }
	FIntPoint ExitCell() const { return FIntPoint(Cell.X + Size.X / 2, Cell.Y + Size.Y); }
	bool Powered() const;
	bool Jammed() const;
	void Update(float Dt);
	void UpdateVisual(float Dt);
	bool CanHit(const FRSWeapon* W, const ARSEntity* T) const;
	virtual void OnAttacked(ARSEntity* Attacker) override;
	virtual float GroundZ() const override;
	virtual FVector Center3D() const override;

private:
	void UpdateDefense(float Dt);
	void UpdateGarrison(float Dt);
};
