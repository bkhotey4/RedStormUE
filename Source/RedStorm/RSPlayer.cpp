#include "RSPlayer.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGame.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSWorldEvents.h"
#include "RSAudio.h"

void URSPlayer::InitQueues()
{
	Queues.Reset();
	for (const TCHAR* C : { TEXT("building"), TEXT("defense"), TEXT("infantry"), TEXT("vehicle") }) Queues.Add(C);
	Ready.Reset();
	Ready.Add(TEXT("building"), FString());
	Ready.Add(TEXT("defense"), FString());
}

bool URSPlayer::IsEnemyOf(const URSPlayer* Other) const
{
	if (!Other || Other == this) return false;
	if (bNeutral || Other->bNeutral) return false;
	if (Allies.Contains(Other->Index) || Other->Allies.Contains(Index)) return false;
	if (const float* C = Ceasefire.Find(Other->Index))
	{
		if (Game && Game->Time < *C) return false;
	}
	return Other->Team != Team;
}

float URSPlayer::Relation(const URSPlayer* Other) const
{
	const float* R = Other ? Relations.Find(Other->Index) : nullptr;
	return R ? *R : 30.f;
}

void URSPlayer::AddRelation(const URSPlayer* Other, float D)
{
	if (!Other) return;
	Relations.Add(Other->Index, FMath::Clamp(Relation(Other) + D, 0.f, 100.f));
}

bool URSPlayer::HasRole(const FString& Role) const
{
	for (ARSBuilding* B : Game->Buildings)
	{
		if (B && B->Player == this && !B->bDead && B->Built())
		{
			if (B->Def->Role == Role || (Role == TEXT("airfield") && B->Def->bAirfield)) return true;
		}
	}
	return false;
}

int32 URSPlayer::CountRole(const FString& Role) const
{
	int32 N = 0;
	for (ARSBuilding* B : Game->Buildings)
	{
		if (B && B->Player == this && !B->bDead)
		{
			if (B->Def->Role == Role || (Role == TEXT("airfield") && B->Def->bAirfield)) N++;
		}
	}
	return N;
}

int32 URSPlayer::CountType(const FString& Id) const
{
	int32 N = 0;
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == this && !B->bDead && B->TypeId == Id) N++;
	for (ARSUnit* U : Game->Units)
		if (U && U->Player == this && !U->bDead && U->TypeId == Id) N++;
	for (const auto& Q : Queues)
		for (const FRSQueueItem& It : Q.Value)
			if (It.Id == Id) N++;
	for (const auto& R : Ready)
		if (R.Value == Id) N++;
	return N;
}

int32 URSPlayer::NumUnits() const
{
	int32 N = 0;
	for (ARSUnit* U : Game->Units)
		if (U && U->Player == this && !U->bDead) N++;
	return N;
}

int32 URSPlayer::NumBuildings() const
{
	int32 N = 0;
	for (ARSBuilding* B : Game->Buildings)
		if (B && B->Player == this && !B->bDead) N++;
	return N;
}

FString URSPlayer::ProducerRole(const FRSDef& Def)
{
	if (Def.Cat == TEXT("building") || Def.Cat == TEXT("defense")) return TEXT("cy");
	if (Def.Cat == TEXT("infantry")) return TEXT("bar");
	if (Def.Cat == TEXT("naval")) return TEXT("yard");
	if (Def.Cat == TEXT("air"))
	{
		if (Def.bDrone) return TEXT("dronehub");
		return Def.bJet ? TEXT("airfield") : TEXT("fac");
	}
	return TEXT("fac");
}

FString URSPlayer::QueueCat(const FRSDef& Def)
{
	if (Def.Cat == TEXT("air") || Def.Cat == TEXT("naval")) return TEXT("vehicle");
	return Def.Cat;
}

TArray<FString> URSPlayer::TechFactions(const FString& Role) const
{
	TArray<FString> Out = { Faction };
	for (ARSBuilding* B : Game->Buildings)
	{
		if (B && B->Player == this && !B->bDead && B->Def->Role == Role && !Out.Contains(B->Def->Faction)) Out.Add(B->Def->Faction);
	}
	return Out;
}

