#include "RSUnit.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSBuilding.h"
#include "RSFx.h"
#include "RSAudio.h"
#include "RSVisuals.h"
#include "RSMap.h"

void ARSUnit::Setup(ARSGame* G, URSPlayer* P, const FString& Id, const FVector2D& At)
{
	Game = G;
	Player = P;
	TypeId = Id;
	bIsBuilding = false;
	Def = &FRSDefs::Get().GetDef(Id);
	Pos = At;
	UKind = Def->Kind;
	Speed = Def->Speed;
	MaxHp = Def->Hp;
	Hp = MaxHp;
	Armor = Def->Armor;
	Sight = Def->Sight;
	bHasTurret = Def->bTurret;
	if (!Def->Weapon.IsEmpty()) Weapon = &FRSDefs::Get().GetWeapon(Def->Weapon);
	if (!Def->Weapon2.IsEmpty()) Weapon2 = &FRSDefs::Get().GetWeapon(Def->Weapon2);
	if (UKind == TEXT("inf")) { Radius = 0.17f; TurnRate = 10.f; }
	else if (UKind == TEXT("ship")) { Radius = 0.55f; TurnRate = 1.8f; }
	else if (UKind == TEXT("veh"))
	{
		Radius = 0.36f;
		TurnRate = 3.6f;
		if (Id.EndsWith(TEXT("mammoth")) || Id.EndsWith(TEXT("mcv")) || Id.EndsWith(TEXT("harv")))
		{
			Radius = 0.44f;
			TurnRate = 2.6f;
		}
	}
	else if (UKind == TEXT("air"))
	{
		Radius = 0.3f;
		TurnRate = Def->bJet ? 3.f : 1.2f;
		Alt = CruiseAlt();
		Ammo = HasWeapon() ? Weapon->Ammo : 0;
	}
	HomePos = Pos;
	ScanT = RS::Rand(0.f, 0.5f);
	RSV::BuildEntity(this);
	UpdateVisual(0.f);
}

float ARSUnit::CruiseAlt() const
{
	if (Def->bJet) return 70.f;
	if (TypeId == TEXT("a_apache")) return 48.f;
	if (TypeId == TEXT("a_tb2")) return 85.f;
	if (TypeId == TEXT("a_fpv")) return 30.f;
	if (TypeId == TEXT("s_lancet")) return 42.f;
	if (TypeId == TEXT("s_shahed")) return 55.f;
	return 50.f;
}

FVector ARSUnit::Center3D() const
{
	float Z = GroundZ();
	if (IsAir()) Z = Alt * RS_ZS;
	return FVector(Pos.X * RS_CELL, Pos.Y * RS_CELL, Z + VisualTop * 0.5f);
}

void ARSUnit::Update(float Dt)
{
	if (bDead) return;
	// 受損車輛冒煙（低於 20% 起火）
	if ((UKind == TEXT("veh") || UKind == TEXT("ship")) && bShown && Hp < MaxHp * 0.45f)
	{
		DmgSmokeT -= Dt;
		if (DmgSmokeT <= 0.f)
		{
			DmgSmokeT = Hp < MaxHp * 0.2f ? 0.25f : 0.5f;
			Game->Fx->Smoke(Pos + RS::FromAngle(Facing) * -0.15f, 16.f, Hp < MaxHp * 0.2f);
		}
	}
	if (ParaT >= 0.f)
	{
		ParaT -= Dt;
		if (ParaT <= 0.f)
		{
			ParaT = -1.f;
			Game->Fx->OreDust(Pos);
		}
		return;
	}
	AnimT += Dt;
	Cd -= Dt;
	Cd2 -= Dt;
	FireT -= Dt;
	FlashT -= Dt;
	ScanT -= Dt;
	Scan2T -= Dt;
	RepathT -= Dt;
	if (Hp < MaxHp)
	{
		if (Def->bHeal) Heal(MaxHp * 0.012f * Dt);
		else if (Rank == 2) Heal(MaxHp * 0.02f * Dt);
	}
	if (Burst > 0)
	{
		BurstT -= Dt;
		if (BurstT <= 0.f)
		{
			Burst--;
			BurstT = 0.2f;
			ARSEntity* T = Target.Get();
			if (RSAlive(T)) FireWeapon(Weapon, T);
		}
	}
	if (UKind == TEXT("air"))
	{
		UpdateAir(Dt);
	}
	else
	{
		UpdateGround(Dt);
		if (Weapon2 && Weapon2->IsValid() && !bDead) UpdateAA(Dt);
	}
	if (!bDead && UKind != TEXT("air")) ResolveSolid();
	if (!bDead && UKind == TEXT("veh") && bMoving) CrushInfantry();
	if (!bDead && UKind == TEXT("ship") && bMoving && RS::Rand01() < Dt * 8.f && bShown)
	{
		Game->Fx->Wake(Pos - RS::FromAngle(Facing) * 0.45f);
	}
}

// ───────────────────────── 指令 ─────────────────────────
void ARSUnit::QueueMove(const FVector2D& Dest, bool bAttackMove)
{
	if ((Order == TEXT("move") || Order == TEXT("amove") || Order == TEXT("engage_am")) && UKind != TEXT("air"))
	{
		FRSWaypoint W;
		W.P = Dest;
		W.bAttackMove = bAttackMove;
		Waypoints.Add(W);
	}
	else
	{
		CmdMove(Dest, bAttackMove);
	}
}

