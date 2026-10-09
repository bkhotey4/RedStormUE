#include "RSAI.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSMap.h"

namespace
{
	struct FPlanStep { const TCHAR* Role; int32 N; };
	const FPlanStep PLAN[] = {
		{TEXT("power"), 1}, {TEXT("ref"), 1}, {TEXT("bar"), 1}, {TEXT("power"), 2}, {TEXT("fac"), 1}, {TEXT("ref"), 2}, {TEXT("radar"), 1}, {TEXT("def"), 2},
		{TEXT("power"), 3}, {TEXT("yard"), 1}, {TEXT("dronehub"), 1}, {TEXT("tech"), 1}, {TEXT("def"), 4}, {TEXT("power"), 4}, {TEXT("bank"), 1}, {TEXT("sw"), 1},
		{TEXT("ref"), 3}, {TEXT("power"), 5}, {TEXT("def"), 6}, {TEXT("fac"), 2}, {TEXT("bar"), 2}, {TEXT("power"), 6}, {TEXT("def"), 8} };
}

void URSAI::Setup(ARSGame* G, URSPlayer* Pl, int32 D)
{
	Game = G;
	P = Pl;
	Diff = D;
	Pre = P->Faction == TEXT("allies") ? TEXT("a_") : TEXT("s_");
	switch (Diff)
	{
	case 0: NextAttack = 480.f; AttackSize = 5; break;
	case 2: NextAttack = 200.f; AttackSize = 10; break;
	default: NextAttack = 300.f; AttackSize = 8; break;
	}
	NextAttack += RS::Rand(-20.f, 20.f);
}

bool URSAI::NavalOk()
{
	if (WaterOk < 0)
	{
		const FVector2D BP = BasePos();
		int32 N = 0;
		const FRSMap& M = *Game->Map;
		for (int32 Y = int32(BP.Y) - 20; Y <= int32(BP.Y) + 20; ++Y)
			for (int32 X = int32(BP.X) - 20; X <= int32(BP.X) + 20; ++X)
				if (M.Inb(X, Y) && M.Terrain[M.Idx(X, Y)] == FRSMap::T_WATER) N++;
		WaterOk = N > 40 ? 1 : 0;
	}
	return WaterOk == 1;
}

FVector2D URSAI::BasePos() const
{
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == P && B->Def->Role == TEXT("cy") && !B->bDead) return B->Pos;
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == P && !B->bDead) return B->Pos;
	return RS::CellCenter(P->StartCell);
}

TArray<URSPlayer*> URSAI::EnemyPlayers() const
{
	TArray<URSPlayer*> Out;
	for (URSPlayer* Q : Game->Players)
		if (Q->bAlive && !Q->bNeutral && P->IsEnemyOf(Q)) Out.Add(Q);
	return Out;
}

FVector2D URSAI::EnemyDir() const
{
	const TArray<URSPlayer*> Es = EnemyPlayers();
	const FVector2D BP = BasePos();
	if (Es.Num() == 0) return FVector2D(1, 1).GetSafeNormal();
	FVector2D Best = RS::Cell2(Es[0]->StartCell);
	float BD = 1e9f;
	for (URSPlayer* Q : Es)
	{
		const float D = FVector2D::Distance(BP, RS::Cell2(Q->StartCell));
		if (D < BD)
		{
			BD = D;
			Best = RS::Cell2(Q->StartCell);
		}
	}
	return (Best - BP).GetSafeNormal();
}

void URSAI::Update(float Dt)
{
	if (!P->bAlive) return;
	ThinkT -= Dt;
	RetargetT -= Dt;
	if (ThinkT > 0.f) return;
	static const float Think[3] = { 1.3f, 1.f, 0.7f };
	ThinkT = Think[FMath::Clamp(Diff, 0, 2)];
	if (!bNoBuild)
	{
		ManageMcv();
		PlaceReady();
		ManageBuildings();
		ManageProduction();
		ManageWalls();
	}
	ManageArmy();
	ManageRaid();
	ManageSuper();
	ManageRepair();
}

