#include "RSMission.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSAI.h"
#include "RSAudio.h"
#include "RSMap.h"

namespace
{
	FVector2D R2(float A) { return FVector2D(RS::Rand(-A, A), RS::Rand(-A, A)); }
}

void URSMission::SetAI(bool bNoBuild, bool bPassive)
{
	for (URSAI* AI : Game->AIs)
	{
		AI->bNoBuild = bNoBuild;
		AI->bPassive = bPassive;
	}
}

void URSMission::SpawnMcv(URSPlayer* P)
{
	Game->SpawnUnit(P, P->Faction == TEXT("allies") ? TEXT("a_mcv") : TEXT("s_mcv"), RS::CellCenter(P->StartCell));
}

void URSMission::BuildBase(URSPlayer* P, const TArray<FRSBaseItem>& List)
{
	const FIntPoint SC = P->StartCell;
	for (const FRSBaseItem& It : List)
	{
		const FIntPoint TL = SC + It.Off;
		bool bPlaced = false;
		for (int32 R = 0; R < 5 && !bPlaced; ++R)
			for (int32 DY = -R; DY <= R && !bPlaced; ++DY)
				for (int32 DX = -R; DX <= R && !bPlaced; ++DX)
				{
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != R) continue;
					if (Game->CanPlace(It.Id, TL + FIntPoint(DX, DY), P, nullptr, false))
					{
						Game->PlaceBuilding(P, It.Id, TL + FIntPoint(DX, DY), true);
						bPlaced = true;
					}
				}
		if (!bPlaced) UE_LOG(LogRS, Warning, TEXT("mission: could not place %s"), *It.Id);
	}
}

