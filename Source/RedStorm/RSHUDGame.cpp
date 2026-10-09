// ARSHUD：遊戲中介面（建造欄、雷達、上方資訊、世界疊加、各種面板）
#include "RSHUD.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGlobals.h"
#include "RSGame.h"
#include "RSGameMode.h"
#include "RSPlayerController.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSAudio.h"
#include "RSMap.h"
#include "RSMission.h"
#include "RSWorldEvents.h"
#include "RSSaveLoad.h"
#include "RSCameraPawn.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

static const TCHAR* TABS[5] = { TEXT("building"), TEXT("defense"), TEXT("infantry"), TEXT("vehicle"), TEXT("naval") };
static const TCHAR* TAB_NAMES[5] = { TEXT("建築"), TEXT("防禦"), TEXT("步兵"), TEXT("載具"), TEXT("海軍") };

FLinearColor ARSHUD::HpColor(float R) const
{
	if (R > 0.6f) return FLinearColor(0.25f, 0.95f, 0.25f);
	if (R > 0.3f) return FLinearColor(1.f, 0.85f, 0.15f);
	return FLinearColor(1.f, 0.2f, 0.15f);
}

void ARSHUD::DrawGame()
{
	ARSGame* G = Game();
	if (!G || !G->Human) return;
	FpsFrames++;
	FpsT += GetWorld()->GetDeltaSeconds();
	if (FpsT >= 0.5f)
	{
		FpsShow = FpsFrames / FpsT;
		FpsT = 0.f;
		FpsFrames = 0;
	}
	if (G->bCinema && !G->bGameOver)
	{
		// 電影鏡頭：上下黑邊＋事件字幕，隱藏一般介面
		const float Bar = VS.Y * 0.1f;
		Rect(0, 0, VS.X, Bar, FLinearColor(0, 0, 0, 1));
		Rect(0, VS.Y - Bar, VS.X, Bar, FLinearColor(0, 0, 0, 1));
		Text(TEXT("● 電影鏡頭"), 24, Bar * 0.5f - 10, 16, FLinearColor(1.f, 0.3f, 0.25f));
		Text(TEXT("C / Esc / 右鍵：返回指揮畫面"), VS.X - 300, Bar * 0.5f - 9, 13, FLinearColor(0.7f, 0.7f, 0.7f));
		if (!G->CineLabel.IsEmpty()) Text(G->CineLabel, VS.X * 0.5f - G->CineLabel.Len() * 10.f, VS.Y - Bar * 0.5f - 12, 20, FLinearColor(1.f, 0.9f, 0.6f));
		return;
	}
	DrawWorldOverlay(G);
	DrawTop(G);
	// 側欄先處理點擊（在面板之前，面板開啟時會遮住）
	const bool bModal = IsModalOpen() || G->bHelpPanel;
	if (bModal)
	{
		const bool bC = bClick;
		bClick = false;
		DrawSidebar(G);
		bClick = bC;
	}
	else
	{
		DrawSidebar(G);
	}
	if (G->bGameOver) DrawEndPanel(G);
	else if (G->bSavePanel) DrawSavePanel(G);
	else if (G->bDiploPanel) DrawDiploPanel(G);
	else if (G->bPausePanel) DrawPausePanel(G);
	if (G->bHelpPanel) DrawHelpPanel();
}

// ───────────────────────── 世界疊加 ─────────────────────────
void ARSHUD::DrawHpBar(float CX, float Y, float W, float R, int32 Segs, const FLinearColor& Override)
{
	const float X0 = CX - W * 0.5f;
	Rect(X0 - 1, Y - 1, W + 2, 6, FLinearColor(0, 0, 0, 0.8f));
	const FLinearColor C = Override.R < 0.f ? HpColor(R) : Override;
	const float SWd = W / Segs;
	const int32 Filled = FMath::CeilToInt(R * Segs);
	for (int32 I = 0; I < Segs; ++I) Rect(X0 + I * SWd + 0.5f, Y, SWd - 1.f, 4, I < Filled ? C : FLinearColor(0.15f, 0.15f, 0.15f));
}

void ARSHUD::DrawUnitBox(ARSEntity* E, const FLinearColor& Col, bool bHp)
{
	ARSGame* G = Game();
	if (ARSBuilding* B = Cast<ARSBuilding>(E))
	{
		const float Z = B->GroundZ() + 2.f;
		const FVector2D C4[4] = { RS::Cell2(B->Cell), FVector2D(B->Cell.X + B->Size.X, B->Cell.Y), RS::Cell2(B->Cell + B->Size), FVector2D(B->Cell.X, B->Cell.Y + B->Size.Y) };
		FVector2D P[4];
		bool bOk = true;
		for (int32 K = 0; K < 4; ++K) bOk &= Project(FVector(C4[K].X * RS_CELL, C4[K].Y * RS_CELL, Z), P[K]);
		if (!bOk) return;
		for (int32 K = 0; K < 4; ++K) Line(P[K].X, P[K].Y, P[(K + 1) % 4].X, P[(K + 1) % 4].Y, RS::WithA(Col, 0.8f), 1.5f);
		FVector2D Top;
		if (bHp && Project(FVector(B->Pos.X * RS_CELL, B->Pos.Y * RS_CELL, Z + B->VisualTop + 10.f), Top))
		{
			const float W = FMath::Clamp(FVector2D::Distance(P[1], P[3]) * 0.8f, 40.f, 220.f);
			DrawHpBar(Top.X, Top.Y - 8, W, B->Hp / B->MaxHp, 5 * (B->Size.X + B->Size.Y));
		}
		return;
	}
	ARSUnit* U = Cast<ARSUnit>(E);
	if (!U) return;
	const FVector C = U->Center3D();
	FVector2D Sc, Tp, Rt;
	if (!Project(C, Sc) || !Project(C + FVector(0, 0, U->VisualTop * 0.6f + 6.f), Tp) || !G->Cam || !Project(C + G->Cam->RightDir() * U->PickRadius, Rt)) return;
	const float HW = FMath::Clamp(FVector2D::Distance(Sc, Rt), 8.f, 90.f);
	const float HH = FMath::Clamp(Sc.Y - Tp.Y, 8.f, 90.f);
	const float X0 = Sc.X - HW, X1 = Sc.X + HW, Y0 = Sc.Y - HH, Y1 = Sc.Y + HH * 0.7f;
	const float L = 6.f;
	Line(X0, Y0, X0 + L, Y0, Col, 1.5f); Line(X0, Y0, X0, Y0 + L, Col, 1.5f);
	Line(X1, Y0, X1 - L, Y0, Col, 1.5f); Line(X1, Y0, X1, Y0 + L, Col, 1.5f);
	Line(X0, Y1, X0 + L, Y1, Col, 1.5f); Line(X0, Y1, X0, Y1 - L, Col, 1.5f);
	Line(X1, Y1, X1 - L, Y1, Col, 1.5f); Line(X1, Y1, X1, Y1 - L, Col, 1.5f);
	if (bHp)
	{
		DrawHpBar(Sc.X, Y0 - 8, X1 - X0, U->Hp / U->MaxHp, U->UKind == TEXT("inf") ? 8 : 12);
		if (U->Def->bHarvester) DrawHpBar(Sc.X, Y0 - 15, X1 - X0, U->Cargo / ARSUnit::CARGO_MAX, 10, FLinearColor(1.f, 0.8f, 0.2f));
	}
}

void ARSHUD::DrawGroundQuad(ARSGame* G, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Col, float Z)
{
	FVector2D P[4];
	const FVector2D W4[4] = { A, B, C, D };
	for (int32 K = 0; K < 4; ++K)
	{
		if (!Project(G->ToWorld(W4[K], Z), P[K])) return;
	}
	Tri(P[0], P[1], P[2], Col);
	Tri(P[0], P[2], P[3], Col);
}

void ARSHUD::DrawGroundCircle(ARSGame* G, const FVector2D& C, float R, const FLinearColor& Col, float Th)
{
	const int32 Seg = 40;
	FVector2D Prev;
	bool bPrev = false;
	for (int32 I = 0; I <= Seg; ++I)
	{
		const float A = 2.f * PI * I / Seg;
		FVector2D P;
		const bool bOk = Project(G->ToWorld(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, 4.f), P);
		if (bOk && bPrev) Line(Prev.X, Prev.Y, P.X, P.Y, Col, Th);
		Prev = P;
		bPrev = bOk;
	}
}