void URSAI::ManageMcv()
{
	const bool bHasCy = P->HasRole(TEXT("cy")) || P->CountRole(TEXT("cy")) > 0;
	for (ARSUnit* U : TArray<ARSUnit*>(Game->Units))
	{
		if (!U || U->Player != P || U->bDead || U->Def->Mcv.IsEmpty()) continue;
		if (!bHasCy || DeployTries > 0)
		{
			if (U->Order == TEXT("idle") || U->Order == TEXT("deploy"))
			{
				U->CmdDeploy();
				Game->TryDeploy(U);
				if (!U->bDead)
				{
					DeployTries++;
					U->CmdMove(RS::CellCenter(P->StartCell) + FVector2D(RS::Rand(-4, 4), RS::Rand(-4, 4)));
				}
				else
				{
					DeployTries = 0;
				}
			}
			return;
		}
	}
}

FString URSAI::RoleId(const FString& Role) const
{
	if (Role == TEXT("bank")) return Pre == TEXT("a_") ? TEXT("a_bank") : TEXT("s_gas");
	if (Role == TEXT("def"))
	{
		const bool bRadar = P->HasRole(TEXT("radar"));
		int32 EnemyDrones = 0;
		bool bEnemyAir = false;
		for (ARSUnit* U : Game->Units)
		{
			if (!U || U->bDead || !P->IsEnemyOf(U->Player)) continue;
			if (U->Def->bDrone) EnemyDrones++;
			if (U->IsAir()) bEnemyAir = true;
		}
		if (EnemyDrones > 2 && bRadar && P->CountType(Pre + TEXT("ewt")) < 3 && RS::Rand01() < 0.6f) return Pre + TEXT("ewt");
		if (bEnemyAir && bRadar && RS::Rand01() < 0.5f) return Pre + (Pre == TEXT("a_") ? TEXT("aa") : TEXT("flak"));
		if (bRadar && RS::Rand01() < 0.6f) return Pre + (Pre == TEXT("a_") ? TEXT("prism") : TEXT("tcoil"));
		return Pre + (Pre == TEXT("a_") ? TEXT("pill") : TEXT("sentry"));
	}
	return Pre + Role;
}

void URSAI::ManageBuildings()
{
	if (!P->HasRole(TEXT("cy"))) return;
	if (P->Queues.FindOrAdd(TEXT("building")).Num() == 0 && P->GetReady(TEXT("building")).IsEmpty())
	{
		FString Need;
		if (P->PowerProd - P->PowerUse < 40.f && P->CountRole(TEXT("power")) > 0 && P->CountRole(TEXT("ref")) > 0)
		{
			Need = Pre + TEXT("power");
		}
		else
		{
			for (const FPlanStep& S : PLAN)
			{
				const FString Role = S.Role;
				if (Role == TEXT("def")) continue;
				if (Role == TEXT("sw") && Diff == 0) continue;
				if (Role == TEXT("yard") && !NavalOk()) continue;
				if (P->CountRole(Role) < S.N)
				{
					Need = RoleId(Role);
					break;
				}
			}
		}
		if (!Need.IsEmpty() && P->CanBuild(Need)) P->Enqueue(Need);
	}
	if (P->Queues.FindOrAdd(TEXT("defense")).Num() == 0 && P->GetReady(TEXT("defense")).IsEmpty())
	{
		const int32 NDef = P->CountRole(TEXT("def"));
		int32 Want = 0;
		for (const FPlanStep& S : PLAN)
		{
			if (FString(S.Role) == TEXT("def") && (P->CountRole(TEXT("radar")) > 0 || S.N <= 2)) Want = S.N;
		}
		Want = FMath::Min(Want, 2 + Diff * 3);
		if (NDef < Want && P->Money > 800.0)
		{
			const FString Id = RoleId(TEXT("def"));
			if (P->CanBuild(Id)) P->Enqueue(Id);
		}
	}
}