void URSMission::Setup(ARSGame* G, const FString& Mid)
{
	Game = G;
	Id = Mid;
	Enemy = Game->Players.Num() > 1 ? Game->Players[1].Get() : nullptr;
	URSPlayer* H = Game->Human;
	const FVector2D HS = RS::CellCenter(H->StartCell);
	const FIntPoint ES = Enemy ? Enemy->StartCell : FIntPoint::ZeroValue;
	auto U = [this](URSPlayer* P, const TCHAR* UId, const FVector2D& At) { return Game->SpawnUnit(P, UId, At); };
	if (Id == TEXT("h1"))
	{
		BuildBase(H, { {TEXT("a_cy"), {-1, -1}}, {TEXT("a_power"), {-4, -1}}, {TEXT("a_ref"), {2, 2}}, {TEXT("a_bar"), {-1, 3}}, {TEXT("a_fac"), {-5, -5}}, {TEXT("a_radar"), {3, -4}} });
		for (int32 K = 0; K < 3; ++K) U(H, TEXT("a_tank"), HS + FVector2D(-4 + K, -7));
		for (int32 K = 0; K < 5; ++K) U(H, TEXT("a_gi"), HS + FVector2D(-6, -3) + R2(1));
		SpawnMcv(Enemy);
		Enemy->Money = 30000;
		Enemy->Powers.Add(TEXT("energy"), 0.85f);
		for (URSAI* AI : Game->AIs) { AI->bDroneFocus = true; AI->NextAttack = 120.f; AI->AttackSize = 5; }
		H->EnergyCutUntil = Game->Time + 40.f;
		H->bPowerDirty = true;
		Game->Objective = TEXT("目標：堅守 08:00，結束時電力須充足");
	}
	else if (Id == TEXT("h2"))
	{
		SpawnMcv(H);
		for (int32 K = 0; K < 3; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		U(H, TEXT("a_himars"), HS + FVector2D(-2.5f, 2));
		U(H, TEXT("a_ew"), HS + FVector2D(2.5f, -1));
		BuildBase(Enemy, { {TEXT("s_cy"), {-1, -1}}, {TEXT("s_power"), {3, -1}}, {TEXT("s_power"), {3, 2}}, {TEXT("s_ref"), {-1, 3}}, {TEXT("s_bar"), {-4, -1}},
			{TEXT("s_fac"), {6, 3}}, {TEXT("s_radar"), {6, -1}}, {TEXT("s_ewt"), {-6, 2}}, {TEXT("s_ewt"), {2, -6}}, {TEXT("s_ewt"), {8, 7}}, {TEXT("s_flak"), {-5, -5}}, {TEXT("s_flak"), {8, -4}}, {TEXT("s_tcoil"), {-4, 6}} });
		Enemy->Money = 18000;
		for (int32 K = 0; K < 3; ++K) U(Enemy, TEXT("s_ew"), RS::Cell2(ES) + FVector2D(-8 + K * 2, 8));
		Game->Objective = TEXT("目標：摧毀敵方所有建築");
	}
	else if (Id == TEXT("h3"))
	{
		SpawnMcv(H);
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		U(H, TEXT("a_spy"), HS + FVector2D(1.5f, 1.5f));
		U(H, TEXT("a_spy"), HS + FVector2D(2.f, 1.f));
		BuildBase(Enemy, { {TEXT("s_cy"), {-1, -1}}, {TEXT("s_power"), {3, -1}}, {TEXT("s_power"), {3, 2}}, {TEXT("s_power"), {-4, 3}}, {TEXT("s_ref"), {-1, 3}},
			{TEXT("s_bar"), {-4, -1}}, {TEXT("s_fac"), {6, 3}}, {TEXT("s_radar"), {6, -1}}, {TEXT("s_gas"), {-5, 6}}, {TEXT("s_tech"), {2, 6}}, {TEXT("s_sentry"), {8, 6}}, {TEXT("s_sentry"), {-7, -3}} });
		Enemy->Money = 16000;
		H->Powers.Add(TEXT("sanction"), 0.f);
		Game->Objective = TEXT("目標：間諜滲透 0/3，金融制裁 0/1");
	}
	else if (Id == TEXT("h4"))
	{
		SpawnMcv(H);
		U(H, TEXT("a_tank"), HS + FVector2D(-2.5f, 2));
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		const FIntPoint WC = Game->NearestWater(H->StartCell, 30);
		if (WC.X >= 0)
		{
			U(H, TEXT("a_cvn"), RS::CellCenter(WC));
			const FIntPoint W2 = Game->NearestWater(WC + FIntPoint(3, 3), 10);
			if (W2.X >= 0) U(H, TEXT("a_destroyer"), RS::CellCenter(W2));
		}
		BuildBase(Enemy, { {TEXT("s_cy"), {-1, -1}}, {TEXT("s_power"), {3, -1}}, {TEXT("s_power"), {3, 2}}, {TEXT("s_ref"), {-1, 3}}, {TEXT("s_bar"), {-4, -1}},
			{TEXT("s_fac"), {6, 3}}, {TEXT("s_radar"), {6, -1}}, {TEXT("s_dronehub"), {-5, 3}}, {TEXT("s_flak"), {-5, -5}}, {TEXT("s_flak"), {8, -4}} });
		const FIntPoint EW = Game->NearestWater(ES, 40);
		if (EW.X >= 0)
		{
			bool bPlaced = false;
			for (int32 R = 0; R < 8 && !bPlaced; ++R)
				for (int32 DY = -R; DY <= R && !bPlaced; ++DY)
					for (int32 DX = -R; DX <= R && !bPlaced; ++DX)
					{
						if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != R) continue;
						const FIntPoint TL = EW + FIntPoint(DX, DY);
						if (Game->CanPlace(TEXT("s_yard"), TL, Enemy, nullptr, false))
						{
							Game->PlaceBuilding(Enemy, TEXT("s_yard"), TL, true);
							bPlaced = true;
						}
					}
			for (int32 K = 0; K < 4; ++K)
			{
				const FIntPoint W2 = Game->NearestWater(EW + FIntPoint(RS::RandI(-6, 6), RS::RandI(-6, 6)), 8);
				if (W2.X >= 0) U(Enemy, K < 3 ? TEXT("s_fastboat") : TEXT("s_sub"), RS::CellCenter(W2));
			}
		}
		Enemy->Money = 30000;
		for (URSAI* AI : Game->AIs) AI->bDroneFocus = true;
		Game->Objective = TEXT("目標：摧毀敵方造船廠、無人機指揮站與所有艦艇");
	}
	else if (Id == TEXT("h5"))
	{
		BuildBase(H, { {TEXT("a_cy"), {-1, -1}}, {TEXT("a_power"), {-4, -1}}, {TEXT("a_power"), {-4, 2}}, {TEXT("a_ref"), {2, 2}}, {TEXT("a_bar"), {-1, 3}},
			{TEXT("a_fac"), {-5, -5}}, {TEXT("a_radar"), {3, -4}}, {TEXT("a_pill"), {-7, 0}}, {TEXT("a_pill"), {-7, 3}}, {TEXT("a_aa"), {-6, -3}} });
		for (int32 K = 0; K < 3; ++K) U(H, TEXT("a_hf"), HS + FVector2D(-6 + K, -8));
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_jav"), HS + FVector2D(-8, -2) + R2(1));
		SpawnMcv(Enemy);
		Enemy->Money = 40000;
		for (URSAI* AI : Game->AIs) AI->NextAttack = 200.f;
		Game->Objective = TEXT("目標：堅守 15:00");
	}
	else if (Id == TEXT("h6"))
	{
		SpawnMcv(H);
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		U(H, TEXT("a_type10"), HS + FVector2D(-2.5f, 2));
		for (URSPlayer* Q : Game->Players)
		{
			if (Q->bAI)
			{
				SpawnMcv(Q);
				Q->Money = 12000;
				Q->Relations.Add(H->Index, 42.f);
			}
		}
		Game->Objective = TEXT("目標：締結同盟，然後消滅所有非盟友");
	}
	else if (Id == TEXT("h7"))
	{
		SpawnMcv(H);
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		U(H, TEXT("a_tank"), HS + FVector2D(-2.5f, 2));
		U(H, TEXT("a_tank"), HS + FVector2D(-2.5f, 3));
		for (URSPlayer* Q : Game->Players)
		{
			if (!Q->bAI) continue;
			const FString Pre = Q->Faction == TEXT("soviet") ? TEXT("s_") : TEXT("a_");
			const bool bS = Pre == TEXT("s_");
			BuildBase(Q, { {Pre + TEXT("cy"), {-1, -1}}, {Pre + TEXT("power"), {3, -1}}, {Pre + TEXT("power"), {3, 2}}, {Pre + TEXT("power"), {-4, 3}}, {Pre + TEXT("ref"), {-1, 3}},
				{Pre + TEXT("bar"), {-4, -1}}, {Pre + TEXT("fac"), {6, 3}}, {Pre + TEXT("radar"), {6, -1}}, {Pre + TEXT("tech"), {-4, 6}}, {Pre + TEXT("sw"), {2, 6}},
				{bS ? TEXT("s_tcoil") : TEXT("a_prism"), {6, 7}}, {bS ? TEXT("s_sentry") : TEXT("a_pill"), {8, 6}} });
			Q->Money = 20000;
			Q->Powers.Add(bS ? TEXT("nuke") : TEXT("storm"), 0.f);
		}
		Game->Objective = TEXT("目標：摧毀所有敵方超級武器與建造廠");
	}
	else if (Id == TEXT("m1"))
	{
		for (int32 K = 0; K < 6; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		for (int32 K = 0; K < 3; ++K) U(H, TEXT("a_rocket"), HS + R2(2));
		for (int32 K = 0; K < 3; ++K) U(H, TEXT("a_tank"), HS + FVector2D(-2.5f + K * 1.3f, -3.f));
		U(H, TEXT("a_eng"), HS + FVector2D(1, 1));
		U(H, TEXT("a_eng"), HS + FVector2D(1.5f, 0.5f));
		U(H, TEXT("a_cmdo"), HS + FVector2D(0, 1.5f));
		BuildBase(Enemy, { {TEXT("s_bar"), {0, 0}}, {TEXT("s_power"), {3, 0}}, {TEXT("s_power"), {0, 3}}, {TEXT("s_radar"), {3, 3}},
			{TEXT("s_sentry"), {5, 5}}, {TEXT("s_sentry"), {6, 1}}, {TEXT("s_sentry"), {1, 6}}, {TEXT("s_tcoil"), {5, 3}}, {TEXT("s_wall"), {6, 3}}, {TEXT("s_wall"), {6, 4}} });
		for (int32 K = 0; K < 8; ++K) U(Enemy, TEXT("s_con"), RS::Cell2(ES) + FVector2D(4, 4) + R2(2));
		for (int32 K = 0; K < 2; ++K) U(Enemy, TEXT("s_tank"), RS::Cell2(ES) + FVector2D(6.5f + K, 6.5f));
		SetAI(true, true);
		Game->Objective = TEXT("目標：摧毀集團軍前哨所有建築");
	}
	else if (Id == TEXT("m2"))
	{
		BuildBase(H, { {TEXT("s_cy"), {-1, -1}}, {TEXT("s_power"), {-4, -1}}, {TEXT("s_power"), {-4, 2}}, {TEXT("s_power"), {3, -4}},
			{TEXT("s_ref"), {2, 2}}, {TEXT("s_bar"), {-1, 3}}, {TEXT("s_fac"), {-5, -5}}, {TEXT("s_radar"), {-1, -4}},
			{TEXT("s_sentry"), {-3, -7}}, {TEXT("s_sentry"), {-7, -3}}, {TEXT("s_tcoil"), {-6, -6}} });
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("s_tank"), HS + FVector2D(-5 + K, -7));
		for (int32 K = 0; K < 6; ++K) U(H, TEXT("s_con"), HS + FVector2D(-7, -3) + R2(1));
		SpawnMcv(Enemy);
		Enemy->Money = 30000;
		SetAI(false, false);
		for (URSAI* AI : Game->AIs) { AI->NextAttack = 150.f; AI->AttackSize = 6; }
		Game->Objective = TEXT("目標：堅守 10:00");
	}
	else if (Id == TEXT("m3"))
	{
		SpawnMcv(H);
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		U(H, TEXT("a_ew"), HS + FVector2D(2.5f, -1));
		U(H, TEXT("a_tank"), HS + FVector2D(-2.5f, 2));
		U(H, TEXT("a_tank"), HS + FVector2D(-2.5f, 3));
		SpawnMcv(Enemy);
		Enemy->Money = 20000;
		for (URSAI* AI : Game->AIs) AI->bDroneFocus = true;
		Game->Objective = TEXT("目標：摧毀敵方所有建築");
	}
	else if (Id == TEXT("m4"))
	{
		SpawnMcv(H);
		for (int32 K = 0; K < 4; ++K) U(H, TEXT("a_gi"), HS + R2(2));
		U(H, TEXT("a_tank"), HS + FVector2D(-2.5f, 2));
		BuildBase(Enemy, { {TEXT("s_cy"), {-1, -1}}, {TEXT("s_power"), {3, -1}}, {TEXT("s_power"), {3, 2}}, {TEXT("s_power"), {-4, 3}},
			{TEXT("s_ref"), {-1, 3}}, {TEXT("s_bar"), {-4, -1}}, {TEXT("s_fac"), {6, 3}}, {TEXT("s_radar"), {6, -1}},
			{TEXT("s_tech"), {-4, 6}}, {TEXT("s_sw"), {2, 6}}, {TEXT("s_tcoil"), {6, 7}}, {TEXT("s_sentry"), {8, 6}} });
		Enemy->Money = 15000;
		Enemy->Powers.Add(TEXT("nuke"), 0.f);
		Game->Objective = TEXT("目標：摧毀敵方核彈發射井");
	}
}

