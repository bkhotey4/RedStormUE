// 紅色風暴 Red Storm — 共用常數與小工具
#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Math/RandomStream.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "TextureResource.h"
#include "RenderUtils.h"
#include "RHI.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRS, Log, All);

// 1 格 = 100 uu（1 公尺）；高地抬高 RS_HIGH
static constexpr float RS_CELL = 100.f;
static constexpr float RS_HIGH = 60.f;

namespace RS
{
	inline FVector2D FromAngle(float A) { return FVector2D(FMath::Cos(A), FMath::Sin(A)); }
	inline float AngleOf(const FVector2D& V) { return FMath::Atan2(V.Y, V.X); }
	inline float WrapPi(float A)
	{
		A = FMath::Fmod(A + PI, 2.f * PI);
		if (A < 0.f) A += 2.f * PI;
		return A - PI;
	}
	inline float AngleToAngle(float From, float To, float MaxStep)
	{
		const float D = WrapPi(To - From);
		if (FMath::Abs(D) <= MaxStep) return To;
		return From + FMath::Sign(D) * MaxStep;
	}
	inline float ADiff(float A, float B) { return FMath::Abs(WrapPi(A - B)); }
	inline float Rand(float A, float B) { return FMath::FRandRange(A, B); }
	inline int32 RandI(int32 A, int32 B) { return FMath::RandRange(A, B); }
	inline float Rand01() { return FMath::FRand(); }
	inline FVector2D Rot(const FVector2D& V, float A)
	{
		const float C = FMath::Cos(A), S = FMath::Sin(A);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}
	inline FVector2D Ortho(const FVector2D& V) { return FVector2D(-V.Y, V.X); }
	inline FVector2D Cell2(const FIntPoint& C) { return FVector2D((float)C.X, (float)C.Y); }
	inline FVector2D CellCenter(const FIntPoint& C) { return FVector2D(C.X + 0.5f, C.Y + 0.5f); }
	inline FIntPoint ToCell(const FVector2D& P) { return FIntPoint(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y)); }
	inline FVector2D LimitLen(const FVector2D& V, float L)
	{
		const float S = V.Size();
		return S > L && S > 0.f ? V * (L / S) : V;
	}
	inline FVector2D Lerp2(const FVector2D& A, const FVector2D& B, float T) { return A + (B - A) * T; }
	inline FString FmtMoney(double V)
	{
		const int64 N = (int64)V;
		FString S = FString::Printf(TEXT("%lld"), N < 0 ? -N : N);
		FString Out;
		int32 C = 0;
		for (int32 I = S.Len() - 1; I >= 0; --I)
		{
			Out.InsertAt(0, S[I]);
			if (++C % 3 == 0 && I > 0) Out.InsertAt(0, TCHAR(','));
		}
		return N < 0 ? TEXT("-") + Out : Out;
	}
	inline FString MMSS(float T)
	{
		const int32 S = FMath::Max(0, (int32)T);
		return FString::Printf(TEXT("%02d:%02d"), S / 60, S % 60);
	}
	inline FLinearColor Lighten(const FLinearColor& C, float A) { return FLinearColor(C.R + (1.f - C.R) * A, C.G + (1.f - C.G) * A, C.B + (1.f - C.B) * A, C.A); }
	inline FLinearColor Darken(const FLinearColor& C, float A) { return FLinearColor(C.R * (1.f - A), C.G * (1.f - A), C.B * (1.f - A), C.A); }
	inline FLinearColor WithA(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, A); }
	inline FString ContentDir() { return FPaths::ProjectContentDir() / TEXT("RedStorm"); }
	inline FString SaveDir() { return FPaths::ProjectSavedDir() / TEXT("RedStorm"); }
}