void URSAI::PlaceReady()
{
	for (const TCHAR* Cat : { TEXT("building"), TEXT("defense") })
	{
		const FString Id = P->GetReady(Cat);
		if (Id.IsEmpty()) continue;
		const FIntPoint TL = FindSpot(Id);
		if (TL.X >= 0)
		{
			Game->PlaceBuilding(P, Id, TL);
			P->Ready.Add(Cat, FString());
		}
		else
		{
			P->Cancel(Id);
		}
	}
}

FIntPoint URSAI::FindSpot(const FString& Id) const
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const FIntPoint Sz = Def.Size;
	const FVector2D BP = BasePos();
	const FVector2D ED = EnemyDir();
	const bool bDef = Def.Cat == TEXT("defense");
	FVector2D Anchor = BP;
	if (Def.bNaval)
	{
		FIntPoint Best(-1, -1);
		float BD = 1e9f;
		for (int32 Y = int32(BP.Y) - 20; Y <= int32(BP.Y) + 20; ++Y)
			for (int32 X = int32(BP.X) - 20; X <= int32(BP.X) + 20; ++X)
			{
				const FIntPoint TL(X, Y);
				const float D = FVector2D::Distance(RS::Cell2(TL), BP);
				if (D < BD && Game->CanPlace(Id, TL, P))
				{
					BD = D;
					Best = TL;
				}
			}
		return Best;
	}
	if (Def.Role == TEXT("ref"))
	{
		const FIntPoint OC = Game->Map->FindOreNear(BP, 30.f);
		if (OC.X >= 0) Anchor = RS::Lerp2(BP, RS::Cell2(OC), 0.6f);
	}
	int32 NB = 0;
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == P) NB++;
	for (int32 Attempt = 0; Attempt < 80; ++Attempt)
	{
		float R, Ang;
		if (bDef)
		{
			R = RS::Rand(5.f, 9.f);
			Ang = RS::AngleOf(ED) + RS::Rand(-1.1f, 1.1f);
		}
		else
		{
			R = RS::Rand(2.5f, 5.f + NB * 0.45f + Attempt * 0.05f);
			Ang = RS::Rand(0.f, 2.f * PI);
		}
		const FVector2D C = Anchor + RS::FromAngle(Ang) * R;
		const FIntPoint TL(int32(C.X - Sz.X * 0.5f), int32(C.Y - Sz.Y * 0.5f));
		if (!Game->CanPlace(Id, TL, P)) continue;
		if (!bDef && !MarginFree(TL, Sz)) continue;
		return TL;
	}
	return FIntPoint(-1, -1);
}

void URSAI::ManageWalls()
{
	static const float Think[3] = { 1.3f, 1.f, 0.7f };
	WallT -= Think[FMath::Clamp(Diff, 0, 2)];
	if (WallT > 0.f) return;
	WallT = 18.f;
	static const int32 Limits[3] = { 0, 14, 26 };
	const int32 Limit = Limits[FMath::Clamp(Diff, 0, 2)];
	const FString Wid = Pre + TEXT("wall");
	if (Limit == 0 || P->Money < 700.0 || !P->CanBuild(Wid)) return;
	int32 NW = 0;
	TArray<ARSBuilding*> Defs;
	for (ARSBuilding* B : Game->Buildings)
	{
		if (!B || B->Player != P || B->bDead) continue;
		if (B->Def->bWall) NW++;
		else if (B->Def->Cat == TEXT("defense")) Defs.Add(B);
	}
	if (NW >= Limit || Defs.Num() == 0) return;
	ARSBuilding* D = Defs[RS::RandI(0, Defs.Num() - 1)];
	const FVector2D ED = EnemyDir();
	const FVector2D Perp(-ED.Y, ED.X);
	const FVector2D C = D->Pos + ED * 2.2f;
	for (int32 K = -2; K <= 2; ++K)
	{
		const FVector2D Q = C + Perp * K;
		const FIntPoint TL = RS::ToCell(Q);
		if (Game->BldAt(TL.X, TL.Y) != nullptr || !Game->CanPlace(Wid, TL, P)) continue;
		bool bBad = false;
		for (int32 OY = -1; OY <= 1; ++OY)
			for (int32 OX = -1; OX <= 1; ++OX)
			{
				ARSBuilding* O = Game->BldAt(TL.X + OX, TL.Y + OY);
				if (O && !O->Def->bWall && O->Def->Cat != TEXT("defense")) bBad = true;
			}
		if (bBad) continue;
		const float Cost = FRSDefs::Get().GetDef(Wid).Cost;
		if (P->Money < Cost) break;
		P->Money -= Cost;
		Game->PlaceBuilding(P, Wid, TL, true);
	}
}