bool ARSUnit::NextWaypoint()
{
	if (Waypoints.Num() == 0) return false;
	const FRSWaypoint W = Waypoints[0];
	Waypoints.RemoveAt(0);
	TArray<FRSWaypoint> Rest = Waypoints;
	CmdMove(W.P, W.bAttackMove);
	Waypoints = Rest;
	return true;
}

void ARSUnit::CmdMove(const FVector2D& Dest, bool bAttackMove)
{
	Waypoints.Reset();
	FormSpeed = 0.f;
	Target = nullptr;
	MoveDest = Dest;
	if (UKind == TEXT("air"))
	{
		Order = TEXT("move");
		bLanded = false;
		return;
	}
	Order = (bAttackMove && HasWeapon()) ? TEXT("amove") : TEXT("move");
	AmoveDest = Dest;
	SetPath(Dest);
}

void ARSUnit::CmdAttack(ARSEntity* T)
{
	FormSpeed = 0.f;
	if (!T || (!HasWeapon() && !Def->bC4)) return;
	Target = T;
	if (Def->bC4)
	{
		const ARSUnit* TU = Cast<ARSUnit>(T);
		if (T->bIsBuilding || (TU && TU->UKind == TEXT("veh")))
		{
			Order = TEXT("c4");
			SetPath(T->Pos);
			return;
		}
	}
	if (!CanHit(Weapon, T)) return;
	Order = TEXT("attack");
	bLanded = false;
	if (UKind != TEXT("air")) SetPath(T->Pos);
}

void ARSUnit::CmdStop()
{
	Waypoints.Reset();
	Target = nullptr;
	Path.Reset();
	Order = TEXT("idle");
	HomePos = Pos;
	bMoving = false;
}

void ARSUnit::CmdCapture(ARSBuilding* B)
{
	Target = B;
	Order = TEXT("capture");
	SetPath(B->Pos);
}

void ARSUnit::CmdGarrison(ARSBuilding* B)
{
	Target = B;
	Order = TEXT("garrison");
	SetPath(B->Pos);
}

void ARSUnit::CmdHarvest(FIntPoint Cell)
{
	OreCell = Cell;
	Order = TEXT("harvest");
	Target = nullptr;
	Path.Reset();
}

void ARSUnit::CmdDock(ARSBuilding* Ref)
{
	Dock = Ref;
	Order = TEXT("toref");
	Path.Reset();
}

void ARSUnit::CmdDeploy()
{
	if (Def->Mcv.IsEmpty()) return;
	Order = TEXT("deploy");
	Path.Reset();
}

bool ARSUnit::CanHit(const FRSWeapon* W, const ARSEntity* T) const
{
	if (!W || !W->IsValid() || !T) return false;
	if (T->IsAir()) return W->bAir;
	if (W->bAirOnly) return false;
	return FRSDefs::Get().WarheadMult(W->Wh, T->Armor) > 0.01f;
}

// ───────────────────────── 地面 ─────────────────────────
void ARSUnit::UpdateGround(float Dt)
{
	if (Def->bHarvester)
	{
		UpdateHarvester(Dt);
		Separate(Dt);
		return;
	}
	if (Order == TEXT("idle"))
	{
		bMoving = false;
		if (bHasTurret) Turret = RS::AngleToAngle(Turret, Facing, 2.f * Dt);
		if (HasWeapon() && ScanT <= 0.f)
		{
			ScanT = 0.35f + RS::Rand(0.f, 0.25f);
			ARSEntity* T = Game->FindTarget(this, *Weapon, WeaponRange() + (UKind == TEXT("inf") ? 1.f : 1.5f));
			if (T)
			{
				Target = T;
				Order = TEXT("engage");
				HomePos = Pos;
			}
		}
	}
	else if (Order == TEXT("move"))
	{
		if (Follow(Dt))
		{
			if (!NextWaypoint())
			{
				Order = TEXT("idle");
				HomePos = Pos;
			}
		}
		else if (bHasTurret && HasWeapon())
		{
			FireOnMove(Dt);
		}
	}
	else if (Order == TEXT("amove"))
	{
		if (ScanT <= 0.f)
		{
			ScanT = 0.35f + RS::Rand(0.f, 0.2f);
			ARSEntity* T = Game->FindTarget(this, *Weapon, WeaponRange() + 2.f);
			if (T)
			{
				Target = T;
				Order = TEXT("engage_am");
			}
		}
		if (Order == TEXT("amove") && Follow(Dt))
		{
			if (!NextWaypoint())
			{
				Order = TEXT("idle");
				HomePos = Pos;
			}
		}
	}
	else if (Order == TEXT("attack") || Order == TEXT("engage") || Order == TEXT("engage_am"))
	{
		ARSEntity* T = Target.Get();
		if (!RSAlive(T) || T->Player == Player)
		{
			LostTarget();
		}
		else if (Order == TEXT("engage") && FVector2D::Distance(Pos, HomePos) > 7.f)
		{
			Target = nullptr;
			Order = TEXT("return_home");
			SetPath(HomePos);
		}
		else
		{
			AttackStep(Dt, T);
			if (Order == TEXT("engage_am") && ScanT <= 0.f)
			{
				ScanT = 0.8f;
				if (T->bIsBuilding && T->Def->Weapon.IsEmpty())
				{
					ARSEntity* T2 = Game->FindTarget(this, *Weapon, WeaponRange() + 1.f, true);
					if (T2 && !T2->bIsBuilding) Target = T2;
				}
			}
		}
	}
	else if (Order == TEXT("return_home"))
	{
		if (Follow(Dt)) Order = TEXT("idle");
		if (ScanT <= 0.f && HasWeapon())
		{
			ScanT = 0.5f;
			ARSEntity* T = Game->FindTarget(this, *Weapon, WeaponRange() + 0.5f);
			if (T)
			{
				Target = T;
				Order = TEXT("engage");
			}
		}
	}
	else if (Order == TEXT("capture") || Order == TEXT("garrison") || Order == TEXT("c4"))
	{
		ARSEntity* T = Target.Get();
		if (!RSAlive(T))
		{
			CmdStop();
		}
		else
		{
			const float Reach = Order == TEXT("capture") ? 0.55f : 0.6f;
			if (T->DistTo(Pos) < Reach)
			{
				if (Order == TEXT("capture"))
				{
					if (ARSBuilding* B = Cast<ARSBuilding>(T)) Game->EngineerEnter(this, B);
					else CmdStop();
					return;
				}
				if (Order == TEXT("garrison"))
				{
					if (ARSBuilding* B = Cast<ARSBuilding>(T)) Game->GarrisonEnter(this, B);
					else CmdStop();
					return;
				}
				Game->PlantC4(this, T);
				CmdStop();
			}
			else
			{
				Chase(Dt, T);
			}
		}
	}
	else if (Order == TEXT("deploy"))
	{
		Game->TryDeploy(this);
		if (!bDead) Order = TEXT("idle");
	}
	if (!bDead) Separate(Dt);
}