void ARSHUD::DrawWorldOverlay(ARSGame* G)
{
	URSPlayer* Human = G->Human;
	// 指令標記
	for (const FRSMarker& Mk : G->Markers)
	{
		const float F = Mk.T / 0.8f;
		DrawGroundCircle(G, Mk.P, 0.15f + 0.35f * (1.f - F), RS::WithA(Mk.C, 1.f - F), 2.f);
	}
	// 軍階、特殊狀態圖示
	for (ARSUnit* U : G->Units)
	{
		if (!U || U->bDead || !U->bShown || U->Rank <= 0) continue;
		FVector2D P;
		if (!Project(U->Center3D() + FVector(0, 0, U->VisualTop * 0.6f + 12.f), P)) continue;
		for (int32 K = 0; K < U->Rank; ++K)
		{
			const float CX = P.X + 14.f + K * 7.f;
			Line(CX - 3, P.Y, CX, P.Y + 3, FLinearColor(1.f, 0.85f, 0.2f), 2.f);
			Line(CX, P.Y + 3, CX + 3, P.Y, FLinearColor(1.f, 0.85f, 0.2f), 2.f);
		}
	}
	for (ARSBuilding* B : G->Buildings)
	{
		if (!B || B->bDead || !B->bShown) continue;
		const bool bLowIcon = B->HasWeapon() && B->Weapon->bPower && B->Player->bLowPower && B->Built();
		const bool bGarr = B->Def->Garrison > 0 && (B->Occupants.Num() > 0 || B->bSelected);
		if (!bLowIcon && !B->bRepairing && !bGarr) continue;
		FVector2D P;
		if (!Project(FVector(B->Pos.X * RS_CELL, B->Pos.Y * RS_CELL, B->GroundZ() + B->VisualTop + 18.f), P)) continue;
		if (bLowIcon)
		{
			const float A = 0.5f + 0.5f * FMath::Sin(T * 6.f);
			Line(P.X + 3, P.Y - 10, P.X - 3, P.Y, FLinearColor(1.f, 0.9f, 0.2f, A), 3.f);
			Line(P.X - 3, P.Y, P.X + 2, P.Y, FLinearColor(1.f, 0.9f, 0.2f, A), 3.f);
			Line(P.X + 2, P.Y, P.X - 2, P.Y + 10, FLinearColor(1.f, 0.9f, 0.2f, A), 3.f);
		}
		if (B->bRepairing)
		{
			const float A = 0.6f + 0.4f * FMath::Sin(T * 5.f);
			Circle(FVector2D(P.X, P.Y - 16), 9, FLinearColor(0.1f, 0.4f, 0.1f, 0.7f * A), 16);
			Rect(P.X - 6, P.Y - 18, 12, 4, FLinearColor(0.5f, 1.f, 0.5f, A));
			Rect(P.X - 2, P.Y - 22, 4, 12, FLinearColor(0.5f, 1.f, 0.5f, A));
		}
		if (bGarr)
		{
			const int32 N = B->Def->Garrison;
			for (int32 K = 0; K < N; ++K)
			{
				const float X = P.X + (K - (N - 1) * 0.5f) * 9.f;
				Rect(X - 3, P.Y + 4, 7, 7, FLinearColor(0, 0, 0, 0.7f));
				Rect(X - 2, P.Y + 5, 5, 5, K < B->Occupants.Num() ? RS::Lighten(B->Player->Color, 0.3f) : FLinearColor(0.3f, 0.3f, 0.3f));
			}
		}
	}
	// 選取
	for (ARSEntity* E : G->ValidSelection())
	{
		const FLinearColor Col = E->Player == Human ? FLinearColor::White : FLinearColor(1.f, 0.35f, 0.3f);
		DrawUnitBox(E, Col, true);
		if (ARSUnit* U = Cast<ARSUnit>(E))
		{
			if (U->Player == Human && U->Waypoints.Num() > 0)
			{
				FVector2D Prev;
				if (!Project(G->ToWorld(U->MoveDest, 4.f), Prev)) continue;
				for (const FRSWaypoint& Wp : U->Waypoints)
				{
					FVector2D Np;
					if (!Project(G->ToWorld(Wp.P, 4.f), Np)) break;
					Line(Prev.X, Prev.Y, Np.X, Np.Y, FLinearColor(0.3f, 1.f, 0.8f, 0.6f), 1.5f);
					Circle(Np, 3, FLinearColor(0.3f, 1.f, 0.8f, 0.8f), 8);
					Prev = Np;
				}
			}
		}
		else if (ARSBuilding* B = Cast<ARSBuilding>(E))
		{
			if (B->Player == Human && B->Rally.X >= 0)
			{
				FVector2D A, Bp;
				if (Project(G->ToWorld(B->Pos, 20.f), A) && Project(G->ToWorld(B->Rally, 4.f), Bp))
				{
					Line(A.X, A.Y, Bp.X, Bp.Y, FLinearColor(0.4f, 1.f, 0.4f, 0.7f), 1.5f);
					Circle(Bp, 4, FLinearColor(0.4f, 1.f, 0.4f, 0.8f), 10);
				}
			}
		}
	}
	// 滑鼠停留
	ARSEntity* H = G->Hover.Get();
	if (H && !H->bDead && !H->bSelected)
	{
		FLinearColor Col = H->Player == Human ? FLinearColor(1, 1, 1, 0.5f) : FLinearColor(1.f, 0.4f, 0.3f, 0.6f);
		if (H->Player->bNeutral) Col = FLinearColor(0.8f, 0.8f, 0.8f, 0.6f);
		DrawUnitBox(H, Col, true);
		FString Nm = H->Def->Name;
		if (H->Player != Human && !H->Player->bNeutral) Nm = FString::Printf(TEXT("%s (%s)"), *Nm, *H->Player->Name);
		FVector2D P;
		if (Project(H->Center3D() - FVector(0, 0, H->VisualTop * 0.5f), P)) Text(Nm, P.X - 100, P.Y + 10, 13, FLinearColor(1, 1, 1, 0.95f), false, 1, 200);
	}
	// 框選
	if (G->bDragging && PC())
	{
		const FVector2D A = G->DragStart / S;
		const FVector2D B = M;
		if (FVector2D::Distance(A, B) > 8.f / S)
		{
			const float X0 = FMath::Min(A.X, B.X), Y0 = FMath::Min(A.Y, B.Y);
			Rect(X0, Y0, FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FLinearColor(0.3f, 1.f, 0.3f, 0.12f));
			Frame(X0, Y0, FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FLinearColor(0.4f, 1.f, 0.4f, 0.9f), 1.5f);
		}
	}
	// 放置預覽
	if (!G->PlaceId.IsEmpty())
	{
		const FString& Id = G->PlaceId;
		const FIntPoint TL = G->PlaceTL();
		const FRSDef& Def = FRSDefs::Get().GetDef(Id);
		const bool bOk = G->CanPlace(Id, TL, Human);
		for (ARSBuilding* B : G->Buildings)
		{
			if (!B || B->Player != Human || B->Def->bWall) continue;
			const int32 Gp = ARSGame::BUILD_GAP;
			const FVector2D C0 = RS::Cell2(B->Cell - FIntPoint(Gp, Gp));
			const FVector2D C1 = RS::Cell2(B->Cell + B->Size + FIntPoint(Gp, Gp));
			FVector2D P[4];
			const FVector2D W4[4] = { C0, FVector2D(C1.X, C0.Y), C1, FVector2D(C0.X, C1.Y) };
			bool bP = true;
			for (int32 K = 0; K < 4; ++K) bP &= Project(FVector(W4[K].X * RS_CELL, W4[K].Y * RS_CELL, 3.f), P[K]);
			if (bP)
				for (int32 K = 0; K < 4; ++K) Line(P[K].X, P[K].Y, P[(K + 1) % 4].X, P[(K + 1) % 4].Y, FLinearColor(1, 1, 1, 0.12f));
		}
		for (int32 Y = TL.Y; Y < TL.Y + Def.Size.Y; ++Y)
			for (int32 X = TL.X; X < TL.X + Def.Size.X; ++X)
			{
				const bool bCell = G->CellBuildable(Id, X, Y);
				const FLinearColor C = (bCell && bOk) ? FLinearColor(0.2f, 1.f, 0.3f, 0.35f) : FLinearColor(1.f, 0.2f, 0.15f, 0.4f);
				DrawGroundQuad(G, FVector2D(X, Y), FVector2D(X + 1, Y), FVector2D(X + 1, Y + 1), FVector2D(X, Y + 1), C, 4.f);
			}
	}
	// 超級武器瞄準
	FVector2D MW;
	const bool bMW = G->MouseWorld(MW);
	if (G->CmdMode.StartsWith(TEXT("power:")) && bMW)
	{
		const FString Kind = G->CmdMode.Mid(6);
		const float R = FMath::Max(0.8f, FRSDefs::Get().GetSuper(Kind).Radius);
		DrawGroundCircle(G, MW, R, FLinearColor(1.f, 0.2f, 0.1f, 0.8f), 2.f);
		DrawGroundCircle(G, MW, R * (0.5f + 0.5f * FMath::Fmod(T, 1.f)), FLinearColor(1.f, 0.4f, 0.2f, 0.5f), 1.5f);
		Line(M.X - 14, M.Y, M.X + 14, M.Y, FLinearColor(1.f, 0.3f, 0.2f), 2.f);
		Line(M.X, M.Y - 10, M.X, M.Y + 10, FLinearColor(1.f, 0.3f, 0.2f), 2.f);
	}
	// 游標提示
	FString Hint;
	FLinearColor HC = FLinearColor::White;
	if (G->CmdMode == TEXT("amove")) { Hint = TEXT("攻擊移動"); HC = FLinearColor(1.f, 0.4f, 0.3f); }
	else if (G->CmdMode == TEXT("sell")) { Hint = TEXT("$ 出售"); HC = FLinearColor(0.4f, 1.f, 0.4f); }
	else if (G->CmdMode == TEXT("repair")) { Hint = TEXT("修理"); HC = FLinearColor(0.5f, 0.8f, 1.f); }
	else if (G->CmdMode.StartsWith(TEXT("power:"))) { Hint = FRSDefs::Get().GetSuper(G->CmdMode.Mid(6)).Name; HC = FLinearColor(1.f, 0.4f, 0.2f); }
	const TArray<ARSUnit*> Own = G->OwnSelectedUnits();
	if (Hint.IsEmpty() && G->PlaceId.IsEmpty() && H && Own.Num() > 0)
	{
		ARSBuilding* HB = Cast<ARSBuilding>(H);
		if (H->Player != Human && !H->Player->bNeutral)
		{
			Hint = TEXT("攻擊");
			HC = FLinearColor(1.f, 0.3f, 0.25f);
			for (ARSUnit* U : Own)
			{
				if (U->Def->bEngineer && HB) { Hint = TEXT("佔領"); HC = FLinearColor(1.f, 0.85f, 0.3f); }
				else if (U->Def->bSpy && HB) { Hint = TEXT("滲透"); HC = FLinearColor(1.f, 0.95f, 0.4f); }
			}
		}
		else if (HB && HB->Def->Garrison > 0 && (HB->Player->bNeutral || HB->Player == Human))
		{
			for (ARSUnit* U : Own)
				if (G->CanGarrison(U, HB)) { Hint = TEXT("進駐"); HC = FLinearColor(0.4f, 0.85f, 1.f); }
		}
		else if (HB && H->Player->bNeutral)
		{
			for (ARSUnit* U : Own)
				if (U->Def->bEngineer) { Hint = TEXT("佔領"); HC = FLinearColor(1.f, 0.85f, 0.3f); }
		}
	}
	if (!Hint.IsEmpty()) Text(Hint, M.X + 16, M.Y + 20, 14, HC, true);
}

