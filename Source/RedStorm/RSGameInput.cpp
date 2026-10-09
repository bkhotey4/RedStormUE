// ARSGame：座標轉換、選取、滑鼠與鍵盤指令（對應 Godot game.gd 後半部）
#include "RSGame.h"
#include "RSCommon.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSAudio.h"
#include "RSCameraPawn.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

// ───────────────────────── 座標 ─────────────────────────
float ARSGame::GroundHeight(const FVector2D& P) const
{
	if (!Map.IsValid()) return 0.f;
	return Map->HeightAt(P) * RS_HIGH;
}

FVector ARSGame::ToWorld(const FVector2D& P, float ZUp) const
{
	return FVector(P.X * RS_CELL, P.Y * RS_CELL, GroundHeight(P) + ZUp);
}

void ARSGame::CenterOn(const FVector2D& P)
{
	if (Cam) Cam->SetFocusWorld(ToWorld(P));
}

bool ARSGame::Project(const FVector& World, FVector2D& Screen) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC && PC->ProjectWorldLocationToScreen(World, Screen, false);
}

bool ARSGame::ScreenToCell(const FVector2D& Screen, FVector2D& Out) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return false;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(Screen.X, Screen.Y, O, D)) return false;
	if (FMath::Abs(D.Z) < 1e-4f) return false;
	// 先試高地平面，該格確實是高地才採用
	{
		const float T = (RS_HIGH - O.Z) / D.Z;
		if (T > 0.f)
		{
			const FVector P = O + D * T;
			const FVector2D C(P.X / RS_CELL, P.Y / RS_CELL);
			if (Map->ElevAt(C) == 1 && Map->HeightAt(C) > 0.5f)
			{
				Out = C;
				return true;
			}
		}
	}
	const float T = (0.f - O.Z) / D.Z;
	if (T <= 0.f) return false;
	const FVector P = O + D * T;
	Out = FVector2D(P.X / RS_CELL, P.Y / RS_CELL);
	return true;
}

bool ARSGame::MouseWorld(FVector2D& Out) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	float MX, MY;
	if (!PC || !PC->GetMousePosition(MX, MY)) return false;
	return ScreenToCell(FVector2D(MX, MY), Out);
}

bool ARSGame::HighGround(const ARSEntity* Ent, const ARSEntity* T) const
{
	if (!Ent || !T || Ent->bIsBuilding) return false;
	return Map->ElevAt(Ent->Pos) == 1 && Map->ElevAt(T->Pos) == 0;
}

float ARSGame::SoundAttenuation(const FVector2D& WP) const
{
	FVector2D S;
	if (!GetWorld() || !GEngine || !GEngine->GameViewport) return 0.f;
	FVector2D VS;
	GEngine->GameViewport->GetViewportSize(VS);
	if (!Project(ToWorld(WP), S)) return -60.f;
	const float NX = FMath::Abs(S.X - VS.X * 0.5f) / (VS.X * 0.5f);
	const float NY = FMath::Abs(S.Y - VS.Y * 0.5f) / (VS.Y * 0.5f);
	const float N = FMath::Max(NX, NY);
	if (N <= 1.f) return 0.f;
	if (N > 2.5f) return -60.f;
	return -(N - 1.f) * 14.f;
}

// ───────────────────────── 選取 ─────────────────────────
ARSEntity* ARSGame::EntityAtScreen(const FVector2D& SP) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !Cam) return nullptr;
	const FVector Right = Cam->RightDir();
	ARSEntity* Best = nullptr;
	float BD = 1e9f;
	for (ARSUnit* U : Units)
	{
		if (!U || U->bDead || !U->bShown) continue;
		const FVector C = U->Center3D();
		FVector2D S, S2;
		if (!Project(C, S) || !Project(C + Right * U->PickRadius, S2)) continue;
		const float Rad = FMath::Max(10.f, FVector2D::Distance(S, S2));
		const float D = FVector2D::Distance(S, SP);
		if (D < Rad && D < BD)
		{
			BD = D;
			Best = U;
		}
	}
	if (Best) return Best;
	FVector O, Dir;
	if (!PC->DeprojectScreenPositionToWorld(SP.X, SP.Y, O, Dir)) return nullptr;
	const FVector End = O + Dir * 100000.f;
	float BestT = 1e12f;
	for (ARSBuilding* B : Buildings)
	{
		if (!B || B->bDead || !B->bShown) continue;
		const float Z0 = B->GroundZ();
		const FBox Box(FVector(B->Cell.X * RS_CELL, B->Cell.Y * RS_CELL, Z0 - 5.f),
			FVector((B->Cell.X + B->Size.X) * RS_CELL, (B->Cell.Y + B->Size.Y) * RS_CELL, Z0 + FMath::Max(30.f, B->VisualTop * 0.85f)));
		if (FMath::LineBoxIntersection(Box, O, End, End - O))
		{
			const float T = FVector::DistSquared(O, Box.GetCenter());
			if (T < BestT)
			{
				BestT = T;
				Best = B;
			}
		}
	}
	return Best;
}

