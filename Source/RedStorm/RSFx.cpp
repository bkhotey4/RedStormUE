#include "RSFx.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGame.h"
#include "RSMap.h"
#include "RSEntity.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSPlayer.h"
#include "RSAudio.h"
#include "RSVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	constexpr float SZ = 1.3f;   // Godot 像素尺寸 → uu
	constexpr int32 MAX_PARTS = 700;
}

ARSFx::ARSFx()
{
	PrimaryActorTick.bCanEverTick = false;
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = RootScene;
}

void ARSFx::Setup(ARSGame* G)
{
	Game = G;
	for (int32 I = 0; I < 10; ++I)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(RootScene);
		L->SetIntensity(0.f);
		L->SetCastShadows(false);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetVisibility(false);
		L->RegisterComponent();
		LightPool.Add(L);
		FRSLight Lt;
		Lt.L = L;
		Lt.T = 1.f;
		Lt.D = 0.f;
		Lights.Add(Lt);
	}
}

FVector ARSFx::S(const FVector2D& World, float Z) const
{
	return Game->ToWorld(World, Z * RS_ZS);
}

float ARSFx::TargetZ(const ARSEntity* T) const
{
	if (T->bIsBuilding) return 18.f;
	if (const ARSUnit* U = Cast<ARSUnit>(T))
	{
		if (U->IsAir()) return U->Alt;
		return U->UKind == TEXT("inf") ? 8.f : 10.f;
	}
	return 8.f;
}

UStaticMeshComponent* ARSFx::Acquire(UStaticMesh* Mesh, const FLinearColor& C, bool bGlow)
{
	UStaticMeshComponent* M = nullptr;
	while (Free.Num() > 0 && !M)
	{
		M = Free.Pop();
		if (!IsValid(M)) M = nullptr;
	}
	if (!M)
	{
		M = NewObject<UStaticMeshComponent>(this);
		M->SetupAttachment(RootScene);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->SetMobility(EComponentMobility::Movable);
		M->RegisterComponent();
		Pool.Add(M);
	}
	if (M->GetStaticMesh() != Mesh) M->SetStaticMesh(Mesh);
	RSV::SetColor(M, C, bGlow);
	M->SetVisibility(true);
	return M;
}

void ARSFx::Release(UStaticMeshComponent* C)
{
	if (!C) return;
	C->SetVisibility(false);
	Free.Add(C);
}

void ARSFx::AddPart(const FVector& P, const FVector& V, float S0, float S1, const FLinearColor& C, float D, float G, bool bSmoke, float Delay, bool bFlat)
{
	if (Parts.Num() >= MAX_PARTS) return;
	FRSPart Pt;
	Pt.Mesh = Acquire(bFlat ? RSV::Cylinder() : RSV::Sphere(), C, !bSmoke);
	Pt.P = P;
	Pt.V = V;
	Pt.S0 = S0;
	Pt.S1 = S1;
	Pt.C = C;
	Pt.D = D;
	Pt.G = G;
	Pt.T = -Delay;
	Pt.bFlat = bFlat;
	Pt.Mesh->SetWorldLocation(P);
	Pt.Mesh->SetWorldScale3D(FVector(0.001f));
	Parts.Add(Pt);
}

void ARSFx::Flash(const FVector& P, float Intensity, float Radius, const FLinearColor& C, float Life)
{
	int32 Best = 0;
	float BestRem = 1e9f;
	for (int32 I = 0; I < Lights.Num(); ++I)
	{
		const float Rem = Lights[I].D - Lights[I].T;
		if (Rem < BestRem)
		{
			BestRem = Rem;
			Best = I;
		}
	}
	FRSLight& L = Lights[Best];
	L.T = 0.f;
	L.D = Life;
	L.I0 = Intensity * 8.f;
	L.L->SetWorldLocation(P);
	L.L->SetLightColor(C);
	L.L->SetAttenuationRadius(Radius);
	L.L->SetIntensity(L.I0);
	L.L->SetVisibility(true);
}

void ARSFx::Glow(const FVector& P, float Size, const FLinearColor& C, float Life, float Grow, const FVector& V)
{
	AddPart(P, V, Size * SZ * 0.6f, Size * SZ * 0.6f * Grow, C, Life, 0.f, false);
}

void ARSFx::Muzzle(const FVector& P, float S)
{
	Glow(P, 26.f * S, FLinearColor(1.f, 0.8f, 0.4f), 0.08f);
	if (S >= 1.f) Flash(P, 8.f * S, 300.f, FLinearColor(1.f, 0.75f, 0.4f), 0.08f);
}