// ───────────────────────── 上方資訊 ─────────────────────────
void ARSHUD::DrawTop(ARSGame* G)
{
	URSPlayer* Human = G->Human;
	const FRSDefs& D = FRSDefs::Get();
	FString Info = RS::MMSS(G->Time);
	if (!Human->CountryName().IsEmpty()) Info += TEXT("  ") + Human->CountryName();
	if (G->Speed != 1.f) Info += FString::Printf(TEXT("  x%.2f"), G->Speed);
	if (G->bPaused) Info += TEXT("  [暫停]");
	Info += TEXT("  ") + G->EnvLabel();
	Rect(8, 6, 330, 24, FLinearColor(0.015f, 0.03f, 0.045f, 0.72f));
	Rect(8, 6, 3, 24, Accent);
	Rect(8, 30, 330, 1, RS::WithA(Accent, 0.5f));
	Text(Info, 16, 8, 15, FLinearColor(0.9f, 0.9f, 0.85f), true);
	Text(TEXT("F1 操作說明   T 外交   C 電影鏡頭"), 350, 10, 12, FLinearColor(0.7f, 0.7f, 0.7f));
	if (Human->Proposal.Num() > 0 && int32(T * 2.f) % 2 == 0) Text(TEXT("✉ 外交提議"), 420, 9, 13, FLinearColor(1.f, 0.85f, 0.3f), true);
	if (G->bShowFps) Text(FString::Printf(TEXT("FPS %d  單位 %d  地圖 %dx%d"), int32(FpsShow), G->Units.Num(), G->W, G->H), VS.X - SW - 300, 8, 13, FLinearColor(0.6f, 1.f, 0.6f), true);
	// 敵方超級武器倒數
	float PX = (VS.X - SW) * 0.5f - 170.f, PY = 8.f;
	int32 NEx = 0;
	static const TSet<FString> Shown = { TEXT("nuke"), TEXT("storm"), TEXT("barrage"), TEXT("strait"), TEXT("blockade"), TEXT("armor"), TEXT("paradrop"), TEXT("militia"), TEXT("df") };
	for (URSPlayer* Pl : G->Players)
	{
		if (Pl == Human || Pl->bNeutral || !Pl->bAlive || !Human->IsEnemyOf(Pl)) continue;
		for (const FString& K : Pl->PowerList())
		{
			if (!Shown.Contains(K)) continue;
			const float C = Pl->Powers[K];
			const FRSCountry* Co = D.GetCountry(Pl->Country);
			const float* Cm = Co ? Co->Charge.Find(K) : nullptr;
			const int32 Rem = int32((1.f - C) * D.GetSuper(K).Charge / (Cm ? *Cm : 1.f));
			const bool bBig = K == TEXT("nuke") || K == TEXT("storm") || K == TEXT("barrage");
			Rect(PX, PY, 340, 22, bBig ? FLinearColor(0.25f, 0.02f, 0.02f, 0.72f) : FLinearColor(0.05f, 0.05f, 0.08f, 0.7f));
			Rect(PX, PY, 340 * C, 22, RS::WithA(Pl->Color, 0.35f));
			Frame(PX, PY, 340, 22, RS::WithA(Pl->Color, 0.8f), 1.f);
			FString Txt = FString::Printf(TEXT("%s  %s  %s"), *Pl->Name, *D.GetSuper(K).Name, *RS::MMSS(Rem));
			if (C >= 1.f)
			{
				Txt = FString::Printf(TEXT("%s  %s  ‼ 就緒"), *Pl->Name, *D.GetSuper(K).Name);
				if (int32(T * 4.f) % 2 == 0) Rect(PX, PY, 340, 22, FLinearColor(1.f, 0.2f, 0.1f, 0.35f));
			}
			Text(Txt, PX + 8, PY + 2, 14, bBig ? FLinearColor(1.f, 0.9f, 0.85f) : FLinearColor(0.9f, 0.9f, 0.9f), true);
			PY += 25.f;
			NEx++;
		}
	}
	if (NEx > 0) Text(TEXT("敵方超級武器 / 特殊能力倒數"), PX, PY, 11, FLinearColor(1.f, 0.6f, 0.5f, 0.8f));
	// RSN 快訊
	if (G->WorldEv && G->WorldEv->BannerT > 0.f)
	{
		const float BW = 760.f;
		const float BX = (VS.X - SW) * 0.5f - BW * 0.5f, BY = VS.Y - 150.f;
		const float Fade = FMath::Clamp(G->WorldEv->BannerT / 1.5f, 0.f, 1.f);
		Rect(BX, BY, BW, 34, FLinearColor(0.04f, 0.04f, 0.07f, 0.9f * Fade));
		Rect(BX, BY, 64, 34, FLinearColor(0.75f, 0.08f, 0.06f, Fade));
		Text(TEXT("RSN"), BX, BY + 6, 17, FLinearColor(1, 1, 1, Fade), true, 1, 64);
		Rect(BX, BY, BW, 2, FLinearColor(0.75f, 0.08f, 0.06f, Fade));
		Text(G->WorldEv->Banner, BX + 76, BY + 7, 15, FLinearColor(1.f, 0.95f, 0.85f, Fade), true);
	}
	if (G->WorldEv)
	{
		float LY = 36.f;
		for (const FString& L : G->WorldEv->StatusLines())
		{
			Text(TEXT("◆ ") + L, VS.X - SW - 270, LY, 12, FLinearColor(1.f, 0.8f, 0.5f, 0.9f));
			LY += 16.f;
		}
	}
	float Y = 40.f;
	if (!G->Objective.IsEmpty())
	{
		Rect(8, 34, FMath::Max(330.f, TextW(G->Objective, 15, true) + 20), 24, FLinearColor(0.3f, 0.05f, 0.02f, 0.6f));
		Text(G->Objective, 16, 36, 15, FLinearColor(1.f, 0.85f, 0.4f), true);
		Y = 66.f;
	}
	for (const FRSMessage& Mg : G->Msgs)
	{
		const float A = FMath::Clamp(7.f - Mg.T, 0.f, 1.f);
		Text(Mg.Text, 16, Y, 16, FLinearColor(0.55f, 1.f, 0.55f, A), true);
		Y += 22.f;
	}
	// 選取資訊
	const TArray<ARSEntity*> Sel = G->ValidSelection();
	if (Sel.Num() > 0)
	{
		ARSEntity* E = Sel[0];
		const float PW = 330.f, PH = 64.f;
		const float RX = 8.f, RY = VS.Y - PH - 30.f;
		Rect(RX, RY, PW, PH, FLinearColor(0.05f, 0.06f, 0.07f, 0.8f));
		Frame(RX, RY, PW, PH, RS::Darken(Accent, 0.2f), 1.5f);
		FString Title = E->Def->Name;
		if (Sel.Num() > 1) Title = FString::Printf(TEXT("已選取 %d 個單位"), Sel.Num());
		else if (E->Player != Human) Title += FString::Printf(TEXT("  (%s)"), *E->Player->Name);
		Text(Title, RX + 10, RY + 6, 16, FLinearColor(1.f, 0.9f, 0.5f), true);
		if (Sel.Num() == 1)
		{
			const float Hr = E->Hp / E->MaxHp;
			Rect(RX + 10, RY + 32, 200, 8, FLinearColor(0.15f, 0.15f, 0.15f));
			Rect(RX + 10, RY + 32, 200 * Hr, 8, HpColor(Hr));
			FString Extra = FString::Printf(TEXT("%d / %d"), int32(E->Hp), int32(E->MaxHp));
			if (ARSUnit* U = Cast<ARSUnit>(E))
			{
				if (U->Rank > 0) Extra += U->Rank == 1 ? TEXT("   老兵 ★") : TEXT("   精英 ★★");
				if (U->Def->bHarvester) Extra += FString::Printf(TEXT("   載礦 %d%%"), int32(U->Cargo / ARSUnit::CARGO_MAX * 100));
			}
			else if (E->Def->Power != 0.f)
			{
				Extra += FString::Printf(TEXT("   電力 %+d"), int32(E->Def->Power));
			}
			Text(Extra, RX + 10, RY + 44, 13, FLinearColor(0.85f, 0.85f, 0.85f));
		}
		else
		{
			TMap<FString, int32> Counts;
			TArray<FString> Order;
			for (ARSEntity* Se : Sel)
			{
				if (!Counts.Contains(Se->Def->Name)) Order.Add(Se->Def->Name);
				Counts.FindOrAdd(Se->Def->Name)++;
			}
			TArray<FString> Parts;
			for (const FString& K : Order) Parts.Add(FString::Printf(TEXT("%s x%d"), *K, Counts[K]));
			TextWrap(FString::Join(Parts, TEXT("、")), RX + 10, RY + 32, PW - 20, 13, FLinearColor(0.85f, 0.85f, 0.85f), 16.f);
		}
	}
	// 底部提示列
	Rect(0, VS.Y - 22, VS.X - SW, 22, FLinearColor(0, 0, 0, 0.55f));
	Text(TEXT("左鍵:選取/框選　右鍵:移動/攻擊　A:攻擊移動　S:停止　D:展開基地車　Ctrl+數字:編隊　H:回基地　N:閒置礦車　I:閒置單位　Tab:生產建築　F:陣型　滾輪:縮放"), 10, VS.Y - 19, 12, FLinearColor(0.75f, 0.75f, 0.72f));
	if (!G->PlaceId.IsEmpty())
	{
		Text(FString::Printf(TEXT("放置 %s：左鍵確定，右鍵取消"), *D.GetDef(G->PlaceId).Name), 0, 80, 18, FLinearColor(1.f, 0.9f, 0.4f), true, 1, VS.X - SW);
	}
}