void ARSUnit::LostTarget()
{
	Target = nullptr;
	if (Order == TEXT("engage_am"))
	{
		Order = TEXT("amove");
		SetPath(AmoveDest);
	}
	else if (Order == TEXT("engage"))
	{
		Order = TEXT("return_home");
		if (FVector2D::Distance(Pos, HomePos) > 1.f) SetPath(HomePos);
	}
	else
	{
		Order = TEXT("idle");
		HomePos = Pos;
	}
}

void ARSUnit::Chase(float Dt, ARSEntity* T)
{
	if (RepathT <= 0.f || Path.Num() == 0)
	{
		RepathT = 0.9f + RS::Rand(0.f, 0.4f);
		SetPath(T->Pos);
	}
	Follow(Dt);
}

void ARSUnit::AttackStep(float Dt, ARSEntity* T)
{
	const float D = T->DistTo(Pos);
	float Rng = WeaponRange() * (1.f + 0.1f * Rank);
	if (Game->HighGround(this, T)) Rng += 1.f;
	const float MinR = Weapon->MinRange;
	if (D > Rng)
	{
		if (UKind == TEXT("air")) return;
		Chase(Dt, T);
		if (bHasTurret) Turret = RS::AngleToAngle(Turret, RS::AngleOf(T->Pos - Pos), 5.f * Dt);
		return;
	}
	if (D < MinR)
	{
		LostTarget();
		return;
	}
	Path.Reset();
	bMoving = false;
	AimAndFire(Dt, T, Weapon);
}

FVector2D ARSUnit::AimPoint(const ARSEntity* T) const
{
	return T->bIsBuilding ? T->ClosestPoint(Pos) : T->Pos;
}

bool ARSUnit::AimAndFire(float Dt, ARSEntity* T, const FRSWeapon* W)
{
	const float Ang = RS::AngleOf(AimPoint(T) - Pos);
	bool bAimed = false;
	if (!Def->Carrier.IsEmpty())
	{
		bAimed = true;
	}
	else if (bHasTurret)
	{
		Turret = RS::AngleToAngle(Turret, Ang, 6.f * Dt);
		bAimed = RS::ADiff(Turret, Ang) < 0.18f;
	}
	else if (UKind == TEXT("inf"))
	{
		Facing = Ang;
		bAimed = true;
	}
	else
	{
		Facing = RS::AngleToAngle(Facing, Ang, TurnRate * Dt);
		bAimed = RS::ADiff(Facing, Ang) < 0.2f;
	}
	if (bAimed && Cd <= 0.f)
	{
		FireWeapon(W, T);
		Cd = W->Rof * (1.f - 0.1f * Rank);
		if (W->Burst > 0)
		{
			Burst = W->Burst - 1;
			BurstT = 0.2f;
		}
		return true;
	}
	return false;
}