void ARSGame::UpdateHover()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	float MX, MY;
	if (!PC || !PC->GetMousePosition(MX, MY))
	{
		Hover = nullptr;
		return;
	}
	Hover = EntityAtScreen(FVector2D(MX, MY));
}

TArray<ARSEntity*> ARSGame::ValidSelection() const
{
	TArray<ARSEntity*> Out;
	for (const TWeakObjectPtr<ARSEntity>& Wk : Selection)
		if (ARSEntity* E = Wk.Get())
			if (!E->bDead) Out.Add(E);
	return Out;
}

void ARSGame::SelectList(const TArray<ARSEntity*>& List, bool bAdd)
{
	if (!bAdd)
	{
		for (ARSEntity* E : ValidSelection()) E->bSelected = false;
		Selection.Reset();
	}
	for (ARSEntity* E : List)
	{
		if (!E) continue;
		bool bHas = false;
		for (const TWeakObjectPtr<ARSEntity>& Wk : Selection)
			if (Wk.Get() == E) bHas = true;
		if (!bHas)
		{
			Selection.Add(E);
			E->bSelected = true;
		}
	}
}

TArray<ARSUnit*> ARSGame::OwnSelectedUnits() const
{
	TArray<ARSUnit*> Out;
	for (ARSEntity* E : ValidSelection())
		if (ARSUnit* U = Cast<ARSUnit>(E))
			if (U->Player == Human) Out.Add(U);
	return Out;
}