void URSMission::Landing()
{
	const FIntPoint HS = Game->Human->StartCell;
	const FRSMap& M = *Game->Map;
	FIntPoint Best(-1, -1);
	for (int32 Tries = 0; Tries < 200 && Best.X < 0; ++Tries)
	{
		const int32 X = HS.X - 4 - RS::RandI(0, 10);
		const int32 Y = HS.Y + RS::RandI(-18, 18);
		if (M.Inb(X, Y) && M.Passable(X, Y) && M.NearType(X, Y, FRSMap::T_WATER, 2) && !Game->BldAt(X, Y)) Best = FIntPoint(X, Y);
	}
	if (Best.X < 0) return;
	Waves++;
	TArray<FString> Ids = { TEXT("s_tank"), TEXT("s_tank"), TEXT("s_bmpt"), TEXT("s_con"), TEXT("s_con"), TEXT("s_con"), TEXT("s_flak") };
	if (Waves >= 3) { Ids.Add(TEXT("s_mammoth")); Ids.Add(TEXT("s_tos")); }
	for (const FString& UId : Ids)
	{
		const FIntPoint C = Game->NearestFree(Best + FIntPoint(RS::RandI(-2, 2), RS::RandI(-2, 2)), 5);
		if (C.X >= 0)
		{
			if (ARSUnit* Un = Game->SpawnUnit(Enemy, UId, RS::CellCenter(C))) Un->CmdMove(RS::CellCenter(HS), true);
		}
	}
	Game->Message(FString::Printf(TEXT("警告：西岸偵測到第 %d 波登陸部隊！"), Waves));
	Game->Ping(RS::Cell2(Best));
	Game->Audio()->Play(TEXT("alarm"), -4.f);
}

