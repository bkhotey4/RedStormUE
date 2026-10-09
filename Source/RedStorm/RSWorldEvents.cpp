#include "RSWorldEvents.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSBuilding.h"
#include "RSAI.h"
#include "RSAudio.h"

void URSWorldEvents::Setup(ARSGame* G)
{
	Game = G;
	NextT = 120.f + RS::Rand(0.f, 90.f);
}

bool URSWorldEvents::Enabled() const
{
	return Game->Mission == nullptr && Game->Settings.bEvents;
}

void URSWorldEvents::Update(float Dt)
{
	BannerT = FMath::Max(0.f, BannerT - Dt);
	for (int32 I = Active.Num() - 1; I >= 0; --I)
	{
		if (Game->Time >= Active[I].Until)
		{
			const FRSActiveEvent E = Active[I];
			Active.RemoveAt(I);
			EndEvent(E);
		}
	}
	if (!Enabled()) return;
	if (Game->Time >= NextT)
	{
		NextT = Game->Time + 120.f + RS::Rand(0.f, 120.f);
		FireRandom();
	}
}

float URSWorldEvents::CostMult(const URSPlayer* P) const
{
	const float* V = P ? CostMod.Find(P->Index) : nullptr;
	return V ? *V : 1.f;
}

float URSWorldEvents::SpeedMult(const URSPlayer* P) const
{
	const float* V = P ? SpeedMod.Find(P->Index) : nullptr;
	return V ? *V : 1.f;
}

void URSWorldEvents::Show(const FString& Txt)
{
	Banner = Txt;
	BannerT = 14.f;
	Game->Message(TEXT("RSN 快訊：") + Txt);
	Game->Audio()->Play(TEXT("radio"), -8.f);
}

void URSWorldEvents::FireRandom()
{
	TArray<URSPlayer*> Alive;
	for (URSPlayer* P : Game->Players)
		if (!P->bNeutral && P->bAlive) Alive.Add(P);
	const TArray<FRSEventDef>& Evs = FRSDefs::Get().Events;
	if (Alive.Num() == 0 || Evs.Num() == 0) return;
	const FRSEventDef* Ev = &Evs[RS::RandI(0, Evs.Num() - 1)];
	if (Hist.Num() > 0 && Hist.Last() == Ev->Id) Ev = &Evs[RS::RandI(0, Evs.Num() - 1)];
	URSPlayer* Target = Alive[RS::RandI(0, Alive.Num() - 1)];
	if (Target == Game->Human && Ev->bBad && Game->Settings.Difficulty == 0 && RS::Rand01() < 0.5f)
	{
		Target = Alive[RS::RandI(0, Alive.Num() - 1)];
	}
	Fire(*Ev, Target);
}

void URSWorldEvents::Fire(const FRSEventDef& Ev, URSPlayer* Target)
{
	Hist.Add(Ev.Id);
	const FString Nm = Target->CountryName().IsEmpty() ? Target->Name : Target->CountryName();
	FRSActiveEvent E;
	E.Id = Ev.Id;
	E.Name = Ev.Name;
	E.Until = Game->Time + Ev.Dur;
	E.Target = Target;
	E.bGlobal = Ev.bGlobal;
	const float T = Game->Time;
	if (Ev.Id == TEXT("sanction"))
	{
		Target->SanctionedUntil = FMath::Max(Target->SanctionedUntil, T + Ev.Dur);
	}
	else if (Ev.Id == TEXT("hack"))
	{
		Target->EnergyCutUntil = FMath::Max(Target->EnergyCutUntil, T + Ev.Dur);
		Target->bPowerDirty = true;
	}
	else if (Ev.Id == TEXT("strike"))
	{
		SpeedMod.Add(Target->Index, 0.55f);
	}
	else if (Ev.Id == TEXT("inflate"))
	{
		CostMod.Add(Target->Index, 1.3f);
	}
	else if (Ev.Id == TEXT("aid"))
	{
		Target->Money += 1500.0 + 500.0 * RS::RandI(0, 3);
		const FString Pre = Target->Faction == TEXT("allies") ? TEXT("a_") : TEXT("s_");
		const TArray<FString> Ids = { Pre + TEXT("tank"), Pre + TEXT("tank"), Pre + (Target->Faction == TEXT("allies") ? TEXT("ifv") : TEXT("flaktrk")) };
		ARSBuilding* Cy = nullptr;
		for (ARSBuilding* B : Game->Buildings)
			if (B && B->Player == Target && !B->bDead && B->Def->Role == TEXT("cy")) Cy = B;
		if (Cy)
		{
			for (const FString& Id : Ids)
			{
				if (!FRSDefs::Get().Units.Contains(Id)) continue;
				const FIntPoint C = Game->NearestFree(RS::ToCell(Cy->Pos + FVector2D(RS::Rand(-2.f, 2.f), 3.5f)), 5);
				if (C.X >= 0) Game->SpawnUnit(Target, Id, RS::CellCenter(C));
			}
		}
	}
	else if (Ev.Id == TEXT("boom"))
	{
		SpeedMod.Add(Target->Index, 1.5f);
	}
	else if (Ev.Id == TEXT("opinion"))
	{
		Target->NoTreatyUntil = T + Ev.Dur;
	}
	else if (Ev.Id == TEXT("leak"))
	{
		if (Target != Game->Human)
		{
			for (ARSBuilding* B : Game->Buildings)
				if (B && B->Player == Target && !B->bDead) Game->Reveal(B->Pos, 5.f);
		}
		else
		{
			for (URSAI* AI : Game->AIs) AI->NextAttack = FMath::Min(AI->NextAttack, T + 30.f);
		}
	}
	else if (Ev.Id == TEXT("ceasefire"))
	{
		for (URSPlayer* P : Game->Players)
		{
			if (P->bNeutral) continue;
			for (URSPlayer* Q : Game->Players)
				if (Q != P && !Q->bNeutral && P->IsEnemyOf(Q)) P->Ceasefire.Add(Q->Index, T + Ev.Dur);
		}
	}
	if (Ev.Dur > 0.f) Active.Add(E);
	Show(Ev.News.Contains(TEXT("%s")) ? Ev.News.Replace(TEXT("%s"), *Nm).Replace(TEXT("%%"), TEXT("%")) : Ev.News.Replace(TEXT("%%"), TEXT("%")));
}

void URSWorldEvents::EndEvent(const FRSActiveEvent& E)
{
	URSPlayer* T = E.Target.Get();
	if (T)
	{
		if (E.Id == TEXT("strike") || E.Id == TEXT("boom")) SpeedMod.Remove(T->Index);
		else if (E.Id == TEXT("inflate")) CostMod.Remove(T->Index);
	}
	if (T == Game->Human || E.bGlobal) Game->Message(FString::Printf(TEXT("RSN：「%s」事件結束"), *E.Name));
}

float URSWorldEvents::OilBonus(const URSPlayer* P) const
{
	for (const FRSActiveEvent& E : Active)
		if (E.Id == TEXT("oil") && E.Target.Get() == P) return 2.f;
	return 1.f;
}

TArray<FString> URSWorldEvents::StatusLines() const
{
	TArray<FString> Out;
	for (const FRSActiveEvent& E : Active)
	{
		const URSPlayer* T = E.Target.Get();
		const FString Who = T ? (T->CountryName().IsEmpty() ? T->Name : T->CountryName()) : FString();
		Out.Add(FString::Printf(TEXT("%s（%s）%s"), *E.Name, *Who, *RS::MMSS(E.Until - Game->Time)));
	}
	return Out;
}
