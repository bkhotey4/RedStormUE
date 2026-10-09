// RTS 斜角鏡頭：焦點在地面，固定俯角與方位（與 Godot 等角視角相同的方向）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RSCameraPawn.generated.h"

class UCameraComponent;

UCLASS()
class REDSTORM_API ARSCameraPawn : public APawn
{
	GENERATED_BODY()
public:
	ARSCameraPawn();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Focus = nullptr;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera = nullptr;

	// Godot 版的縮放值（0.55 ~ 2.0），越大越近
	float Zoom = 1.1f;
	float Yaw = 225.f;
	float Pitch = -52.f;
	FVector ShakeOffset = FVector::ZeroVector;
	FVector2D BoundsMin = FVector2D(-1000, -1000), BoundsMax = FVector2D(1e5, 1e5);

	void SetFocusWorld(const FVector& P);
	FVector GetFocusWorld() const { return Focus ? Focus->GetComponentLocation() : FVector::ZeroVector; }
	void MoveScreen(const FVector2D& Dir, float Amount);
	void SetZoom(float Z);
	void ApplyCamera();
	FVector RightDir() const;
	FVector UpDir() const;    // 地面上「畫面往上」的方向
};