int32 URSMission::CountBld(URSPlayer* P) const
{
	int32 N = 0;
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == P && !B->bDead && !B->Def->bCiv && !B->Def->bWall) N++;
	return N;
}

FString URSMission::Check()
{
	URSPlayer* H = Game->Human;
	const float T = Game->Time;
	if (Id == TEXT("h1"))
	{
		const float Left = 480.f - T;
		Game->Objective = FString::Printf(TEXT("目標：堅守 %s（結束時電力須充足）"), *RS::MMSS(Left));
		if (!H->bAlive) return TEXT("lose");
		if (Left <= 0.f) return (H->PowerProd >= H->PowerUse && H->PowerProd > 0.f) ? TEXT("win") : TEXT("lose");
		const float* E = Enemy->Powers.Find(TEXT("energy"));
		if (T > 200.f && E && *E >= 1.f) Game->FirePower(Enemy, TEXT("energy"), RS::Cell2(H->StartCell));
	}
	else if (Id == TEXT("h2") || Id == TEXT("h4") || Id == TEXT("h6") || Id == TEXT("h7"))
	{
		if (!H->bAlive) return TEXT("lose");
		if (Id == TEXT("h2") && CountBld(Enemy) == 0) return TEXT("win");
		if (Id == TEXT("h4"))
		{
			int32 LeftB = 0, Ships = 0;
			for (ARSBuilding* B : Game->Buildings)
				if (B && B->Player == Enemy && !B->bDead && (B->Def->Role == TEXT("yard") || B->Def->Role == TEXT("dronehub"))) LeftB++;
			for (ARSUnit* Un : Game->Units)
				if (Un && Un->Player == Enemy && !Un->bDead && Un->UKind == TEXT("ship")) Ships++;
			Game->Objective = FString::Printf(TEXT("目標：敵方造船廠/無人機站剩 %d，艦艇剩 %d"), LeftB, Ships);
			if (LeftB == 0 && Ships == 0 && T > 120.f) return TEXT("win");
		}
		if (Id == TEXT("h6"))
		{
			const bool bAllied = Game->HumanAllies().Num() > 0;
			int32 Foes = 0;
			for (URSPlayer* Q : Game->Players)
				if (Q->bAI && Q->bAlive && H->IsEnemyOf(Q)) Foes++;
			Game->Objective = bAllied ? FString::Printf(TEXT("目標：消滅所有非盟友（剩 %d）"), Foes) : FString(TEXT("目標：先與一名電腦締結同盟（按 T）"));
			if (bAllied && Foes == 0) return TEXT("win");
		}
		if (Id == TEXT("h7"))
		{
			int32 LeftB = 0;
			for (ARSBuilding* B : Game->Buildings)
				if (B && B->Player != H && !B->Player->bNeutral && !B->bDead && (B->Def->Role == TEXT("sw") || B->Def->Role == TEXT("cy"))) LeftB++;
			Game->Objective = FString::Printf(TEXT("目標：敵方超級武器/建造廠剩 %d"), LeftB);
			if (LeftB == 0) return TEXT("win");
		}
	}
	else if (Id == TEXT("h3"))
	{
		if (!H->bAlive) return TEXT("lose");
		Game->Objective = FString::Printf(TEXT("目標：間諜滲透 %d/3，金融制裁 %d/1"), FMath::Min(SpyHits, 3), bSanctionFired ? 1 : 0);
		if (SpyHits >= 3 && bSanctionFired) return TEXT("win");
	}
	else if (Id == TEXT("h5"))
	{
		const float Left = 900.f - T;
		Game->Objective = FString::Printf(TEXT("目標：堅守 %s（第 %d 波登陸）"), *RS::MMSS(Left), Waves);
		if (!H->bAlive) return TEXT("lose");
		if (Left <= 0.f) return TEXT("win");
		WaveT -= 1.f;
		if (WaveT <= 0.f)
		{
			WaveT = Waves > 0 ? 150.f : 90.f;
			Landing();
		}
	}
	else if (Id == TEXT("m1"))
	{
		bool bAlive = false;
		for (ARSUnit* Un : Game->Units)
			if (Un && Un->Player == H && !Un->bDead) { bAlive = true; break; }
		if (!bAlive)
			for (ARSBuilding* B : Game->Buildings)
				if (B && B->Player == H && !B->bDead && !B->Def->bCiv) bAlive = true;
		if (!bAlive) return TEXT("lose");
		if (CountBld(Enemy) == 0) return TEXT("win");
	}
	else if (Id == TEXT("m2"))
	{
		const float Left = 600.f - T;
		Game->Objective = FString::Printf(TEXT("目標：堅守 %s"), *RS::MMSS(Left));
		if (!H->bAlive) return TEXT("lose");
		if (Left <= 0.f) return TEXT("win");
	}
	else if (Id == TEXT("m3"))
	{
		if (!H->bAlive) return TEXT("lose");
		if (CountBld(Enemy) == 0) return TEXT("win");
	}
	else if (Id == TEXT("m4"))
	{
		if (!H->bAlive) return TEXT("lose");
		bool bSilo = false;
		for (ARSBuilding* B : Game->Buildings)
			if (B && B->Player == Enemy && !B->bDead && B->TypeId == TEXT("s_sw")) bSilo = true;
		if (!bSilo) return TEXT("win");
	}
	return FString();
}