void ARSUnit::FireOnMove(float Dt)
{
	if (ScanT <= 0.f)
	{
		ScanT = 0.4f;
		Target = Game->FindTarget(this, *Weapon, WeaponRange());
	}
	ARSEntity* T = Target.Get();
	if (RSAlive(T) && T->DistTo(Pos) <= WeaponRange())
	{
		const float Ang = RS::AngleOf(AimPoint(T) - Pos);
		Turret = RS::AngleToAngle(Turret, Ang, 6.f * Dt);
		if (RS::ADiff(Turret, Ang) < 0.18f && Cd <= 0.f)
		{
			FireWeapon(Weapon, T);
			Cd = Weapon->Rof;
		}
	}
	else
	{
		Target = nullptr;
		Turret = RS::AngleToAngle(Turret, Facing, 3.f * Dt);
	}
}

void ARSUnit::UpdateAA(float Dt)
{
	if (Scan2T <= 0.f)
	{
		Scan2T = 0.5f;
		ARSEntity* T = Game->FindTarget(this, *Weapon2, Weapon2->Range, false, true);
		if (T && Cd2 <= 0.f)
		{
			FireWeapon(Weapon2, T);
			Cd2 = Weapon2->Rof;
		}
	}
}

void ARSUnit::FireWeapon(const FRSWeapon* W, ARSEntity* T)
{
	if (!W || !T) return;
	if (!Def->Carrier.IsEmpty() && W == Weapon)
	{
		LaunchJet(T);
		return;
	}
	if (W->Proj == TEXT("kamikaze") && UKind != TEXT("air"))
	{
		if (RSAlive(T)) Game->DroneDetonate(this, T);
		return;
	}
	const float Ang = bHasTurret ? Turret : Facing;
	float Z = 14.f;
	if (UKind == TEXT("inf")) Z = 13.f;
	else if (UKind == TEXT("veh")) Z = bHasTurret ? 20.f : 16.f;
	else if (UKind == TEXT("air")) Z = Alt;
	const FVector2D From = Pos + RS::FromAngle(Ang) * Radius * 0.9f;
	Game->Fx->FireWeapon(this, *W, From, Z, T);
	FireT = 0.22f;
}

void ARSUnit::LaunchJet(ARSEntity* T)
{
	int32 N = 0;
	for (ARSUnit* O : Game->Units)
	{
		if (O && !O->bDead && O->bFromCarrier && O->CarrierHome.Get() == this) N++;
	}
	if (N >= Def->Hangar) return;
	ARSUnit* J = Game->SpawnUnit(Player, Def->Carrier, Pos + RS::FromAngle(Facing) * 0.6f);
	if (!J) return;
	J->CarrierHome = this;
	J->bFromCarrier = true;
	J->Facing = Facing;
	J->Alt = 18.f;
	J->bLanded = false;
	J->Target = T;
	J->Order = TEXT("attack");
	FireT = 0.22f;
	Game->Audio()->Play(TEXT("jet"), -6.f, &Pos);
}

void ARSUnit::OnAttacked(ARSEntity* Attacker)
{
	if (Player == Game->Human && Def->bHarvester && Game->Time - Player->LastUnderAttack > 15.f)
	{
		Player->LastUnderAttack = Game->Time;
		Game->Message(TEXT("採礦車遭受攻擊！"));
		Game->Audio()->Say(TEXT("base_under_attack"), 15.f);
	}
	if (UKind == TEXT("air")) return;
	if ((Order == TEXT("idle") || Order == TEXT("return_home")) && CanHit(Weapon, Attacker))
	{
		if (Attacker->DistTo(Pos) < WeaponRange() + 5.f)
		{
			Target = Attacker;
			Order = TEXT("engage");
			HomePos = Pos;
		}
	}
	Game->NotifyAttack(this, Attacker);
}

// ───────────────────────── 移動 ─────────────────────────
bool ARSUnit::SetPath(const FVector2D& To)
{
	Path = Game->FindPath(Pos, To, UKind == TEXT("ship"));
	Repaths = 0;
	StuckT = 0.f;
	StuckRef = Pos;
	return Path.Num() > 0;
}

bool ARSUnit::Follow(float Dt)
{
	if (Path.Num() == 0)
	{
		bMoving = false;
		return true;
	}
	const FVector2D Wp = Path[0];
	const FVector2D To = Wp - Pos;
	const float D = To.Size();
	float Spd = Speed;
	if (FormSpeed > 0.f && (Order == TEXT("move") || Order == TEXT("amove"))) Spd = FMath::Min(Spd, FormSpeed);
	const bool bShip = UKind == TEXT("ship");
	if (UKind == TEXT("veh") || bShip)
	{
		const float Ang = RS::AngleOf(To);
		if (D > 0.05f) Facing = RS::AngleToAngle(Facing, Ang, TurnRate * Dt);
		const float Df = RS::ADiff(Facing, Ang);
		if (Df > 1.f && D > 0.15f)
		{
			bMoving = true;
			return false;
		}
		Spd *= 1.f - 0.6f * FMath::Min(1.f, Df);
	}
	else if (D > 0.01f)
	{
		Facing = RS::AngleOf(To);
	}
	const float StepLen = Spd * Dt;
	bMoving = true;
	if (D <= StepLen)
	{
		Pos = Wp;
		Path.RemoveAt(0);
		if (Path.Num() == 0)
		{
			bMoving = false;
			return true;
		}
		if (!Game->PassablePos(Path[0], bShip) && Path.Num() > 1)
		{
			SetPath(Path.Last());
		}
	}
	else
	{
		Pos += To / D * StepLen;
	}
	StuckT += Dt;
	if (StuckT > 1.2f)
	{
		if (FVector2D::Distance(Pos, StuckRef) < 0.2f && Path.Num() > 0)
		{
			Repaths++;
			if (Repaths > 3 || FVector2D::Distance(Pos, Path.Last()) < 1.6f)
			{
				Path.Reset();
				bMoving = false;
				return true;
			}
			const FVector2D Dest = Path.Last();
			Path = Game->FindPath(Pos, Dest, bShip);
		}
		StuckRef = Pos;
		StuckT = 0.f;
	}
	return false;
}

