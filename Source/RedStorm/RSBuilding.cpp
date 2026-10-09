#include "RSBuilding.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSFx.h"
#include "RSAudio.h"
#include "RSVisuals.h"
#include "RSWorldEvents.h"
#include "Components/StaticMeshComponent.h"

void ARSBuilding::Setup(ARSGame* G, URSPlayer* P, const FString& Id, FIntPoint TL, bool bInstant)
{
	Game = G;
	Player = P;
	TypeId = Id;
	bIsBuilding = true;
	Def = &FRSDefs::Get().GetDef(Id);
	Cell = TL;
	Size = Def->Size;
	Pos = RS::Cell2(TL) + FVector2D(Size.X, Size.Y) * 0.5f;
	MaxHp = Def->Hp;
	Hp = MaxHp;
	Armor = TEXT("bld");
	Sight = Def->Sight;
	if (!Def->Weapon.IsEmpty()) Weapon = &FRSDefs::Get().GetWeapon(Def->Weapon);
	BuildP = bInstant ? 1.f : 0.f;
	Turret = PI * 0.75f;
	AnimT = RS::Rand(0.f, 10.f);
	SetActorLocation(Game->ToWorld(Pos));
	RSV::BuildEntity(this);
	UpdateVisual(0.f);
}

FVector2D ARSBuilding::ClosestPoint(const FVector2D& P) const
{
	return FVector2D(FMath::Clamp(P.X, (float)Cell.X, (float)(Cell.X + Size.X)), FMath::Clamp(P.Y, (float)Cell.Y, (float)(Cell.Y + Size.Y)));
}

bool ARSBuilding::Powered() const
{
	return !Player->bLowPower || !(Weapon && Weapon->bPower);
}

bool ARSBuilding::Jammed() const
{
	return Game->Time < JamUntil;
}

float ARSBuilding::GroundZ() const
{
	if (Def && Def->bNaval) return 0.f;
	return Game ? Game->GroundHeight(Pos) : 0.f;
}

FVector ARSBuilding::Center3D() const
{
	return FVector(Pos.X * RS_CELL, Pos.Y * RS_CELL, GroundZ() + VisualTop * 0.45f);
}

void ARSBuilding::Update(float Dt)
{
	if (bDead) return;
	AnimT += Dt;
	FlashT -= Dt;
	CaptureFlash -= Dt;
	if (BuildP < 1.f)
	{
		BuildP = FMath::Min(1.f, BuildP + Dt / 2.8f);
		DustT -= Dt;
		if (DustT <= 0.f)
		{
			DustT = 0.35f;
			const FVector2D Span = FVector2D(Size.X, Size.Y) * 0.45f;
			Game->Fx->OreDust(Pos + FVector2D(RS::Rand(-Span.X, Span.X), RS::Rand(-Span.Y, Span.Y)));
		}
		if (BuildP >= 1.f)
		{
			Player->bPowerDirty = true;
			Game->OnBuildingComplete(this);
		}
	}
	if (bSelling)
	{
		SellT += Dt;
		if (SellT >= 1.f) Game->FinishSell(this);
		return;
	}
	if (bRepairing)
	{
		if (Hp >= MaxHp)
		{
			bRepairing = false;
		}
		else
		{
			const float Amt = MaxHp * 0.06f * Dt;
			const float Cost = Def->Cost * 0.5f * Amt / MaxHp;
			if (Player->Money >= Cost)
			{
				Player->Money -= Cost;
				Heal(Amt);
				if (RS::Rand01() < Dt * 3.f) Game->Fx->Spark(Pos + FVector2D(RS::Rand(-0.5f, 0.5f), RS::Rand(-0.5f, 0.5f)), 18.f);
			}
		}
	}
	if (HasWeapon() && Built()) UpdateDefense(Dt);
	if (Occupants.Num() > 0) UpdateGarrison(Dt);
	if (Def->Role == TEXT("oil") && !Player->bNeutral)
	{
		OilT += Dt;
		if (OilT >= 4.f)
		{
			OilT = 0.f;
			Player->Money += 40.0 * (Player->Sanctioned() ? 0.5 : 1.0) * (Game->WorldEv ? Game->WorldEv->OilBonus(Player) : 1.0);
		}
	}
	if (Def->Income > 0.f && Built())
	{
		OilT += Dt;
		if (OilT >= 3.f)
		{
			OilT = 0.f;
			Player->Money += Def->Income * (Player->Sanctioned() ? 0.3f : 1.f);
		}
	}
	FxT -= Dt;
	if (FxT <= 0.f && Built() && bShown)
	{
		FxT = 0.35f;
		const float R = Hp / MaxHp;
		const FVector2D Span = FVector2D(Size.X, Size.Y) * 0.3f;
		if (R < 0.5f) Game->Fx->Smoke(Pos + FVector2D(RS::Rand(-Span.X, Span.X), RS::Rand(-Span.Y, Span.Y)), 20.f + Size.X * 8.f, R < 0.25f);
		if (Def->Role == TEXT("power") && !Player->bLowPower && RS::Rand01() < 0.5f)
		{
			Game->Fx->Steam(Def->Faction == TEXT("allies") ? Pos + FVector2D(-0.4f, -0.4f) : Pos + FVector2D(0.15f, 0.f), 50.f);
		}
		if (Def->Role == TEXT("ref") && UnloadingT < 1.f && RS::Rand01() < 0.3f) Game->Fx->Steam(Pos + FVector2D(0.1f, -0.6f), 40.f);
	}
	UnloadingT += Dt;
}