bool URSAI::MarginFree(FIntPoint TL, FIntPoint Sz) const
{
	const FRSMap& M = *Game->Map;
	for (int32 Y = TL.Y - 1; Y < TL.Y + Sz.Y + 1; ++Y)
		for (int32 X = TL.X - 1; X < TL.X + Sz.X + 1; ++X)
		{
			if (X >= TL.X && X < TL.X + Sz.X && Y >= TL.Y && Y < TL.Y + Sz.Y) continue;
			if (!M.Inb(X, Y)) return false;
			if (Game->BldAt(X, Y) != nullptr) return false;
		}
	for (int32 Y = TL.Y + Sz.Y; Y < TL.Y + Sz.Y + 2; ++Y)
		for (int32 X = TL.X; X < TL.X + Sz.X; ++X)
			if (M.Inb(X, Y) && !M.Passable(X, Y)) return false;
	return true;
}

void URSAI::ManageProduction()
{
	const int32 NRef = P->CountRole(TEXT("ref"));
	int32 NHarv = 0;
	for (ARSUnit* U : Game->Units)
		if (U && U->Player == P && !U->bDead && U->Def->bHarvester) NHarv++;
	const int32 WantH = NRef + ((Diff == 2 && NRef > 1) ? 1 : 0);
	TArray<FRSQueueItem>& VQ = P->Queues.FindOrAdd(TEXT("vehicle"));
	const FString HarvId = Pre + TEXT("harv");
	if (NHarv + P->QueuedCount(HarvId) < WantH && P->CanBuild(HarvId) && VQ.Num() < 2)
	{
		FRSQueueItem It;
		It.Id = HarvId;
		VQ.Insert(It, 0);
		return;
	}
	const double Reserve = P->Queues.FindOrAdd(TEXT("building")).Num() > 0 ? 600.0 : 200.0;
	if (P->Money < Reserve) return;
	EngT -= 1.f;
	if (EngT <= 0.f)
	{
		EngT = 150.f;
		if (NearestOil() && P->CanBuild(Pre + TEXT("eng"))) P->Enqueue(Pre + TEXT("eng"));
	}
	const bool bA = Pre == TEXT("a_");
	if (P->Queues.FindOrAdd(TEXT("infantry")).Num() < 2 && P->HasRole(TEXT("bar")))
	{
		TArray<TPair<FString, int32>> Opts;
		if (bA) Opts = { {TEXT("a_gi"), 5}, {TEXT("a_rocket"), 3}, {TEXT("a_jav"), 3}, {TEXT("a_cmdo"), Diff >= 1 ? 1 : 0}, {TEXT("a_spy"), Diff >= 1 ? 1 : 0} };
		else Opts = { {TEXT("s_con"), 6}, {TEXT("s_flak"), 2}, {TEXT("s_tesla"), 3}, {TEXT("s_guerrilla"), 5}, {TEXT("s_agent"), Diff >= 1 ? 1 : 0} };
		const FString Id = Pick(Opts);
		if (!Id.IsEmpty() && P->CanBuild(Id)) P->Enqueue(Id);
	}
	if (VQ.Num() < 2 && (P->HasRole(TEXT("fac")) || P->HasRole(TEXT("yard"))))
	{
		const int32 NC = NavalCount();
		const bool bDr = EnemyHasDrones();
		const int32 DF = bDroneFocus ? 3 : 1;
		TArray<TPair<FString, int32>> Opts;
		if (bA)
		{
			Opts = { {TEXT("a_tank"), 6}, {TEXT("a_ifv"), 3}, {TEXT("a_beam"), 2}, {TEXT("a_himars"), 2}, {TEXT("a_ew"), bDr ? 1 : 0}, {TEXT("a_fpv"), 3 * DF}, {TEXT("a_tb2"), 2 * DF}, {TEXT("a_jet"), 1},
				{TEXT("a_destroyer"), NC < 4 ? 2 : 0}, {TEXT("a_carrier"), NC < 4 ? 1 : 0}, {TEXT("a_apache"), 2}, {TEXT("a_type10"), 5}, {TEXT("a_k9"), 2}, {TEXT("a_hf"), 2},
				{TEXT("a_seadrone"), NC < 6 ? 2 : 0}, {TEXT("a_tuojiang"), NC < 6 ? 2 : 0}, {TEXT("a_cvn"), NC < 5 ? 1 : 0} };
		}
		else
		{
			Opts = { {TEXT("s_tank"), 6}, {TEXT("s_flaktrk"), 2}, {TEXT("s_arty"), 2}, {TEXT("s_mammoth"), 2}, {TEXT("s_tos"), 2}, {TEXT("s_iskander"), 1}, {TEXT("s_ew"), bDr ? 1 : 0},
				{TEXT("s_lancet"), 2 * DF}, {TEXT("s_shahed"), 2 * DF}, {TEXT("s_airship"), 1}, {TEXT("s_sub"), NC < 4 ? 2 : 0}, {TEXT("s_dread"), NC < 4 ? 1 : 0},
				{TEXT("s_bmpt"), 3}, {TEXT("s_hwasong"), 1}, {TEXT("s_fastboat"), NC < 6 ? 3 : 0}, {TEXT("s_technical"), 3}, {TEXT("s_cvn"), NC < 5 ? 1 : 0} };
		}
		const FString Id = Pick(Opts);
		if (!Id.IsEmpty() && P->CanBuild(Id)) P->Enqueue(Id);
	}
}

