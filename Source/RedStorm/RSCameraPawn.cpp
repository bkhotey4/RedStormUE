#include "RSCameraPawn.h"
#include "RSCommon.h"
#include "Camera/CameraComponent.h"

ARSCameraPawn::ARSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	Focus = CreateDefaultSubobject<USceneComponent>(TEXT("Focus"));
	RootComponent = Focus;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Focus);
	Camera->SetFieldOfView(42.f);
	Camera->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
	Camera->PostProcessSettings.AutoExposureSpeedUp = 1.f;
	Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
	Camera->PostProcessSettings.AutoExposureSpeedDown = 1.f;
	Camera->PostProcessSettings.bOverride_BloomIntensity = true;
	Camera->PostProcessSettings.BloomIntensity = 0.8f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.35f;
	bFindCameraComponentWhenViewTarget = true;
	ApplyCamera();
}

FVector ARSCameraPawn::RightDir() const
{
	const float Y = FMath::DegreesToRadians(Yaw);
	return FVector(-FMath::Sin(Y), FMath::Cos(Y), 0.f);
}

FVector ARSCameraPawn::UpDir() const
{
	const float Y = FMath::DegreesToRadians(Yaw);
	return FVector(FMath::Cos(Y), FMath::Sin(Y), 0.f);
}

void ARSCameraPawn::ApplyCamera()
{
	if (!Camera) return;
	const float Dist = 3600.f / FMath::Clamp(Zoom, 0.4f, 2.5f);
	const FRotator Rot(Pitch, Yaw, 0.f);
	Camera->SetRelativeLocation(-Rot.Vector() * Dist + ShakeOffset);
	Camera->SetRelativeRotation(Rot);
}

void ARSCameraPawn::SetFocusWorld(const FVector& P)
{
	FVector Q = P;
	Q.X = FMath::Clamp(Q.X, BoundsMin.X, BoundsMax.X);
	Q.Y = FMath::Clamp(Q.Y, BoundsMin.Y, BoundsMax.Y);
	Q.Z = 0.f;
	SetActorLocation(Q);
}

void ARSCameraPawn::MoveScreen(const FVector2D& Dir, float Amount)
{
	const FVector D = RightDir() * Dir.X + UpDir() * (-Dir.Y);
	SetFocusWorld(GetFocusWorld() + D * Amount);
}

void ARSCameraPawn::SetZoom(float Z)
{
	Zoom = FMath::Clamp(Z, 0.55f, 2.f);
	ApplyCamera();
}
