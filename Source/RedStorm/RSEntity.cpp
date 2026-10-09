#include "RSEntity.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSVisuals.h"
#include "Components/StaticMeshComponent.h"

ARSEntity::ARSEntity()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

FLinearColor ARSEntity::TC() const
{
	return Player ? Player->Color : FLinearColor::White;
}

bool ARSEntity::IsEnemy(const ARSEntity* O) const
{
	return RSAlive(O) && Player && Player->IsEnemyOf(O->Player);
}

float ARSEntity::ArmorMult() const
{
	static const float M[3] = { 1.f, 0.85f, 0.7f };
	return M[FMath::Clamp(Rank, 0, 2)];
}

float ARSEntity::DmgMult() const
{
	static const float M[3] = { 1.f, 1.2f, 1.5f };
	return M[FMath::Clamp(Rank, 0, 2)];
}

void ARSEntity::AddKill()
{
	Kills++;
	int32 NR = 0;
	if (Kills >= 6) NR = 2;
	else if (Kills >= 3) NR = 1;
	if (NR > Rank)
	{
		Rank = NR;
		if (Game && Player == Game->Human)
		{
			Game->Message(FString::Printf(TEXT("%s 晉升為%s！"), *Def->Name, Rank == 1 ? TEXT("老兵") : TEXT("精英")));
		}
	}
}

void ARSEntity::ApplyDamage(float Amount, ARSEntity* Attacker, const FString& Wh)
{
	if (bDead) return;
	const FRSDefs& D = FRSDefs::Get();
	float Dd = Amount * D.WarheadMult(Wh, Armor) * ArmorMult();
	if (Armor == TEXT("naval") && Attacker && IsValid(Attacker) && Attacker->Player)
	{
		if (const FRSCountry* C = D.GetCountry(Attacker->Player->Country)) Dd *= C->DmgVsShip;
	}
	if (Dd <= 0.f) return;
	Hp -= Dd;
	LastHit = Game->Time;
	FlashT = 0.08f;
	const bool bAttOk = Attacker && IsValid(Attacker) && !Attacker->bDead;
	if (bAttOk)
	{
		LastAttacker = Attacker;
		OnAttacked(Attacker);
		if (Attacker->Player != Player && !Player->bNeutral && !Attacker->Player->bNeutral)
		{
			Player->AddRelation(Attacker->Player, -0.4f);
		}
	}
	if (Hp <= 0.f && !bDead)
	{
		Hp = 0.f;
		if (Attacker && IsValid(Attacker) && Attacker->Player != Player)
		{
			Attacker->AddKill();
			Attacker->Player->Stats.Killed++;
		}
		Die();
	}
}

void ARSEntity::Die()
{
	if (bDead) return;
	bDead = true;
	bSelected = false;
	Game->OnEntityDied(this);
}

float ARSEntity::GroundZ() const
{
	return Game ? Game->GroundHeight(Pos) : 0.f;
}

FVector ARSEntity::Center3D() const
{
	return FVector(Pos.X * RS_CELL, Pos.Y * RS_CELL, GroundZ() + VisualTop * 0.5f);
}

void ARSEntity::RefreshTeamColor()
{
	const FLinearColor Team = TC();
	for (int32 I = 0; I < TeamParts.Num(); ++I)
	{
		const float Sh = TeamShade.IsValidIndex(I) ? TeamShade[I] : 0.f;
		RSV::SetColor(TeamParts[I], Sh < 0.f ? RS::Darken(Team, -Sh) : RS::Lighten(Team, Sh));
	}
}

void ARSEntity::SetShown(bool b)
{
	if (bShown == b) return;
	bShown = b;
	SetActorHiddenInGame(!b);
}