int32 URSAI::NavalCount() const
{
	int32 N = 0;
	for (ARSUnit* U : Game->Units)
		if (U && U->Player == P && !U->bDead && U->UKind == TEXT("ship")) N++;
	return N;
}

FString URSAI::Pick(const TArray<TPair<FString, int32>>& Opts) const
{
	TArray<TPair<FString, int32>> Valid;
	int32 Tot = 0;
	for (const TPair<FString, int32>& O : Opts)
	{
		if (O.Value > 0 && P->CanBuild(O.Key))
		{
			Valid.Add(O);
			Tot += O.Value;
		}
	}
	if (Tot == 0) return FString();
	int32 R = RS::RandI(0, Tot - 1);
	for (const TPair<FString, int32>& O : Valid)
	{
		R -= O.Value;
		if (R < 0) return O.Key;
	}
	return Valid[0].Key;
}

ARSBuilding* URSAI::NearestOil() const
{
	const FVector2D BP = BasePos();
	ARSBuilding* Best = nullptr;
	float BD = 1e9f;
	for (ARSBuilding* B : Game->Buildings)
	{
		if (B && B->Def->Role == TEXT("oil") && B->Player != P && !B->bDead && (B->Player->bNeutral || P->IsEnemyOf(B->Player)))
		{
			const float D = FVector2D::Distance(B->Pos, BP);
			if (D < BD)
			{
				BD = D;
				Best = B;
			}
		}
	}
	return Best;
}