float ARSUnit::WeaponRange() const
{
	if (!HasWeapon()) return 0.f;
	float R = Weapon->Range;
	// 高地：地面直射武器射程 +1 格
	if (UKind != TEXT("air") && Game && Game->Map.IsValid() && Game->Map->ElevAt(Pos) > 0)
	{
		const FString& Pj = Weapon->Proj;
		if (Pj == TEXT("bullet") || Pj == TEXT("shell") || Pj == TEXT("rocket") || Pj == TEXT("beam") || Pj == TEXT("tesla")) R += 1.f;
	}
	return R;
}

// 戰車輾壓：行進中的車輛壓過敵方步兵即擊殺
void ARSUnit::CrushInfantry()
{
	TArray<ARSUnit*> Nb;
	Game->Neighbors(Pos, Nb);
	for (ARSUnit* O : Nb)
	{
		if (!O || O == this || O->bDead || O->UKind != TEXT("inf") || !IsEnemy(O) || O->ParaT >= 0.f) continue;
		if (FVector2D::Distance(O->Pos, Pos) < Radius * 0.8f)
		{
			Game->Fx->Puff(Game->Fx->S(O->Pos, 4.f), 0.5f, FLinearColor(0.45f, 0.08f, 0.06f));
			O->ApplyDamage(O->MaxHp * 50.f, this, TEXT("ap"));
		}
	}
}

void ARSUnit::ResolveSolid()
{
	// 進入建築的動作（卸礦、佔領、進駐、展開）不受限制
	static const TSet<FString> Exempt = { TEXT("unload"), TEXT("capture"), TEXT("garrison"), TEXT("toref"), TEXT("deploy") };
	if (Exempt.Contains(Order) || ParaT >= 0.f) return;
	const bool bShip = UKind == TEXT("ship");
	// 中心已在阻擋格內（剛生成或被擠進去）時交給尋路帶出來，避免卡死
	if (!Game->PassablePos(Pos, bShip)) return;
	const float R = Radius * 0.95f;
	const int32 CX = FMath::FloorToInt(Pos.X), CY = FMath::FloorToInt(Pos.Y);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				if (DX == 0 && DY == 0) continue;
				const int32 X = CX + DX, Y = CY + DY;
				if (Game->PassablePos(FVector2D(X + 0.5f, Y + 0.5f), bShip)) continue;
				// 圓與方格的最近點，有重疊就推開
				const FVector2D Q(FMath::Clamp(Pos.X, float(X), float(X + 1)), FMath::Clamp(Pos.Y, float(Y), float(Y + 1)));
				const FVector2D Dv = Pos - Q;
				const float D = Dv.Size();
				if (D < R && D > 1e-4f)
				{
					const FVector2D NP = Pos + Dv / D * (R - D);
					if (Game->PassablePos(NP, bShip)) Pos = NP;
				}
			}
	}
}

void ARSUnit::Separate(float Dt)
{
	FVector2D Push = FVector2D::ZeroVector;
	TArray<ARSUnit*> Nb;
	Game->Neighbors(Pos, Nb);
	for (ARSUnit* O : Nb)
	{
		if (O == this || O->bDead || O->UKind == TEXT("air")) continue;
		const FVector2D Dv = Pos - O->Pos;
		const float Dd = Dv.Size();
		const float MinR = Radius + O->Radius + ((UKind == TEXT("inf") && O->UKind == TEXT("veh") && O->bMoving && IsEnemy(O)) ? 0.35f : 0.f);
		if (Dd < MinR)
		{
			if (Dd < 0.0001f)
			{
				Push += RS::FromAngle(RS::Rand(0.f, 2.f * PI)) * 0.05f;
			}
			else
			{
				float Wt = (bMoving && !O->bMoving) ? 0.6f : 1.f;
				// 步兵閃避逼近的敵方車輛
				if (UKind == TEXT("inf") && O->UKind == TEXT("veh") && O->bMoving && IsEnemy(O)) Wt *= 3.f;
				// 質量：步兵被車輛推開較多，車輛幾乎不被步兵推動
				const bool bMeInf = UKind == TEXT("inf"), bOInf = O->UKind == TEXT("inf");
				if (bMeInf && !bOInf) Wt *= 1.6f;
				else if (!bMeInf && bOInf) Wt *= 0.25f;
				Push += Dv / Dd * (MinR - Dd) * Wt;
			}
		}
	}
	if (!Push.IsNearlyZero())
	{
		Push = RS::LimitLen(Push, 5.f * Dt);
		const FVector2D NP = Pos + Push;
		if (Game->PassablePos(NP, UKind == TEXT("ship"))) Pos = NP;
	}
}