FString URSPlayer::MissingReq(const FString& Id) const
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	static const TMap<FString, FString> Names = {
		{TEXT("cy"), TEXT("建造廠")}, {TEXT("power"), TEXT("發電廠")}, {TEXT("ref"), TEXT("精煉廠")}, {TEXT("bar"), TEXT("兵營")},
		{TEXT("fac"), TEXT("戰車工廠")}, {TEXT("radar"), TEXT("雷達")}, {TEXT("tech"), TEXT("科技中心")}, {TEXT("airfield"), TEXT("航空作戰中心")},
		{TEXT("dronehub"), TEXT("無人機站")}, {TEXT("bank"), TEXT("金融/能源設施")}, {TEXT("yard"), TEXT("造船廠")} };
	for (const FString& R : Def.Req)
	{
		if (!HasRole(R))
		{
			const FString* N = Names.Find(R);
			return N ? *N : R;
		}
	}
	return FString();
}

bool URSPlayer::Available(const FString& Id) const
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	if (!Def.IsValid() || !Def.bBuildable) return false;
	if (!Def.Country.IsEmpty() && Def.Country != Country) return false;
	return TechFactions(ProducerRole(Def)).Contains(Def.Faction);
}

bool URSPlayer::CanBuild(const FString& Id) const
{
	if (!Available(Id)) return false;
	if (!MissingReq(Id).IsEmpty()) return false;
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	if (Def.Limit > 0 && CountType(Id) >= Def.Limit) return false;
	return HasRole(ProducerRole(Def));
}

bool URSPlayer::Enqueue(const FString& Id)
{
	if (!CanBuild(Id)) return false;
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const FString Cat = QueueCat(Def);
	TArray<FRSQueueItem>& Q = Queues.FindOrAdd(Cat);
	if (Cat == TEXT("building") || Cat == TEXT("defense"))
	{
		if (Q.Num() > 0 || !GetReady(Cat).IsEmpty()) return false;
	}
	else if (Q.Num() >= 15)
	{
		return false;
	}
	FRSQueueItem It;
	It.Id = Id;
	Q.Add(It);
	return true;
}

float URSPlayer::CostOf(const FString& Id) const
{
	const float Em = (Game && Game->WorldEv) ? Game->WorldEv->CostMult(this) : 1.f;
	return FRSDefs::Get().GetDef(Id).Cost * FRSDefs::Get().CountryMult(Country, TEXT("cost"), Id) * Em;
}

FString URSPlayer::CountryName() const
{
	const FRSCountry* C = FRSDefs::Get().GetCountry(Country);
	return C ? C->Name : FString();
}

bool URSPlayer::Cancel(const FString& Id)
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const FString Cat = QueueCat(Def);
	if (FString* R = Ready.Find(Cat))
	{
		if (*R == Id)
		{
			*R = FString();
			Money += CostOf(Id);
			return true;
		}
	}
	TArray<FRSQueueItem>* Q = Queues.Find(Cat);
	if (!Q) return false;
	for (int32 I = Q->Num() - 1; I >= 0; --I)
	{
		if ((*Q)[I].Id == Id)
		{
			Money += (*Q)[I].Spent;
			Q->RemoveAt(I);
			return true;
		}
	}
	return false;
}

float URSPlayer::ProgressOf(const FString& Id) const
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const TArray<FRSQueueItem>* Q = Queues.Find(QueueCat(Def));
	if (Q && Q->Num() > 0 && (*Q)[0].Id == Id) return (*Q)[0].Progress;
	return -1.f;
}

int32 URSPlayer::QueuedCount(const FString& Id) const
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const TArray<FRSQueueItem>* Q = Queues.Find(QueueCat(Def));
	int32 N = 0;
	if (Q)
		for (const FRSQueueItem& It : *Q)
			if (It.Id == Id) N++;
	return N;
}

void URSPlayer::RecalcPower()
{
	PowerProd = 0.f;
	PowerUse = 0.f;
	for (ARSBuilding* B : Game->Buildings)
	{
		if (B && B->Player == this && !B->bDead && B->Built())
		{
			const float Pw = B->Def->Power;
			if (Pw > 0.f) PowerProd += Pw * FMath::Clamp(B->Hp / B->MaxHp * 1.2f, 0.3f, 1.f);
			else PowerUse += -Pw;
		}
	}
	if (Game->Time < EnergyCutUntil) PowerProd = 0.f;
	bLowPower = PowerUse > PowerProd;
	if (bLowPower && !bWasLow && !bAI && PowerUse > 0.f)
	{
		Game->Audio()->Say(TEXT("low_power"), 20.f);
		Game->Audio()->Play(TEXT("power_down"), -4.f);
		Game->Message(TEXT("電力不足！生產減慢、防禦停擺"));
	}
	bWasLow = bLowPower;
}