void ARSFx::Puff(const FVector& P, float S, const FLinearColor& C)
{
	AddPart(P, FVector(RS::Rand(-10, 10), RS::Rand(-10, 10), 30.f), 10.f * S * SZ, 28.f * S * SZ, C, 0.8f, 0.f, true);
}

void ARSFx::HitSpark(const FVector& P, bool bMetal)
{
	if (bMetal)
	{
		for (int32 K = 0; K < 2; ++K)
			AddPart(P, FVector(RS::Rand(-120, 120), RS::Rand(-120, 120), RS::Rand(40, 160)), 4.f, 1.5f, FLinearColor(1.f, 0.85f, 0.4f), 0.25f, -500.f);
	}
	else
	{
		AddPart(P, FVector(0, 0, 20), 6.f, 12.f, FLinearColor(0.6f, 0.55f, 0.45f), 0.35f, 0.f, true);
	}
}

void ARSFx::Spark(const FVector2D& World, float Z, const FLinearColor& C)
{
	const FVector P = S(World, Z);
	for (int32 K = 0; K < 5; ++K)
		AddPart(P, FVector(RS::Rand(-150, 150), RS::Rand(-150, 150), RS::Rand(40, 200)), 4.f, 1.5f, C, 0.35f, -500.f);
	Glow(P, 18.f, C, 0.12f);
}

void ARSFx::Smoke(const FVector2D& World, float Z, bool bFire)
{
	const FVector P = S(World, Z);
	const float Dk = bFire ? 0.18f : 0.35f;
	AddPart(P, FVector(RS::Rand(5, 20), RS::Rand(-20, 5), RS::Rand(40, 70)), 12.f, 52.f, FLinearColor(Dk, Dk, Dk), 2.4f, 0.f, true);
	if (bFire) Glow(P + FVector(0, 0, -10), 18.f, FLinearColor(1.f, 0.5f, 0.1f), 0.6f, 0.4f, FVector(0, 0, 40));
}

void ARSFx::Steam(const FVector2D& World, float Z)
{
	AddPart(S(World, Z), FVector(RS::Rand(4, 16), RS::Rand(-8, 8), RS::Rand(35, 60)), 10.f, 42.f, FLinearColor(0.92f, 0.94f, 0.96f), 2.f, 0.f, true);
}

void ARSFx::OreDust(const FVector2D& World)
{
	AddPart(S(World, 4.f) + FVector(RS::Rand(-12, 12), RS::Rand(-12, 12), 0), FVector(RS::Rand(-8, 8), RS::Rand(-8, 8), 25), 8.f, 26.f, FLinearColor(0.8f, 0.65f, 0.3f), 1.f, 0.f, true);
}

void ARSFx::Scorch(const FVector2D& World, float Sc)
{
	const int32 CX = (int32)World.X, CY = (int32)World.Y;
	const FRSMap& M = *Game->Map;
	if (M.Inb(CX, CY) && M.Terrain[M.Idx(CX, CY)] == FRSMap::T_WATER && M.Bridge[M.Idx(CX, CY)] == 0) return;
	if (Decals.Num() >= 120)
	{
		Release(Decals[0].Mesh);
		Decals.RemoveAt(0);
	}
	FRSDecal D;
	D.Mesh = Acquire(RSV::Cylinder(), FLinearColor(0.04f, 0.035f, 0.03f), false);
	D.Mesh->SetWorldLocation(S(World, 0.f) + FVector(0, 0, 1.5f));
	const float R = 40.f * Sc * SZ;
	D.Mesh->SetWorldScale3D(FVector(R / 100.f, R / 100.f, 0.01f));
	D.T = 0.f;
	D.D = 25.f;
	Decals.Add(D);
}