void URSAI::OnUnitBuilt(ARSUnit* U)
{
	if (U->Def->bSpy)
	{
		ARSBuilding* Best = nullptr;
		float BD = 1e9f;
		for (ARSBuilding* B : Game->Buildings)
		{
			if (!B || B->bDead || !P->IsEnemyOf(B->Player)) continue;
			const FString& R = B->Def->Role;
			if (!(R == TEXT("power") || R == TEXT("ref") || R == TEXT("sw") || R == TEXT("radar"))) continue;
			const float D = FVector2D::Distance(B->Pos, BasePos());
			if (D < BD)
			{
				BD = D;
				Best = B;
			}
		}
		if (Best) U->CmdCapture(Best);
		return;
	}
	if (U->Def->bEngineer)
	{
		if (ARSBuilding* Oil = NearestOil()) U->CmdCapture(Oil);
		return;
	}
	if (U->Def->bHarvester) return;
	if (U->UKind != TEXT("air")) U->CmdMove(RallyPos() + FVector2D(RS::Rand(-2, 2), RS::Rand(-2, 2)));
}

bool URSAI::IsCombat(const ARSUnit* U) const
{
	return !U->Def->bHarvester && U->Def->Mcv.IsEmpty() && !U->Def->bEngineer && U->HasWeapon();
}

void URSAI::ManageArmy()
{
	AttackGroup.RemoveAll([](const TWeakObjectPtr<ARSUnit>& W) { return !RSAlive(W); });
	TArray<ARSUnit*> Home;
	for (ARSUnit* U : Game->Units)
	{
		if (!U || U->Player != P || U->bDead || !IsCombat(U)) continue;
		if (AttackGroup.Contains(U)) continue;
		Home.Add(U);
	}
	if (!bPassive && Game->Time > NextAttack && Home.Num() >= AttackSize)
	{
		LaunchAttack(Home);
		return;
	}
	if (RetargetT <= 0.f)
	{
		RetargetT = 4.f;
		for (const TWeakObjectPtr<ARSUnit>& W : AttackGroup)
		{
			ARSUnit* U = W.Get();
			if (!U) continue;
			if (U->UKind == TEXT("air"))
			{
				if (U->Order == TEXT("idle") || U->Order == TEXT("return"))
				{
					if (U->Def->bJet && ((U->HasWeapon() && U->Ammo < U->Weapon->Ammo) || !U->bLanded)) continue;
					if (ARSEntity* T = PickTarget(U->Pos, true)) U->CmdAttack(T);
				}
				continue;
			}
			if (U->Order == TEXT("idle") || U->Order == TEXT("return_home"))
			{
				if (ARSEntity* T = PickTarget(U->Pos, false)) U->CmdMove(T->Pos, true);
			}
		}
	}
	GarrisonT -= 1.f;
	if (GarrisonT <= 0.f)
	{
		GarrisonT = 40.f;
		TryGarrison(Home);
	}
	const FVector2D RP = RallyPos();
	if (bPassive) return;
	for (ARSUnit* U : Home)
	{
		if (U->UKind == TEXT("air") || U->bDead) continue;
		if (U->Order == TEXT("idle") && FVector2D::Distance(U->Pos, RP) > 6.f && FVector2D::Distance(U->Pos, BasePos()) > 3.f)
		{
			U->CmdMove(RP + FVector2D(RS::Rand(-2.5f, 2.5f), RS::Rand(-2.5f, 2.5f)));
		}
	}
	if (AttackGroup.Num() > 0 && AttackGroup.Num() < 3 && Game->Time > NextAttack - 60.f)
	{
		for (const TWeakObjectPtr<ARSUnit>& W : AttackGroup)
			if (ARSUnit* U = W.Get())
				if (U->UKind != TEXT("air")) U->CmdMove(RP, false);
		AttackGroup.Reset();
	}
}