void ARSGame::OnLeftPress(bool bShift, bool bDouble)
{
	if (bGameOver) return;
	FVector2D MW;
	const bool bHasMW = MouseWorld(MW);
	if (!PlaceId.IsEmpty())
	{
		TryPlace();
		return;
	}
	if (CmdMode.StartsWith(TEXT("power:")))
	{
		const FString Kind = CmdMode.Mid(6);
		if (bHasMW && FirePower(Human, Kind, MW)) CmdMode.Empty();
		else Message(TEXT("無效的目標"));
		return;
	}
	if (CmdMode == TEXT("sell") || CmdMode == TEXT("repair"))
	{
		ARSBuilding* B = Cast<ARSBuilding>(Hover.Get());
		if (B && B->Player == Human)
		{
			if (CmdMode == TEXT("sell")) Sell(B);
			else
			{
				B->bRepairing = !B->bRepairing;
				Audio()->Play(TEXT("click"), -4.f);
			}
		}
		if (!bShift) CmdMode.Empty();
		return;
	}
	if (CmdMode == TEXT("amove"))
	{
		if (!bHasMW) return;
		if (bShift)
		{
			for (ARSUnit* U : OwnSelectedUnits()) U->QueueMove(MW, true);
		}
		else
		{
			CommandMove(OwnSelectedUnits(), MW, true);
			CmdMode.Empty();
		}
		return;
	}
	if (bDouble)
	{
		ClickSelect(false, true);
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	float MX = 0.f, MY = 0.f;
	if (PC) PC->GetMousePosition(MX, MY);
	bDragging = true;
	DragStart = FVector2D(MX, MY);
}

void ARSGame::OnLeftRelease(bool bShift)
{
	if (!bDragging) return;
	bDragging = false;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	float MX = 0.f, MY = 0.f;
	if (PC) PC->GetMousePosition(MX, MY);
	const FVector2D Cur(MX, MY);
	if (FVector2D::Distance(Cur, DragStart) > 8.f) BoxSelect(DragStart, Cur, bShift);
	else ClickSelect(bShift, false);
}

void ARSGame::OnRightPress(bool bCtrl, bool bShift)
{
	if (bGameOver) return;
	if (!PlaceId.IsEmpty() || !CmdMode.IsEmpty())
	{
		PlaceId.Empty();
		CmdMode.Empty();
		return;
	}
	ContextOrder(bCtrl, bShift);
}

void ARSGame::ClickSelect(bool bShift, bool bDbl)
{
	ARSEntity* E = Hover.Get();
	if (!E)
	{
		if (!bShift) SelectList({});
		return;
	}
	if (E->Player == Human)
	{
		ARSUnit* U = Cast<ARSUnit>(E);
		if (bDbl && U)
		{
			if (!U->Def->Mcv.IsEmpty())
			{
				U->CmdDeploy();
				return;
			}
			FVector2D VS(1920, 1080);
			if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VS);
			TArray<ARSEntity*> List;
			for (ARSUnit* O : Units)
			{
				if (!O || O->Player != Human || O->bDead || O->TypeId != U->TypeId) continue;
				FVector2D S;
				if (Project(O->Center3D(), S) && S.X >= 0 && S.Y >= 0 && S.X <= VS.X && S.Y <= VS.Y) List.Add(O);
			}
			SelectList(List, bShift);
		}
		else if (bShift)
		{
			bool bHas = false;
			for (const TWeakObjectPtr<ARSEntity>& Wk : Selection)
				if (Wk.Get() == E) bHas = true;
			if (bHas)
			{
				Selection.RemoveAll([E](const TWeakObjectPtr<ARSEntity>& Wk) { return Wk.Get() == E; });
				E->bSelected = false;
			}
			else
			{
				SelectList({ E }, true);
			}
		}
		else
		{
			SelectList({ E });
			if (ARSBuilding* B = Cast<ARSBuilding>(E))
			{
				const FString& R = B->Def->Role;
				if (R == TEXT("bar") || R == TEXT("fac") || B->Def->bAirfield)
				{
					const FString Key = (!B->Def->bAirfield || R == TEXT("fac")) ? R : FString(TEXT("airfield"));
					Human->Primary.Add(Key, B);
				}
			}
		}
		if (U) AckSelect();
		else Audio()->Play(TEXT("click"), -8.f);
	}
	else
	{
		SelectList({ E });
	}
}

void ARSGame::AckSelect()
{
	if (AckT > 0.f) return;
	AckT = 0.4f;
	Audio()->Play(TEXT("select"), -6.f);
}