void ARSFx::Explosion(const FVector2D& World, float Size, float Z)
{
	const FVector P = S(World, Z);
	Glow(P, 50.f * Size, FLinearColor(1.f, 0.95f, 0.75f), 0.12f, 1.3f);
	Flash(P + FVector(0, 0, 40), 18.f * Size, 250.f + 250.f * Size, FLinearColor(1.f, 0.6f, 0.25f), 0.25f + Size * 0.1f);
	const int32 N = int32(1 + Size * 2);
	for (int32 K = 0; K < N; ++K)
	{
		const FVector Off(RS::Rand(-14, 14) * Size, RS::Rand(-14, 14) * Size, RS::Rand(0, 10) * Size);
		Glow(P + Off, 22.f * Size, FLinearColor(1.f, RS::Rand(0.55f, 0.75f), 0.2f), RS::Rand(0.35f, 0.7f) * (0.8f + Size * 0.2f), 2.2f,
			FVector(RS::Rand(-20, 20), RS::Rand(-20, 20), RS::Rand(30, 80)) * Size * 0.6f);
	}
	for (int32 K = 0; K < int32(1 + Size * 1.5f); ++K)
	{
		const float Dk = RS::Rand(0.2f, 0.3f);
		AddPart(P + FVector(RS::Rand(-12, 12) * Size, RS::Rand(-12, 12) * Size, RS::Rand(0, 10) * Size),
			FVector(RS::Rand(-15, 15), RS::Rand(-15, 15), RS::Rand(30, 70)) * (0.6f + Size * 0.3f), 16.f * Size * SZ, 56.f * Size * SZ,
			FLinearColor(Dk, Dk * 0.95f, Dk * 0.9f), RS::Rand(1.2f, 2.4f) * (0.7f + Size * 0.2f), 0.f, true);
	}
	for (int32 K = 0; K < int32(Size * 6); ++K)
	{
		AddPart(P, FVector(RS::Rand(-200, 200), RS::Rand(-200, 200), RS::Rand(100, 320)) * (0.5f + Size * 0.25f), 4.f, 2.f, FLinearColor(1.f, 0.8f, 0.4f), RS::Rand(0.4f, 0.9f), -700.f);
	}
	if (Size >= 1.5f)
	{
		AddPart(P + FVector(0, 0, 2), FVector::ZeroVector, 20.f * Size * SZ, 90.f * Size * SZ, FLinearColor(1.f, 0.8f, 0.5f), 0.4f, 0.f, false, 0.f, true);
	}
	if (Z < 4.f) Scorch(World, 0.35f + Size * 0.35f);
	if (Size >= 1.5f) Shake = FMath::Max(Shake, Size * 2.5f);
	if (Size >= 2.f) Game->Audio()->Play(TEXT("big_explode"), -2.f, &World);
	else if (Size >= 0.9f) Game->Audio()->Play(TEXT("explode"), -6.f, &World);
}

void ARSFx::FireBlast(const FVector2D& World, float Size)
{
	const FVector P = S(World, 0.f);
	for (int32 K = 0; K < int32(6 + Size * 6); ++K)
	{
		const FVector Off(RS::Rand(-22, 22) * Size, RS::Rand(-22, 22) * Size, 0);
		AddPart(P + Off, FVector(RS::Rand(-8, 8), RS::Rand(-8, 8), RS::Rand(30, 70)), 18.f * Size * SZ, 44.f * Size * SZ,
			FLinearColor(1.f, RS::Rand(0.45f, 0.65f), 0.15f), RS::Rand(0.6f, 1.1f), 0.f, false, RS::Rand(0.f, 0.25f));
	}
	for (int32 K = 0; K < int32(3 + Size * 3); ++K)
	{
		AddPart(P + FVector(RS::Rand(-20, 20) * Size, RS::Rand(-20, 20) * Size, 10), FVector(RS::Rand(-6, 6), RS::Rand(-6, 6), RS::Rand(30, 60)),
			20.f * Size * SZ, 60.f * Size * SZ, FLinearColor(0.15f, 0.13f, 0.12f), RS::Rand(1.8f, 3.f), 0.f, true);
	}
	Flash(P + FVector(0, 0, 60), 25.f * Size, 400.f * Size, FLinearColor(1.f, 0.5f, 0.15f), 0.6f);
	Scorch(World, Size);
}

void ARSFx::Wake(const FVector2D& World)
{
	AddPart(S(World, 0.f) + FVector(RS::Rand(-6, 6), RS::Rand(-6, 6), 0), FVector(RS::Rand(-5, 5), RS::Rand(-5, 5), 0), 6.f, 24.f, FLinearColor(0.9f, 0.95f, 1.f), 1.4f, 0.f, true, 0.f, true);
}

void ARSFx::Coins(const FVector2D& World)
{
	const FVector P = S(World, 40.f);
	for (int32 K = 0; K < 6; ++K)
		AddPart(P + FVector(RS::Rand(-30, 30), RS::Rand(-30, 30), RS::Rand(-10, 10)), FVector(0, 0, RS::Rand(-60, -30)), 14.f, 6.f, FLinearColor(1.f, 0.85f, 0.2f), 1.2f, 0.f, false, RS::Rand(0.f, 0.6f));
}

void ARSFx::ScanRing(const FVector2D& World, const FLinearColor& C)
{
	const FVector P = S(World, 0.f) + FVector(0, 0, 3);
	for (int32 K = 0; K < 3; ++K)
		AddPart(P, FVector::ZeroVector, 40.f, 900.f, C, 1.4f, 0.f, false, K * 0.3f, true);
}