bool URSPlayer::Sanctioned() const
{
	return Game && Game->Time < SanctionedUntil;
}

void URSPlayer::Update(float Dt)
{
	if (bNeutral || !bAlive) return;
	const FRSDefs& D = FRSDefs::Get();
	RecalcPower();
	for (auto& QP : Queues)
	{
		const FString& Cat = QP.Key;
		TArray<FRSQueueItem>& Q = QP.Value;
		if (Q.Num() == 0) continue;
		if (!GetReady(Cat).IsEmpty()) continue;
		FRSQueueItem& It = Q[0];
		const FRSDef& Def = D.GetDef(It.Id);
		const FString Role = ProducerRole(Def);
		const int32 N = CountRole(Role);
		if (N == 0 || !HasRole(Role)) continue;
		const float T = D.BuildTime(It.Id) / BuildMult * D.CountryMult(Country, TEXT("time"), It.Id);
		float Mult = 1.f + 0.4f * (FMath::Min(N, 4) - 1);
		if (bLowPower) Mult *= 0.45f;
		if (Game->Time < SabotageUntil) Mult *= 0.3f;
		if (Sanctioned()) Mult *= 0.5f;
		if (Game->WorldEv) Mult *= Game->WorldEv->SpeedMult(this);
		const float Step = Dt * Mult / T;
		const float CostFull = CostOf(It.Id);
		const float CostStep = CostFull * Step;
		if (Money < CostStep)
		{
			if (!bAI && Game->Time - FundsWarned > 12.f)
			{
				FundsWarned = Game->Time;
				Game->Audio()->Say(TEXT("funds_low"), 12.f);
				Game->Message(TEXT("資金不足"));
			}
			continue;
		}
		Money -= CostStep;
		It.Spent += CostStep;
		It.Progress += Step;
		if (It.Progress >= 1.f)
		{
			Money -= FMath::Max(0.f, CostFull - It.Spent);
			const FString Id = It.Id;
			Q.RemoveAt(0);
			if (Cat == TEXT("building") || Cat == TEXT("defense"))
			{
				Ready.Add(Cat, Id);
				if (!bAI)
				{
					Game->Audio()->Say(TEXT("building_ready"), 1.f);
					Game->Message(FString::Printf(TEXT("%s 建造完成，請選擇放置位置"), *Def.Name));
				}
			}
			else
			{
				Game->SpawnProduced(this, Id);
				Stats.Built++;
			}
		}
	}
	// 特殊武器
	TSet<FString> Have;
	const FRSCountry* C = D.GetCountry(Country);
	for (ARSBuilding* B : Game->Buildings)
	{
		if (!B || B->Player != this || B->bDead || !B->Built()) continue;
		if (!B->Def->PowerSw.IsEmpty()) Have.Add(B->Def->PowerSw);
		// 國家特殊能力：需要雷達 / 航空作戰中心
		if (B->Def->Role == TEXT("radar") && C && !C->Power.IsEmpty()) Have.Add(C->Power);
		// 網路作戰：所有陣營有雷達站即可使用
		if (B->Def->Role == TEXT("radar")) Have.Add(TEXT("cyber"));
	}
	if (Allies.Num() > 0 && !bAI) Have.Add(TEXT("allyaid"));
	TArray<FString> Keys;
	Powers.GetKeys(Keys);
	for (const FString& K : Keys)
		if (!Have.Contains(K)) Powers.Remove(K);
	for (const FString& K : Have)
	{
		float& Cv = Powers.FindOrAdd(K);
		if (Cv < 1.f && !bLowPower)
		{
			const float* Cm = C ? C->Charge.Find(K) : nullptr;
			const float Nc = FMath::Min(1.f, Cv + Dt * (Cm ? *Cm : 1.f) / FMath::Max(1.f, D.GetSuper(K).Charge));
			Cv = Nc;
			if (Nc >= 1.f) Game->OnPowerReady(this, K);
		}
	}
}

TArray<FString> URSPlayer::PowerList() const
{
	TArray<FString> Out;
	for (const FString& K : FRSDefs::Get().PowerOrder)
		if (Powers.Contains(K)) Out.Add(K);
	return Out;
}