void ARSGame::BoxSelect(const FVector2D& A, const FVector2D& B, bool bAdd)
{
	const FBox2D R(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	TArray<ARSEntity*> List;
	bool bCombat = false;
	for (ARSUnit* U : Units)
	{
		if (!U || U->Player != Human || U->bDead) continue;
		FVector2D S;
		if (Project(U->Center3D(), S) && R.IsInside(S))
		{
			List.Add(U);
			if (!U->Def->bHarvester) bCombat = true;
		}
	}
	if (bCombat) List.RemoveAll([](ARSEntity* E) { return E->Def->bHarvester; });
	SelectList(List, bAdd);
	if (List.Num() > 0) AckSelect();
}

void ARSGame::ContextOrder(bool bForceFire, bool bShift)
{
	TArray<ARSUnit*> List = OwnSelectedUnits();
	FVector2D WP;
	if (!MouseWorld(WP)) return;
	ARSEntity* T = Hover.Get();
	if (List.Num() == 0)
	{
		for (ARSEntity* E : ValidSelection())
		{
			ARSBuilding* B = Cast<ARSBuilding>(E);
			if (!B || B->Player != Human) continue;
			const FString& R = B->Def->Role;
			if (R == TEXT("bar") || R == TEXT("fac") || R == TEXT("yard") || R == TEXT("dronehub") || B->Def->bAirfield)
			{
				B->Rally = WP;
				Markers.Add({ WP, 0.f, FLinearColor(0.3f, 1.f, 0.3f) });
				Audio()->Play(TEXT("click"), -6.f);
			}
		}
		return;
	}
	Audio()->Play(TEXT("radio"), -10.f);
	ARSBuilding* TB = Cast<ARSBuilding>(T);
	if (TB && TB->Def->Garrison > 0 && (TB->Player->bNeutral || TB->Player == Human))
	{
		TArray<ARSUnit*> Rest;
		for (ARSUnit* U : List)
		{
			if (CanGarrison(U, TB)) U->CmdGarrison(TB);
			else Rest.Add(U);
		}
		if (Rest.Num() < List.Num())
		{
			Markers.Add({ TB->Pos, 0.f, FLinearColor(0.3f, 0.8f, 1.f) });
			if (Rest.Num() > 0) CommandMove(Rest, WP);
			return;
		}
	}
	if (T && T->Player != Human && T->bShown)
	{
		const bool bEnemy = Human->IsEnemyOf(T->Player);
		for (ARSUnit* U : List)
		{
			if (U->Def->bSpy && TB && bEnemy && !TB->Def->bWall && !TB->Def->bCiv) U->CmdCapture(TB);
			else if (U->Def->bEngineer && TB && !TB->Def->bWall && !TB->Def->bCiv) U->CmdCapture(TB);
			else if (bEnemy || bForceFire)
			{
				if (U->HasWeapon() || U->Def->bC4) U->CmdAttack(T);
			}
			else U->CmdMove(T->Pos);
		}
		Markers.Add({ T->Pos, 0.f, FLinearColor(1.f, 0.25f, 0.2f) });
		UnitAck(List, bEnemy);
		return;
	}
	if (TB && TB->Player == Human)
	{
		TArray<ARSUnit*> Rest;
		for (ARSUnit* U : List)
		{
			if (U->Def->bHarvester && TB->Def->Role == TEXT("ref")) U->CmdDock(TB);
			else if (U->Def->bEngineer && TB->Hp < TB->MaxHp) U->CmdCapture(TB);
			else if (bForceFire) U->CmdAttack(TB);
			else Rest.Add(U);
		}
		if (Rest.Num() > 0) CommandMove(Rest, WP);
		return;
	}
	const FIntPoint Cell = RS::ToCell(WP);
	if (Map->HasOre(Cell.X, Cell.Y) && IsExplored(Cell.X, Cell.Y))
	{
		TArray<ARSUnit*> Rest;
		for (ARSUnit* U : List)
		{
			if (U->Def->bHarvester) U->CmdHarvest(Cell);
			else Rest.Add(U);
		}
		if (Rest.Num() == 0)
		{
			Markers.Add({ WP, 0.f, FLinearColor(1.f, 0.85f, 0.2f) });
			return;
		}
		List = Rest;
	}
	if (bShift)
	{
		for (ARSUnit* U : List) U->QueueMove(WP + FVector2D(RS::Rand(-0.6f, 0.6f), RS::Rand(-0.6f, 0.6f)));
		Markers.Add({ WP, 0.f, FLinearColor(0.3f, 1.f, 0.8f) });
		return;
	}
	CommandMove(List, WP);
}

void ARSGame::UnitAck(const TArray<ARSUnit*>& List, bool bAttack)
{
	if (List.Num() == 0 || List[0]->Player != Human) return;
	ARSUnit* U = List[0];
	if (U->TypeId == TEXT("s_arty")) Audio()->Ack(bAttack ? TEXT("ack_arty_attack") : TEXT("ack_arty_move"));
	else if (U->UKind == TEXT("inf")) { if (!bAttack) Audio()->Ack(TEXT("ack_inf_move")); }
	else if (U->TypeId == TEXT("s_mammoth") && bAttack) Audio()->Ack(TEXT("ack_heavy_attack"));
	else if (U->UKind == TEXT("veh") && !U->Def->bHarvester) Audio()->Ack(bAttack ? TEXT("ack_veh_attack") : TEXT("ack_veh_move"));
}

void ARSGame::CommandMove(const TArray<ARSUnit*>& List, const FVector2D& Dest, bool bAmove)
{
	if (List.Num() == 0) return;
	UnitAck(List, bAmove);
	Markers.Add({ Dest, 0.f, bAmove ? FLinearColor(1.f, 0.3f, 0.2f) : FLinearColor(0.3f, 1.f, 0.3f) });
	TArray<ARSUnit*> Ground;
	for (ARSUnit* U : List)
	{
		if (U->UKind == TEXT("air"))
		{
			U->CmdMove(bAmove ? Dest : Dest + FVector2D(RS::Rand(-0.6f, 0.6f), RS::Rand(-0.6f, 0.6f)));
		}
		else
		{
			Ground.Add(U);
		}
	}
	if (Ground.Num() == 0) return;
	if (bFormation && Ground.Num() >= 4 && !Ground.ContainsByPredicate([](const ARSUnit* U) { return U->UKind == TEXT("ship"); }))
	{
		FormationMove(Ground, Dest, bAmove);
		return;
	}
	const FIntPoint DC(FMath::Clamp(FMath::FloorToInt(Dest.X), 0, W - 1), FMath::Clamp(FMath::FloorToInt(Dest.Y), 0, H - 1));
	int32 Need = 0;
	for (ARSUnit* U : Ground) Need += U->UKind != TEXT("inf") ? 1 : 0;
	const int32 NInf = Ground.Num() - Need;
	Need += FMath::CeilToInt(NInf / 3.f);
	TArray<FIntPoint> Cells = FreeCellsNear(DC, Need);
	Ground.Sort([&Dest](const ARSUnit& A, const ARSUnit& B) { return FVector2D::DistSquared(A.Pos, Dest) < FVector2D::DistSquared(B.Pos, Dest); });
	int32 InfSlot = 0;
	FIntPoint InfCell(-1, -1);
	const FVector2D Offs[3] = { {0.3f, 0.3f}, {0.7f, 0.35f}, {0.5f, 0.72f} };
	for (ARSUnit* U : Ground)
	{
		if (Cells.Num() == 0)
		{
			U->CmdMove(Dest, bAmove);
			continue;
		}
		if (U->UKind == TEXT("inf"))
		{
			if (InfSlot == 0 || InfCell.X < 0) InfCell = PopNearest(Cells, U->Pos);
			U->CmdMove(RS::Cell2(InfCell) + Offs[InfSlot], bAmove);
			InfSlot = (InfSlot + 1) % 3;
		}
		else
		{
			const FIntPoint C = PopNearest(Cells, Dest);
			U->CmdMove(RS::CellCenter(C), bAmove);
		}
	}
}

// 陣型移動：戰車在前、步兵居中、砲兵與支援在後，橫向展開、面向行進方向，並與最慢單位同速前進
void ARSGame::FormationMove(const TArray<ARSUnit*>& Group, const FVector2D& Dest, bool bAmove)
{
	FVector2D Cen(0.f, 0.f);
	float MinSpd = 1e9f;
	for (ARSUnit* U : Group)
	{
		Cen += U->Pos;
		MinSpd = FMath::Min(MinSpd, U->Speed);
	}
	Cen /= float(Group.Num());
	FVector2D Fwd = Dest - Cen;
	if (Fwd.Size() < 0.5f) Fwd = FVector2D(1.f, 1.f);
	Fwd.Normalize();
	const FVector2D Side(-Fwd.Y, Fwd.X);
	auto RankOf = [](const ARSUnit* U) -> int32
	{
		if (!U->HasWeapon() || U->Def->bHarvester || U->Def->bEngineer) return 3;
		if (U->Weapon->MinRange > 0.f || U->Weapon->Range >= 7.f) return 2;
		if (U->UKind == TEXT("inf")) return 1;
		return 0;
	};
	TArray<ARSUnit*> Ranks[4];
	for (ARSUnit* U : Group) Ranks[RankOf(U)].Add(U);
	struct FSlotRow { TArray<ARSUnit*> Units; TArray<FVector2D> Slots; };
	TArray<FSlotRow> Rows;
	float Depth = 0.f;
	for (int32 K = 0; K < 4; ++K)
	{
		TArray<ARSUnit*>& RU = Ranks[K];
		if (RU.Num() == 0) continue;
		const bool bAllInf = !RU.ContainsByPredicate([](const ARSUnit* U) { return U->UKind != TEXT("inf"); });
		const float Sp = bAllInf ? 0.6f : 1.05f;
		const int32 N = RU.Num();
		const int32 Cols = FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(N * 2.5f)), 1, 9);
		// 越靠前的單位排越前面的列
		RU.Sort([&Fwd](const ARSUnit& A, const ARSUnit& B) { return FVector2D::DotProduct(A.Pos, Fwd) > FVector2D::DotProduct(B.Pos, Fwd); });
		for (int32 Start = 0; Start < N; Start += Cols)
		{
			FSlotRow Row;
			const int32 Cnt = FMath::Min(Cols, N - Start);
			for (int32 C = 0; C < Cnt; ++C)
			{
				Row.Units.Add(RU[Start + C]);
				Row.Slots.Add(-Fwd * Depth + Side * ((C - (Cnt - 1) * 0.5f) * Sp));
			}
			Rows.Add(Row);
			Depth += Sp;
		}
		Depth += 0.35f;
	}
	const FVector2D Origin = Dest + Fwd * (Depth * 0.5f - 0.5f);
	TSet<FIntPoint> Used;
	for (FSlotRow& Row : Rows)
	{
		// 同一列依橫向位置配對，避免交叉
		Row.Units.Sort([&Side](const ARSUnit& A, const ARSUnit& B) { return FVector2D::DotProduct(A.Pos, Side) < FVector2D::DotProduct(B.Pos, Side); });
		for (int32 I = 0; I < Row.Units.Num(); ++I)
		{
			ARSUnit* U = Row.Units[I];
			FVector2D P = Origin + Row.Slots[I];
			P.X = FMath::Clamp(P.X, 0.3f, W - 0.3f);
			P.Y = FMath::Clamp(P.Y, 0.3f, H - 0.3f);
			FIntPoint C(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y));
			const bool bVeh = U->UKind != TEXT("inf");
			if (!Map->Passable(C.X, C.Y) || BldAt(C.X, C.Y) || (bVeh && Used.Contains(C)))
			{
				TArray<FIntPoint> Alt = FreeCellsNear(C, 10);
				Alt.RemoveAll([&Used, bVeh](const FIntPoint& Q) { return bVeh && Used.Contains(Q); });
				if (Alt.Num() > 0)
				{
					C = PopNearest(Alt, P);
					P = RS::CellCenter(C);
				}
			}
			if (bVeh) Used.Add(C);
			U->CmdMove(P, bAmove);
			if (FVector2D::Distance(U->Pos, Cen) < 6.f) U->FormSpeed = MinSpd;
			Markers.Add({ P, 0.25f, bAmove ? FLinearColor(0.9f, 0.35f, 0.25f) : FLinearColor(0.35f, 0.85f, 0.5f) });
		}
	}
}