void ARSFx::InfDeath(const FVector2D& World, bool bFried)
{
	const FVector P = S(World, 6.f);
	if (bFried)
	{
		Glow(P, 30.f, FLinearColor(0.5f, 0.7f, 1.f), 0.3f);
		AddPart(P, FVector(0, 0, 40), 8.f, 22.f, FLinearColor(0.1f, 0.1f, 0.1f), 1.f, 0.f, true);
	}
	else
	{
		AddPart(P, FVector(0, 0, 12), 8.f, 16.f, FLinearColor(0.55f, 0.5f, 0.42f), 0.6f, 0.f, true);
	}
	Game->Audio()->Play(TEXT("inf_die"), -10.f, &World, 0.15f);
}

void ARSFx::Lightning(const FVector2D& World)
{
	const FVector B = S(World, 0.f);
	const FVector A = B + FVector(RS::Rand(-150, 150), RS::Rand(-150, 150), 1400.f);
	AddBeam(A, B, 0.35f, TEXT("tesla"), 10.f, FLinearColor(0.7f, 0.8f, 1.f));
	Glow(B, 120.f, FLinearColor(0.7f, 0.8f, 1.f), 0.35f);
	Flash(B + FVector(0, 0, 200), 60.f, 1500.f, FLinearColor(0.7f, 0.8f, 1.f), 0.3f);
}

void ARSFx::NukeBlast(const FVector2D& World)
{
	const FVector P = S(World, 0.f);
	Glow(P, 700.f, FLinearColor(1.f, 1.f, 0.9f), 0.5f, 1.2f);
	Flash(P + FVector(0, 0, 400), 400.f, 5000.f, FLinearColor(1.f, 0.85f, 0.6f), 1.5f);
	for (int32 K = 0; K < 40; ++K)
	{
		const float A = RS::Rand(0.f, 2.f * PI), R = RS::Rand(0.f, 1.f);
		const FVector Off(FMath::Cos(A) * R * 350.f, FMath::Sin(A) * R * 350.f, 0);
		AddPart(P + Off, FVector(0, 0, RS::Rand(100, 400)), 80.f, 200.f, FLinearColor(1.f, RS::Rand(0.5f, 0.8f), 0.2f), RS::Rand(1.f, 2.5f));
	}
	for (int32 K = 0; K < 40; ++K)
	{
		const FVector Off(RS::Rand(-400, 400), RS::Rand(-400, 400), RS::Rand(0, 100));
		AddPart(P + Off, FVector(RS::Rand(-30, 30), RS::Rand(-30, 30), RS::Rand(80, 260)), 80.f, 260.f, FLinearColor(0.28f, 0.24f, 0.2f), RS::Rand(3.f, 6.f), 0.f, true);
	}
	for (int32 K = 0; K < 3; ++K)
		AddPart(P + FVector(0, 0, 4), FVector::ZeroVector, 60.f, 1200.f + K * 250.f, FLinearColor(1.f, 0.8f, 0.5f), 1.2f, 0.f, false, K * 0.15f, true);
	Scorch(World, 6.f);
	Shake = 22.f;
	Game->Audio()->Play(TEXT("big_explode"), 4.f);
}

FVector ARSFx::ShakeOffset() const
{
	if (Shake <= 0.f) return FVector::ZeroVector;
	return FVector(RS::Rand(-Shake, Shake), RS::Rand(-Shake, Shake), RS::Rand(-Shake, Shake) * 0.6f) * 2.f;
}

// ───────────────────────── 武器 ─────────────────────────
void ARSFx::AddProj(const FRSProj& InP)
{
	FRSProj P = InP;
	UStaticMesh* Mesh = RSV::Sphere();
	FLinearColor C(1.f, 0.9f, 0.6f);
	if (P.Kind == TEXT("rocket") || P.Kind == TEXT("topattack") || P.Kind == TEXT("mlrs") || P.Kind == TEXT("ballistic")) { Mesh = RSV::Cylinder(); C = FLinearColor(0.85f, 0.85f, 0.8f); }
	else if (P.Kind == TEXT("arty")) C = FLinearColor(0.9f, 0.9f, 0.85f);
	else if (P.Kind == TEXT("bomb") || P.Kind == TEXT("torpedo")) C = FLinearColor(0.15f, 0.15f, 0.15f);
	P.Mesh = Acquire(Mesh, C, false);
	float Sc = 6.f;
	if (P.Kind == TEXT("arty")) Sc = 9.f;
	else if (P.Kind == TEXT("flak")) Sc = 4.f;
	else if (P.Kind == TEXT("bomb")) Sc = 12.f;
	else if (P.Kind == TEXT("torpedo")) Sc = 8.f;
	if (Mesh == RSV::Cylinder())
	{
		const float Big = P.Kind == TEXT("ballistic") ? 2.2f : 1.f;
		P.Mesh->SetWorldScale3D(FVector(0.05f * Big, 0.05f * Big, 0.22f * Big));
	}
	else
	{
		P.Mesh->SetWorldScale3D(FVector(Sc / 100.f));
	}
	P.Mesh->SetWorldLocation(ProjPos(P, 0.f));
	Projs.Add(P);
}