// ───────────────────────── 採礦車 ─────────────────────────
void ARSUnit::UpdateHarvester(float Dt)
{
	FRSMap& M = *Game->Map;
	if (Order == TEXT("idle") || Order == TEXT("move"))
	{
		if (Order == TEXT("move"))
		{
			if (Follow(Dt))
			{
				Order = TEXT("idle");
				IdleT = 0.f;
			}
			return;
		}
		IdleT += Dt;
		bMoving = false;
		if (IdleT > 3.f)
		{
			IdleT = 0.f;
			if (M.FindOreNear(Pos, 60.f).X >= 0) Order = Cargo < CARGO_MAX ? TEXT("harvest") : TEXT("toref");
		}
	}
	else if (Order == TEXT("harvest"))
	{
		if (Cargo >= CARGO_MAX)
		{
			Order = TEXT("toref");
			Dock = nullptr;
			Path.Reset();
			return;
		}
		if (OreCell.X < 0 || !M.HasOre(OreCell.X, OreCell.Y) || Game->BldAt(OreCell.X, OreCell.Y) != nullptr)
		{
			OreCell = M.FindOreNear(Pos, 60.f);
			Path.Reset();
			if (OreCell.X < 0)
			{
				Order = Cargo > 0.f ? TEXT("toref") : TEXT("idle");
				return;
			}
			SetPath(RS::CellCenter(OreCell));
		}
		const FVector2D CC = RS::CellCenter(OreCell);
		const float Dd = FVector2D::Distance(Pos, CC);
		if (Dd < 0.4f || (Path.Num() == 0 && Dd < 1.f))
		{
			bMoving = false;
			Path.Reset();
			MineT += Dt;
			Facing += FMath::Sin(AnimT * 2.f) * Dt * 0.3f;
			if (MineT >= 0.45f)
			{
				MineT = 0.f;
				const int32 Got = M.TakeOre(OreCell.X, OreCell.Y, 45);
				Cargo += Got;
				if (Got > 0 && RS::Rand01() < 0.5f && bShown) Game->Fx->OreDust(Pos);
				if (Got == 0) OreCell = FIntPoint(-1, -1);
			}
		}
		else
		{
			if (Path.Num() == 0)
			{
				if (RepathT <= 0.f)
				{
					RepathT = 1.f;
					if (!SetPath(CC)) OreCell = FIntPoint(-1, -1);
				}
			}
			else if (Follow(Dt) && FVector2D::Distance(Pos, CC) > 1.f)
			{
				OreCell = M.FindOreNear(Pos + FVector2D(RS::Rand(-3, 3), RS::Rand(-3, 3)), 60.f);
			}
		}
	}
	else if (Order == TEXT("toref"))
	{
		ARSBuilding* Dk = Dock.Get();
		if (!Dk || Dk->bDead || Dk->Player != Player)
		{
			Dk = Game->NearestRefinery(Player, Pos);
			Dock = Dk;
			Path.Reset();
			if (!Dk)
			{
				Order = TEXT("idle");
				return;
			}
		}
		const FVector2D DP = Dk->ClosestPoint(Pos);
		if (Dk->DistTo(Pos) < 0.95f)
		{
			Order = TEXT("unload");
			MineT = 0.f;
			Path.Reset();
			bMoving = false;
		}
		else
		{
			if (Path.Num() == 0 && RepathT <= 0.f)
			{
				RepathT = 1.2f;
				SetPath(DP);
			}
			Follow(Dt);
		}
	}
	else if (Order == TEXT("unload"))
	{
		bMoving = false;
		ARSBuilding* Dk = Dock.Get();
		if (!Dk || Dk->bDead)
		{
			Order = TEXT("toref");
			return;
		}
		Facing = RS::AngleToAngle(Facing, PI * 0.5f, 2.f * Dt);
		MineT += Dt;
		if (MineT >= 0.15f)
		{
			MineT = 0.f;
			const float Amt = FMath::Min(Cargo, 50.f);
			Cargo -= Amt;
			const float Gain = Amt * Player->IncomeFactor();
			Player->Money += Gain;
			Player->Stats.Harvested += Gain;
			if (Player == Game->Human && RS::Rand01() < 0.3f) Game->Audio()->Play(TEXT("tick"), -14.f);
			if (Cargo <= 0.f)
			{
				Cargo = 0.f;
				Order = TEXT("harvest");
				Dk->UnloadingT = 0.f;
			}
		}
	}
}

// ───────────────────────── 飛行器 ─────────────────────────
ARSEntity* ARSUnit::DroneTarget()
{
	if (Def->bPreferBld)
	{
		ARSBuilding* Best = nullptr;
		float BD = Sight;
		for (ARSBuilding* B : Game->Buildings)
		{
			if (!B || B->bDead || !Player->IsEnemyOf(B->Player) || B->Def->bWall) continue;
			const float D = B->DistTo(Pos);
			if (D < BD)
			{
				BD = D;
				Best = B;
			}
		}
		if (Best) return Best;
	}
	if (!HasWeapon()) return nullptr;
	FRSWeapon W = *Weapon;
	if (Def->bKamikaze) W.Range = Sight;
	return Game->FindTarget(this, W, Sight, false);
}