FIntPoint ARSGame::PopNearest(TArray<FIntPoint>& Cells, const FVector2D& P) const
{
	int32 BI = 0;
	float BD = 1e9f;
	for (int32 I = 0; I < Cells.Num(); ++I)
	{
		const float D = FVector2D::DistSquared(RS::CellCenter(Cells[I]), P);
		if (D < BD)
		{
			BD = D;
			BI = I;
		}
	}
	const FIntPoint C = Cells[BI];
	Cells.RemoveAt(BI);
	return C;
}

TArray<FIntPoint> ARSGame::FreeCellsNear(FIntPoint C, int32 N) const
{
	TArray<FIntPoint> Out;
	int32 R = 0;
	while (Out.Num() < N && R < 12)
	{
		for (int32 Y = C.Y - R; Y <= C.Y + R; ++Y)
			for (int32 X = C.X - R; X <= C.X + R; ++X)
			{
				if (FMath::Max(FMath::Abs(X - C.X), FMath::Abs(Y - C.Y)) != R) continue;
				if (Map->Inb(X, Y) && Map->Passable(X, Y) && !BldAt(X, Y)) Out.Add(FIntPoint(X, Y));
			}
		R++;
	}
	return Out;
}

void ARSGame::HomeKey()
{
	ARSBuilding* Cy = nullptr;
	for (ARSBuilding* B : Buildings)
		if (B && B->Player == Human && B->Def->Role == TEXT("cy")) Cy = B;
	if (Cy)
	{
		CenterOn(Cy->Pos);
		return;
	}
	const TArray<ARSUnit*> Own = OwnSelectedUnits();
	if (Own.Num() > 0)
	{
		CenterOn(Own[0]->Pos);
		return;
	}
	for (ARSUnit* U : Units)
	{
		if (U && U->Player == Human)
		{
			CenterOn(U->Pos);
			return;
		}
	}
}