void ARSFx::FireWeapon(ARSEntity* Att, const FRSWeapon& W, const FVector2D& From, float Z, ARSEntity* T)
{
	if (!Att || !T) return;
	float Dmg = W.Dmg * Att->DmgMult();
	// 掩體與高地：只影響地面直射武器（子彈、戰車砲、火箭、光束、電弧）
	static const TSet<FString> DirectKinds = { TEXT("bullet"), TEXT("shell"), TEXT("rocket"), TEXT("beam"), TEXT("tesla") };
	if (DirectKinds.Contains(W.Proj) && !Att->IsAir() && !T->IsAir())
	{
		const int32 EA = Game->Map->ElevAt(Att->Pos), ET = Game->Map->ElevAt(T->Pos);
		if (EA > ET) Dmg *= 1.25f;                                      // 居高臨下
		else if (EA < ET && !T->bIsBuilding && RS::Rand01() < 0.3f)      // 由下往上打：部分射擊打在坡面上
		{
			const FVector2D Mid = FMath::Lerp(From, T->Pos, 0.8f);
			HitSpark(S(Mid, Game->GroundHeight(Mid) / RS_ZS + 2.f), false);
			return;
		}
		if (ARSEntity* Blk = Game->FireBlocker(From, T->bIsBuilding ? T->ClosestPoint(From) : T->Pos, Att, T))
		{
			T = Blk;   // 彈道被牆或建築擋下，改打在障礙物上
		}
	}
	const FVector2D TP = T->bIsBuilding ? T->ClosestPoint(From) : T->Pos;
	const float TZ = TargetZ(T);
	if (!W.Snd.IsEmpty()) Game->Audio()->Play(W.Snd, W.Snd == TEXT("mg") ? -6.f : -3.f, &From);
	const FVector MS = S(From, Z);
	auto MakeProj = [&](const FString& Kind, const FVector2D& To, float Z1, float Dur, bool bTrack, float Arc, float Splash)
	{
		FRSProj P;
		P.Kind = Kind;
		P.From = From;
		P.To = To;
		P.Z0 = Z;
		P.Z1 = Z1;
		P.Dur = Dur;
		if (bTrack) P.Target = T;
		P.Dmg = Dmg;
		P.Wh = W.Wh;
		P.Att = Att;
		P.Splash = Splash;
		P.Arc = Arc;
		P.Owner = Att->Player;
		AddProj(P);
	};
	const float Dist = FVector2D::Distance(From, TP);
	const FString& Pj = W.Proj;
	if (Pj == TEXT("bullet"))
	{
		Muzzle(MS, 0.5f);
		const FVector B = S(TP, TZ) + FVector(RS::Rand(-6, 6), RS::Rand(-6, 6), RS::Rand(-4, 4));
		AddBeam(MS, B, 0.07f, TEXT("tracer"), 1.5f, FLinearColor(1.f, 0.9f, 0.5f));
		HitSpark(B, T->bIsBuilding || T->Armor != TEXT("inf"));
		T->ApplyDamage(Dmg, Att, W.Wh);
	}
	else if (Pj == TEXT("shell"))
	{
		Muzzle(MS, 1.f);
		Puff(MS, 0.6f);
		MakeProj(TEXT("shell"), TP, TZ, FMath::Max(0.08f, Dist / 16.f), true, 4.f, W.Splash);
	}
	else if (Pj == TEXT("rocket"))
	{
		Muzzle(MS, 0.7f);
		MakeProj(TEXT("rocket"), TP, TZ, FMath::Max(0.15f, Dist / 9.f), true, 10.f, W.Splash);
	}
	else if (Pj == TEXT("arty"))
	{
		Muzzle(MS, 1.2f);
		Puff(MS, 1.2f);
		MakeProj(TEXT("arty"), TP + FVector2D(RS::Rand(-0.35f, 0.35f), RS::Rand(-0.35f, 0.35f)), 0.f, 1.1f + Dist * 0.09f, false, 70.f + Dist * 9.f, W.Splash > 0.f ? W.Splash : 1.2f);
	}
	else if (Pj == TEXT("topattack"))
	{
		Muzzle(MS, 0.8f);
		Puff(MS, 0.8f);
		MakeProj(TEXT("topattack"), TP, TZ, FMath::Max(0.5f, Dist / 6.5f), true, 45.f + Dist * 6.f, 0.f);
	}
	else if (Pj == TEXT("mlrs"))
	{
		Muzzle(MS, 1.f);
		Puff(MS, 1.f);
		MakeProj(TEXT("mlrs"), TP + FVector2D(RS::Rand(-0.7f, 0.7f), RS::Rand(-0.7f, 0.7f)), 0.f, 0.7f + Dist * 0.07f, false, 30.f + Dist * 5.f, W.Splash > 0.f ? W.Splash : 1.f);
	}
	else if (Pj == TEXT("ballistic"))
	{
		Puff(MS, 2.f);
		Glow(MS, 60.f, FLinearColor(1.f, 0.7f, 0.3f), 0.4f);
		MakeProj(TEXT("ballistic"), TP, 0.f, 2.2f + Dist * 0.05f, false, 260.f + Dist * 8.f, W.Splash > 0.f ? W.Splash : 1.8f);
	}
	else if (Pj == TEXT("torpedo"))
	{
		MakeProj(TEXT("torpedo"), TP, 0.f, FMath::Max(0.3f, Dist / 6.f), true, 0.f, 0.f);
		Projs.Last().Z0 = 0.f;
	}
	else if (Pj == TEXT("flak"))
	{
		Muzzle(MS, 0.6f);
		MakeProj(TEXT("flak"), TP, TZ, FMath::Max(0.08f, Dist / 20.f), true, 0.f, 0.f);
	}
	else if (Pj == TEXT("tesla"))
	{
		const FVector B = S(TP, TZ);
		AddBeam(MS, B, 0.28f, TEXT("tesla"), 2.5f, FLinearColor(0.55f, 0.7f, 1.f));
		Glow(MS, 34.f, FLinearColor(0.4f, 0.6f, 1.f), 0.25f);
		Glow(B, 40.f, FLinearColor(0.5f, 0.7f, 1.f), 0.3f);
		Flash(B, 12.f, 400.f, FLinearColor(0.5f, 0.7f, 1.f), 0.2f);
		T->ApplyDamage(Dmg, Att, W.Wh);
		if (!T->bIsBuilding && T->Armor == TEXT("inf")) Spark(TP, TZ, FLinearColor(0.6f, 0.8f, 1.f));
	}
	else if (Pj == TEXT("beam"))
	{
		const FVector B = S(TP, TZ);
		AddBeam(MS, B, 0.32f, TEXT("beam"), 5.f, FLinearColor(0.5f, 0.95f, 1.f));
		Glow(MS, 30.f, FLinearColor(0.5f, 0.9f, 1.f), 0.3f);
		Glow(B, 50.f, FLinearColor(0.5f, 0.9f, 1.f), 0.35f);
		Flash(B, 14.f, 450.f, FLinearColor(0.5f, 0.9f, 1.f), 0.25f);
		if (W.Splash > 0.f) Game->DamageArea(TP, W.Splash, Dmg, W.Wh, Att, T);
		else T->ApplyDamage(Dmg, Att, W.Wh);
		Scorch(TP, 0.5f);
	}
	else if (Pj == TEXT("bomb"))
	{
		FRSProj P;
		P.Kind = TEXT("bomb");
		P.From = From;
		P.To = From + FVector2D(0.1f, 0.1f);
		P.Z0 = Z - 8.f;
		P.Z1 = 0.f;
		P.Dur = 0.7f;
		P.Dmg = Dmg;
		P.Wh = W.Wh;
		P.Att = Att;
		P.Splash = W.Splash > 0.f ? W.Splash : 1.5f;
		P.Owner = Att->Player;
		AddProj(P);
	}
}