// ───────────────────────── 雷達 / 小地圖 ─────────────────────────
float ARSHUD::MMK(ARSGame* G) const
{
	return FMath::Min((SW - 32.f) / float(G->W + G->H), (MMH - 8.f) / float(G->W + G->H) * 2.f);
}

FVector2D ARSHUD::MMOrigin(ARSGame* G) const
{
	const float RX = VS.X - SW + 16, RY = 46, RW = SW - 32, RH = MMH;
	const float K = MMK(G);
	return FVector2D(RX + RW * 0.5f, RY + (RH - (G->W + G->H) * K * 0.5f) * 0.5f);
}

FVector2D ARSHUD::WorldToMM(ARSGame* G, const FVector2D& P) const
{
	const float K = MMK(G);
	return MMOrigin(G) + FVector2D((P.X - P.Y) * K, (P.X + P.Y) * K * 0.5f);
}

FVector2D ARSHUD::MMToWorld(ARSGame* G, const FVector2D& P) const
{
	const float K = MMK(G);
	const FVector2D Dd = P - MMOrigin(G);
	const float A = Dd.X / K, B = Dd.Y / (K * 0.5f);
	return FVector2D((A + B) * 0.5f, (B - A) * 0.5f);
}

void ARSHUD::UpdateMinimap(ARSGame* G)
{
	const int32 W = G->W, H = G->H;
	if (MiniBase.Num() != W * H) MiniBase = G->Map->MakePreview(false);
	if (!MiniTex || MiniTex->GetSizeX() != W)
	{
		MiniTex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!MiniTex) return;
		MiniTex->Filter = TF_Nearest;
		MiniTex->SRGB = true;
		MiniTex->UpdateResource();
	}
	TArray<FColor> Px = MiniBase;
	const FRSMap& Mp = *G->Map;
	for (int32 I : Mp.OreCells) Px[I] = Mp.Gem[I] == 0 ? FColor(230, 184, 38) : FColor(191, 89, 242);
	for (ARSBuilding* B : G->Buildings)
	{
		if (!B || B->bDead || !B->bShown) continue;
		const FColor C = B->Player->Color.ToFColor(true);
		for (int32 Y = B->Cell.Y; Y < B->Cell.Y + B->Size.Y; ++Y)
			for (int32 X = B->Cell.X; X < B->Cell.X + B->Size.X; ++X)
				if (Mp.Inb(X, Y)) Px[Mp.Idx(X, Y)] = C;
	}
	for (ARSUnit* U : G->Units)
	{
		if (!U || U->bDead || !U->bShown) continue;
		const int32 X = FMath::Clamp(int32(U->Pos.X), 0, W - 1), Y = FMath::Clamp(int32(U->Pos.Y), 0, H - 1);
		Px[Y * W + X] = RS::Lighten(U->Player->Color, 0.35f).ToFColor(true);
	}
	for (int32 I = 0; I < W * H; ++I)
		if (G->Explored[I] == 0) Px[I] = FColor(0, 0, 0, 255);
	uint8* Data = new uint8[W * H * 4];
	FMemory::Memcpy(Data, Px.GetData(), W * H * 4);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, W, H);
	MiniTex->UpdateTextureRegions(0, 1, Region, W * 4, 4, Data, [](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
	{
		delete[] SrcData;
		delete Regions;
	});
}

