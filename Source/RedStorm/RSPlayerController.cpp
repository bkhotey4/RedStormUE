#include "RSPlayerController.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSGameMode.h"
#include "RSHUD.h"
#include "RSCameraPawn.h"
#include "RSGlobals.h"
#include "RSAudio.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"

ARSPlayerController::ARSPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ARSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
	SetInputMode(Mode);
	bShowMouseCursor = true;
}

ARSGameMode* ARSPlayerController::GM() const
{
	return GetWorld() ? Cast<ARSGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
}

ARSHUD* ARSPlayerController::RSHUD() const
{
	return Cast<ARSHUD>(GetHUD());
}

ARSCameraPawn* ARSPlayerController::Cam() const
{
	return Cast<ARSCameraPawn>(GetPawn());
}

void ARSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	float MX = 0.f, MY = 0.f;
	if (GetMousePosition(MX, MY)) Mouse = FVector2D(MX, MY);
	bLeftClick = WasInputKeyJustPressed(EKeys::LeftMouseButton);
	bLeftRelease = WasInputKeyJustReleased(EKeys::LeftMouseButton);
	bRightClick = WasInputKeyJustPressed(EKeys::RightMouseButton);
	bLeftDown = IsInputKeyDown(EKeys::LeftMouseButton);
	Wheel = 0.f;
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp)) Wheel += 1.f;
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) Wheel -= 1.f;
	bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	bCtrl = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
	bUIConsumedClick = false;

	ARSGameMode* G = GM();
	ARSHUD* H = RSHUD();
	if (!G || !H) return;
	const bool bAnyKey = WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar) || bLeftClick;
	switch (G->State)
	{
	case ERSState::Intro:
		if (G->StateT > 1.f && bAnyKey) G->FinishIntro();
		break;
	case ERSState::Briefing:
		if (G->StateT > 1.f && bAnyKey) G->FinishBriefing();
		break;
	case ERSState::Menu:
		if (WasInputKeyJustPressed(EKeys::Escape)) H->MenuBack();
		break;
	case ERSState::Game:
		if (G->Game) GameInput(G->Game, DeltaTime);
		break;
	}
}

void ARSPlayerController::GameInput(ARSGame* Game, float Dt)
{
	ARSHUD* H = RSHUD();
	ARSCameraPawn* C = Cam();
	const bool bModal = H->IsModalOpen();
	const bool bOverUI = H->IsOverUI(Mouse);
	// Esc：取消放置 / 指令，否則開關暫停選單
	// 電影鏡頭：C 開關；開啟時 Esc 只用來離開，並停用一般鏡頭與滑鼠指令
	if (WasInputKeyJustPressed(EKeys::C) && !bModal && !Game->bGameOver) Game->ToggleCinema();
	if (Game->bCinema)
	{
		if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::RightMouseButton)) Game->ToggleCinema();
		return;
	}
	if (WasInputKeyJustPressed(EKeys::Escape))
	{
		if (!Game->PlaceId.IsEmpty() || !Game->CmdMode.IsEmpty())
		{
			Game->PlaceId.Empty();
			Game->CmdMode.Empty();
		}
		else
		{
			H->Escape();
		}
		return;
	}
	if (WasInputKeyJustPressed(EKeys::F1)) H->ToggleHelp();
	if (Game->bGameOver || bModal)
	{
		Game->bDragging = false;
		return;
	}
	if (WasInputKeyJustPressed(EKeys::T))
	{
		H->ToggleDiplomacy();
		return;
	}
	if (WasInputKeyJustPressed(EKeys::F9))
	{
		if (ARSGameMode* G = GM()) G->LoadGame(0);
		return;
	}
	// 鏡頭
	if (C)
	{
		FVector2D V = FVector2D::ZeroVector;
		if (IsInputKeyDown(EKeys::Left)) V.X -= 1.f;
		if (IsInputKeyDown(EKeys::Right)) V.X += 1.f;
		if (IsInputKeyDown(EKeys::Up)) V.Y -= 1.f;
		if (IsInputKeyDown(EKeys::Down)) V.Y += 1.f;
		FVector2D VS(1920, 1080);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VS);
		const bool bFocused = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveTopLevelWindow().IsValid();
		if (FRSUserSettings::Get().bEdgeScroll && !bPanning && bFocused && Mouse.X >= 0.f && Mouse.Y >= 0.f)
		{
			const float M = 6.f;
			if (Mouse.X <= M) V.X -= 1.f;
			else if (Mouse.X >= VS.X - M) V.X += 1.f;
			if (Mouse.Y <= M) V.Y -= 1.f;
			else if (Mouse.Y >= VS.Y - M) V.Y += 1.f;
		}
		if (!V.IsNearlyZero()) C->MoveScreen(V.GetSafeNormal(), 2600.f / C->Zoom * Dt);
		if (Wheel != 0.f && !bOverUI) C->SetZoom(C->Zoom * (Wheel > 0.f ? 1.1f : 1.f / 1.1f));
		if (WasInputKeyJustPressed(EKeys::MiddleMouseButton))
		{
			bPanning = true;
			PanLast = Mouse;
		}
		if (WasInputKeyJustReleased(EKeys::MiddleMouseButton)) bPanning = false;
		if (bPanning)
		{
			const FVector2D D = Mouse - PanLast;
			PanLast = Mouse;
			// 每像素大約對應的世界距離
			const float K = 2.4f / C->Zoom;
			C->MoveScreen(FVector2D(-D.X, -D.Y), K);
		}
	}
	// 滑鼠指令
	if (bOverUI)
	{
		Game->Hover = nullptr;
	}
	else
	{
		Game->UpdateHover();
	}
	if (bLeftClick && !bOverUI)
	{
		const double Now = FPlatformTime::Seconds();
		const bool bDouble = Now - LastClickTime < 0.32 && FVector2D::Distance(Mouse, LastClickPos) < 8.f;
		LastClickTime = Now;
		LastClickPos = Mouse;
		Game->OnLeftPress(bShift, bDouble);
	}
	if (bLeftRelease && Game->bDragging) Game->OnLeftRelease(bShift);
	if (bRightClick && !bOverUI) Game->OnRightPress(bCtrl, bShift);
	// 鍵盤
	static const FKey Keys[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
		EKeys::A, EKeys::S, EKeys::D, EKeys::H, EKeys::SpaceBar, EKeys::E, EKeys::N, EKeys::I, EKeys::Tab, EKeys::B, EKeys::Q, EKeys::Z, EKeys::X,
		EKeys::P, EKeys::Pause, EKeys::Equals, EKeys::Add, EKeys::Hyphen, EKeys::Subtract, EKeys::F3, EKeys::F5, EKeys::Delete };
	for (const FKey& K : Keys)
	{
		if (WasInputKeyJustPressed(K)) Game->OnKey(K, bCtrl, bShift);
	}
}
