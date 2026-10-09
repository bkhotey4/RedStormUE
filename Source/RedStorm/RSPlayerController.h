// 輸入：滑鼠與鍵盤 → HUD（介面）或遊戲（世界指令）、鏡頭捲動與縮放
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RSPlayerController.generated.h"

class ARSGame;
class ARSHUD;
class ARSGameMode;
class ARSCameraPawn;

UCLASS()
class REDSTORM_API ARSPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ARSPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	// 本影格的滑鼠狀態（HUD 讀取）
	FVector2D Mouse = FVector2D::ZeroVector;
	bool bLeftClick = false, bLeftRelease = false, bRightClick = false, bLeftDown = false;
	float Wheel = 0.f;
	bool bShift = false, bCtrl = false;
	bool bUIConsumedClick = false;

	ARSGameMode* GM() const;
	ARSHUD* RSHUD() const;
	ARSCameraPawn* Cam() const;
	bool KeyPressed(const FKey& K) const { return WasInputKeyJustPressed(K); }

private:
	double LastClickTime = 0.0;
	FVector2D LastClickPos = FVector2D::ZeroVector;
	bool bPanning = false;
	FVector2D PanLast = FVector2D::ZeroVector;
	void GameInput(ARSGame* G, float Dt);
};