void ARSHUD::DrawMinimap(ARSGame* G, float X, float Y, float W, float H)
{
	URSPlayer* Human = G->Human;
	const bool bHacked = G->Time < Human->RadarJamUntil;
	const bool bRadar = Human->HasRole(TEXT("radar")) && !Human->bLowPower && !bHacked;
	Bevel(X - 3, Y - 3, W + 6, H + 6, FLinearColor(0.02f, 0.03f, 0.02f), true);
	if (!bRadar)
	{
		FRandomStream R(int32(T * 12.f));
		for (int32 K = 0; K < 140; ++K)
		{
			const float V = R.FRand() * 0.25f;
			Rect(X + R.FRand() * W, Y + R.FRand() * H, R.FRandRange(2.f, 8.f), 1, FLinearColor(V, V * 1.1f, V));
		}
		const bool bHas = Human->HasRole(TEXT("radar"));
		if (bHacked)
		{
			Text(TEXT("雷達遭駭入"), X, Y + H * 0.5f - 14, 18, FLinearColor(1.f, 0.35f, 0.25f, 0.6f + 0.3f * FMath::Sin(T * 8.f)), true, 1, W);
			Text(FString::Printf(TEXT("恢復倒數 %d 秒"), int32(Human->RadarJamUntil - G->Time)), X, Y + H * 0.5f + 10, 12, FLinearColor(1.f, 0.6f, 0.5f, 0.7f), false, 1, W);
			return;
		}
		Text(bHas ? TEXT("電力不足") : TEXT("雷達離線"), X, Y + H * 0.5f - 14, 18, FLinearColor(0.6f, 0.8f, 0.6f, 0.7f), true, 1, W);
		if (!bHas) Text(TEXT("(建造雷達站以開啟小地圖)"), X, Y + H * 0.5f + 10, 12, FLinearColor(0.6f, 0.7f, 0.6f, 0.6f), false, 1, W);
		return;
	}
	MiniT -= GetWorld()->GetDeltaSeconds();
	if (MiniT <= 0.f || !MiniTex)
	{
		MiniT = 0.4f;
		UpdateMinimap(G);
	}
	const float K = MMK(G);
	const FVector2D O = MMOrigin(G);
	DrawDiamondTex(MiniTex, G->W, G->H, FVector2D(O.X, O.Y + (G->W + G->H) * K * 0.25f), K);
	// 鏡頭視野
	APlayerController* P = GetOwningPlayerController();
	if (P)
	{
		const FVector2D Corners[4] = { FVector2D(0, 0), FVector2D((VS.X - SW) * S, 0), FVector2D((VS.X - SW) * S, VS.Y * S), FVector2D(0, VS.Y * S) };
		FVector2D MP[4];
		bool bOk = true;
		for (int32 I = 0; I < 4; ++I)
		{
			FVector2D Wc;
			if (!G->ScreenToCell(Corners[I], Wc)) { bOk = false; break; }
			MP[I] = WorldToMM(G, Wc);
			MP[I].X = FMath::Clamp(MP[I].X, X, X + W);
			MP[I].Y = FMath::Clamp(MP[I].Y, Y, Y + H);
		}
		if (bOk)
			for (int32 I = 0; I < 4; ++I) Line(MP[I].X, MP[I].Y, MP[(I + 1) % 4].X, MP[(I + 1) % 4].Y, FLinearColor(1, 1, 1, 0.8f), 1.f);
	}
	const FVector2D CC(X + W * 0.5f, Y + H * 0.5f);
	const float SA = T * 1.6f;
	for (int32 I = 0; I < 12; ++I)
	{
		const FVector2D Dd = RS::FromAngle(SA - I * 0.05f) * 100.f;
		Line(CC.X, CC.Y, CC.X + Dd.X, CC.Y + Dd.Y * 0.6f, RS::WithA(Accent, 0.22f - I * 0.018f), 2.f);
	}
	for (float SY = Y; SY < Y + H; SY += 3.f) Rect(X, SY, W, 1, FLinearColor(0, 0, 0, 0.12f));
	for (const FRSPing& Pg : G->Pings)
	{
		const FVector2D PP = WorldToMM(G, Pg.P);
		Ring(PP, 4.f + FMath::Fmod(Pg.T * 20.f, 14.f), FLinearColor(1.f, 0.3f, 0.2f, 1.f - Pg.T / 3.f), 1.5f, 20);
	}
	// 雷達互動
	ARSPlayerController* RP = PC();
	if (Hovered(X, Y, W, H) && RP)
	{
		if (bClick)
		{
			bClick = false;
			bMMDrag = true;
		}
		if (bRClick)
		{
			bRClick = false;
			const TArray<ARSUnit*> List = G->OwnSelectedUnits();
			if (List.Num() > 0)
			{
				G->CommandMove(List, MMToWorld(G, M));
				G->Audio()->Play(TEXT("radio"), -10.f);
			}
		}
	}
	if (bMMDrag && RP)
	{
		if (!RP->bLeftDown) bMMDrag = false;
		else G->CenterOn(MMToWorld(G, FVector2D(FMath::Clamp(M.X, X, X + W), FMath::Clamp(M.Y, Y, Y + H))));
	}
}

// ───────────────────────── 建造欄 ─────────────────────────
bool ARSHUD::TabEnabled(ARSGame* G, int32 I) const
{
	URSPlayer* H = G->Human;
	switch (I)
	{
	case 0: case 1: return H->HasRole(TEXT("cy"));
	case 2: return H->HasRole(TEXT("bar"));
	case 3: return H->HasRole(TEXT("fac")) || H->HasRole(TEXT("airfield"));
	case 4: return H->HasRole(TEXT("yard"));
	}
	return false;
}

TArray<FString> ARSHUD::TabItems(ARSGame* G, int32 I) const
{
	TArray<FString> Out;
	if (const TArray<FString>* L = FRSDefs::Get().BuildOrder.Find(TABS[I]))
	{
		for (const FString& Id : *L)
			if (G->Human->Available(Id)) Out.Add(Id);
	}
	return Out;
}

static FVector2D RectRay(const FVector2D& C, float A, float X0, float Y0, float X1, float Y1)
{
	const FVector2D Dd(FMath::Cos(A), FMath::Sin(A));
	float Tm = 1e9f;
	if (Dd.X > 1e-5f) Tm = FMath::Min(Tm, (X1 - C.X) / Dd.X);
	if (Dd.X < -1e-5f) Tm = FMath::Min(Tm, (X0 - C.X) / Dd.X);
	if (Dd.Y > 1e-5f) Tm = FMath::Min(Tm, (Y1 - C.Y) / Dd.Y);
	if (Dd.Y < -1e-5f) Tm = FMath::Min(Tm, (Y0 - C.Y) / Dd.Y);
	return C + Dd * Tm;
}

void ARSHUD::DrawCameo(ARSGame* G, float X, float Y, const FString& Id)
{
	URSPlayer* Human = G->Human;
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const bool bOk = Human->CanBuild(Id);
	const FString Missing = Human->MissingReq(Id);
	const FString Cat = URSPlayer::QueueCat(Def);
	const bool bReady = Human->GetReady(Cat) == Id;
	for (int32 I = 0; I < 8; ++I)
	{
		const FLinearColor C = Def.Faction == TEXT("soviet") ? FMath::Lerp(FLinearColor(0.3f, 0.2f, 0.18f), FLinearColor(0.08f, 0.06f, 0.06f), I / 8.f)
			: FMath::Lerp(FLinearColor(0.2f, 0.25f, 0.3f), FLinearColor(0.06f, 0.07f, 0.09f), I / 8.f);
		Rect(X, Y + CH * I / 8.f, CW, CH / 8.f + 1, C);
	}
	if (UTexture2D* Cm = Cameo(Id))
	{
		const float DW = CW - 4, DH = DW * 200.f / 272.f;
		Tex(Cm, X + 2, Y, DW, DH);
		if (UTexture2D* Tm = Cameo(Id + TEXT("_team"))) Tex(Tm, X + 2, Y, DW, DH, Human->Color);
	}
	else
	{
		Text(Def.Name.Left(2), X, Y + 20, 30, RS::WithA(Human->Color, 0.6f), true, 1, CW);
	}
	Rect(X, Y + CH - 18, CW, 18, FLinearColor(0, 0, 0, 0.62f));
	Text(Def.Name, X + 4, Y + CH - 17, 13, FLinearColor(0.95f, 0.95f, 0.9f));
	Rect(X + CW - 52, Y, 52, 16, FLinearColor(0, 0, 0, 0.45f));
	Text(FString::Printf(TEXT("$%d"), int32(Human->CostOf(Id))), X, Y, 12, FLinearColor(1.f, 0.85f, 0.3f), true, 2, CW - 4);
	const float Prog = Human->ProgressOf(Id);
	if (!bOk && !bReady && Prog < 0.f)
	{
		Rect(X, Y, CW, CH, FLinearColor(0, 0, 0, 0.62f));
		if (!Missing.IsEmpty()) Text(TEXT("需要") + Missing, X, Y + CH * 0.5f - 8, 12, FLinearColor(1.f, 0.5f, 0.4f), false, 1, CW);
		else if (Def.Limit > 0 && Human->CountType(Id) >= Def.Limit) Text(TEXT("已達上限"), X, Y + CH * 0.5f - 8, 12, FLinearColor(1.f, 0.7f, 0.4f), false, 1, CW);
	}
	if (Prog >= 0.f)
	{
		// 時鐘式進度遮罩
		const FVector2D C(X + CW * 0.5f, Y + CH * 0.5f);
		const float A0 = -PI * 0.5f + 2.f * PI * Prog, A1 = PI * 1.5f;
		TArray<float> Angs;
		const int32 Steps = 32;
		for (int32 K = 0; K <= Steps; ++K) Angs.Add(FMath::Lerp(A0, A1, float(K) / Steps));
		const FVector2D Corners[4] = { FVector2D(X, Y), FVector2D(X + CW, Y), FVector2D(X + CW, Y + CH), FVector2D(X, Y + CH) };
		for (const FVector2D& Cr : Corners)
		{
			float A = FMath::Atan2(Cr.Y - C.Y, Cr.X - C.X);
			while (A < A0) A += 2.f * PI;
			if (A <= A1) Angs.Add(A);
		}
		Angs.Sort();
		for (int32 K = 0; K + 1 < Angs.Num(); ++K)
		{
			Tri(C, RectRay(C, Angs[K], X, Y, X + CW, Y + CH), RectRay(C, Angs[K + 1], X, Y, X + CW, Y + CH), FLinearColor(0, 0, 0, 0.55f));
		}
		Text(FString::Printf(TEXT("%d%%"), int32(Prog * 100)), X, Y + CH * 0.5f - 10, 14, FLinearColor::White, true, 1, CW);
	}
	if (bReady)
	{
		const float A = 0.6f + 0.4f * FMath::Sin(T * 8.f);
		Rect(X, Y, CW, CH, FLinearColor(1.f, 0.85f, 0.2f, 0.18f * A));
		Text(TEXT("就緒"), X, Y + CH * 0.5f - 12, 18, FLinearColor(1.f, 0.9f, 0.3f, A), true, 1, CW);
	}
	const int32 QN = Human->QueuedCount(Id);
	if (QN > 1) Text(FString::Printf(TEXT("x%d"), QN), X + 4, Y + 2, 13, FLinearColor(0.6f, 1.f, 0.6f), true);
	Frame(X, Y, CW, CH, HoverItem == Id ? FLinearColor(1, 1, 1, 0.7f) : FLinearColor(1, 1, 1, 0.1f), 1.f);
}