void ARSUnit::UpdateAir(float Dt)
{
	const bool bJet = Def->bJet;
	const bool bKami = Def->bKamikaze;
	const bool bLoiter = Def->bLoiter;
	float TgtAlt = CruiseAlt();
	if (bLanded)
	{
		TgtAlt = 16.f;
		ARSBuilding* Af = bJet ? Game->NearestAirfield(Player, Pos) : nullptr;
		if (Af && Af->Def->bRunway && FVector2D::Distance(Af->Pos, Pos) < 3.f) TgtAlt = 3.f;
	}
	ARSEntity* T = Target.Get();
	if (bKami && Order == TEXT("attack") && RSAlive(T))
	{
		const float Dd = T->DistTo(Pos);
		if (Dd < 2.5f) TgtAlt = FMath::Lerp(6.f, CruiseAlt(), Dd / 2.5f);
	}
	Alt = FMath::FInterpConstantTo(Alt, TgtAlt, Dt, bKami ? 90.f : 40.f);
	JamT -= Dt;
	if (Order == TEXT("idle"))
	{
		bMoving = false;
		if (bJet && !bLanded)
		{
			Order = TEXT("return");
			bApproach = false;
		}
		else if (!bJet && ScanT <= 0.f)
		{
			ScanT = Def->bDrone ? 0.6f : 1.f;
			ARSEntity* NT = Def->bDrone ? DroneTarget() : (HasWeapon() ? Game->FindTarget(this, *Weapon, Sight, true) : nullptr);
			if (NT)
			{
				Target = NT;
				Order = TEXT("attack");
			}
		}
		if (bJet && bLanded && HasWeapon() && Ammo < Weapon->Ammo)
		{
			IdleT += Dt;
			if (IdleT > 2.f)
			{
				IdleT = 0.f;
				Ammo++;
			}
		}
		if (Def->bDrone && !bLanded)
		{
			Pos += RS::FromAngle(AnimT * 1.3f + EntId) * 0.35f * Dt;
			Facing = AnimT * 1.3f + EntId + PI * 0.5f;
		}
	}
	else if (Order == TEXT("move"))
	{
		bLanded = false;
		if (Def->bDrone && ScanT <= 0.f && FVector2D::Distance(Pos, MoveDest) < 4.f)
		{
			ScanT = 0.5f;
			ARSEntity* NT = DroneTarget();
			if (NT && NT->DistTo(Pos) < 5.f)
			{
				Target = NT;
				Order = TEXT("attack");
				return;
			}
		}
		if (FlyTo(Dt, MoveDest, 0.3f))
		{
			Order = TEXT("idle");
			if (bJet)
			{
				Order = TEXT("return");
				bApproach = false;
			}
		}
	}
	else if (Order == TEXT("attack"))
	{
		bLanded = false;
		if (!RSAlive(T))
		{
			Target = nullptr;
			Order = bJet ? TEXT("return") : TEXT("idle");
			bApproach = false;
			return;
		}
		if (bJet && Ammo <= 0)
		{
			Order = TEXT("return");
			bApproach = false;
			return;
		}
		const float D = T->DistTo(Pos);
		if (bKami)
		{
			const FVector2D TP = T->bIsBuilding ? T->ClosestPoint(Pos) : T->Pos;
			FlyTo(Dt, TP, 0.f);
			if (D < 0.45f) Game->DroneDetonate(this, T);
			return;
		}
		if (bJet)
		{
			const float Ang = RS::AngleOf(T->Pos - Pos);
			Facing = RS::AngleToAngle(Facing, Ang, TurnRate * Dt);
			Pos += RS::FromAngle(Facing) * Speed * Dt;
			bMoving = true;
			if (D < WeaponRange() && RS::ADiff(Facing, Ang) < 0.35f && Cd <= 0.f)
			{
				FireWeapon(Weapon, T);
				Cd = Weapon->Rof;
				Ammo--;
			}
		}
		else if (bLoiter)
		{
			if (D > WeaponRange() * 0.8f)
			{
				FlyTo(Dt, T->Pos, 0.2f);
			}
			else
			{
				bMoving = false;
				Pos += RS::FromAngle(AnimT * 0.8f + EntId) * 0.4f * Dt;
				Facing = RS::AngleToAngle(Facing, RS::AngleOf(T->Pos - Pos), 2.f * Dt);
				if (Cd <= 0.f && D <= WeaponRange())
				{
					FireWeapon(Weapon, T);
					Cd = Weapon->Rof;
				}
			}
		}
		else
		{
			if (D > 0.4f) FlyTo(Dt, T->Pos, 0.2f);
			if (HasWeapon() && D <= WeaponRange() && Cd <= 0.f)
			{
				FireWeapon(Weapon, T);
				Cd = Weapon->Rof;
			}
		}
	}
	else if (Order == TEXT("return"))
	{
		if (bFromCarrier)
		{
			ARSUnit* Cv = CarrierHome.Get();
			if (!RSAlive(Cv))
			{
				Game->Fx->Explosion(Pos, 0.8f, Alt);
				Game->RemoveUnitSilent(this);
				return;
			}
			if (FlyTo(Dt, Cv->Pos, 0.6f)) Game->RemoveUnitSilent(this);
			return;
		}
		ARSBuilding* Af = Game->NearestAirfield(Player, Pos);
		if (!Af)
		{
			Order = TEXT("move");
			MoveDest = Pos;
			return;
		}
		if (Af->Def->bRunway)
		{
			// 沿跑道降落：先飛到跑道頭外的進場點，再沿跑道中心線滑行到停機位置
			const float RY = Af->Pos.Y + 0.5f;
			int32 Slot = 0;
			for (ARSUnit* O : Game->Units)
			{
				if (O != this && O && !O->bDead && O->bLanded && O->Player == Player && FVector2D::Distance(O->Pos, Af->Pos) < 3.f) Slot++;
			}
			const FVector2D Park(Af->Pos.X + 1.2f - (Slot % 4) * 0.75f, RY);
			if (!bApproach)
			{
				if (FlyTo(Dt, FVector2D(Af->Pos.X - 3.5f, RY), 0.6f)) bApproach = true;
			}
			else if (FlyTo(Dt, Park, 0.2f))
			{
				bApproach = false;
				Facing = 0.f;
				bLanded = true;
				Order = TEXT("idle");
				IdleT = 0.f;
			}
		}
		else if (FlyTo(Dt, Af->Pos + FVector2D(0.3f, 0.3f), 0.25f))
		{
			bLanded = true;
			Order = TEXT("idle");
			IdleT = 0.f;
		}
	}
	if (JamT > 0.f) Pos += FVector2D(RS::Rand(-1, 1), RS::Rand(-1, 1)) * 0.6f * Dt;
	Pos.X = FMath::Clamp(Pos.X, 0.5f, Game->W - 0.5f);
	Pos.Y = FMath::Clamp(Pos.Y, 0.5f, Game->H - 0.5f);
}

