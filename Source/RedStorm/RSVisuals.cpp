#include "RSVisuals.h"
#include "RSCommon.h"
#include "RSEntity.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

URSVisualSettings::URSVisualSettings()
{
	BaseMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
}

namespace RSV
{
	static UStaticMesh* LoadMesh(const TCHAR* Path, UStaticMesh*& Cache)
	{
		if (!Cache)
		{
			Cache = LoadObject<UStaticMesh>(nullptr, Path);
			if (Cache) Cache->AddToRoot();
		}
		return Cache;
	}
	UStaticMesh* Cube() { static UStaticMesh* M = nullptr; return LoadMesh(TEXT("/Engine/BasicShapes/Cube.Cube"), M); }
	UStaticMesh* Sphere() { static UStaticMesh* M = nullptr; return LoadMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"), M); }
	UStaticMesh* Cylinder() { static UStaticMesh* M = nullptr; return LoadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), M); }
	UStaticMesh* Cone() { static UStaticMesh* M = nullptr; return LoadMesh(TEXT("/Engine/BasicShapes/Cone.Cone"), M); }
	UStaticMesh* Plane() { static UStaticMesh* M = nullptr; return LoadMesh(TEXT("/Engine/BasicShapes/Plane.Plane"), M); }

	UMaterialInterface* BaseMat()
	{
		static UMaterialInterface* M = nullptr;
		if (!M)
		{
			const URSVisualSettings* S = GetDefault<URSVisualSettings>();
			M = S->BaseMaterial.LoadSynchronous();
			if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			if (M) M->AddToRoot();
		}
		return M;
	}

	UMaterialInterface* GlowMat()
	{
		static UMaterialInterface* M = nullptr;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			M = GetDefault<URSVisualSettings>()->GlowMaterial.LoadSynchronous();
			if (M) M->AddToRoot();
		}
		return M ? M : BaseMat();
	}

	UMaterialInterface* Mat(const FLinearColor& C, bool bGlow)
	{
		static TMap<uint32, UMaterialInstanceDynamic*> Cache;
		const FColor Q = FLinearColor(FMath::Clamp(C.R, 0.f, 4.f) / 4.f, FMath::Clamp(C.G, 0.f, 4.f) / 4.f, FMath::Clamp(C.B, 0.f, 4.f) / 4.f, 1.f).ToFColor(false);
		const uint32 Key = (uint32(Q.R) << 16) | (uint32(Q.G) << 8) | uint32(Q.B) | (bGlow ? 0x1000000u : 0u);
		if (UMaterialInstanceDynamic** Found = Cache.Find(Key))
		{
			if (IsValid(*Found)) return *Found;
		}
		UMaterialInterface* Parent = bGlow ? GlowMat() : BaseMat();
		if (!Parent) return nullptr;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, GetTransientPackage());
		// 資料表的顏色沿用 Godot 的 sRGB 數值；材質參數是線性空間，需轉換，否則整體偏白、隊伍色太淡
		auto ToLin = [](float V) { return V <= 1.f ? FMath::Pow(FMath::Max(V, 0.f), 2.2f) : V; };
		MID->SetVectorParameterValue(GetDefault<URSVisualSettings>()->ColorParam, FLinearColor(ToLin(C.R), ToLin(C.G), ToLin(C.B), C.A));
		MID->AddToRoot();
		Cache.Add(Key, MID);
		return MID;
	}

	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FVector& Size,
		const FLinearColor& Col, const FRotator& Rot, bool bShadow)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Mesh);
		C->SetRelativeLocation(Loc);
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Size / 100.f);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(bShadow);
		C->SetMaterial(0, Mat(Col));
		C->RegisterComponent();
		Owner->AddInstanceComponent(C);
		return C;
	}

	void SetColor(UStaticMeshComponent* C, const FLinearColor& Col, bool bGlow)
	{
		if (C) C->SetMaterial(0, Mat(Col, bGlow));
	}

	// ───────────────────────── 建構小工具 ─────────────────────────
	static const FLinearColor Concrete(0.52f, 0.52f, 0.5f);
	static const FLinearColor Dark(0.08f, 0.085f, 0.09f);
	static const FLinearColor Metal(0.33f, 0.35f, 0.37f);
	static const FLinearColor Skin(0.85f, 0.66f, 0.5f);
	static const FLinearColor Glass(0.25f, 0.45f, 0.6f);
	static const FLinearColor Gold(0.9f, 0.7f, 0.15f);
	static const FLinearColor Cyan(0.4f, 0.9f, 1.f);
	static const FLinearColor Roof(0.45f, 0.18f, 0.12f);

	struct FB
	{
		ARSEntity* E = nullptr;
		FLinearColor Team;
		UStaticMeshComponent* P(USceneComponent* Par, UStaticMesh* M, FVector L, FVector S, FLinearColor C, FRotator R = FRotator::ZeroRotator)
		{
			return AddPart(E, Par ? Par : E->Body.Get(), M, L, S, C, R);
		}
		// 迷彩底色（v3：現代軍事塗裝；建築用中性灰鋼色）
		FLinearColor Camo = FLinearColor(0.36f, 0.4f, 0.26f);
		FLinearColor CamoDark = FLinearColor(0.22f, 0.25f, 0.16f);
		FLinearColor CamoLight = FLinearColor(0.5f, 0.48f, 0.34f);
		UStaticMeshComponent* T(USceneComponent* Par, UStaticMesh* M, FVector L, FVector S, float Shade = 0.f, FRotator R = FRotator::ZeroRotator)
		{
			const FLinearColor C = Shade < 0.f ? RS::Darken(Camo, -Shade * 0.6f) : RS::Lighten(Camo, Shade * 0.6f);
			return AddPart(E, Par ? Par : E->Body.Get(), M, L, S, C, R);
		}
		// 隊伍色標記（識別條、旗幟、砲塔頂板）：會隨擁有者改變顏色
		UStaticMeshComponent* Mk(USceneComponent* Par, UStaticMesh* M, FVector L, FVector S, float Shade = 0.f, FRotator R = FRotator::ZeroRotator)
		{
			const FLinearColor C = Shade < 0.f ? RS::Darken(Team, -Shade) : RS::Lighten(Team, Shade);
			UStaticMeshComponent* Cmp = AddPart(E, Par ? Par : E->Body.Get(), M, L, S, C, R);
			E->TeamParts.Add(Cmp);
			E->TeamShade.Add(Shade);
			return Cmp;
		}
		// 頂面迷彩斑塊
		void Blotch(USceneComponent* Par, FVector TopCenter, float SX, float SY, int32 Seed)
		{
			FRandomStream R(Seed);
			for (int32 K = 0; K < 4; ++K)
			{
				const FVector O(R.FRandRange(-0.32f, 0.32f) * SX, R.FRandRange(-0.32f, 0.32f) * SY, 0.4f + K * 0.05f);
				AddPart(E, Par ? Par : E->Body.Get(), Cube(), TopCenter + O, FVector(SX * R.FRandRange(0.18f, 0.34f), SY * R.FRandRange(0.16f, 0.3f), 0.8f),
					K % 2 ? CamoDark : CamoLight, FRotator(0, R.FRandRange(-30.f, 30.f), 0), false);
			}
		}
		USceneComponent* Pivot(USceneComponent* Par, FVector L)
		{
			USceneComponent* S = NewObject<USceneComponent>(E);
			S->SetupAttachment(Par ? Par : E->Body.Get());
			S->SetRelativeLocation(L);
			S->RegisterComponent();
			E->AddInstanceComponent(S);
			return S;
		}
		// 沿 X 軸的圓柱（砲管、機身）
		UStaticMeshComponent* Barrel(USceneComponent* Par, FVector L, float Len, float D, FLinearColor C, float Pitch = 0.f)
		{
			return P(Par, Cylinder(), L, FVector(D, D, Len), C, FRotator(-90.f + Pitch, 0.f, 0.f));
		}
	};

	// ───────────────────────── 細節小工具（v2 外觀） ─────────────────────────
	// 鏡頭從 +X+Y 方向往下看，所以門窗、標誌主要放在 +X 與 +Y 兩個面
	static const FLinearColor Olive(0.3f, 0.33f, 0.22f);
	static const FLinearColor Rubber(0.05f, 0.05f, 0.055f);
	static const FLinearColor Steel(0.55f, 0.57f, 0.6f);
	static const FLinearColor Warn(0.95f, 0.75f, 0.1f);
	static const FLinearColor Lamp(1.f, 0.85f, 0.55f);
	static const FLinearColor Sand(0.62f, 0.56f, 0.42f);

	// 一排窗戶：沿 Axis（0=X 面、1=Y 面）在面上平均分布
	static void WinRow(FB& B, USceneComponent* Par, int32 Axis, float FaceOff, float Span, float Z, int32 N, float WW, float WH, const FLinearColor& C = Glass)
	{
		if (N <= 0) return;
		const float Step = Span / N;
		for (int32 K = 0; K < N; ++K)
		{
			const float T = -Span * 0.5f + Step * (K + 0.5f);
			if (Axis == 0) B.P(Par, Cube(), FVector(FaceOff, T, Z), FVector(2.5f, WW, WH), C);
			else B.P(Par, Cube(), FVector(T, FaceOff, Z), FVector(WW, 2.5f, WH), C);
		}
	}

	// 旗桿＋隊伍色旗幟
	static void Flag(FB& B, FVector Base, float H)
	{
		B.P(nullptr, Cylinder(), Base + FVector(0, 0, H * 0.5f), FVector(2.5f, 2.5f, H), Steel);
		B.Mk(nullptr, Cube(), Base + FVector(0, 11, H - 7), FVector(1.5f, 22, 13), 0.05f);
		B.P(nullptr, Sphere(), Base + FVector(0, 0, H + 1), FVector(4, 4, 4), Gold);
	}

	// 屋頂空調 / 設備箱
	static void RoofBox(FB& B, FVector L, float S)
	{
		B.P(nullptr, Cube(), L, FVector(S, S * 0.8f, S * 0.55f), Steel);
		B.P(nullptr, Cylinder(), L + FVector(0, 0, S * 0.3f), FVector(S * 0.55f, S * 0.55f, 1.5f), Dark);
	}

	// 履帶組：履帶、負重輪、誘導輪、隊伍色側裙
	static void Tracks(FB& B, float L, float Wd, float Sc = 1.f)
	{
		for (int32 S = -1; S <= 1; S += 2)
		{
			const float Y = S * Wd * 0.4f;
			const float TW = Wd * 0.22f;
			B.P(nullptr, Cube(), FVector(0, Y, 9 * Sc), FVector(L * 0.86f, TW, 13 * Sc), Rubber);
			for (int32 E2 = -1; E2 <= 1; E2 += 2)
				B.P(nullptr, Cylinder(), FVector(E2 * L * 0.43f, Y, 9 * Sc), FVector(13 * Sc, 13 * Sc, TW), Rubber, FRotator(0, 0, 90));
			const int32 NW = L > 85.f ? 6 : 5;
			for (int32 K = 0; K < NW; ++K)
			{
				const float X = -L * 0.36f + K * (L * 0.72f / (NW - 1));
				B.P(nullptr, Cylinder(), FVector(X, Y + S * TW * 0.5f, 8 * Sc), FVector(10 * Sc, 10 * Sc, 2.5f), Metal, FRotator(0, 0, 90));
				B.P(nullptr, Cylinder(), FVector(X, Y + S * (TW * 0.5f + 1.2f), 8 * Sc), FVector(3.5f, 3.5f, 1.f), Dark, FRotator(0, 0, 90));
			}
			B.T(nullptr, Cube(), FVector(0, Y + S * TW * 0.42f, 17 * Sc), FVector(L * 0.92f, 3, 8 * Sc), -0.2f);   // 側裙
			B.Mk(nullptr, Cube(), FVector(-L * 0.1f, Y + S * (TW * 0.42f + 1.7f), 17 * Sc), FVector(L * 0.18f, 0.8f, 5 * Sc), 0.f);   // 識別條
			B.P(nullptr, Cube(), FVector(0, Y, 17.5f * Sc), FVector(L * 0.94f, TW * 1.1f, 2), Metal);               // 擋泥板
		}
	}

	// 輪式底盤：輪胎＋輪轂
	static void Wheels(FB& B, float L, float Wd, int32 Axles, float R = 18.f)
	{
		for (int32 K = 0; K < Axles; ++K)
		{
			const float X = Axles == 1 ? 0.f : -L * 0.34f + K * (L * 0.68f / (Axles - 1));
			for (int32 S = -1; S <= 1; S += 2)
			{
				B.P(nullptr, Cylinder(), FVector(X, S * Wd * 0.46f, R * 0.5f), FVector(R, R, 8), Rubber, FRotator(0, 0, 90));
				B.P(nullptr, Cylinder(), FVector(X, S * (Wd * 0.46f + 4.2f), R * 0.5f), FVector(R * 0.45f, R * 0.45f, 1.f), Steel, FRotator(0, 0, 90));
			}
		}
	}

	// 主砲（含砲口制退器）
	static void Gun(FB& B, USceneComponent* Par, FVector Root, float Len, float D, float Pitch = 0.f)
	{
		const float Rad = FMath::DegreesToRadians(Pitch);
		const FVector Dir(FMath::Cos(Rad), 0.f, FMath::Sin(Rad));
		B.Barrel(Par, Root + Dir * Len * 0.5f, Len, D, Metal, Pitch);
		B.Barrel(Par, Root + Dir * (Len * 0.25f), Len * 0.18f, D * 1.5f, Metal, Pitch);   // 抽煙器
		B.Barrel(Par, Root + Dir * (Len + 2.f), 6.f, D * 1.45f, Dark, Pitch);               // 制退器
	}

	static void Antenna(FB& B, USceneComponent* Par, FVector L, float H)
	{
		B.P(Par, Cylinder(), L + FVector(0, 0, H * 0.5f), FVector(1.2f, 1.2f, H), Dark);
		B.P(Par, Sphere(), L + FVector(0, 0, H), FVector(2.5f, 2.5f, 2.5f), Dark);
	}

	// ───────────────────────── 步兵 ─────────────────────────
	static void BuildInfantry(FB& B, const FString& Id)
	{
		ARSEntity* E = B.E;
		const bool bEng = E->Def->bEngineer;
		const bool bSpy = E->Def->bSpy;
		const float K = 1.25f;   // 步兵整體放大，讓遠景也看得清楚
		FLinearColor Coat(-1, 0, 0);
		if (bSpy) Coat = FLinearColor(0.13f, 0.13f, 0.15f);
		if (Id == TEXT("a_cmdo")) Coat = FLinearColor(0.1f, 0.1f, 0.1f);
		if (Id == TEXT("s_guerrilla")) Coat = FLinearColor(0.3f, 0.36f, 0.2f);
		auto Cloth = [&](FVector L, FVector S, float Shade) -> UStaticMeshComponent*
		{
			if (Coat.R < 0.f) return B.T(nullptr, Cube(), L * K, S * K, Shade);
			return B.P(nullptr, Cube(), L * K, S * K, Shade < 0.f ? RS::Darken(Coat, -Shade) : RS::Lighten(Coat, Shade));
		};
		// 腿與靴子
		// 腿掛在髖關節樞紐上，走路時由 ARSUnit::UpdateVisual 擺動
		E->Limbs.Reset();
		for (int32 S = -1; S <= 1; S += 2)
		{
			USceneComponent* Hip = B.Pivot(nullptr, FVector(0, S * 3.2f, 15) * K);
			B.P(Hip, Cylinder(), FVector(0, 0, -7) * K, FVector(4.6f, 4.6f, 14) * K, Coat.R < 0.f ? RS::Darken(B.Camo, 0.25f) : RS::Darken(Coat, 0.3f));
			B.P(Hip, Cube(), FVector(1.2f, 0, -13.5f) * K, FVector(7, 4.5f, 3) * K, Rubber);
			E->Limbs.Add(Hip);
		}
		// 軀幹、戰術背心、腰帶
		Cloth(FVector(0, 0, 21), FVector(8, 12, 13), -0.05f);
		if (!bSpy) B.P(nullptr, Cube(), FVector(0.6f, 0, 22) * K, FVector(8.6f, 12.4f, 7) * K, B.CamoDark);
		B.P(nullptr, Cube(), FVector(0, 0, 15) * K, FVector(8.4f, 12.4f, 2) * K, Rubber);
		// 肩章（隊伍色辨識）
		B.Mk(nullptr, Cube(), FVector(0, 0, 27.6f) * K, FVector(8, 14, 2.5f) * K, 0.25f);
		// 手臂（往前持槍）
		for (int32 S = -1; S <= 1; S += 2)
			Cloth(FVector(4, S * 6.5f, 21), FVector(10, 3.4f, 3.4f), -0.12f);
		// 頭、頭盔／帽子
		B.P(nullptr, Sphere(), FVector(0, 0, 32.5f) * K, FVector(8.5f, 8.5f, 9) * K, Skin);
		if (bSpy)
		{
			B.P(nullptr, Cylinder(), FVector(0, 0, 37) * K, FVector(9, 9, 2) * K, Rubber);
			B.P(nullptr, Cylinder(), FVector(0, 0, 38.5f) * K, FVector(6.5f, 6.5f, 3.5f) * K, Rubber);
		}
		else
		{
			B.P(nullptr, Sphere(), FVector(-0.3f, 0, 35.5f) * K, FVector(10, 10, 6.5f) * K, bEng ? Warn : (Id == TEXT("s_guerrilla") ? FLinearColor(0.25f, 0.2f, 0.15f) : B.CamoDark));
			if (!bEng && Id != TEXT("s_guerrilla")) B.P(nullptr, Cube(), FVector(4.6f, 0, 36.5f) * K, FVector(2, 4, 2) * K, Rubber);   // 夜視鏡
		}
		// 背包
		if (!bSpy) B.P(nullptr, Cube(), FVector(-5.8f, 0, 22) * K, FVector(4.5f, 9, 10) * K, bEng ? Warn : RS::Darken(Olive, 0.15f));
		// 武器
		if (Id == TEXT("a_rocket") || Id == TEXT("a_jav") || Id == TEXT("s_flak"))
		{
			B.Barrel(nullptr, FVector(2, 5.5f, 31) * K, 30 * K, 5 * K, Id == TEXT("a_jav") ? Olive : FLinearColor(0.25f, 0.28f, 0.2f));
			B.P(nullptr, Cube(), FVector(3, 5.5f, 27) * K, FVector(6, 3, 5) * K, Dark);
		}
		else if (Id == TEXT("s_tesla"))
		{
			B.P(nullptr, Cylinder(), FVector(-7, 0, 26) * K, FVector(9, 9, 18) * K, Metal);
			for (int32 R = 0; R < 3; ++R) B.P(nullptr, Cylinder(), FVector(-7, 0, 20 + R * 5) * K, FVector(11, 11, 1.5f) * K, Cyan);
			B.P(nullptr, Sphere(), FVector(-7, 0, 37) * K, FVector(6, 6, 6) * K, Cyan);
		}
		else if (bEng)
		{
			B.P(nullptr, Cube(), FVector(4, 7.5f, 13) * K, FVector(7, 3, 5) * K, Warn);   // 工具箱
		}
		else if (!bSpy)
		{
			B.Barrel(nullptr, FVector(10, 3.5f, 21) * K, 20 * K, 2.6f * K, Dark);
			B.P(nullptr, Cube(), FVector(8, 3.5f, 18.5f) * K, FVector(2.5f, 1.8f, 4) * K, Dark);   // 彈匣
			if (Id == TEXT("a_cmdo")) B.P(nullptr, Cube(), FVector(-5.8f, 0, 28) * K, FVector(4, 7, 4) * K, FLinearColor(0.8f, 0.15f, 0.1f));  // C4
		}
		E->VisualTop = 50.f;
		E->PickRadius = 24.f;
	}

	// ───────────────────────── 車輛 ─────────────────────────
	static void BuildVehicle(FB& B, const FString& Id)
	{
		ARSEntity* E = B.E;
		const FRSDef& D = *E->Def;
		float L = 76, Wd = 48, Hh = 18;
		if (Id.EndsWith(TEXT("mammoth"))) { L = 98; Wd = 64; Hh = 22; }
		if (Id.EndsWith(TEXT("mcv")) || Id.EndsWith(TEXT("harv"))) { L = 100; Wd = 56; Hh = 26; }
		if (Id == TEXT("s_technical")) { L = 64; Wd = 36; Hh = 14; }
		if (Id == TEXT("s_hwasong") || Id == TEXT("s_iskander")) { L = 108; Wd = 46; Hh = 18; }
		const bool bTruck = Id.Contains(TEXT("himars")) || Id.Contains(TEXT("iskander")) || Id.Contains(TEXT("hwasong")) || Id == TEXT("a_hf")
			|| Id == TEXT("s_technical") || Id.EndsWith(TEXT("_ew")) || Id.EndsWith(TEXT("mcv")) || Id.EndsWith(TEXT("harv"));
		if (bTruck)
		{
			const int32 Ax = (Id.Contains(TEXT("hwasong")) || Id.EndsWith(TEXT("mcv"))) ? 4 : (Id == TEXT("s_technical") ? 2 : 3);
			Wheels(B, L, Wd, Ax, Id == TEXT("s_technical") ? 15.f : 19.f);
			// 底盤大樑與擋泥板
			B.P(nullptr, Cube(), FVector(-L * 0.05f, 0, 15), FVector(L * 0.92f, Wd * 0.62f, 6), Dark);
			for (int32 S = -1; S <= 1; S += 2) B.T(nullptr, Cube(), FVector(-L * 0.05f, S * Wd * 0.46f, 21), FVector(L * 0.84f, 9, 2.5f), -0.25f);
			// 駕駛室：車頭、擋風玻璃、車頂燈
			const float CX = L * 0.36f;
			B.T(nullptr, Cube(), FVector(CX, 0, 26), FVector(L * 0.24f, Wd * 0.9f, 22), -0.1f);
			B.T(nullptr, Cube(), FVector(CX + L * 0.13f, 0, 19), FVector(L * 0.08f, Wd * 0.86f, 10), -0.2f);
			B.P(nullptr, Cube(), FVector(CX + L * 0.115f, 0, 31), FVector(2.5f, Wd * 0.76f, 9), Glass, FRotator(-12, 0, 0));
			for (int32 S = -1; S <= 1; S += 2)
			{
				B.P(nullptr, Cube(), FVector(CX, S * Wd * 0.455f, 30), FVector(L * 0.14f, 2.5f, 7), Glass);
				B.P(nullptr, Sphere(), FVector(CX + L * 0.17f, S * Wd * 0.32f, 19), FVector(4, 4, 4), Lamp);
			}
			B.P(nullptr, Cube(), FVector(CX - 2, 0, 38), FVector(6, Wd * 0.5f, 2.5f), Warn);
			// 貨台
			B.T(nullptr, Cube(), FVector(-L * 0.14f, 0, 21), FVector(L * 0.66f, Wd * 0.88f, 6), -0.35f);
			B.Mk(nullptr, Cube(), FVector(CX, 0, 37.5f), FVector(L * 0.14f, Wd * 0.5f, 1), 0.f);
			B.Blotch(nullptr, FVector(CX, 0, 37.2f), L * 0.24f, Wd * 0.9f, GetTypeHash(Id) + 3);
		}
		else
		{
			Tracks(B, L, Wd);
			// 車體：下車體、上甲板、前傾斜裝甲、後方引擎蓋與散熱格柵
			B.T(nullptr, Cube(), FVector(-L * 0.02f, 0, 15 + Hh * 0.25f), FVector(L * 0.82f, Wd * 0.64f, Hh * 0.9f), -0.3f);
			B.T(nullptr, Cube(), FVector(-L * 0.04f, 0, 15 + Hh * 0.72f), FVector(L * 0.78f, Wd * 0.86f, 4), -0.15f);
			B.Blotch(nullptr, FVector(-L * 0.04f, 0, 17 + Hh * 0.72f), L * 0.78f, Wd * 0.86f, GetTypeHash(Id));
			B.Mk(nullptr, Cube(), FVector(L * 0.28f, 0, 17.6f + Hh * 0.72f), FVector(4, Wd * 0.7f, 0.8f), 0.f);   // 車首識別帶
			B.T(nullptr, Cube(), FVector(L * 0.4f, 0, 14 + Hh * 0.35f), FVector(L * 0.16f, Wd * 0.84f, Hh * 0.75f), -0.32f, FRotator(-28, 0, 0));
			for (int32 G = 0; G < 4; ++G)
				B.P(nullptr, Cube(), FVector(-L * 0.36f + G * 4.5f, 0, 18 + Hh * 0.75f), FVector(2.2f, Wd * 0.5f, 1.2f), Rubber);
			for (int32 S = -1; S <= 1; S += 2)
			{
				B.P(nullptr, Sphere(), FVector(L * 0.47f, S * Wd * 0.3f, 14 + Hh * 0.45f), FVector(4.5f, 4.5f, 4.5f), Lamp);
				B.P(nullptr, Cylinder(), FVector(-L * 0.47f, S * Wd * 0.25f, 15 + Hh * 0.5f), FVector(4, 4, 6), Dark, FRotator(90, 0, 0));   // 排氣管
				B.P(nullptr, Cube(), FVector(-L * 0.15f, S * Wd * 0.39f, 19 + Hh * 0.75f), FVector(L * 0.3f, 6, 5), Olive);              // 工具箱
			}
		}
		const float TZ = bTruck ? 28.f : 17.f + Hh * 0.8f;
		USceneComponent* Tp = B.Pivot(nullptr, FVector(bTruck ? -L * 0.12f : -L * 0.06f, 0, TZ));
		E->TurretPivot = Tp;
		if (Id.EndsWith(TEXT("harv")))
		{
			// 礦斗、礦石、前方鏟斗
			B.P(nullptr, Cube(), FVector(-L * 0.14f, 0, 36), FVector(L * 0.6f, Wd * 0.82f, 26), RS::Darken(Sand, 0.2f));
			B.P(nullptr, Cube(), FVector(-L * 0.14f, 0, 50), FVector(L * 0.52f, Wd * 0.66f, 5), Gold);
			for (int32 K2 = 0; K2 < 3; ++K2) B.P(nullptr, Sphere(), FVector(-L * 0.3f + K2 * L * 0.14f, (K2 - 1) * 6.f, 53), FVector(14, 14, 10), Gold);
			B.T(nullptr, Cube(), FVector(-L * 0.14f, 0, 31), FVector(L * 0.62f, Wd * 0.86f, 3), 0.f);
			B.P(nullptr, Cube(), FVector(L * 0.52f, 0, 10), FVector(10, Wd * 0.95f, 12), Steel, FRotator(20, 0, 0));
			for (int32 T2 = 0; T2 < 5; ++T2) B.P(nullptr, Cone(), FVector(L * 0.57f, -Wd * 0.38f + T2 * Wd * 0.19f, 6), FVector(5, 5, 8), Metal, FRotator(-90, 0, 0));
		}
		else if (Id.EndsWith(TEXT("mcv")))
		{
			// 摺疊式基地模組：主艙、天線、吊臂、警示燈
			B.T(nullptr, Cube(), FVector(-L * 0.14f, 0, 40), FVector(L * 0.62f, Wd * 0.86f, 32), 0.f);
			B.P(nullptr, Cube(), FVector(-L * 0.14f, 0, 57), FVector(L * 0.64f, Wd * 0.88f, 3), Metal);
			WinRow(B, nullptr, 1, Wd * 0.435f, L * 0.5f, 42, 3, 10, 7);
			B.P(nullptr, Cylinder(), FVector(-L * 0.36f, Wd * 0.25f, 72), FVector(7, 7, 30), Gold);
			B.P(nullptr, Cube(), FVector(-L * 0.18f, Wd * 0.25f, 87), FVector(54, 5, 5), Gold);
			B.P(nullptr, Cube(), FVector(-L * 0.36f, -Wd * 0.25f, 61), FVector(16, 16, 6), Steel);
			B.P(nullptr, Sphere(), FVector(-L * 0.36f, -Wd * 0.25f, 66), FVector(6, 6, 6), Warn);
			Antenna(B, nullptr, FVector(-L * 0.42f, 0, 58), 28);
		}
		else if (Id.EndsWith(TEXT("_ew")))
		{
			B.P(nullptr, Cube(), FVector(-L * 0.14f, 0, 33), FVector(L * 0.52f, Wd * 0.82f, 22), Metal);
			WinRow(B, nullptr, 1, Wd * 0.415f, L * 0.4f, 34, 2, 10, 6, FLinearColor(0.2f, 0.9f, 0.5f));
			USceneComponent* Sp = B.Pivot(nullptr, FVector(-L * 0.16f, 0, 52));
			B.P(Sp, Cylinder(), FVector(0, 0, 0), FVector(4, 4, 18), Dark);
			B.P(Sp, Sphere(), FVector(0, 0, 12), FVector(36, 36, 8), FLinearColor(0.88f, 0.88f, 0.88f), FRotator(0, 0, 25));
			B.P(Sp, Cylinder(), FVector(6, 0, 18), FVector(2, 2, 10), Dark, FRotator(25, 0, 0));
			E->Spinner = Sp;
			Antenna(B, nullptr, FVector(-L * 0.4f, Wd * 0.3f, 44), 34);
			Antenna(B, nullptr, FVector(-L * 0.4f, -Wd * 0.3f, 44), 26);
		}
		else if (Id.Contains(TEXT("iskander")) || Id.Contains(TEXT("hwasong")))
		{
			const float Ms = Id.Contains(TEXT("hwasong")) ? 1.35f : 1.f;
			const FLinearColor Mis(0.82f, 0.82f, 0.78f);
			B.P(nullptr, Cube(), FVector(-L * 0.12f, 0, 28), FVector(L * 0.66f, Wd * 0.5f, 6), Metal);   // 發射架
			B.P(nullptr, Cylinder(), FVector(-L * 0.12f, 0, 38), FVector(14 * Ms, 14 * Ms, L * 0.68f), Mis, FRotator(-82, 0, 0));
			B.P(nullptr, Cone(), FVector(L * 0.26f, 0, 41), FVector(14 * Ms, 14 * Ms, 20), Mis, FRotator(-82, 0, 0));
			for (int32 R = 0; R < 3; ++R) B.P(nullptr, Cylinder(), FVector(-L * 0.36f + R * L * 0.22f, 0, 37 + R * 1.2f), FVector(15 * Ms, 15 * Ms, 2), FLinearColor(0.75f, 0.15f, 0.1f), FRotator(-82, 0, 0));
			for (int32 S = -1; S <= 1; S += 2) B.P(nullptr, Cube(), FVector(-L * 0.44f, S * 8 * Ms, 36), FVector(8, 2, 12 * Ms), Dark, FRotator(-82, 0, 0));
		}
		else if (Id == TEXT("s_arty"))
		{
			B.T(nullptr, Cube(), FVector(-L * 0.12f, 0, 32), FVector(L * 0.45f, Wd * 0.72f, 16), -0.1f);
			Gun(B, nullptr, FVector(L * 0.05f, 0, 38), 58, 7, 22.f);
			for (int32 S = -1; S <= 1; S += 2) B.P(nullptr, Cube(), FVector(-L * 0.5f, S * Wd * 0.3f, 8), FVector(18, 5, 4), Metal, FRotator(-30, 0, 0));  // 駐鋤
		}
		if (D.bTurret)
		{
			if (Id.Contains(TEXT("himars")) || Id == TEXT("s_tos"))
			{
				// 火箭發射箱：可見的發射管
				B.T(Tp, Cube(), FVector(0, 0, 10), FVector(L * 0.5f, Wd * 0.72f, 20), 0.f, FRotator(18, 0, 0));
				const float Fx = L * 0.25f + 1.f;
				for (int32 R = 0; R < 2; ++R)
					for (int32 C = 0; C < 3; ++C)
						B.P(Tp, Cylinder(), FVector(Fx + R * 1.5f, -Wd * 0.22f + C * Wd * 0.22f, 12 + R * 7 + Fx * 0.32f), FVector(6, 6, 2), Rubber, FRotator(-72, 0, 0));
				B.P(Tp, Cube(), FVector(-L * 0.18f, 0, 2), FVector(10, Wd * 0.4f, 6), Metal);
			}
			else if (Id == TEXT("a_beam"))
			{
				B.T(Tp, Cube(), FVector(0, 0, 5), FVector(30, 26, 11), 0.f);
				B.P(Tp, Cylinder(), FVector(0, 0, 13), FVector(8, 8, 10), Metal);
				B.P(Tp, Sphere(), FVector(0, 0, 22), FVector(22, 22, 24), Cyan);
				for (int32 R = 0; R < 2; ++R) B.P(Tp, Cylinder(), FVector(0, 0, 16 + R * 9), FVector(26, 26, 1.5f), Steel);
			}
			else if (Id == TEXT("a_ifv") || Id == TEXT("s_flaktrk"))
			{
				B.T(Tp, Cube(), FVector(0, 0, 6), FVector(28, 26, 12), 0.f);
				for (int32 S = -1; S <= 1; S += 2)
				{
					B.P(Tp, Cube(), FVector(6, S * 15, 13), FVector(24, 8, 9), Metal, FRotator(15, 0, 0));
					B.Barrel(Tp, FVector(22, S * 15, 17), 14, 3, Dark, 15.f);
				}
				Antenna(B, Tp, FVector(-10, 8, 10), 22);
			}
			else if (Id == TEXT("s_technical"))
			{
				B.P(Tp, Cylinder(), FVector(0, 0, 6), FVector(5, 5, 12), Dark);
				B.P(Tp, Cube(), FVector(6, 0, 14), FVector(4, 14, 9), Metal);   // 防盾
				B.Barrel(Tp, FVector(16, 0, 13), 28, 4, Dark);
			}
			else if (Id == TEXT("a_k9"))
			{
				B.T(Tp, Cube(), FVector(-8, 0, 9), FVector(44, 36, 20), 0.f);
				B.T(Tp, Cube(), FVector(14, 0, 9), FVector(8, 30, 16), -0.2f, FRotator(-20, 0, 0));
				Gun(B, Tp, FVector(16, 0, 12), 54, 6.5f, 8.f);
				B.P(Tp, Cylinder(), FVector(-14, 9, 21), FVector(8, 8, 4), Metal);
				Antenna(B, Tp, FVector(-26, -12, 18), 24);
			}
			else
			{
				const float TS = Id.EndsWith(TEXT("mammoth")) ? 1.3f : 1.f;
				// 砲塔：底座、楔形正面、尾艙、指揮塔、天線
				B.T(Tp, Cube(), FVector(-3 * TS, 0, 6 * TS), FVector(30 * TS, 28 * TS, 12 * TS), 0.08f);
				B.T(Tp, Cube(), FVector(13 * TS, 0, 6 * TS), FVector(10 * TS, 24 * TS, 10 * TS), -0.05f, FRotator(-22, 0, 0));
				B.T(Tp, Cube(), FVector(-19 * TS, 0, 7 * TS), FVector(10 * TS, 24 * TS, 10 * TS), -0.18f);
				B.P(Tp, Cube(), FVector(16 * TS, 0, 7 * TS), FVector(6 * TS, 10 * TS, 8 * TS), Metal);   // 砲盾
				B.P(Tp, Cylinder(), FVector(-6 * TS, 7 * TS, 14 * TS), FVector(9 * TS, 9 * TS, 4 * TS), Metal);
				B.P(Tp, Cylinder(), FVector(-6 * TS, 7 * TS, 16.5f * TS), FVector(7 * TS, 7 * TS, 1.5f), Dark);
				B.P(Tp, Cube(), FVector(-2 * TS, -8 * TS, 13.5f * TS), FVector(6 * TS, 5 * TS, 3 * TS), Glass);   // 瞄準鏡
				B.Blotch(Tp, FVector(-3 * TS, 0, 12 * TS), 30 * TS, 28 * TS, GetTypeHash(Id) + 7);
				B.Mk(Tp, Cube(), FVector(-12 * TS, 0, 12.6f * TS), FVector(8 * TS, 14 * TS, 0.8f), 0.f);   // 砲塔頂識別板
				// 遙控武器站＋主動防禦雷達面板（現代化套件）
				B.P(Tp, Cube(), FVector(4 * TS, 9 * TS, 15 * TS), FVector(7 * TS, 6 * TS, 5 * TS), RS::Darken(B.Camo, 0.35f));
				B.Barrel(Tp, FVector(10 * TS, 9 * TS, 16 * TS), 10 * TS, 1.5f, Dark);
				B.P(Tp, Cube(), FVector(1 * TS, 9 * TS, 16 * TS), FVector(2.5f, 2.5f, 2), Cyan);
				for (int32 S2 = -1; S2 <= 1; S2 += 2)
				{
					B.P(Tp, Cube(), FVector(4 * TS, S2 * 14.5f * TS, 8 * TS), FVector(6 * TS, 1.2f, 5 * TS), Rubber, FRotator(0, S2 * 20.f, 0));
					B.P(Tp, Cube(), FVector(-14 * TS, S2 * 13 * TS, 9 * TS), FVector(5 * TS, 3, 3 * TS), Metal);   // 煙幕彈發射器
				}
				Antenna(B, Tp, FVector(-20 * TS, -9 * TS, 12 * TS), 26);
				if (Id.EndsWith(TEXT("mammoth")) || Id == TEXT("s_bmpt"))
				{
					Gun(B, Tp, FVector(18 * TS, 6 * TS, 8 * TS), 42 * TS, 5);
					Gun(B, Tp, FVector(18 * TS, -6 * TS, 8 * TS), 42 * TS, 5);
					if (Id.EndsWith(TEXT("mammoth")))
						for (int32 S = -1; S <= 1; S += 2)
						{
							B.P(Tp, Cube(), FVector(-6 * TS, S * 17 * TS, 9 * TS), FVector(18 * TS, 7 * TS, 8 * TS), Metal);   // 側掛飛彈莢艙
							for (int32 R = 0; R < 2; ++R) B.P(Tp, Cylinder(), FVector(3.5f * TS, S * 17 * TS, (7.5f + R * 3.5f) * TS), FVector(2.8f, 2.8f, 1), FLinearColor(0.75f, 0.15f, 0.1f), FRotator(-90, 0, 0));
						}
				}
				else
				{
					Gun(B, Tp, FVector(18 * TS, 0, 8 * TS), 44 * TS, 5.5f);
				}
			}
		}
		E->VisualTop = 58.f;
		E->PickRadius = FMath::Max(L, Wd) * 0.55f;
	}

	// ───────────────────────── 艦艇 ─────────────────────────
	static void BuildShip(FB& B, const FString& Id)
	{
		ARSEntity* E = B.E;
		float L = 120, Wd = 34;
		if (Id.Contains(TEXT("cvn"))) { L = 200; Wd = 56; }
		else if (Id == TEXT("a_carrier") || Id == TEXT("s_dread")) { L = 150; Wd = 40; }
		else if (Id == TEXT("s_fastboat")) { L = 60; Wd = 22; }
		else if (Id == TEXT("a_seadrone")) { L = 40; Wd = 14; }
		else if (Id == TEXT("a_tuojiang")) { L = 90; Wd = 40; }
		const FLinearColor Hull(0.42f, 0.45f, 0.48f);
		const FLinearColor HullD(0.3f, 0.32f, 0.35f);
		if (Id == TEXT("s_sub"))
		{
			B.T(nullptr, Cylinder(), FVector(0, 0, -4), FVector(28, 28, 130), -0.5f, FRotator(-90, 0, 0));
			B.T(nullptr, Sphere(), FVector(64, 0, -4), FVector(30, 28, 28), -0.5f);
			B.P(nullptr, Cube(), FVector(14, 0, 16), FVector(26, 9, 22), Dark);
			B.P(nullptr, Cube(), FVector(14, 0, 22), FVector(6, 30, 2), Dark);
			B.P(nullptr, Cube(), FVector(-62, 0, 2), FVector(6, 4, 22), Dark);
			Antenna(B, nullptr, FVector(10, 0, 27), 14);
			E->VisualTop = 40.f;
			E->PickRadius = 60.f;
			E->TurretPivot = B.Pivot(nullptr, FVector(0, 0, 10));
			return;
		}
		// 船殼：下層深色、上層、船艏、吃水線
		B.P(nullptr, Cube(), FVector(0, 0, 0), FVector(L, Wd * 0.88f, 10), HullD);
		B.P(nullptr, Cube(), FVector(0, 0, 8), FVector(L, Wd, 8), Hull);
		B.P(nullptr, Cone(), FVector(L * 0.5f + 9, 0, 4), FVector(Wd, 16, 22), Hull, FRotator(-90, 0, 0));
		B.P(nullptr, Cube(), FVector(0, 0, -3), FVector(L * 1.01f, Wd * 0.9f, 2), FLinearColor(0.55f, 0.12f, 0.1f));
		B.Mk(nullptr, Cube(), FVector(0, 0, 12.5f), FVector(L * 0.98f, Wd * 1.02f, 2), -0.1f);
		if (Id.Contains(TEXT("cvn")) || Id == TEXT("a_carrier"))
		{
			// 飛行甲板、跑道標線、艦島
			B.P(nullptr, Cube(), FVector(0, 0, 16), FVector(L * 1.02f, Wd * 1.25f, 4), FLinearColor(0.22f, 0.23f, 0.25f));
			for (int32 K = 0; K < 8; ++K) B.P(nullptr, Cube(), FVector(-L * 0.42f + K * L * 0.12f, -Wd * 0.1f, 18.3f), FVector(L * 0.06f, 2, 0.6f), FLinearColor(0.9f, 0.9f, 0.85f));
			B.P(nullptr, Cube(), FVector(L * 0.1f, -Wd * 0.1f, 18.3f), FVector(L * 0.9f, 1.2f, 0.6f), Warn, FRotator(0, -6, 0));
			B.P(nullptr, Cube(), FVector(10, Wd * 0.48f, 38), FVector(32, 12, 40), Hull);
			WinRow(B, nullptr, 0, 26.5f, 10, 52, 1, 8, 4);
			B.P(nullptr, Cylinder(), FVector(10, Wd * 0.48f, 66), FVector(3, 3, 18), Dark);
			USceneComponent* Sp = B.Pivot(nullptr, FVector(10, Wd * 0.48f, 74));
			B.P(Sp, Cube(), FVector::ZeroVector, FVector(22, 3, 5), Dark);
			E->Spinner = Sp;
			for (int32 K = 0; K < 3; ++K) B.P(nullptr, Cube(), FVector(-L * 0.3f + K * 26.f, -Wd * 0.3f, 22), FVector(16, 14, 5), FLinearColor(0.35f, 0.4f, 0.45f));   // 停放機
		}
		else
		{
			// 上層建築、艦橋窗、煙囪、桅桿與雷達
			B.P(nullptr, Cube(), FVector(-L * 0.08f, 0, 24), FVector(L * 0.3f, Wd * 0.66f, 22), Hull);
			B.P(nullptr, Cube(), FVector(-L * 0.02f, 0, 38), FVector(L * 0.14f, Wd * 0.5f, 10), Hull);
			WinRow(B, nullptr, 0, -L * 0.02f + L * 0.07f + 0.5f, Wd * 0.4f, 39, 3, 5, 4);
			B.P(nullptr, Cylinder(), FVector(-L * 0.24f, 0, 34), FVector(10, 14, 22), HullD);
			B.P(nullptr, Cylinder(), FVector(-L * 0.24f, 0, 45.5f), FVector(9, 12, 1.5f), Rubber);
			B.P(nullptr, Cylinder(), FVector(-L * 0.08f, 0, 56), FVector(3, 3, 30), Dark);
			B.P(nullptr, Cube(), FVector(-L * 0.08f, 0, 64), FVector(3, 18, 2), Dark);
			USceneComponent* Sp = B.Pivot(nullptr, FVector(-L * 0.08f, 0, 72));
			B.P(Sp, Cube(), FVector::ZeroVector, FVector(4, 20, 6), Steel);
			E->Spinner = Sp;
			if (Id == TEXT("s_dread") || Id == TEXT("a_destroyer"))
			{
				// 垂直發射格
				for (int32 R = 0; R < 2; ++R)
					for (int32 C = 0; C < 4; ++C)
						B.P(nullptr, Cube(), FVector(-L * 0.38f + R * 7.f, -Wd * 0.22f + C * Wd * 0.146f, 13.5f), FVector(5.5f, 5.5f, 1), Rubber);
			}
		}
		USceneComponent* Tp = B.Pivot(nullptr, FVector(L * 0.3f, 0, 20));
		E->TurretPivot = Tp;
		if (!Id.Contains(TEXT("cvn")) && Id != TEXT("a_carrier") && Id != TEXT("a_seadrone"))
		{
			B.T(Tp, Cylinder(), FVector(0, 0, 4), FVector(18, 18, 9), 0.f);
			B.T(Tp, Cube(), FVector(4, 0, 9), FVector(14, 14, 7), -0.1f, FRotator(-10, 0, 0));
			Gun(B, Tp, FVector(10, 0, 8), 28, 4);
		}
		E->VisualTop = 60.f;
		E->PickRadius = L * 0.45f;
	}

	// ───────────────────────── 空中單位 ─────────────────────────
	static void BuildAir(FB& B, const FString& Id)
	{
		ARSEntity* E = B.E;
		const FRSDef& D = *E->Def;
		if (D.bAirship)
		{
			B.T(nullptr, Sphere(), FVector(0, 0, 0), FVector(170, 70, 64), -0.2f);
			for (int32 R = 0; R < 3; ++R) B.P(nullptr, Cylinder(), FVector(-50 + R * 50, 0, 0), FVector(6, 72, 66), Steel, FRotator(0, 0, 0));
			B.P(nullptr, Cube(), FVector(0, 0, -40), FVector(56, 20, 14), Dark);
			WinRow(B, nullptr, 1, 10.5f, 46, -40, 4, 7, 5);
			for (int32 S = -1; S <= 1; S += 2) B.Mk(nullptr, Cube(), FVector(-80, S * 24, 0), FVector(28, 4, 42), 0.1f);
			B.Mk(nullptr, Cube(), FVector(-80, 0, 24), FVector(28, 4, 30), 0.1f);
			E->VisualTop = 50.f;
			E->PickRadius = 90.f;
			return;
		}
		if (D.bHeli)
		{
			B.T(nullptr, Sphere(), FVector(0, 0, 0), FVector(60, 24, 24), -0.2f);
			B.P(nullptr, Sphere(), FVector(16, 0, 6), FVector(22, 14, 12), Glass);
			B.Barrel(nullptr, FVector(-44, 0, 2), 50, 7, RS::Darken(B.Team, 0.3f));
			B.P(nullptr, Cube(), FVector(-68, 0, 10), FVector(12, 3, 20), Dark);
			B.P(nullptr, Cylinder(), FVector(-68, 3, 14), FVector(20, 20, 1), Dark, FRotator(0, 0, 90));
			for (int32 S = -1; S <= 1; S += 2)
			{
				B.P(nullptr, Cube(), FVector(-2, S * 18, -2), FVector(14, 20, 3), Dark);                                    // 短翼
				B.P(nullptr, Cylinder(), FVector(0, S * 26, -6), FVector(6, 6, 18), Olive, FRotator(-90, 0, 0));            // 火箭莢艙
				B.P(nullptr, Cube(), FVector(6, S * 9, -15), FVector(36, 2, 2), Dark);                                     // 起落滑橇
			}
			B.Barrel(nullptr, FVector(34, 0, -10), 16, 3, Dark);
			USceneComponent* Sp = B.Pivot(nullptr, FVector(0, 0, 18));
			B.P(nullptr, Cylinder(), FVector(0, 0, 14), FVector(6, 6, 8), Dark);
			B.P(Sp, Cube(), FVector::ZeroVector, FVector(124, 5, 1.5f), Dark, FRotator::ZeroRotator);
			B.P(Sp, Cube(), FVector::ZeroVector, FVector(5, 124, 1.5f), Dark, FRotator::ZeroRotator);
			E->Spinner = Sp;
			E->VisualTop = 30.f;
			E->PickRadius = 50.f;
			return;
		}
		if (D.bJet)
		{
			const FLinearColor Fus = RS::Darken(B.Team, 0.3f);
			B.Barrel(nullptr, FVector(0, 0, 0), 84, 14, Fus);
			B.P(nullptr, Cone(), FVector(48, 0, 0), FVector(14, 14, 18), Metal, FRotator(-90, 0, 0));
			B.P(nullptr, Sphere(), FVector(22, 0, 7), FVector(20, 9, 8), Glass);
			B.T(nullptr, Cube(), FVector(-6, 0, 0), FVector(32, 80, 2.5f), 0.f, FRotator(0, 0, 0));
			for (int32 S = -1; S <= 1; S += 2)
			{
				B.P(nullptr, Cube(), FVector(-2, S * 40, 0), FVector(18, 2.5f, 3), Warn);   // 翼尖
				B.P(nullptr, Cylinder(), FVector(-4, S * 24, -5), FVector(4, 4, 18), FLinearColor(0.85f, 0.85f, 0.82f), FRotator(-90, 0, 0));   // 飛彈
				B.P(nullptr, Cube(), FVector(-38, S * 7, 12), FVector(12, 2, 16), Metal, FRotator(0, 0, S * 15.f));   // 雙垂尾
			}
			B.T(nullptr, Cube(), FVector(-36, 0, 3), FVector(14, 32, 2), 0.f);
			B.P(nullptr, Cylinder(), FVector(-44, 0, 0), FVector(11, 11, 3), Rubber, FRotator(-90, 0, 0));
			B.P(nullptr, Cylinder(), FVector(-46, 0, 0), FVector(8, 8, 2), FLinearColor(1.f, 0.55f, 0.2f), FRotator(-90, 0, 0));   // 尾焰
			E->VisualTop = 20.f;
			E->PickRadius = 45.f;
			return;
		}
		// 無人機
		const float Sc = Id == TEXT("a_tb2") ? 1.2f : (Id == TEXT("s_shahed") ? 1.f : 0.6f);
		if (Id == TEXT("s_shahed"))
		{
			B.T(nullptr, Cone(), FVector(0, 0, 0), FVector(50, 50, 4), -0.4f, FRotator(0, 0, 0));
			B.Barrel(nullptr, FVector(0, 0, 3), 42, 7, Metal);
			B.P(nullptr, Cylinder(), FVector(-22, 0, 3), FVector(12, 12, 1), Dark, FRotator(-90, 0, 0));
			for (int32 S = -1; S <= 1; S += 2) B.P(nullptr, Cube(), FVector(-18, S * 22, 5), FVector(8, 1.5f, 8), Dark);
		}
		else if (Id == TEXT("a_fpv"))
		{
			B.P(nullptr, Cube(), FVector(0, 0, 0), FVector(30, 4, 3), Dark, FRotator(0, 45, 0));
			B.P(nullptr, Cube(), FVector(0, 0, 0), FVector(30, 4, 3), Dark, FRotator(0, -45, 0));
			B.T(nullptr, Sphere(), FVector(0, 0, 2), FVector(10, 10, 6), 0.f);
			for (int32 K = 0; K < 4; ++K)
			{
				const float A = FMath::DegreesToRadians(45.f + K * 90.f);
				B.P(nullptr, Cylinder(), FVector(FMath::Cos(A) * 11, FMath::Sin(A) * 11, 3), FVector(10, 10, 0.8f), FLinearColor(0.6f, 0.6f, 0.6f));
			}
			B.P(nullptr, Cube(), FVector(4, 0, -3), FVector(8, 4, 4), FLinearColor(0.8f, 0.15f, 0.1f));
		}
		else
		{
			B.Barrel(nullptr, FVector(0, 0, 0), 46 * Sc, 8 * Sc, RS::Darken(B.Team, 0.3f));
			B.P(nullptr, Sphere(), FVector(20 * Sc, 0, -2), FVector(10 * Sc, 8 * Sc, 8 * Sc), Glass);   // 光電球
			B.T(nullptr, Cube(), FVector(4 * Sc, 0, 2), FVector(10 * Sc, 80 * Sc, 2), 0.f);
			for (int32 S = -1; S <= 1; S += 2) B.P(nullptr, Cube(), FVector(-20 * Sc, S * 8 * Sc, 6), FVector(8 * Sc, 1.5f, 10 * Sc), Metal, FRotator(0, 0, S * 40.f));  // V 尾
			B.P(nullptr, Cylinder(), FVector(-25 * Sc, 0, 0), FVector(12 * Sc, 12 * Sc, 0.8f), Dark, FRotator(-90, 0, 0));
		}
		E->VisualTop = 15.f;
		E->PickRadius = 34.f;
	}

	// ───────────────────────── 建築 ─────────────────────────
	static void BuildBuilding(FB& B, ARSBuilding* Bd)
	{
		ARSEntity* E = B.E;
		const FRSDef& D = *E->Def;
		const FString& Id = E->TypeId;
		const float SX = Bd->Size.X * RS_CELL, SY = Bd->Size.Y * RS_CELL;
		const FString& Role = D.Role;
		const bool bSov = D.Faction == TEXT("soviet");
		const FLinearColor Wall = bSov ? FLinearColor(0.56f, 0.5f, 0.44f) : FLinearColor(0.68f, 0.7f, 0.72f);
		const FLinearColor WallD = RS::Darken(Wall, 0.28f);
		if (!D.bWall && Role != TEXT("def") && !D.bNaval && Role != TEXT("oil"))
		{
			// 地基：水泥台、深色邊、黃黑警示角
			B.P(nullptr, Cube(), FVector(0, 0, 3), FVector(SX - 4, SY - 4, 6), Concrete);
			B.P(nullptr, Cube(), FVector(0, 0, 1), FVector(SX, SY, 2), RS::Darken(Concrete, 0.35f));
			if (!D.bCiv)
				for (int32 C = 0; C < 4; ++C)
					B.P(nullptr, Cube(), FVector((C % 2 ? 1 : -1) * (SX * 0.5f - 7), (C / 2 ? 1 : -1) * (SY * 0.5f - 7), 6.5f), FVector(8, 8, 1), Warn);
		}
		float Top = 80.f;
		if (D.bCiv)
		{
			const FLinearColor Civ = Bd->VarFrame == 0 ? FLinearColor(0.78f, 0.7f, 0.56f) : FLinearColor(0.66f, 0.64f, 0.66f);
			if (Id == TEXT("civ_office"))
			{
				B.P(nullptr, Cube(), FVector(0, 0, 90), FVector(SX * 0.8f, SY * 0.8f, 180), FLinearColor(0.45f, 0.5f, 0.56f));
				for (int32 K = 0; K < 6; ++K)
				{
					B.P(nullptr, Cube(), FVector(0, 0, 22 + K * 28), FVector(SX * 0.81f, SY * 0.81f, 12), Glass);
				}
				B.P(nullptr, Cube(), FVector(0, 0, 182), FVector(SX * 0.84f, SY * 0.84f, 5), Concrete);
				RoofBox(B, FVector(-15, -10, 188), 22);
				B.P(nullptr, Cylinder(), FVector(20, 20, 200), FVector(3, 3, 30), Steel);
				Top = 190.f;
			}
			else if (Id == TEXT("civ_gas"))
			{
				B.P(nullptr, Cube(), FVector(-SX * 0.25f, 0, 25), FVector(SX * 0.35f, SY * 0.7f, 50), FLinearColor(0.85f, 0.85f, 0.82f));
				WinRow(B, nullptr, 0, -SX * 0.25f + SX * 0.175f + 0.5f, SY * 0.6f, 22, 2, 30, 22);
				B.P(nullptr, Cube(), FVector(SX * 0.18f, 0, 56), FVector(SX * 0.55f, SY * 0.8f, 6), FLinearColor(0.85f, 0.2f, 0.15f));
				for (int32 S = -1; S <= 1; S += 2)
				{
					B.P(nullptr, Cylinder(), FVector(SX * 0.18f, S * SY * 0.25f, 28), FVector(6, 6, 56), Metal);
					B.P(nullptr, Cube(), FVector(SX * 0.18f, S * SY * 0.12f, 14), FVector(10, 8, 22), FLinearColor(0.9f, 0.9f, 0.9f));   // 加油機
				}
				Top = 60.f;
			}
			else if (Id == TEXT("civ_tower"))
			{
				for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cylinder(), FVector((K % 2 ? 1 : -1) * 20, (K / 2 ? 1 : -1) * 20, 50), FVector(5, 5, 100), Metal);
				for (int32 K = 0; K < 3; ++K) B.P(nullptr, Cube(), FVector(0, 0, 25 + K * 30), FVector(44, 44, 2), Metal);
				B.P(nullptr, Cylinder(), FVector(0, 0, 120), FVector(70, 70, 50), FLinearColor(0.72f, 0.72f, 0.7f));
				B.P(nullptr, Cylinder(), FVector(0, 0, 124), FVector(71, 71, 8), FLinearColor(0.3f, 0.45f, 0.7f));
				Top = 150.f;
			}
			else if (Id == TEXT("civ_barn"))
			{
				B.P(nullptr, Cube(), FVector(0, 0, 35), FVector(SX * 0.8f, SY * 0.6f, 70), FLinearColor(0.6f, 0.15f, 0.1f));
				B.P(nullptr, Cube(), FVector(SX * 0.4f + 1, 0, 26), FVector(2, SY * 0.3f, 44), FLinearColor(0.9f, 0.88f, 0.82f));
				B.P(nullptr, Cube(), FVector(0, 0, 75), FVector(SX * 0.82f, SY * 0.45f, SY * 0.45f), FLinearColor(0.3f, 0.25f, 0.22f), FRotator(0, 0, 45));
				B.P(nullptr, Cylinder(), FVector(-SX * 0.3f, -SY * 0.38f, 55), FVector(30, 30, 110), FLinearColor(0.75f, 0.75f, 0.72f));
				B.P(nullptr, Sphere(), FVector(-SX * 0.3f, -SY * 0.38f, 110), FVector(30, 30, 20), FLinearColor(0.55f, 0.55f, 0.52f));
				Top = 120.f;
			}
			else
			{
				const float Hb = Id == TEXT("civ_shop") ? 55.f : 60.f;
				B.P(nullptr, Cube(), FVector(0, 0, Hb * 0.5f), FVector(SX * 0.78f, SY * 0.7f, Hb), Civ);
				WinRow(B, nullptr, 0, SX * 0.39f + 0.5f, SY * 0.55f, Hb * 0.55f, 3, 18, 16);
				WinRow(B, nullptr, 1, SY * 0.35f + 0.5f, SX * 0.6f, Hb * 0.55f, 3, 18, 16);
				B.P(nullptr, Cube(), FVector(0, 0, Hb + 10), FVector(SX * 0.8f, SY * 0.5f, SY * 0.5f), Roof, FRotator(0, 0, 45));
				B.P(nullptr, Cube(), FVector(-SX * 0.2f, SY * 0.15f, Hb + 34), FVector(10, 10, 26), RS::Darken(Roof, 0.2f));   // 煙囪
				if (Id == TEXT("civ_shop"))
				{
					B.P(nullptr, Cube(), FVector(SX * 0.4f, 0, 20), FVector(4, SY * 0.6f, 30), Glass);
					B.P(nullptr, Cube(), FVector(SX * 0.45f, 0, 40), FVector(14, SY * 0.66f, 3), FLinearColor(0.2f, 0.55f, 0.35f), FRotator(15, 0, 0));
				}
				Top = Hb + 40.f;
			}
		}
		else if (Role == TEXT("oil"))
		{
			B.P(nullptr, Cube(), FVector(0, 0, 4), FVector(SX - 20, SY - 20, 8), FLinearColor(0.2f, 0.2f, 0.18f));
			// 鑽油井架
			for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cylinder(), FVector(-20 + (K % 2 ? 12 : -12), -20 + (K / 2 ? 12 : -12), 70), FVector(3, 3, 140), Metal, FRotator((K / 2 ? -5.f : 5.f), 0, (K % 2 ? 5.f : -5.f)));
			for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cube(), FVector(-20, -20, 30 + K * 30), FVector(26 - K * 3, 26 - K * 3, 2), Metal);
			B.P(nullptr, Sphere(), FVector(-20, -20, 142), FVector(6, 6, 6), FLinearColor(1.f, 0.3f, 0.2f));
			// 抽油機
			USceneComponent* Sp = B.Pivot(nullptr, FVector(30, 25, 40));
			B.P(Sp, Cube(), FVector::ZeroVector, FVector(80, 10, 10), Dark);
			B.P(Sp, Cube(), FVector(40, 0, -6), FVector(10, 12, 18), Dark);
			B.P(nullptr, Cylinder(), FVector(30, 25, 20), FVector(8, 8, 40), Dark);
			B.T(nullptr, Cylinder(), FVector(30, -30, 18), FVector(30, 30, 36), 0.f);
			B.P(nullptr, Cylinder(), FVector(30, -30, 37), FVector(30, 30, 2), Steel);
			E->Spinner = Sp;
			Top = 140.f;
		}
		else if (D.bWall)
		{
			B.T(nullptr, Cube(), FVector(0, 0, 22), FVector(96, 96, 44), -0.55f);
			B.P(nullptr, Cube(), FVector(0, 0, 46), FVector(90, 90, 6), Concrete);
			B.Mk(nullptr, Cube(), FVector(0, 0, 38), FVector(97, 97, 3), 0.f);
			for (int32 C = 0; C < 4; ++C) B.P(nullptr, Cube(), FVector((C % 2 ? 1 : -1) * 32, (C / 2 ? 1 : -1) * 32, 52), FVector(20, 20, 8), Concrete);   // 垛口
			B.P(nullptr, Cube(), FVector(0, 0, 14), FVector(97, 97, 4), RS::Darken(Concrete, 0.3f));
			Top = 56.f;
		}
		else if (Role == TEXT("def"))
		{
			B.P(nullptr, Cylinder(), FVector(0, 0, 8), FVector(84, 84, 16), Concrete);
			B.Mk(nullptr, Cylinder(), FVector(0, 0, 16.5f), FVector(86, 86, 2), -0.1f);   // 隊伍色基座環
			for (int32 K = 0; K < 8; ++K)
			{
				const float A = K * PI / 4.f;
				B.P(nullptr, Sphere(), FVector(FMath::Cos(A) * 38, FMath::Sin(A) * 38, 17), FVector(18, 12, 8), Sand, FRotator(0, FMath::RadiansToDegrees(A) + 90.f, 0));   // 沙包
			}
			USceneComponent* Tp = B.Pivot(nullptr, FVector(0, 0, 40));
			if (Id == TEXT("s_tcoil"))
			{
				B.T(nullptr, Cylinder(), FVector(0, 0, 70), FVector(26, 26, 120), -0.3f);
				for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cylinder(), FVector(0, 0, 50 + K * 25), FVector(54 - K * 8, 54 - K * 8, 5), Metal);
				for (int32 K = 0; K < 3; ++K) B.P(nullptr, Cube(), FVector(0, 0, 40), FVector(70, 6, 6), Steel, FRotator(0, K * 60.f, 0));
				B.P(nullptr, Sphere(), FVector(0, 0, 140), FVector(36, 36, 36), Cyan);
				Tp->SetRelativeLocation(FVector(0, 0, 140));
				Top = 160.f;
			}
			else if (Id == TEXT("a_prism"))
			{
				B.T(nullptr, Cylinder(), FVector(0, 0, 60), FVector(34, 34, 100), -0.2f);
				for (int32 K = 0; K < 3; ++K) B.P(nullptr, Cube(), FVector(0, 0, 30 + K * 30), FVector(36, 36, 3), Steel, FRotator(0, 45, 0));
				B.P(nullptr, Cone(), FVector(0, 0, 125), FVector(40, 40, 50), Cyan, FRotator(180, 0, 0));
				B.P(nullptr, Sphere(), FVector(0, 0, 150), FVector(26, 26, 26), Cyan);
				Tp->SetRelativeLocation(FVector(0, 0, 145));
				Top = 165.f;
			}
			else if (Id.EndsWith(TEXT("ewt")))
			{
				B.T(nullptr, Cylinder(), FVector(0, 0, 55), FVector(20, 20, 100), -0.2f);
				for (int32 K = 0; K < 3; ++K) B.P(nullptr, Cylinder(), FVector(0, 0, 30 + K * 26), FVector(26, 26, 3), Steel);
				USceneComponent* Sp = B.Pivot(nullptr, FVector(0, 0, 110));
				B.P(Sp, Sphere(), FVector::ZeroVector, FVector(56, 56, 12), FLinearColor(0.88f, 0.88f, 0.88f), FRotator(0, 0, 20));
				B.P(Sp, Cylinder(), FVector(10, 0, 8), FVector(3, 3, 14), Dark, FRotator(20, 0, 0));
				E->Spinner = Sp;
				Top = 120.f;
			}
			else if (Id == TEXT("a_aa") || Id == TEXT("s_flak"))
			{
				B.T(nullptr, Cube(), FVector(0, 0, 24), FVector(50, 50, 24), -0.3f);
				B.T(Tp, Cube(), FVector(0, 0, 0), FVector(34, 30, 16), 0.f);
				if (Id == TEXT("a_aa"))
				{
					B.P(Tp, Cube(), FVector(4, 0, 14), FVector(40, 30, 14), Metal, FRotator(35, 0, 0));
					for (int32 R = 0; R < 2; ++R)
						for (int32 C = 0; C < 3; ++C)
							B.P(Tp, Cylinder(), FVector(20 + R * 3, -9 + C * 9, 22 + R * 6), FVector(6, 6, 1), Rubber, FRotator(-55, 0, 0));
				}
				else
				{
					Gun(B, Tp, FVector(10, 6, 6), 36, 5, 40.f);
					Gun(B, Tp, FVector(10, -6, 6), 36, 5, 40.f);
				}
				Top = 80.f;
			}
			else
			{
				B.T(nullptr, Cylinder(), FVector(0, 0, 26), FVector(64, 64, 34), -0.35f);
				B.P(nullptr, Sphere(), FVector(0, 0, 42), FVector(60, 60, 30), Concrete);
				B.P(nullptr, Cube(), FVector(26, 0, 36), FVector(8, 30, 6), Rubber);   // 射口
				Gun(B, Tp, FVector(24, 0, 0), 30, 5);
				Top = 70.f;
			}
			E->TurretPivot = Tp;
		}
		else
		{
			// 一般建築：主體 + 角色特徵
			const float Hb = Role == TEXT("cy") ? 70.f : (Role == TEXT("fac") ? 80.f : 55.f);
			if (D.bNaval)
			{
				B.P(nullptr, Cube(), FVector(0, 0, 2), FVector(SX - 10, SY - 10, 12), FLinearColor(0.35f, 0.36f, 0.38f));
				for (int32 K = 0; K < 5; ++K) B.P(nullptr, Cylinder(), FVector(SX * 0.42f, -SY * 0.4f + K * SY * 0.2f, 4), FVector(10, 10, 16), Rubber);   // 碼頭防撞墊
				B.T(nullptr, Cube(), FVector(-SX * 0.3f, 0, 30), FVector(SX * 0.3f, SY * 0.7f, 50), -0.3f);
				WinRow(B, nullptr, 0, -SX * 0.15f + 0.5f, SY * 0.6f, 36, 4, 18, 10);
				B.P(nullptr, Cylinder(), FVector(SX * 0.2f, -SY * 0.3f, 60), FVector(10, 10, 110), Gold);
				B.P(nullptr, Cube(), FVector(SX * 0.2f, -SY * 0.05f, 115), FVector(10, SY * 0.6f, 8), Gold);
				B.P(nullptr, Cylinder(), FVector(SX * 0.2f, SY * 0.2f, 85), FVector(1.5f, 1.5f, 55), Dark);
				Flag(B, FVector(-SX * 0.42f, SY * 0.4f, 12), 70);
				Top = 120.f;
			}
			else if (D.bRunway)
			{
				B.P(nullptr, Cube(), FVector(0, SY * 0.18f, 10), FVector(SX - 10, SY * 0.45f, 4), FLinearColor(0.15f, 0.15f, 0.16f));
				for (int32 K = 0; K < 6; ++K) B.P(nullptr, Cube(), FVector(-SX * 0.4f + K * SX * 0.16f, SY * 0.18f, 13), FVector(SX * 0.07f, 4, 1), FLinearColor(0.9f, 0.9f, 0.85f));
				for (int32 K = 0; K < 8; ++K)
					for (int32 S = -1; S <= 1; S += 2)
						B.P(nullptr, Sphere(), FVector(-SX * 0.45f + K * SX * 0.128f, SY * 0.18f + S * SY * 0.21f, 13), FVector(4, 4, 3), K % 2 ? Lamp : FLinearColor(0.3f, 1.f, 0.4f));
				// 機庫（半圓頂）
				B.T(nullptr, Cube(), FVector(-SX * 0.25f, -SY * 0.25f, 22), FVector(SX * 0.35f, SY * 0.4f, 32), -0.3f);
				B.T(nullptr, Cylinder(), FVector(-SX * 0.25f, -SY * 0.25f, 38), FVector(SY * 0.4f, SY * 0.4f, SX * 0.35f), -0.15f, FRotator(-90, 0, 0));
				B.P(nullptr, Cube(), FVector(-SX * 0.25f + SX * 0.175f + 1, -SY * 0.25f, 25), FVector(2, SY * 0.3f, 34), Dark);
				// 塔台
				B.P(nullptr, Cube(), FVector(SX * 0.25f, -SY * 0.28f, 55), FVector(26, 26, 100), Wall);
				B.P(nullptr, Cube(), FVector(SX * 0.25f, -SY * 0.28f, 112), FVector(38, 38, 16), Glass);
				B.P(nullptr, Cube(), FVector(SX * 0.25f, -SY * 0.28f, 121), FVector(42, 42, 3), WallD);
				USceneComponent* Sp = B.Pivot(nullptr, FVector(SX * 0.25f, -SY * 0.28f, 130));
				B.P(Sp, Cube(), FVector::ZeroVector, FVector(40, 6, 10), Dark);
				E->Spinner = Sp;
				Top = 135.f;
			}
			else
			{
				// 主體：牆、底部陰影帶、隊伍色腰帶、女兒牆、屋頂
				B.P(nullptr, Cube(), FVector(0, 0, Hb * 0.5f + 6), FVector(SX * 0.82f, SY * 0.82f, Hb), Wall);
				B.P(nullptr, Cube(), FVector(0, 0, 10), FVector(SX * 0.83f, SY * 0.83f, 8), WallD);
				B.Mk(nullptr, Cube(), FVector(0, 0, Hb * 0.78f + 6), FVector(SX * 0.835f, SY * 0.835f, 7), -0.1f);
				B.P(nullptr, Cube(), FVector(0, 0, Hb + 8), FVector(SX * 0.86f, SY * 0.86f, 5), Concrete);
				B.P(nullptr, Cube(), FVector(0, 0, Hb + 9), FVector(SX * 0.74f, SY * 0.74f, 4), RS::Darken(Concrete, 0.25f));
				// 窗戶（+X、+Y 面）與大門
				const int32 NX = FMath::Max(1, FMath::RoundToInt(SY * 0.82f / 45.f));
				const int32 NY = FMath::Max(1, FMath::RoundToInt(SX * 0.82f / 45.f));
				WinRow(B, nullptr, 0, SX * 0.41f + 0.5f, SY * 0.7f, Hb * 0.5f + 8, NX, 14, 10);
				WinRow(B, nullptr, 1, SY * 0.41f + 0.5f, SX * 0.7f, Hb * 0.5f + 8, NY, 14, 10);
				B.T(nullptr, Cube(), FVector(SX * 0.41f + 1, 0, Hb * 0.22f + 6), FVector(3, SY * 0.28f, Hb * 0.44f), 0.15f);
				B.P(nullptr, Cube(), FVector(SX * 0.41f + 2, 0, Hb * 0.22f + 6), FVector(1.5f, SY * 0.24f, Hb * 0.38f), Dark);
				for (int32 S = -1; S <= 1; S += 2) B.P(nullptr, Sphere(), FVector(SX * 0.41f + 3, S * SY * 0.17f, Hb * 0.5f + 6), FVector(4, 4, 4), Lamp);
				// 屋簷下 LED 燈帶與監視器（科技感）
				B.P(nullptr, Cube(), FVector(SX * 0.41f + 0.8f, 0, Hb + 2), FVector(1.5f, SY * 0.78f, 1.5f), FLinearColor(0.35f, 0.9f, 1.f));
				B.P(nullptr, Cube(), FVector(0, SY * 0.41f + 0.8f, Hb + 2), FVector(SX * 0.78f, 1.5f, 1.5f), FLinearColor(0.35f, 0.9f, 1.f));
				B.P(nullptr, Cube(), FVector(SX * 0.42f, SY * 0.42f, Hb - 4), FVector(5, 3, 3), Dark, FRotator(-20, 45, 0));
				RoofBox(B, FVector(-SX * 0.15f, -SY * 0.2f, Hb + 13), 18);
				Flag(B, FVector(-SX * 0.38f, SY * 0.38f, Hb + 10), 55);
				Top = Hb + 15.f;
				if (Role == TEXT("cy"))
				{
					// 塔式吊車：格子塔身、吊臂、配重、吊鉤
					const FVector CB(-SX * 0.25f, -SY * 0.25f, Hb + 10);
					for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cylinder(), CB + FVector((K % 2 ? 5 : -5), (K / 2 ? 5 : -5), 50), FVector(2.5f, 2.5f, 100), Gold);
					for (int32 K = 0; K < 5; ++K) B.P(nullptr, Cube(), CB + FVector(0, 0, 10 + K * 20), FVector(13, 13, 1.5f), Gold);
					USceneComponent* Sp = B.Pivot(nullptr, CB + FVector(0, 0, 102));
					B.P(Sp, Cube(), FVector(40, 0, 0), FVector(130, 8, 8), Gold);
					B.P(Sp, Cube(), FVector(-30, 0, -2), FVector(16, 14, 12), Concrete);
					B.P(Sp, Cube(), FVector(0, 0, 6), FVector(10, 10, 8), Glass);
					B.P(Sp, Cylinder(), FVector(80, 0, -25), FVector(1.2f, 1.2f, 50), Dark);
					B.P(Sp, Cube(), FVector(80, 0, -51), FVector(6, 6, 5), Warn);
					E->Spinner = Sp;
					// 指揮中心：隊伍色控制室＋天線
					B.T(nullptr, Cube(), FVector(SX * 0.15f, SY * 0.15f, Hb + 25), FVector(SX * 0.35f, SY * 0.35f, 30), 0.f);
					WinRow(B, nullptr, 0, SX * 0.15f + SX * 0.175f + 0.5f, SY * 0.3f, Hb + 28, 3, 16, 8);
					WinRow(B, nullptr, 1, SY * 0.15f + SY * 0.175f + 0.5f, SX * 0.3f, Hb + 28, 3, 16, 8);
					B.P(nullptr, Sphere(), FVector(SX * 0.15f, SY * 0.15f, Hb + 44), FVector(30, 30, 16), FLinearColor(0.85f, 0.85f, 0.85f));
					Antenna(B, nullptr, FVector(SX * 0.25f, SY * 0.05f, Hb + 40), 40);
					Top = Hb + 120.f;
				}
				else if (Role == TEXT("power"))
				{
					if (bSov)
					{
						// 冷卻塔（上窄下寬）＋蒸汽頂
						const FVector C(SX * 0.15f, 0, Hb + 10);
						B.P(nullptr, Cylinder(), C + FVector(0, 0, 22), FVector(SX * 0.48f, SX * 0.48f, 44), FLinearColor(0.72f, 0.7f, 0.66f));
						B.P(nullptr, Cylinder(), C + FVector(0, 0, 58), FVector(SX * 0.4f, SX * 0.4f, 30), FLinearColor(0.75f, 0.73f, 0.69f));
						B.P(nullptr, Cylinder(), C + FVector(0, 0, 76), FVector(SX * 0.43f, SX * 0.43f, 8), FLinearColor(0.68f, 0.66f, 0.62f));
						B.P(nullptr, Cylinder(), C + FVector(0, 0, 80.5f), FVector(SX * 0.36f, SX * 0.36f, 2), Dark);
						B.P(nullptr, Cylinder(), C + FVector(0, 0, 40), FVector(SX * 0.485f, SX * 0.485f, 6), FLinearColor(0.75f, 0.2f, 0.15f));
						Top = Hb + 95.f;
					}
					else
					{
						// 雙煙囪＋發電機組＋高壓電塔
						for (int32 S = -1; S <= 1; S += 2)
						{
							B.P(nullptr, Cylinder(), FVector(-SX * 0.2f, S * SY * 0.2f, Hb + 45), FVector(16, 16, 90), Wall);
							B.P(nullptr, Cylinder(), FVector(-SX * 0.2f, S * SY * 0.2f, Hb + 75), FVector(17, 17, 8), FLinearColor(0.8f, 0.2f, 0.15f));
							B.P(nullptr, Cylinder(), FVector(-SX * 0.2f, S * SY * 0.2f, Hb + 90.5f), FVector(13, 13, 1), Dark);
						}
						for (int32 K = 0; K < 2; ++K) B.P(nullptr, Cube(), FVector(SX * 0.18f, -SY * 0.15f + K * SY * 0.3f, Hb + 22), FVector(30, 22, 26), Steel);
						for (int32 K = 0; K < 2; ++K) B.P(nullptr, Cylinder(), FVector(SX * 0.18f, -SY * 0.15f + K * SY * 0.3f, Hb + 36), FVector(14, 14, 3), Cyan);
						Top = Hb + 95.f;
					}
				}
				else if (Role == TEXT("ref"))
				{
					for (int32 K = 0; K < 2; ++K)
					{
						const FVector C(-SX * 0.2f, -SY * 0.2f + K * SY * 0.4f, Hb + 30);
						B.P(nullptr, Cylinder(), C, FVector(50, 50, 60), Gold);
						B.P(nullptr, Sphere(), C + FVector(0, 0, 30), FVector(50, 50, 22), RS::Darken(Gold, 0.15f));
						for (int32 R = 0; R < 2; ++R) B.P(nullptr, Cylinder(), C + FVector(0, 0, -15 + R * 25), FVector(51, 51, 3), Metal);
					}
					B.P(nullptr, Cube(), FVector(SX * 0.2f, 0, Hb + 16), FVector(SX * 0.25f, SY * 0.6f, 20), Metal);
					// 卸礦坡道（伸到 +X 面外）
					B.P(nullptr, Cube(), FVector(SX * 0.5f, 0, 14), FVector(SX * 0.3f, SY * 0.3f, 4), RS::Darken(Concrete, 0.2f), FRotator(-12, 0, 0));
					for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cube(), FVector(SX * 0.42f + K * 8, 0, 17 - K * 1.8f), FVector(3, SY * 0.3f, 1), Warn, FRotator(-12, 0, 0));
					Top = Hb + 65.f;
				}
				else if (Role == TEXT("bar"))
				{
					B.T(nullptr, Cube(), FVector(0, 0, Hb + 14), FVector(SX * 0.86f, SY * 0.6f, SY * 0.6f), -0.2f, FRotator(0, 0, 45));
					for (int32 K = 0; K < 5; ++K)
						B.P(nullptr, Sphere(), FVector(SX * 0.48f, -SY * 0.3f + K * SY * 0.15f, 12), FVector(16, 10, 8), Sand);
					B.P(nullptr, Cube(), FVector(-SX * 0.25f, SY * 0.48f, 20), FVector(30, 10, 28), Olive);   // 彈藥箱
					Top = Hb + 50.f;
				}
				else if (Role == TEXT("fac") || Role == TEXT("yard"))
				{
					// 鋸齒屋頂＋大型車庫門＋煙囪
					for (int32 K = 0; K < 3; ++K)
					{
						B.P(nullptr, Cube(), FVector(-SX * 0.25f + K * SX * 0.25f, 0, Hb + 22), FVector(SX * 0.2f, SY * 0.8f, 26), Wall, FRotator(-25, 0, 0));
						B.P(nullptr, Cube(), FVector(-SX * 0.25f + K * SX * 0.25f + SX * 0.08f, 0, Hb + 26), FVector(2, SY * 0.78f, 18), Glass);
					}
					B.P(nullptr, Cube(), FVector(SX * 0.42f, 0, 36), FVector(4, SY * 0.5f, 60), Dark);
					for (int32 K = 0; K < 5; ++K) B.P(nullptr, Cube(), FVector(SX * 0.425f, 0, 12 + K * 12), FVector(4.5f, SY * 0.5f, 1.2f), Metal);
					for (int32 K = 0; K < 6; ++K) B.P(nullptr, Cube(), FVector(SX * 0.43f, -SY * 0.25f + K * SY * 0.1f, 70), FVector(5, SY * 0.05f, 5), K % 2 ? Warn : Dark);
					B.P(nullptr, Cylinder(), FVector(-SX * 0.32f, SY * 0.3f, Hb + 50), FVector(14, 14, 70), Metal);
					B.P(nullptr, Cylinder(), FVector(-SX * 0.32f, SY * 0.3f, Hb + 82), FVector(15, 15, 6), FLinearColor(0.8f, 0.2f, 0.15f));
					Top = Hb + 85.f;
				}
				else if (Role == TEXT("radar"))
				{
					USceneComponent* Sp = B.Pivot(nullptr, FVector(0, 0, Hb + 40));
					B.P(nullptr, Cylinder(), FVector(0, 0, Hb + 20), FVector(14, 14, 40), Metal);
					B.P(Sp, Sphere(), FVector(0, 0, 10), FVector(110, 110, 22), FLinearColor(0.88f, 0.88f, 0.88f), FRotator(0, 0, 25));
					B.P(Sp, Cylinder(), FVector(0, 12, 20), FVector(4, 4, 30), Dark, FRotator(0, 0, 25));
					B.P(Sp, Sphere(), FVector(0, 18, 34), FVector(8, 8, 8), Dark);
					E->Spinner = Sp;
					Top = Hb + 70.f;
				}
				else if (Role == TEXT("tech"))
				{
					B.P(nullptr, Sphere(), FVector(0, 0, Hb + 10), FVector(SX * 0.7f, SY * 0.7f, 70), Glass);
					for (int32 K = 0; K < 3; ++K) B.P(nullptr, Cylinder(), FVector(0, 0, Hb + 10), FVector(SX * 0.71f, SY * 0.71f, 2), Steel, FRotator(K * 60.f, 0, 90));
					Antenna(B, nullptr, FVector(0, 0, Hb + 44), 40);
					Top = Hb + 85.f;
				}
				else if (Role == TEXT("sw"))
				{
					if (D.Super == TEXT("nuke"))
					{
						B.P(nullptr, Cylinder(), FVector(0, 0, Hb + 10), FVector(SX * 0.5f, SX * 0.5f, 24), Dark);
						B.T(nullptr, Cube(), FVector(-SX * 0.16f, 0, Hb + 26), FVector(SX * 0.3f, SX * 0.5f, 8), 0.f);
						B.T(nullptr, Cube(), FVector(SX * 0.16f, 0, Hb + 26), FVector(SX * 0.3f, SX * 0.5f, 8), 0.f);
						for (int32 K = 0; K < 8; ++K) B.P(nullptr, Cube(), FVector(-SX * 0.28f + K * SX * 0.08f, 0, Hb + 30.5f), FVector(SX * 0.04f, SX * 0.5f, 1), K % 2 ? Warn : Dark);
						B.P(nullptr, Cone(), FVector(0, 0, Hb + 46), FVector(24, 24, 30), FLinearColor(0.8f, 0.8f, 0.76f));
						Top = Hb + 60.f;
					}
					else
					{
						B.P(nullptr, Cylinder(), FVector(0, 0, Hb + 80), FVector(30, 30, 160), Wall);
						for (int32 K = 0; K < 4; ++K) B.P(nullptr, Cylinder(), FVector(0, 0, Hb + 30 + K * 36), FVector(56 - K * 6, 56 - K * 6, 4), Steel);
						B.P(nullptr, Sphere(), FVector(0, 0, Hb + 170), FVector(70, 70, 70), Cyan);
						for (int32 K = 0; K < 3; ++K)
						{
							const float A = K * 2.f * PI / 3.f;
							B.P(nullptr, Cylinder(), FVector(FMath::Cos(A) * SX * 0.3f, FMath::Sin(A) * SX * 0.3f, Hb + 40), FVector(10, 10, 80), Metal);
						}
						Top = Hb + 210.f;
					}
				}
				else if (Role == TEXT("dronehub"))
				{
					B.P(nullptr, Cylinder(), FVector(SX * 0.15f, SY * 0.15f, Hb + 12), FVector(SX * 0.5f, SX * 0.5f, 3), Dark);
					B.P(nullptr, Cylinder(), FVector(SX * 0.15f, SY * 0.15f, Hb + 13.6f), FVector(SX * 0.42f, SX * 0.42f, 0.5f), Warn);
					B.P(nullptr, Cylinder(), FVector(SX * 0.15f, SY * 0.15f, Hb + 14), FVector(SX * 0.38f, SX * 0.38f, 0.6f), Dark);
					B.P(nullptr, Cube(), FVector(SX * 0.15f, SY * 0.15f, Hb + 14.5f), FVector(30, 6, 1), FLinearColor(0.9f, 0.9f, 0.3f));
					B.P(nullptr, Cube(), FVector(SX * 0.15f, SY * 0.15f, Hb + 14.5f), FVector(6, 22, 1), FLinearColor(0.9f, 0.9f, 0.3f));
					B.P(nullptr, Cylinder(), FVector(-SX * 0.3f, -SY * 0.3f, Hb + 45), FVector(5, 5, 80), Metal);
					B.P(nullptr, Sphere(), FVector(-SX * 0.3f, -SY * 0.3f, Hb + 86), FVector(6, 6, 6), FLinearColor(1.f, 0.3f, 0.2f));
					Top = Hb + 80.f;
				}
				else if (Role == TEXT("bank"))
				{
					if (bSov)
					{
						for (int32 K = 0; K < 2; ++K)
						{
							B.P(nullptr, Sphere(), FVector(-SX * 0.2f + K * SX * 0.4f, 0, Hb + 30), FVector(60, 60, 60), FLinearColor(0.88f, 0.88f, 0.85f));
							B.P(nullptr, Cylinder(), FVector(-SX * 0.2f + K * SX * 0.4f, 0, Hb + 12), FVector(64, 64, 6), Metal);
						}
						B.P(nullptr, Cylinder(), FVector(0, SY * 0.3f, Hb + 30), FVector(4, 4, 60), Metal);
						Top = Hb + 60.f;
					}
					else
					{
						B.P(nullptr, Cube(), FVector(0, 0, Hb + 80), FVector(SX * 0.5f, SY * 0.5f, 160), Glass);
						for (int32 K = 0; K < 8; ++K) B.P(nullptr, Cube(), FVector(0, 0, Hb + 12 + K * 20), FVector(SX * 0.51f, SY * 0.51f, 2.5f), Steel);
						B.Mk(nullptr, Cube(), FVector(0, 0, Hb + 162), FVector(SX * 0.52f, SY * 0.52f, 6), 0.f);
						for (int32 S = -1; S <= 1; S += 2) B.P(nullptr, Cylinder(), FVector(SX * 0.41f + 4, S * SY * 0.18f, Hb * 0.5f + 6), FVector(7, 7, Hb), Concrete);   // 門廊柱
						Top = Hb + 170.f;
					}
				}
			}
		}
		E->VisualTop = Top;
		E->PickRadius = FMath::Max(SX, SY) * 0.5f;
		// 夜間照明：一盞暖色點光源照亮建築正面（白天強度 0，由 ARSGame 依天色調整）
		if (!D.bWall && !D.bCiv)
		{
			UPointLightComponent* PL = NewObject<UPointLightComponent>(E);
			PL->SetupAttachment(E->Body.Get());
			PL->SetRelativeLocation(FVector(SX * 0.62f, SY * 0.62f, 70.f));
			PL->SetIntensityUnits(ELightUnits::Candelas);
			PL->SetIntensity(0.f);
			PL->SetAttenuationRadius(FMath::Max(SX, SY) * 2.2f);
			PL->SetLightColor(FLinearColor(1.f, 0.82f, 0.58f));
			PL->SetCastShadows(false);
			PL->RegisterComponent();
			E->AddInstanceComponent(PL);
			E->NightLight = PL;
		}
	}

	static FString GTheme = TEXT("temperate");
	void SetTheme(const FString& Theme) { GTheme = Theme; }

	static void ApplyPalette(FB& B, ARSEntity* E)
	{
		// 依地圖主題選迷彩：林地綠、沙漠、雪地、熱帶
		FLinearColor Base(0.36f, 0.4f, 0.26f), Dk(0.2f, 0.23f, 0.15f), Lt(0.5f, 0.47f, 0.33f);
		if (GTheme == TEXT("desert")) { Base = FLinearColor(0.7f, 0.6f, 0.42f); Dk = FLinearColor(0.5f, 0.41f, 0.27f); Lt = FLinearColor(0.82f, 0.74f, 0.56f); }
		else if (GTheme == TEXT("snow")) { Base = FLinearColor(0.78f, 0.8f, 0.82f); Dk = FLinearColor(0.42f, 0.45f, 0.48f); Lt = FLinearColor(0.93f, 0.94f, 0.95f); }
		else if (GTheme == TEXT("tropical")) { Base = FLinearColor(0.3f, 0.37f, 0.22f); Dk = FLinearColor(0.15f, 0.21f, 0.13f); Lt = FLinearColor(0.46f, 0.42f, 0.27f); }
		if (E->Def && E->Def->Faction == TEXT("soviet")) { Base = RS::Darken(Base, 0.1f); Dk = RS::Darken(Dk, 0.1f); }
		else { Base = FMath::Lerp(Base, FLinearColor(0.45f, 0.47f, 0.45f), 0.2f); }
		if (E->bIsBuilding)
		{
			// 建築：中性鋼灰，略帶主題色調
			Base = FMath::Lerp(FLinearColor(0.38f, 0.41f, 0.44f), Base, 0.25f);
			Dk = RS::Darken(Base, 0.35f);
			Lt = RS::Lighten(Base, 0.2f);
		}
		B.Camo = Base; B.CamoDark = Dk; B.CamoLight = Lt;
	}

	void BuildEntity(ARSEntity* E)
	{
		if (!E || !E->Def) return;
		FB B;
		B.E = E;
		B.Team = E->TC();
		ApplyPalette(B, E);
		E->Body = B.Pivot(E->Root.Get(), FVector::ZeroVector);
		const URSVisualSettings* S = GetDefault<URSVisualSettings>();
		if (const TSoftObjectPtr<UStaticMesh>* Ov = S->MeshOverrides.Find(E->TypeId))
		{
			if (UStaticMesh* M = Ov->LoadSynchronous())
			{
				const float* Sc = S->MeshOverrideScale.Find(E->TypeId);
				const float* Yw = S->MeshOverrideYaw.Find(E->TypeId);
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(E);
				C->SetupAttachment(E->Body);
				C->SetStaticMesh(M);
				C->SetRelativeScale3D(FVector(Sc ? *Sc : 1.f));
				C->SetRelativeRotation(FRotator(0, Yw ? *Yw : 0.f, 0));
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (S->bTintOverrides)
				{
					UMaterialInstanceDynamic* MID = C->CreateAndSetMaterialInstanceDynamic(0);
					if (MID) MID->SetVectorParameterValue(S->ColorParam, B.Team);
				}
				C->RegisterComponent();
				E->AddInstanceComponent(C);
				E->TurretPivot = B.Pivot(nullptr, FVector(0, 0, 30));
				const FBoxSphereBounds Bd = M->GetBounds();
				E->VisualTop = Bd.BoxExtent.Z * 2.f * (Sc ? *Sc : 1.f) + 10.f;
				E->PickRadius = FMath::Max(Bd.BoxExtent.X, Bd.BoxExtent.Y) * (Sc ? *Sc : 1.f);
				return;
			}
		}
		if (E->bIsBuilding)
		{
			BuildBuilding(B, Cast<ARSBuilding>(E));
			return;
		}
		const FString& K = E->Def->Kind;
		if (K == TEXT("inf")) BuildInfantry(B, E->TypeId);
		else if (K == TEXT("ship")) BuildShip(B, E->TypeId);
		else if (K == TEXT("air")) BuildAir(B, E->TypeId);
		else BuildVehicle(B, E->TypeId);
		if (!E->TurretPivot) E->TurretPivot = B.Pivot(nullptr, FVector(0, 0, 30));
	}
}