void ARSHUD::DrawTooltip(ARSGame* G, const FString& Id, float Y)
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	URSPlayer* Human = G->Human;
	TArray<TPair<FString, FLinearColor>> Lines;
	Lines.Add({ Def.Name, FLinearColor(1.f, 0.85f, 0.3f) });
	Lines.Add({ FString::Printf(TEXT("造價 $%d"), int32(Human->CostOf(Id))), FLinearColor(0.88f, 0.88f, 0.85f) });
	if (Def.Power != 0.f) Lines.Add({ FString::Printf(TEXT("電力 %+d"), int32(Def.Power)), FLinearColor(0.88f, 0.88f, 0.85f) });
	if (Def.bIsUnit) Lines.Add({ FString::Printf(TEXT("生命 %d   速度 %.1f"), int32(Def.Hp), Def.Speed), FLinearColor(0.88f, 0.88f, 0.85f) });
	const FString Miss = Human->MissingReq(Id);
	const float W = 240.f;
	const float DescH = 18.f * FMath::Max(1, FMath::CeilToInt(TextW(Def.Desc, 12) / (W - 16)) + 1);
	const float H = 22.f * (Lines.Num() + (Miss.IsEmpty() ? 1 : 2)) + DescH + 10.f;
	const float X = VS.X - SW - W - 8.f;
	const float YY = FMath::Clamp(Y - H * 0.5f, 4.f, VS.Y - H - 4.f);
	Rect(X, YY, W, H, FLinearColor(0.05f, 0.06f, 0.07f, 0.95f));
	Frame(X, YY, W, H, Accent, 1.5f);
	float LY = YY + 6;
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		Text(Lines[I].Key, X + 8, LY, I == 0 ? 14 : 12, Lines[I].Value, I == 0);
		LY += 22.f;
	}
	LY = TextWrap(Def.Desc, X + 8, LY, W - 16, 12, FLinearColor(0.85f, 0.85f, 0.8f), 18.f) + 4.f;
	if (!Miss.IsEmpty())
	{
		Text(TEXT("需要：") + Miss, X + 8, LY, 12, FLinearColor(1.f, 0.45f, 0.35f));
		LY += 22.f;
	}
	Text(TEXT("左鍵：建造　右鍵：取消"), X + 8, LY, 12, FLinearColor(0.7f, 0.7f, 0.7f));
}

void ARSHUD::ClickItem(ARSGame* G, const FString& Id)
{
	URSPlayer* Human = G->Human;
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	const FString Cat = URSPlayer::QueueCat(Def);
	if (Human->GetReady(Cat) == Id)
	{
		G->BeginPlace(Id);
		G->Audio()->Play(TEXT("click"), -6.f);
		return;
	}
	if (Human->CanBuild(Id))
	{
		if (Human->Enqueue(Id)) G->Audio()->Play(TEXT("click"), -6.f);
		else G->Message(TEXT("該生產線正忙碌中"));
	}
	else
	{
		const FString Miss = Human->MissingReq(Id);
		if (!Miss.IsEmpty()) G->Message(TEXT("需要先建造：") + Miss);
		G->Audio()->Play(TEXT("click"), 0.f);
	}
}

void ARSHUD::PressPower(ARSGame* G, const FString& K)
{
	const FRSSuper& Sp = FRSDefs::Get().GetSuper(K);
	const float* C = G->Human->Powers.Find(K);
	G->Audio()->Play(TEXT("click"), -6.f);
	if (C && *C >= 1.f && Sp.Target == TEXT("self"))
	{
		G->FirePower(G->Human, K, RS::Cell2(G->Human->StartCell));
		return;
	}
	if (C && *C >= 1.f)
	{
		const FString Mode = TEXT("power:") + K;
		G->CmdMode = G->CmdMode == Mode ? FString() : Mode;
		G->PlaceId.Empty();
		G->Message(Sp.Target == TEXT("enemy") ? FString(TEXT("選擇要制裁的敵方建築")) : FString::Printf(TEXT("選擇「%s」的目標位置"), *Sp.Name));
	}
	else
	{
		G->Message(FString::Printf(TEXT("%s 充能中…… %s"), *Sp.Name, *Sp.Desc));
	}
}