void ARSBuilding::UpdateDefense(float Dt)
{
	if (!Powered() || Jammed()) return;
	Cd -= Dt;
	ScanT -= Dt;
	ARSEntity* T = Target.Get();
	if (!RSAlive(T) || T->DistTo(Pos) > Weapon->Range + 0.5f || !IsEnemy(T))
	{
		Target = nullptr;
		T = nullptr;
		if (ScanT <= 0.f)
		{
			ScanT = 0.3f;
			T = Game->FindTarget(this, *Weapon, Weapon->Range);
			Target = T;
		}
	}
	if (T)
	{
		const float Ang = RS::AngleOf(T->Pos - Pos);
		Turret = RS::AngleToAngle(Turret, Ang, 5.f * Dt);
		if (RS::ADiff(Turret, Ang) < 0.2f && Cd <= 0.f)
		{
			Cd = Weapon->Rof;
			float Z = 22.f;
			if (TypeId == TEXT("s_tcoil")) Z = 56.f;
			else if (TypeId == TEXT("a_prism")) Z = 58.f;
			Game->Fx->FireWeapon(this, *Weapon, Pos, Z, T);
		}
	}
}

void ARSBuilding::UpdateGarrison(float Dt)
{
	ScanT -= Dt;
	const float Bonus = TypeId == TEXT("civ_office") ? 2.f : 1.f;
	for (FRSOccupant& O : Occupants)
	{
		O.Cd -= Dt;
		if (!O.W || !O.W->IsValid() || O.Cd > 0.f) continue;
		ARSEntity* T = Game->FindTarget(this, *O.W, O.W->Range + Bonus);
		if (T)
		{
			O.Cd = O.W->Rof * RS::Rand(0.9f, 1.2f);
			const FVector2D From = Pos + FVector2D(RS::Rand(-0.4f, 0.4f), RS::Rand(-0.4f, 0.4f));
			Game->Fx->FireWeapon(this, *O.W, From, 26.f, T);
		}
	}
}

bool ARSBuilding::CanHit(const FRSWeapon* W, const ARSEntity* T) const
{
	if (!W || !T) return false;
	if (T->IsAir()) return W->bAir;
	if (W->bAirOnly) return false;
	return FRSDefs::Get().WarheadMult(W->Wh, T->Armor) > 0.01f;
}

void ARSBuilding::OnAttacked(ARSEntity* Attacker)
{
	Game->NotifyAttack(this, Attacker);
	if (Player == Game->Human && Game->Time - Player->LastUnderAttack > 18.f)
	{
		Player->LastUnderAttack = Game->Time;
		Game->Audio()->Say(TEXT("base_under_attack"), 18.f);
		Game->Message(TEXT("警告：基地遭受攻擊！"));
		Game->Ping(Pos);
	}
}

void ARSBuilding::UpdateVisual(float Dt)
{
	if (!Body) return;
	float Clip = BuildP;
	if (bSelling) Clip = 1.f - SellT;
	Clip = FMath::Clamp(Clip, 0.02f, 1.f);
	if (!FMath::IsNearlyEqual(Clip, LastVisualP))
	{
		LastVisualP = Clip;
		Body->SetRelativeScale3D(FVector(1.f, 1.f, Clip));
	}
	if (TurretPivot && HasWeapon()) TurretPivot->SetRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(Turret), 0.f));
	if (Spinner)
	{
		const FString& RoleId = Def->Role;
		if (RoleId == TEXT("oil"))
		{
			Spinner->SetRelativeRotation(FRotator(FMath::Sin(AnimT * 3.f) * 18.f, 0.f, 0.f));
		}
		else if (RoleId == TEXT("cy"))
		{
			Spinner->SetRelativeRotation(FRotator(0.f, FMath::Sin(AnimT * 0.4f) * 70.f, 0.f));
		}
		else if (!Player->bLowPower || RoleId == TEXT("def"))
		{
			Spinner->AddRelativeRotation(FRotator(0.f, Dt * 60.f, 0.f));
		}
	}
	// 佔領閃爍
	if (CaptureFlash > 0.f) SetActorHiddenInGame(bShown ? (int32(CaptureFlash * 8.f) % 2 == 0) : true);
	else if (IsHidden() == bShown) SetActorHiddenInGame(!bShown);
}