void URSAI::ManageRaid()
{
	if (Diff == 0 || bPassive) return;
	RaidGroup.RemoveAll([](const TWeakObjectPtr<ARSUnit>& W) { return !RSAlive(W); });
	if (Game->Time < RaidT) return;
	const int32 Want = Diff == 1 ? 3 : 4;
	TArray<ARSUnit*> Cands;
	for (ARSUnit* U : Game->Units)
	{
		if (!U || U->Player != P || U->bDead || !IsCombat(U) || AttackGroup.Contains(U) || RaidGroup.Contains(U)) continue;
		if (U->UKind == TEXT("veh") && U->Def->Speed >= 1.8f && U->Order == TEXT("idle")) Cands.Add(U);
	}
	if (Cands.Num() < Want) return;
	ARSUnit* Target = nullptr;
	float BD = 1e9f;
	const FVector2D BP = BasePos();
	for (ARSUnit* U : Game->Units)
	{
		if (!U || U->bDead || !P->IsEnemyOf(U->Player) || !U->Def->bHarvester) continue;
		const float D = FVector2D::Distance(U->Pos, BP);
		if (D < BD)
		{
			BD = D;
			Target = U;
		}
	}
	if (!Target)
	{
		RaidT = Game->Time + 60.f;
		return;
	}
	RaidGroup.Reset();
	for (int32 I = 0; I < Want; ++I)
	{
		RaidGroup.Add(Cands[I]);
		Cands[I]->CmdAttack(Target);
	}
	RaidT = Game->Time + (Diff == 1 ? 150.f : 90.f);
}

void URSAI::TryGarrison(const TArray<ARSUnit*>& Home)
{
	const FVector2D BP = BasePos();
	ARSBuilding* Best = nullptr;
	float BD = 20.f;
	for (ARSBuilding* B : Game->Buildings)
	{
		if (!B || B->bDead || B->Def->Garrison <= 0 || !(B->Player->bNeutral || B->Player == P)) continue;
		if (B->Occupants.Num() >= B->Def->Garrison) continue;
		const float D = FVector2D::Distance(B->Pos, BP);
		if (D < BD)
		{
			BD = D;
			Best = B;
		}
	}
	if (!Best) return;
	int32 N = 0;
	for (ARSUnit* U : Home)
	{
		if (N >= 3) break;
		if (U->UKind == TEXT("inf") && U->Order == TEXT("idle") && Game->CanGarrison(U, Best))
		{
			U->CmdGarrison(Best);
			N++;
		}
	}
}

void URSAI::LaunchAttack(const TArray<ARSUnit*>& Home)
{
	ARSEntity* T = PickTarget(BasePos(), false);
	if (!T) return;
	Wave++;
	AttackGroup.Reset();
	for (ARSUnit* U : Home)
	{
		AttackGroup.Add(U);
		if (U->UKind == TEXT("air")) U->CmdAttack(T);
		else U->CmdMove(T->Pos + FVector2D(RS::Rand(-1.5f, 1.5f), RS::Rand(-1.5f, 1.5f)), true);
	}
	static const float Gap[3] = { 260.f, 170.f, 120.f };
	static const int32 Inc[3] = { 1, 2, 3 };
	NextAttack = Game->Time + Gap[FMath::Clamp(Diff, 0, 2)];
	AttackSize = FMath::Min(AttackSize + Inc[FMath::Clamp(Diff, 0, 2)], 30);
}

ARSEntity* URSAI::PickTarget(const FVector2D& From, bool bPreferDef) const
{
	ARSEntity* Best = nullptr;
	float BD = 1e9f;
	for (ARSBuilding* B : Game->Buildings)
	{
		if (!B || B->bDead || !P->IsEnemyOf(B->Player) || B->Def->bWall) continue;
		float D = FVector2D::Distance(B->Pos, From);
		if (bPreferDef && B->HasWeapon()) D -= 8.f;
		const FString& R = B->Def->Role;
		if (R == TEXT("ref") || R == TEXT("fac") || R == TEXT("cy")) D -= 3.f;
		if (Diff == 2 && (R == TEXT("sw") || B->TypeId.EndsWith(TEXT("ewt")))) D -= 10.f;
		if (D < BD)
		{
			BD = D;
			Best = B;
		}
	}
	if (!Best)
	{
		for (ARSUnit* U : Game->Units)
		{
			if (!U || U->bDead || !P->IsEnemyOf(U->Player)) continue;
			const float D = FVector2D::Distance(U->Pos, From);
			if (D < BD)
			{
				BD = D;
				Best = U;
			}
		}
	}
	return Best;
}