void ARSHUD::DrawSidebar(ARSGame* G)
{
	URSPlayer* Human = G->Human;
	const float X0 = VS.X - SW;
	const float H = VS.Y;
	for (int32 I = 0; I < 16; ++I)
		Rect(X0, H * I / 16.f, SW, H / 16.f + 1, FMath::Lerp(FLinearColor(0.06f, 0.08f, 0.1f), FLinearColor(0.025f, 0.03f, 0.04f), I / 16.f));
	for (float GY = 0; GY < H; GY += 16.f) Rect(X0 + 4, GY, SW, 1, RS::WithA(Accent, 0.06f));
	for (float GX = 4; GX < SW; GX += 16.f) Rect(X0 + GX, 0, 1, H, RS::WithA(Accent, 0.06f));
	for (int32 K = 0; K < 6; ++K) Rect(X0 + K, 0, 1, H, RS::WithA(Accent, 0.5f - K * 0.08f));
	const float PY = FMath::Fmod(T * 180.f, H + 200.f) - 100.f;
	Rect(X0 + 1, PY, 2, 80, FLinearColor(1, 1, 1, 0.7f));
	// 資金
	Bevel(X0 + 16, 8, SW - 32, 32, FLinearColor(0.05f, 0.07f, 0.09f), true);
	Text(TEXT("$ ") + RS::FmtMoney(Human->Money), X0 + 16, 11, 23, FLinearColor(1.f, 0.86f, 0.35f), true, 1, SW - 32);
	if (FRSDefs::Get().GetCountry(Human->Country)) Flag(X0 + 22, 12, 40, 24, Human->Country);
	// 雷達
	DrawMinimap(G, X0 + 16, 46, SW - 32, MMH);
	// 電力條
	const float GY0 = 44 + MMH + 50;
	const TArray<FString> PL = Human->PowerList();
	const float GH = H - GY0 - 100 - PL.Num() * 30;
	{
		const float PBX = X0 + 6, PBY = GY0, PBH = H - GY0 - 100;
		Bevel(PBX - 1, PBY - 1, 10, PBH + 2, FLinearColor(0.05f, 0.07f, 0.09f), true);
		const float Cap = FMath::Max(Human->PowerProd, Human->PowerUse) * 1.15f + 1.f;
		const float ProdH = PBH * Human->PowerProd / Cap;
		const float UseH = PBH * Human->PowerUse / Cap;
		FLinearColor PC2 = Human->bLowPower ? FLinearColor(1.f, 0.2f, 0.1f) : FLinearColor(0.2f, 0.9f, 0.3f);
		if (!Human->bLowPower && Human->PowerProd - Human->PowerUse < 50.f) PC2 = FLinearColor(1.f, 0.8f, 0.2f);
		Rect(PBX, PBY + PBH - ProdH, 8, ProdH, PC2);
		Rect(PBX - 3, PBY + PBH - UseH - 1, 14, 2, FLinearColor::White);
	}
	// 分頁
	const float TW = (SW - 36 - 8) / 5.f;
	for (int32 I = 0; I < 5; ++I)
	{
		const float TX = X0 + 18 + I * (TW + 2), TY = 44 + MMH + 12;
		const bool bActive = I == Tab;
		const bool bHasReady = I < 2 && !Human->GetReady(TABS[I]).IsEmpty();
		FLinearColor BC = bActive ? RS::Darken(Accent, 0.45f) : FLinearColor(0.05f, 0.08f, 0.1f);
		if (bHasReady && int32(T * 3.f) % 2 == 0) BC = FLinearColor(0.8f, 0.65f, 0.1f);
		Bevel(TX, TY, TW, 30, BC, false);
		FLinearColor TC = bActive ? FLinearColor::White : FLinearColor(0.75f, 0.75f, 0.75f);
		if (!TabEnabled(G, I)) TC = FLinearColor(0.4f, 0.4f, 0.4f);
		Text(TAB_NAMES[I], TX, TY + 5, 16, TC, true, 1, TW);
		if (bClick && Hovered(TX, TY, TW, 30))
		{
			bClick = false;
			Tab = I;
			G->Audio()->Play(TEXT("click"), -8.f);
		}
	}
	// 生產圖示
	const TArray<FString> Items = TabItems(G, Tab);
	const float GX = X0 + 20;
	const int32 MaxRows = FMath::Max(1, int32(GH / (CH + 4)));
	const int32 TotalRows = FMath::CeilToInt(Items.Num() / 2.f);
	if (Hovered(GX, GY0, CW * 2 + 4, GH) && Wheel != 0.f)
	{
		Scroll[Tab] = FMath::Clamp(Scroll[Tab] - int32(Wheel), 0, FMath::Max(0, TotalRows - MaxRows));
		Wheel = 0.f;
	}
	HoverItem.Empty();
	const int32 Start = Scroll[Tab] * 2;
	for (int32 K = Start; K < FMath::Min(Items.Num(), Start + MaxRows * 2); ++K)
	{
		const int32 Idx = K - Start;
		const float CX = GX + (Idx % 2) * (CW + 4), CY = GY0 + (Idx / 2) * (CH + 4);
		if (Hovered(CX, CY, CW, CH)) HoverItem = Items[K];
		DrawCameo(G, CX, CY, Items[K]);
		if (Hovered(CX, CY, CW, CH))
		{
			if (bClick)
			{
				bClick = false;
				ClickItem(G, Items[K]);
			}
			else if (bRClick)
			{
				bRClick = false;
				if (G->Human->Cancel(Items[K]))
				{
					G->Audio()->Play(TEXT("click"), -6.f);
					if (G->PlaceId == Items[K]) G->PlaceId.Empty();
				}
			}
		}
	}
	if (Items.Num() > MaxRows * 2) Text(FString::Printf(TEXT("滾輪捲動 (%d/%d)"), Scroll[Tab] + 1, TotalRows - MaxRows + 1), GX, GY0 + GH + 2, 11, FLinearColor(0.7f, 0.7f, 0.7f), false, 1, CW * 2 + 4);
	// 底部按鈕
	const float BY = H - 88;
	for (int32 I = 0; I < PL.Num(); ++I)
	{
		const FString& K = PL[I];
		const float C = Human->Powers[K];
		const FString& Nm = FRSDefs::Get().GetSuper(K).Name;
		const float RY = BY - 6 - (PL.Num() - I) * 30;
		const bool bReadyP = C >= 1.f;
		const FString Lbl = bReadyP ? FString::Printf(TEXT("> %s 就緒"), *Nm) : FString::Printf(TEXT("%s  %d%%"), *Nm, int32(C * 100));
		const bool bOn = G->CmdMode == TEXT("power:") + K;
		FLinearColor BC = bOn ? RS::Darken(Accent, 0.45f) : FLinearColor(0.05f, 0.08f, 0.1f);
		if (bReadyP && int32(T * 3.f) % 2 == 0) BC = FLinearColor(0.75f, 0.2f, 0.1f);
		if (Hovered(GX, RY, CW * 2 + 4, 27)) BC = RS::Lighten(BC, 0.15f);
		Bevel(GX, RY, CW * 2 + 4, 27, BC, false);
		if (!bReadyP) Rect(GX + 2, RY + 22, (CW * 2) * C, 3, RS::Lighten(Accent, 0.3f));
		Text(Lbl, GX, RY + 4, 14, FLinearColor(0.95f, 0.95f, 0.9f), true, 1, CW * 2 + 4);
		if (bClick && Hovered(GX, RY, CW * 2 + 4, 27))
		{
			bClick = false;
			PressPower(G, K);
		}
	}
	if (Button(GX, BY, CW, 30, TEXT("修理 (X)"), 14, true, G->CmdMode == TEXT("repair")))
	{
		G->CmdMode = G->CmdMode == TEXT("repair") ? FString() : FString(TEXT("repair"));
		G->PlaceId.Empty();
	}
	if (Button(GX + CW + 4, BY, CW, 30, TEXT("出售 (Z)"), 14, true, G->CmdMode == TEXT("sell")))
	{
		G->CmdMode = G->CmdMode == TEXT("sell") ? FString() : FString(TEXT("sell"));
		G->PlaceId.Empty();
	}
	if (Button(GX, BY + 34, CW * 2 + 4, 30, TEXT("選單 (Esc)"), 14)) G->bPausePanel = !G->bPausePanel;
	if (!HoverItem.IsEmpty()) DrawTooltip(G, HoverItem, M.Y);
}

// ───────────────────────── 面板 ─────────────────────────
void ARSHUD::DrawPausePanel(ARSGame* G)
{
	const float W = 440, H = 640;
	const float X = (VS.X - W) * 0.5f, Y = (VS.Y - H) * 0.5f;
	Panel(X, Y, W, H, TEXT("遊戲暫停"));
	float YY = Y + 80;
	const float BX = X + 60, BW = W - 120;
	ARSGameMode* Gm = GM();
	if (Button(BX, YY, BW, 44, TEXT("繼續作戰"))) G->bPausePanel = false;
	YY += 52;
	if (Button(BX, YY, BW, 44, TEXT("存檔 / 讀檔"))) G->bSavePanel = true;
	YY += 52;
	if (Button(BX, YY, BW, 44, TEXT("重新開始本局")) && Gm) { Gm->RestartGame(); return; }
	YY += 52;
	if (Button(BX, YY, BW, 44, TEXT("操作說明"))) G->bHelpPanel = true;
	YY += 60;
	FRSUserSettings& US = FRSUserSettings::Get();
	struct FVol { const TCHAR* Name; float* V; };
	FVol Vols[3] = { { TEXT("音樂音量"), &US.MusicVol }, { TEXT("音效音量"), &US.SfxVol }, { TEXT("語音音量"), &US.VoiceVol } };
	for (FVol& V : Vols)
	{
		Text(V.Name, BX, YY + 2, 16, FLinearColor(0.9f, 0.9f, 0.86f));
		if (Slider(BX + 100, YY, BW - 100, 24, *V.V))
		{
			if (Gm && Gm->Audio) Gm->Audio->UpdateMusicVolume();
			US.Save();
		}
		YY += 36;
	}
	if (Button(BX, YY, BW, 36, US.bEdgeScroll ? TEXT("☑ 滑鼠移到邊緣捲動畫面") : TEXT("☐ 滑鼠移到邊緣捲動畫面"), 15))
	{
		US.bEdgeScroll = !US.bEdgeScroll;
		US.Save();
	}
	YY += 48;
	if (Button(BX, YY, BW, 44, TEXT("返回主選單")) && Gm) { Gm->ToMenu(); return; }
	YY += 52;
	if (Button(BX, YY, BW, 44, TEXT("離開遊戲")) && Gm) Gm->QuitGame();
}

void ARSHUD::DrawSavePanel(ARSGame* G)
{
	const float W = 760, H = 380;
	const float X = (VS.X - W) * 0.5f, Y = (VS.Y - H) * 0.5f;
	Panel(X, Y, W, H, TEXT("存檔 / 讀檔"));
	for (int32 N = 0; N < RSSave::SLOTS; ++N)
	{
		const FRSSlotInfo Info = RSSave::SlotInfo(N);
		const float RY = Y + 80 + N * 64;
		const FString Lbl = Info.bValid ? FString::Printf(TEXT("槽位 %d：%s\n%s"), N + 1, *Info.Title, *Info.Date) : FString::Printf(TEXT("槽位 %d：（空）"), N + 1);
		TextWrap(Lbl, X + 30, RY, 420, 15, FLinearColor(0.9f, 0.9f, 0.86f), 20.f);
		if (Button(X + 470, RY, 120, 40, TEXT("存檔"), 15))
		{
			if (RSSave::SaveGame(G, N)) G->Message(FString::Printf(TEXT("已存檔到槽位 %d"), N + 1));
		}
		if (Button(X + 600, RY, 120, 40, TEXT("讀檔"), 15, Info.bValid))
		{
			if (ARSGameMode* Gm = GM()) Gm->LoadGame(N);
			return;
		}
	}
	Text(TEXT("F5 快速存檔（槽位 1）　F9 快速讀檔"), X + 30, Y + H - 110, 13, FLinearColor(0.75f, 0.75f, 0.7f));
	if (Button(X + (W - 200) * 0.5f, Y + H - 70, 200, 44, TEXT("關閉"))) G->bSavePanel = false;
}