void ARSGame::OnKey(FKey Key, bool bCtrl, bool bShift)
{
	if (bGameOver) return;
	// 編隊 0~9
	static const FKey Digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 N = 0; N < 10; ++N)
	{
		if (Key != Digits[N]) continue;
		if (bCtrl)
		{
			TArray<TWeakObjectPtr<ARSUnit>>& G = Groups.FindOrAdd(N);
			G.Reset();
			for (ARSUnit* U : OwnSelectedUnits()) G.Add(U);
			Message(FString::Printf(TEXT("編隊 %d 已設定 (%d 個單位)"), N, G.Num()));
		}
		else if (TArray<TWeakObjectPtr<ARSUnit>>* G = Groups.Find(N))
		{
			G->RemoveAll([](const TWeakObjectPtr<ARSUnit>& Wk) { return !RSAlive(Wk); });
			if (G->Num() == 0) return;
			TArray<ARSEntity*> List;
			for (const TWeakObjectPtr<ARSUnit>& Wk : *G) List.Add(Wk.Get());
			SelectList(List, bShift);
			const double Now = FPlatformTime::Seconds();
			if (LastGroupKey == N && Now - LastGroupTime < 0.4) CenterOn(List[0]->Pos);
			LastGroupKey = N;
			LastGroupTime = Now;
		}
		return;
	}
	if (Key == EKeys::A)
	{
		if (OwnSelectedUnits().Num() > 0) CmdMode = TEXT("amove");
	}
	else if (Key == EKeys::S)
	{
		for (ARSUnit* U : OwnSelectedUnits()) U->CmdStop();
	}
	else if (Key == EKeys::D)
	{
		for (ARSUnit* U : OwnSelectedUnits())
			if (!U->Def->Mcv.IsEmpty()) U->CmdDeploy();
		for (ARSEntity* E : ValidSelection())
		{
			ARSBuilding* B = Cast<ARSBuilding>(E);
			if (B && B->Player == Human && B->Occupants.Num() > 0) GarrisonExit(B);
		}
	}
	else if (Key == EKeys::H || Key == EKeys::SpaceBar)
	{
		HomeKey();
	}
	else if (Key == EKeys::E)
	{
		TArray<ARSEntity*> List;
		for (ARSUnit* U : Units)
			if (U && U->Player == Human && !U->Def->bHarvester && U->Def->Mcv.IsEmpty() && !U->Def->bEngineer) List.Add(U);
		SelectList(List);
	}
	else if (Key == EKeys::N)
	{
		TArray<ARSEntity*> L;
		for (ARSUnit* U : Units)
			if (U && U->Player == Human && !U->bDead && U->Def->bHarvester && U->Order == TEXT("idle")) L.Add(U);
		CycleSelect(L, TEXT("idle_harv"), TEXT("沒有閒置的採礦車"));
	}
	else if (Key == EKeys::I)
	{
		TArray<ARSEntity*> L;
		for (ARSUnit* U : Units)
			if (U && U->Player == Human && !U->bDead && U->Order == TEXT("idle") && !U->Def->bHarvester && U->Def->Mcv.IsEmpty() && !U->Def->bEngineer && !U->Def->bSpy) L.Add(U);
		CycleSelect(L, TEXT("idle_unit"), TEXT("沒有閒置的戰鬥單位"));
	}
	else if (Key == EKeys::Tab)
	{
		TArray<ARSEntity*> L;
		for (ARSBuilding* B : Buildings)
		{
			if (!B || B->Player != Human || B->bDead || !B->Built()) continue;
			const FString& R = B->Def->Role;
			if (R == TEXT("cy") || R == TEXT("bar") || R == TEXT("fac") || R == TEXT("yard") || R == TEXT("dronehub") || B->Def->bAirfield) L.Add(B);
		}
		CycleSelect(L, TEXT("fac"), TEXT("還沒有生產建築"));
	}
	else if (Key == EKeys::B)
	{
		TArray<ARSEntity*> L;
		for (ARSBuilding* B : Buildings)
			if (B && B->Player == Human && !B->bDead && B->Built()) L.Add(B);
		CycleSelect(L, TEXT("bld"), TEXT("沒有建築"));
	}
	else if (Key == EKeys::Q)
	{
		const TArray<ARSEntity*> Sel = ValidSelection();
		if (Sel.Num() > 0)
		{
			const FString Tid = Sel[0]->TypeId;
			TArray<ARSEntity*> L;
			for (ARSUnit* U : Units)
				if (U && U->Player == Human && U->TypeId == Tid) L.Add(U);
			SelectList(L);
		}
	}
	else if (Key == EKeys::Z)
	{
		CmdMode = TEXT("sell");
	}
	else if (Key == EKeys::X)
	{
		CmdMode = TEXT("repair");
	}
	else if (Key == EKeys::P || Key == EKeys::Pause)
	{
		bPaused = !bPaused;
		Message(bPaused ? TEXT("遊戲暫停") : TEXT("遊戲繼續"));
	}
	else if (Key == EKeys::Equals || Key == EKeys::Add)
	{
		Speed = FMath::Min(3.f, Speed + 0.25f);
		Message(FString::Printf(TEXT("遊戲速度 x%.2f"), Speed));
	}
	else if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
	{
		Speed = FMath::Max(0.5f, Speed - 0.25f);
		Message(FString::Printf(TEXT("遊戲速度 x%.2f"), Speed));
	}
	else if (Key == EKeys::F)
	{
		bFormation = !bFormation;
		Message(bFormation ? TEXT("陣型移動：開啟（戰車在前、步兵居中、砲兵在後）") : TEXT("陣型移動：關閉（各自前往）"));
	}
	else if (Key == EKeys::F3)
	{
		bShowFps = !bShowFps;
	}
	else if (Key == EKeys::F5)
	{
		QuickSave();
	}
	else if (Key == EKeys::Delete)
	{
		for (ARSEntity* E : ValidSelection())
		{
			ARSBuilding* B = Cast<ARSBuilding>(E);
			if (B && B->Player == Human) Sell(B);
		}
	}
}