void ARSFx::Impact(const FRSProj& P)
{
	const FVector2D& To = P.To;
	ARSEntity* Att = P.Att.Get();
	if (Att && Att->bDead) Att = nullptr;
	const FString& K = P.Kind;
	if (K == TEXT("shell")) Explosion(To, 0.7f, P.Z1);
	else if (K == TEXT("rocket")) Explosion(To, 0.8f, P.Z1);
	else if (K == TEXT("arty")) Explosion(To, 1.6f);
	else if (K == TEXT("flak"))
	{
		for (int32 I = 0; I < 3; ++I) Puff(S(To, P.Z1) + FVector(RS::Rand(-15, 15), RS::Rand(-15, 15), RS::Rand(-10, 10)), 0.5f, FLinearColor(0.15f, 0.15f, 0.15f));
		Glow(S(To, P.Z1), 24.f, FLinearColor(1.f, 0.7f, 0.3f), 0.15f);
	}
	else if (K == TEXT("bomb")) Explosion(To, 2.2f);
	else if (K == TEXT("topattack")) Explosion(To, 0.9f, P.Z1);
	else if (K == TEXT("torpedo"))
	{
		Explosion(To, 1.2f);
		for (int32 I = 0; I < 4; ++I) AddPart(S(To, 0.f), FVector(RS::Rand(-40, 40), RS::Rand(-40, 40), RS::Rand(150, 260)), 12.f, 34.f, FLinearColor(0.9f, 0.95f, 1.f), 1.f, -300.f, true);
	}
	else if (K == TEXT("mlrs"))
	{
		if (P.Wh == TEXT("fire")) FireBlast(To, 1.3f);
		else Explosion(To, 1.2f);
	}
	else if (K == TEXT("ballistic"))
	{
		Explosion(To, 2.8f);
		FireBlast(To, 1.6f);
	}
	else if (K == TEXT("nuke_up") || K == TEXT("nuke_down"))
	{
		return;
	}
	if (P.Splash > 0.f)
	{
		Game->DamageArea(To, P.Splash, P.Dmg, P.Wh, Att, nullptr, P.Owner.Get());
	}
	else if (ARSEntity* T = P.Target.Get())
	{
		if (!T->bDead) T->ApplyDamage(P.Dmg, Att, P.Wh);
	}
}

