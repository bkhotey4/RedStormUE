// 特效：彈道、爆炸、煙霧、光束、閃電、焦痕（全部以物件池的元件呈現）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RSFx.generated.h"

class ARSGame;
class ARSEntity;
class URSPlayer;
class UStaticMeshComponent;
class UPointLightComponent;
struct FRSWeapon;

// Godot 版以「像素」表示高度；換算成 uu
static constexpr float RS_ZS = 2.5f;

struct FRSProj
{
	FString Kind;
	FVector2D From, To;
	float Z0 = 0.f, Z1 = 0.f, T = 0.f, Dur = 1.f, Arc = 0.f;
	TWeakObjectPtr<ARSEntity> Target;
	float Dmg = 0.f;
	FString Wh = TEXT("ap");
	TWeakObjectPtr<ARSEntity> Att;
	float Splash = 0.f;
	float Trail = 0.f;
	TWeakObjectPtr<URSPlayer> Owner;
	UStaticMeshComponent* Mesh = nullptr;
};

struct FRSPart
{
	UStaticMeshComponent* Mesh = nullptr;
	FVector P, V;
	float S0 = 10.f, S1 = 10.f, T = 0.f, D = 1.f, G = 0.f;
	FLinearColor C;
	bool bFlat = false;
};

struct FRSBeam
{
	TArray<UStaticMeshComponent*> Segs;
	FVector A, B;
	float T = 0.f, D = 0.2f, W = 2.f;
	FString Kind;
	int32 Seed = 0;
};

struct FRSDecal
{
	UStaticMeshComponent* Mesh = nullptr;
	float T = 0.f, D = 25.f;
};

struct FRSLight
{
	UPointLightComponent* L = nullptr;
	float T = 0.f, D = 0.2f, I0 = 0.f;
};

UCLASS()
class REDSTORM_API ARSFx : public AActor
{
	GENERATED_BODY()
public:
	ARSFx();
	void Setup(ARSGame* G);
	void UpdateFx(float Dt);

	FVector S(const FVector2D& World, float Z = 0.f) const;
	float TargetZ(const ARSEntity* T) const;

	void FireWeapon(ARSEntity* Att, const FRSWeapon& W, const FVector2D& From, float Z, ARSEntity* T);
	void AddProj(const FRSProj& P);
	void Glow(const FVector& P, float Size, const FLinearColor& C, float Life, float Grow = 1.f, const FVector& V = FVector::ZeroVector);
	void Muzzle(const FVector& P, float S);
	void Puff(const FVector& P, float S, const FLinearColor& C = FLinearColor(0.7f, 0.68f, 0.64f));
	void HitSpark(const FVector& P, bool bMetal);
	void Spark(const FVector2D& World, float Z = 10.f, const FLinearColor& C = FLinearColor(1.f, 0.85f, 0.4f));
	void Smoke(const FVector2D& World, float Z, bool bFire = false);
	void Steam(const FVector2D& World, float Z);
	void OreDust(const FVector2D& World);
	void Scorch(const FVector2D& World, float S);
	void Explosion(const FVector2D& World, float Size, float Z = 0.f);
	void FireBlast(const FVector2D& World, float Size);
	void Wake(const FVector2D& World);
	void Coins(const FVector2D& World);
	void ScanRing(const FVector2D& World, const FLinearColor& C = FLinearColor(0.4f, 0.8f, 1.f));
	void InfDeath(const FVector2D& World, bool bFried = false);
	void Lightning(const FVector2D& World);
	void NukeBlast(const FVector2D& World);
	void Flash(const FVector& P, float Intensity, float Radius, const FLinearColor& C, float Life);
	FVector ShakeOffset() const;

	float Shake = 0.f;
	TArray<FRSProj> Projs;

private:
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	UPROPERTY() TObjectPtr<USceneComponent> RootScene = nullptr;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pool;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> LightPool;
	TArray<UStaticMeshComponent*> Free;
	TArray<FRSPart> Parts;
	TArray<FRSBeam> Beams;
	TArray<FRSDecal> Decals;
	TArray<FRSLight> Lights;
	TArray<TPair<float, TFunction<void()>>> Delayed;

	UStaticMeshComponent* Acquire(class UStaticMesh* Mesh, const FLinearColor& C, bool bGlow = true);
	void Release(UStaticMeshComponent* C);
	void AddPart(const FVector& P, const FVector& V, float S0, float S1, const FLinearColor& C, float D, float G = 0.f, bool bSmoke = false, float Delay = 0.f, bool bFlat = false);
	void Impact(const FRSProj& P);
	FVector ProjPos(const FRSProj& P, float F) const;
	void SetSeg(UStaticMeshComponent* C, const FVector& A, const FVector& B, float W);
	void AddBeam(const FVector& A, const FVector& B, float D, const FString& Kind, float W, const FLinearColor& C);
};