void ARSGame::CycleSelect(const TArray<ARSEntity*>& List, const FString& Key, const FString& EmptyMsg)
{
	if (List.Num() == 0)
	{
		Message(EmptyMsg);
		return;
	}
	int32& Idx = CycleIdx.FindOrAdd(Key, -1);
	Idx = (Idx + 1) % List.Num();
	ARSEntity* E = List[Idx];
	SelectList({ E });
	CenterOn(E->Pos);
	Audio()->Play(TEXT("select"), -8.f);
	Message(FString::Printf(TEXT("%s（%d / %d）"), *E->Def->Name, Idx + 1, List.Num()));
}

void ARSGame::BeginPlace(const FString& Id)
{
	PlaceId = Id;
	CmdMode.Empty();
}

FIntPoint ARSGame::PlaceTL() const
{
	FVector2D W2;
	if (!MouseWorld(W2)) return FIntPoint(-100, -100);
	const FIntPoint Sz = FRSDefs::Get().GetDef(PlaceId).Size;
	return FIntPoint(FMath::FloorToInt(W2.X - Sz.X * 0.5f + 0.5f), FMath::FloorToInt(W2.Y - Sz.Y * 0.5f + 0.5f));
}

void ARSGame::TryPlace()
{
	const FIntPoint TL = PlaceTL();
	if (CanPlace(PlaceId, TL, Human))
	{
		const FRSDef& Def = FRSDefs::Get().GetDef(PlaceId);
		PlaceBuilding(Human, PlaceId, TL);
		Human->Ready.Add(Def.Cat, FString());
		Human->Stats.Built++;
		Audio()->Play(TEXT("place"), -2.f);
		PlaceId.Empty();
	}
	else
	{
		Audio()->Play(TEXT("click"), 0.f);
		Message(TEXT("無法在此處放置"));
	}
}