FVector ARSFx::ProjPos(const FRSProj& P, float F) const
{
	const FVector2D W = RS::Lerp2(P.From, P.To, F);
	float Z = FMath::Lerp(P.Z0, P.Z1, F) + P.Arc * 4.f * F * (1.f - F);
	if (P.Kind == TEXT("bomb")) Z = FMath::Lerp(P.Z0, 0.f, F * F);
	return S(W, Z);
}

void ARSFx::SetSeg(UStaticMeshComponent* C, const FVector& A, const FVector& B, float W)
{
	const FVector D = B - A;
	const float L = D.Size();
	if (L < 0.01f) return;
	C->SetWorldLocation((A + B) * 0.5f);
	C->SetWorldRotation(FRotationMatrix::MakeFromZ(D / L).Rotator());
	C->SetWorldScale3D(FVector(W / 100.f, W / 100.f, L / 100.f));
}

void ARSFx::AddBeam(const FVector& A, const FVector& B, float D, const FString& Kind, float W, const FLinearColor& C)
{
	FRSBeam Bm;
	Bm.A = A;
	Bm.B = B;
	Bm.D = D;
	Bm.W = W * SZ;
	Bm.Kind = Kind;
	Bm.Seed = FMath::Rand();
	const int32 N = Kind == TEXT("tesla") ? 9 : 1;
	FRandomStream R(Bm.Seed);
	FVector Prev = A;
	for (int32 K = 1; K <= N; ++K)
	{
		FVector Q = FMath::Lerp(A, B, float(K) / N);
		if (K < N) Q += FVector(R.FRandRange(-25, 25), R.FRandRange(-25, 25), R.FRandRange(-25, 25));
		UStaticMeshComponent* Seg = Acquire(RSV::Cylinder(), C, true);
		SetSeg(Seg, Prev, Q, Bm.W * (Kind == TEXT("beam") ? 2.f : 1.f));
		Bm.Segs.Add(Seg);
		Prev = Q;
	}
	Beams.Add(Bm);
}