void ARSHUD::DrawHelpPanel()
{
	ARSGame* G = Game();
	const float W = 1100, H = 760;
	const float X = (VS.X - W) * 0.5f, Y = (VS.Y - H) * 0.5f;
	Panel(X, Y, W, H, TEXT("操作說明"));
	const FString Txt = TEXT("【基本】\n左鍵點選 / 拖曳框選單位；雙擊選取畫面上同類單位\n右鍵：移動、攻擊敵人、採礦、進入建築（工程師）\n選取基地車後按 D 或雙擊 → 展開成建造廠\n\n【建造】\n右側建造欄：左鍵生產，右鍵取消；建築完成後點選再放到地圖上\n兵營 / 戰車工廠 / 造船廠選取後，右鍵地面可設定集結點\n特殊武器按鈕在建造欄下方，充能完成後點擊再選目標\n電力不足會讓生產變慢、防禦塔停擺\n\n【快捷鍵】\nA 攻擊移動　S 停止　E 選取全部戰鬥單位　Q 選取同類\nN 下一台閒置採礦車　I 下一個閒置戰鬥單位　Tab 循環生產建築　B 循環全部建築\n點小地圖跳視角，右鍵小地圖直接下達移動命令\nShift+右鍵 設定路徑點　步兵右鍵平民建築＝進駐，選取該建築按 D 撤出\nCtrl+1~9 設定編隊，1~9 選取編隊（連按兩下跳過去）\nZ 出售模式　X 修理模式　Delete 出售選取建築\nH / 空白鍵 回到基地　方向鍵 / 滑鼠移到邊緣 捲動畫面\n滾輪縮放　中鍵拖曳 平移　+ / - 調整遊戲速度　P 暫停　F3 顯示 FPS　Esc 選單\nF 開關陣型移動（戰車在前、步兵居中、砲兵在後）　C 電影鏡頭（自動追戰鬥，C / Esc / 右鍵返回）\n\n【外交 / 情報】\nT 開啟外交面板：向電腦提議結盟或停火，結盟後共享視野並獲得「盟友支援」\nF5 快速存檔　F9 快速讀檔　Esc 選單內有 3 個存檔槽位\n間諜右鍵敵方建築＝滲透（發電廠停電、精煉廠偷錢、雷達揭露、超武重置、科技晉升、生產癱瘓）\n畫面上方會顯示敵方超級武器與特殊能力的倒數；航母會自動派出艦載機攻擊 13 格內目標");
	TextWrap(Txt, X + 30, Y + 70, W - 60, 15, FLinearColor(0.92f, 0.92f, 0.88f), 23.f);
	if (Button(X + (W - 200) * 0.5f, Y + H - 60, 200, 44, TEXT("關閉")))
	{
		if (G) G->bHelpPanel = false;
	}
}

void ARSHUD::DrawDiploPanel(ARSGame* G)
{
	URSPlayer* Human = G->Human;
	const TArray<URSPlayer*> Others = G->OthersOf(Human);
	const float W = 1000, H = 240 + Others.Num() * 60;
	const float X = (VS.X - W) * 0.5f, Y = (VS.Y - H) * 0.5f;
	Panel(X, Y, W, H, TEXT("外交（T）"));
	TextWrap(TEXT("好感度會隨時間回升，攻擊對方會下降。結盟需 50%，停火需 35%。\n結盟後共享視野，並獲得「盟友支援」能力；攻擊盟友會導致同盟破裂。"), X + 30, Y + 70, W - 60, 13, FLinearColor(0.8f, 0.8f, 0.75f), 20.f);
	float YY = Y + 130;
	for (URSPlayer* Q : Others)
	{
		Flag(X + 30, YY + 4, 38, 24, Q->Country);
		const float* CF = Human->Ceasefire.Find(Q->Index);
		const FString St = Human->Allies.Contains(Q->Index) ? TEXT("盟友") : ((CF && G->Time < *CF) ? TEXT("停火中") : (Human->IsEnemyOf(Q) ? TEXT("敵對") : TEXT("中立")));
		Text(FString::Printf(TEXT("%s（%s）  好感 %d%%  [%s]"), *Q->Name, *Q->CountryName(), int32(Q->Relation(Human)), *St), X + 80, YY + 6, 16, RS::Lighten(Q->Color, 0.3f), true);
		const float BX = X + W - 330;
		if (const FString* Kind = Human->Proposal.Find(Q->Index))
		{
			const FString K = *Kind;
			if (Button(BX, YY, 150, 36, K == TEXT("ally") ? TEXT("接受結盟") : TEXT("接受停火"), 15)) G->ApplyTreaty(Human, Q, K);
			if (Button(BX + 160, YY, 140, 36, TEXT("拒絕"), 15))
			{
				Human->Proposal.Remove(Q->Index);
				Q->AddRelation(Human, -5.f);
			}
		}
		else if (Human->Allies.Contains(Q->Index))
		{
			if (Button(BX + 160, YY, 140, 36, TEXT("解除同盟"), 15)) G->BreakTreaty(Human, Q);
		}
		else
		{
			if (Button(BX, YY, 150, 36, TEXT("提議結盟"), 15)) G->Propose(Human, Q, TEXT("ally"));
			if (Button(BX + 160, YY, 140, 36, TEXT("提議停火"), 15)) G->Propose(Human, Q, TEXT("ceasefire"));
		}
		YY += 60;
	}
	// 最近訊息（讓玩家看到提議結果）
	if (G->Msgs.Num() > 0) Text(G->Msgs.Last().Text, X + 30, Y + H - 100, 14, FLinearColor(0.55f, 1.f, 0.55f), true);
	if (Button(X + (W - 200) * 0.5f, Y + H - 64, 200, 44, TEXT("關閉"))) G->bDiploPanel = false;
}

void ARSHUD::DrawEndPanel(ARSGame* G)
{
	const bool bWin = G->Result == TEXT("win");
	URSPlayer* Hm = G->Human;
	const float W = 620, H = 520 + G->FreshAch.Num() * 26;
	const float X = (VS.X - W) * 0.5f, Y = (VS.Y - H) * 0.5f;
	Panel(X, Y, W, H, bWin ? TEXT("任務完成！勝利！") : TEXT("任務失敗……"));
	const TArray<FString> Lines = {
		FString::Printf(TEXT("作戰時間：%s"), *RS::MMSS(G->Time)),
		FString::Printf(TEXT("生產單位：%d"), Hm->Stats.Built),
		FString::Printf(TEXT("消滅敵軍：%d"), Hm->Stats.Killed),
		FString::Printf(TEXT("損失單位：%d   損失建築：%d"), Hm->Stats.Lost, Hm->Stats.BldLost),
		FString::Printf(TEXT("採礦收入：$%s"), *RS::FmtMoney(Hm->Stats.Harvested)) };
	float YY = Y + 80;
	for (const FString& L : Lines)
	{
		Text(L, X, YY, 17, FLinearColor(0.92f, 0.92f, 0.88f), false, 1, W);
		YY += 28;
	}
	for (const FRSAchDef& A : G->FreshAch)
	{
		Text(FString::Printf(TEXT("★ 成就解鎖：%s — %s"), *A.Name, *A.Desc), X, YY, 16, FLinearColor(1.f, 0.85f, 0.3f), true, 1, W);
		YY += 26;
	}
	YY += 10;
	ARSGameMode* Gm = GM();
	const float BX = X + 100, BW = W - 200;
	if (bWin && G->Mission && Gm)
	{
		const FString Next = FRSUserSettings::Get().NextMission(G->Mission->Id);
		if (!Next.IsEmpty())
		{
			if (const FRSMissionDef* Md = FRSDefs::Get().FindMission(Next))
			{
				if (Button(BX, YY, BW, 44, TEXT("下一關：") + Md->Name, 16))
				{
					Gm->StartGame(FRSGameSettings::FromMission(*Md, G->Settings.Difficulty));
					return;
				}
			}
		}
		else
		{
			Text(TEXT("★ 戰役全部完成！ ★"), X, YY + 8, 22, FLinearColor(1.f, 0.85f, 0.3f), true, 1, W);
		}
		YY += 52;
	}
	if (Button(BX, YY, BW, 44, TEXT("再玩一局")) && Gm) { Gm->RestartGame(); return; }
	YY += 52;
	if (Button(BX, YY, BW, 44, TEXT("返回主選單")) && Gm) { Gm->ToMenu(); return; }
	YY += 52;
	if (Button(BX, YY, BW, 44, TEXT("離開遊戲")) && Gm) Gm->QuitGame();
}