void URSAI::OnAttacked(ARSEntity* Victim, ARSEntity* Attacker)
{
	if (!RSAlive(Attacker)) return;
	if (Game->Time - LastDefend < 3.f) return;
	const FVector2D BP = BasePos();
	if (FVector2D::Distance(Victim->Pos, BP) > 16.f && !Victim->Def->bHarvester) return;
	LastDefend = Game->Time;
	static const float Rad[3] = { 12.f, 22.f, 32.f };
	for (ARSUnit* U : Game->Units)
	{
		if (!U || U->Player != P || U->bDead || !IsCombat(U) || AttackGroup.Contains(U)) continue;
		if ((U->Order == TEXT("idle") || U->Order == TEXT("move") || U->Order == TEXT("return_home")) && FVector2D::Distance(U->Pos, Victim->Pos) < Rad[FMath::Clamp(Diff, 0, 2)])
		{
			if (U->CanHit(U->Weapon, Attacker)) U->CmdAttack(Attacker);
		}
	}
}

bool URSAI::EnemyHasDrones() const
{
	for (ARSUnit* U : Game->Units)
		if (U && !U->bDead && U->Def->bDrone && P->IsEnemyOf(U->Player)) return true;
	return false;
}

void URSAI::ManageSuper()
{
	TArray<FString> Kinds = ReadyPowers.Array();
	for (const FString& Kind : Kinds)
	{
		const float* C = P->Powers.Find(Kind);
		if (!C || *C < 1.f)
		{
			ReadyPowers.Remove(Kind);
			continue;
		}
		FVector2D Tgt(-1, -1);
		if (Kind == TEXT("sanction") || Kind == TEXT("energy") || Kind == TEXT("cyber"))
		{
			if (ARSEntity* T = PickTarget(BasePos(), false)) Tgt = T->Pos;
		}
		else if (Kind == TEXT("militia") || Kind == TEXT("armor"))
		{
			Tgt = BasePos();
		}
		else if (Kind == TEXT("scan"))
		{
			const TArray<URSPlayer*> Es = EnemyPlayers();
			if (Es.Num() > 0) Tgt = RS::Cell2(Es[0]->StartCell);
		}
		else if (Kind == TEXT("jam"))
		{
			ARSEntity* T = PickTarget(BasePos(), true);
			if (T && AttackGroup.Num() > 0) Tgt = T->Pos;
		}
		else
		{
			ARSBuilding* Best = nullptr;
			float BS = -1.f;
			for (ARSBuilding* B : Game->Buildings)
			{
				if (!B || B->bDead || !P->IsEnemyOf(B->Player)) continue;
				float Sc = 0.f;
				if (Kind == TEXT("swarm"))
				{
					for (ARSUnit* U : Game->Units)
						if (U && !U->bDead && P->IsEnemyOf(U->Player) && U->UKind == TEXT("veh") && FVector2D::Distance(U->Pos, B->Pos) < 5.f) Sc += 1.f;
				}
				else
				{
					for (ARSBuilding* O : Game->Buildings)
						if (O && O->Player == B->Player && !O->bDead && FVector2D::Distance(O->Pos, B->Pos) < 4.f) Sc += 1.f;
					const FString& R = B->Def->Role;
					if (R == TEXT("cy") || R == TEXT("fac") || R == TEXT("sw")) Sc += 2.f;
				}
				if (Sc > BS)
				{
					BS = Sc;
					Best = B;
				}
			}
			if (Best) Tgt = Best->Pos;
		}
		if (Tgt.X >= 0.f && Game->FirePower(P, Kind, Tgt)) ReadyPowers.Remove(Kind);
	}
}

void URSAI::ManageRepair()
{
	if (P->Money < 600.0) return;
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == P && !B->bDead && B->Built() && B->Hp < B->MaxHp * 0.6f && !B->bRepairing) B->bRepairing = true;
}