// ───────────────────────── 更新 ─────────────────────────
void ARSFx::UpdateFx(float Dt)
{
	// 彈道
	for (int32 I = Projs.Num() - 1; I >= 0; --I)
	{
		FRSProj& P = Projs[I];
		P.T += Dt;
		if (ARSEntity* T = P.Target.Get())
		{
			if (!T->bDead && P.Kind != TEXT("arty")) P.To = T->bIsBuilding ? T->ClosestPoint(P.From) : T->Pos;
		}
		const float F = FMath::Clamp(P.T / P.Dur, 0.f, 1.f);
		if (P.T >= 0.f && P.Mesh)
		{
			const FVector Cur = ProjPos(P, F);
			P.Mesh->SetVisibility(true);
			P.Mesh->SetWorldLocation(Cur);
			if (P.Mesh->GetStaticMesh() == RSV::Cylinder())
			{
				const FVector Nx = ProjPos(P, FMath::Min(1.f, F + 0.03f));
				const FVector Dir = (Nx - Cur).GetSafeNormal();
				if (!Dir.IsNearlyZero()) P.Mesh->SetWorldRotation(FRotationMatrix::MakeFromZ(Dir).Rotator());
			}
		}
		else if (P.Mesh)
		{
			P.Mesh->SetVisibility(false);
		}
		if (P.Kind == TEXT("torpedo"))
		{
			P.Trail -= Dt;
			if (P.Trail <= 0.f) { P.Trail = 0.05f; Wake(RS::Lerp2(P.From, P.To, F)); }
		}
		else if (P.T >= 0.f && (P.Kind == TEXT("rocket") || P.Kind == TEXT("arty") || P.Kind == TEXT("topattack") || P.Kind == TEXT("mlrs") || P.Kind == TEXT("ballistic") || P.Kind == TEXT("nuke_up") || P.Kind == TEXT("nuke_down")))
		{
			P.Trail -= Dt;
			if (P.Trail <= 0.f)
			{
				P.Trail = 0.035f;
				const bool bBig = P.Kind == TEXT("ballistic") || P.Kind.StartsWith(TEXT("nuke"));
				AddPart(ProjPos(P, F), FVector(RS::Rand(-6, 6), RS::Rand(-6, 6), 12), bBig ? 14.f : 6.f, bBig ? 46.f : 20.f, FLinearColor(0.75f, 0.72f, 0.68f), bBig ? 1.6f : 0.7f, 0.f, true);
				Glow(ProjPos(P, F), bBig ? 28.f : 12.f, FLinearColor(1.f, 0.7f, 0.3f), 0.06f);
			}
		}
		if (P.T >= P.Dur)
		{
			const FRSProj Done = P;
			Release(P.Mesh);
			Projs.RemoveAt(I);
			Impact(Done);
		}
	}
	// 粒子
	for (int32 I = Parts.Num() - 1; I >= 0; --I)
	{
		FRSPart& Pt = Parts[I];
		Pt.T += Dt;
		if (Pt.T >= Pt.D)
		{
			Release(Pt.Mesh);
			Parts.RemoveAtSwap(I);
			continue;
		}
		if (Pt.T < 0.f) continue;
		Pt.P += Pt.V * Dt;
		Pt.V.Z += Pt.G * Dt;
		const float F = Pt.T / Pt.D;
		float Sz = FMath::Lerp(Pt.S0, Pt.S1, F);
		if (F > 0.55f) Sz *= FMath::Max(0.f, (1.f - F) / 0.45f);
		Pt.Mesh->SetWorldLocation(Pt.P);
		if (Pt.bFlat) Pt.Mesh->SetWorldScale3D(FVector(Sz / 100.f, Sz / 100.f, 0.015f));
		else Pt.Mesh->SetWorldScale3D(FVector(FMath::Max(0.001f, Sz / 100.f)));
	}
	// 光束
	for (int32 I = Beams.Num() - 1; I >= 0; --I)
	{
		FRSBeam& B = Beams[I];
		B.T += Dt;
		if (B.T >= B.D)
		{
			for (UStaticMeshComponent* C : B.Segs) Release(C);
			Beams.RemoveAtSwap(I);
			continue;
		}
		const float A = 1.f - B.T / B.D;
		if (B.Kind == TEXT("tesla"))
		{
			FRandomStream R(B.Seed + int32(B.T * 30.f));
			FVector Prev = B.A;
			const int32 N = B.Segs.Num();
			for (int32 K = 1; K <= N; ++K)
			{
				FVector Q = FMath::Lerp(B.A, B.B, float(K) / N);
				if (K < N) Q += FVector(R.FRandRange(-25, 25), R.FRandRange(-25, 25), R.FRandRange(-25, 25));
				SetSeg(B.Segs[K - 1], Prev, Q, B.W * FMath::Max(0.2f, A));
				Prev = Q;
			}
		}
		else
		{
			for (UStaticMeshComponent* C : B.Segs)
			{
				const FVector Sc = C->GetComponentScale();
				const float Wd = B.W * (B.Kind == TEXT("beam") ? 2.f : 1.f) * FMath::Max(0.2f, A) / 100.f;
				C->SetWorldScale3D(FVector(Wd, Wd, Sc.Z));
			}
		}
	}
	// 焦痕
	for (int32 I = Decals.Num() - 1; I >= 0; --I)
	{
		FRSDecal& D = Decals[I];
		D.T += Dt;
		if (D.T >= D.D)
		{
			Release(D.Mesh);
			Decals.RemoveAt(I);
			continue;
		}
		if (D.T > D.D * 0.6f)
		{
			const float K = 1.f - (D.T - D.D * 0.6f) / (D.D * 0.4f);
			const FVector Sc = D.Mesh->GetComponentScale();
			D.Mesh->SetWorldScale3D(FVector(Sc.X * (0.995f + 0.005f * K), Sc.Y * (0.995f + 0.005f * K), 0.01f));
		}
	}
	// 閃光
	for (FRSLight& L : Lights)
	{
		if (L.D <= 0.f) continue;
		L.T += Dt;
		if (L.T >= L.D)
		{
			L.D = 0.f;
			L.L->SetIntensity(0.f);
			L.L->SetVisibility(false);
		}
		else
		{
			L.L->SetIntensity(L.I0 * (1.f - L.T / L.D));
		}
	}
	Shake = FMath::Max(0.f, Shake - Dt * 18.f);
}