bool ARSUnit::FlyTo(float Dt, const FVector2D& P, float Tol)
{
	const FVector2D To = P - Pos;
	const float D = To.Size();
	if (D <= Tol)
	{
		bMoving = false;
		return true;
	}
	const float Ang = RS::AngleOf(To);
	Facing = RS::AngleToAngle(Facing, Ang, TurnRate * Dt);
	float Spd = Speed;
	if (RS::ADiff(Facing, Ang) > 0.8f && Def->bJet) Spd *= 0.7f;
	if (!Def->bJet) Pos += To / D * FMath::Min(Spd * Dt, D);
	else Pos += RS::FromAngle(Facing) * FMath::Min(Spd * Dt, D + 0.1f);
	bMoving = true;
	return false;
}

// ───────────────────────── 外觀 ─────────────────────────
void ARSUnit::UpdateVisual(float Dt)
{
	float Z = Game->GroundHeight(Pos);
	FVector Loc = Game->ToWorld(Pos);
	if (UKind == TEXT("air"))
	{
		const float Bob = bLanded ? 0.f : FMath::Sin(AnimT * 2.f) * 2.f;
		Loc.Z = FMath::Max(Z, 0.f) + (Alt + Bob) * RS_ZS;
	}
	else if (UKind == TEXT("ship"))
	{
		Loc.Z = FMath::Sin(AnimT * 1.7f + EntId) * 2.f - 2.f;
	}
	if (ParaT >= 0.f)
	{
		Loc.Z += FMath::Min(ParaT, 2.f) * 70.f * RS_ZS;
	}
	SetActorLocation(Loc);
	float Roll = 0.f, Pitch = 0.f;
	if (UKind == TEXT("air") && bMoving && !Def->bAirship) Roll = 10.f * FMath::Sin(AnimT * 1.3f);
	if (UKind == TEXT("inf") && Body)
	{
		// 走路動畫：雙腿前後擺動、身體上下起伏並微微前傾；停下時回到站姿
		const float Ph = AnimT * 11.f;
		const float Swing = bMoving ? FMath::Sin(Ph) * 34.f : 0.f;
		Body->SetRelativeLocation(FVector(0, 0, bMoving ? FMath::Abs(FMath::Cos(Ph)) * 2.5f : 0.f));
		Body->SetRelativeRotation(FRotator(bMoving ? -7.f : 0.f, 0.f, bMoving ? FMath::Sin(Ph) * 3.f : 0.f));
		if (Limbs.Num() >= 2)
		{
			if (Limbs[0]) Limbs[0]->SetRelativeRotation(FRotator(Swing, 0.f, 0.f));
			if (Limbs[1]) Limbs[1]->SetRelativeRotation(FRotator(-Swing, 0.f, 0.f));
		}
	}
	SetActorRotation(FRotator(Pitch, FMath::RadiansToDegrees(Facing), Roll));
	if (TurretPivot && bHasTurret)
	{
		TurretPivot->SetRelativeRotation(FRotator(0.f, FMath::RadiansToDegrees(RS::WrapPi(Turret - Facing)), 0.f));
	}
	if (Spinner)
	{
		Spinner->AddRelativeRotation(FRotator(0.f, Dt * (Def->bHeli ? 1400.f : 90.f), 0.f));
	}
}
