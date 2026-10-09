#include "RSTerrain.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSMap.h"
#include "RSBuilding.h"
#include "RSVisuals.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"

ARSTerrain::ARSTerrain()
{
	PrimaryActorTick.bCanEverTick = false;
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = RootScene;
}

UInstancedStaticMeshComponent* ARSTerrain::MakeISM(UStaticMesh* Mesh, const FLinearColor& C, bool bShadow)
{
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
	I->SetupAttachment(RootScene);
	I->SetStaticMesh(Mesh);
	I->SetMaterial(0, RSV::Mat(C));
	I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	I->SetCastShadow(bShadow);
	I->SetMobility(EComponentMobility::Movable);
	I->RegisterComponent();
	AddInstanceComponent(I);
	Static.Add(I);
	return I;
}

void ARSTerrain::Build(ARSGame* G)
{
	Game = G;
	BuildGround();
	BuildProps();
	BuildClutter();
	BuildFog();
	RebuildOre();
}

namespace
{
	struct FSection
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		FLinearColor C;
	};

	void AddQuad(FSection& S, const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		// A-B-C-D 依序環繞；雙面輸出，避免繞序差異造成看不見
		FVector Nrm = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
		if (Nrm.Z < -0.01f) Nrm = -Nrm;
		if (FMath::Abs(Nrm.Z) < 0.01f)
		{
			// 垂直面：法線朝外（由呼叫端給定順序決定，盡量取水平分量）
			Nrm = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
		}
		const int32 Base = S.V.Num();
		S.V.Append({ A, B, C, D });
		S.N.Append({ Nrm, Nrm, Nrm, Nrm });
		S.UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
		S.T.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		S.T.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
	}

	uint32 Hash2(int32 X, int32 Y)
	{
		uint32 H = uint32(X) * 374761393u + uint32(Y) * 668265263u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return H ^ (H >> 16);
	}
}

void ARSTerrain::BuildGround()
{
	const FRSMap& M = *Game->Map;
	const FRSTheme& Th = FRSDefs::Get().GetTheme(M.Theme);
	Ground = NewObject<UProceduralMeshComponent>(this);
	Ground->SetupAttachment(RootScene);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ground->bUseAsyncCooking = true;
	Ground->RegisterComponent();
	AddInstanceComponent(Ground);

	if (BuildGroundTextured()) return;
	TMap<uint32, FSection> Sections;
	auto Sec = [&Sections](uint32 Key, const FLinearColor& C) -> FSection&
	{
		FSection& S = Sections.FindOrAdd(Key);
		S.C = C;
		return S;
	};
	FRSNoise NTone, NHue;
	NTone.Init(Game->Settings.Seed + 11, 0.11f, 3);
	NHue.Init(Game->Settings.Seed + 29, 0.04f, 2);
	const FLinearColor CliffC(0.42f, 0.35f, 0.27f);
	const FLinearColor CliffDark(0.32f, 0.27f, 0.21f);
	for (int32 Y = 0; Y < M.H; ++Y)
	{
		for (int32 X = 0; X < M.W; ++X)
		{
			const int32 I = M.Idx(X, Y);
			const uint8 T = M.Terrain[I];
			const bool bHigh = M.Elev[I] == 1;
			const uint32 Var = Hash2(X, Y) % 3;
			const float X0 = X * RS_CELL, X1 = (X + 1) * RS_CELL, Y0 = Y * RS_CELL, Y1 = (Y + 1) * RS_CELL;
			if (T == FRSMap::T_WATER && !bHigh)
			{
				FLinearColor WC = Th.Mini[FRSMap::T_WATER];
				WC = RS::Lighten(WC, 0.05f * Var);
				const float Z = WATER_Z;
				AddQuad(Sec(100 + Var, WC), FVector(X0, Y0, Z), FVector(X1, Y0, Z), FVector(X1, Y1, Z), FVector(X0, Y1, Z));
				continue;
			}
			float H00 = 0.f, H10 = 0.f, H11 = 0.f, H01 = 0.f;
			if (bHigh)
			{
				H00 = M.VtxH(X, Y) * RS_HIGH;
				H10 = M.VtxH(X + 1, Y) * RS_HIGH;
				H11 = M.VtxH(X + 1, Y + 1) * RS_HIGH;
				H01 = M.VtxH(X, Y + 1) * RS_HIGH;
			}
			FLinearColor C = Th.Mini[FMath::Clamp<int32>(T, 0, 5)];
			uint32 Key = T * 10 + Var;
			if (bHigh)
			{
				C = RS::Lighten(C, 0.1f);
				Key += 1000;
			}
			if (M.Cliff[I] == 1)
			{
				C = CliffC;
				Key = 2000 + Var;
			}
			if (T == FRSMap::T_ROCK)
			{
				C = FLinearColor(0.36f, 0.34f, 0.31f);
				Key = 3000 + Var;
			}
			// 地表質感：每格切成 2x2，依低頻雜訊分 5 階明暗＋大片色調區（乾草 / 濕地），取代原本的棋盤格
			{
				const bool bSnowTh = M.Theme == TEXT("snow");
				const FLinearColor Alt = bSnowTh ? FLinearColor(C.R * 0.92f, C.G * 0.97f, FMath::Min(1.f, C.B * 1.06f))
					: FLinearColor(FMath::Min(1.f, C.R * 1.22f), C.G * 1.04f, C.B * 0.72f);
				for (int32 SY = 0; SY < 2; ++SY)
					for (int32 SX = 0; SX < 2; ++SX)
					{
						const float PX = X + 0.25f + 0.5f * SX, PY = Y + 0.25f + 0.5f * SY;
						const uint32 Hh = Hash2(X * 2 + SX + 7, Y * 2 + SY + 3);
						const float Tn = 0.5f + 0.5f * NTone.Get2D(PX, PY) + ((Hh & 255) / 255.f - 0.5f) * 0.28f;
						const int32 L = FMath::Clamp(FMath::FloorToInt(Tn * 5.f), 0, 4);
						const int32 Hu = (M.Cliff[I] == 0 && T != FRSMap::T_ROCK && NHue.Get2D(PX, PY) > 0.2f) ? 1 : 0;
						FLinearColor SC = Hu ? FMath::Lerp(C, Alt, 0.4f) : C;
						const float Sh = 0.86f + 0.06f * L;
						SC = FLinearColor(FMath::Min(1.f, SC.R * Sh), FMath::Min(1.f, SC.G * Sh), FMath::Min(1.f, SC.B * Sh), 1.f);
						auto HB = [&](float U, float V) { return FMath::Lerp(FMath::Lerp(H00, H10, U), FMath::Lerp(H01, H11, U), V); };
						const float U0 = 0.5f * SX, U1 = U0 + 0.5f, V0 = 0.5f * SY, V1 = V0 + 0.5f;
						const float XA = X0 + U0 * RS_CELL, XB = X0 + U1 * RS_CELL, YA = Y0 + V0 * RS_CELL, YB = Y0 + V1 * RS_CELL;
						AddQuad(Sec(Key / 10 * 100 + 20000 + L * 2 + Hu, SC), FVector(XA, YA, HB(U0, V0)), FVector(XB, YA, HB(U1, V0)), FVector(XB, YB, HB(U1, V1)), FVector(XA, YB, HB(U0, V1)));
					}
			}
			// 崖面：高地與低地（或地圖邊緣）之間的垂直面
			if (bHigh)
			{
				struct FEdge { int32 DX, DY; FVector A, B; float HA, HB; };
				const FEdge Edges[4] = {
					{ 1, 0, FVector(X1, Y0, 0), FVector(X1, Y1, 0), H10, H11 },
					{ -1, 0, FVector(X0, Y1, 0), FVector(X0, Y0, 0), H01, H00 },
					{ 0, 1, FVector(X1, Y1, 0), FVector(X0, Y1, 0), H11, H01 },
					{ 0, -1, FVector(X0, Y0, 0), FVector(X1, Y0, 0), H00, H10 } };
				for (const FEdge& E : Edges)
				{
					const int32 NX = X + E.DX, NY = Y + E.DY;
					const bool bLow = !M.Inb(NX, NY) || M.Elev[M.Idx(NX, NY)] == 0;
					if (!bLow || E.HA + E.HB < 1.f) continue;
					const bool bWaterN = M.Inb(NX, NY) && M.Terrain[M.Idx(NX, NY)] == FRSMap::T_WATER;
					const float ZB = bWaterN ? WATER_Z : 0.f;
					FVector A = E.A, B = E.B;
					FVector TA = A, TB = B;
					A.Z = ZB;
					B.Z = ZB;
					TA.Z = E.HA;
					TB.Z = E.HB;
					AddQuad(Sec(E.DY != 0 ? 2100 : 2101, E.DY != 0 ? CliffC : CliffDark), A, B, TB, TA);
				}
			}
			// 低地與水面之間的岸邊斜面
			if (!bHigh)
			{
				const int32 DX4[4] = { 1, -1, 0, 0 };
				const int32 DY4[4] = { 0, 0, 1, -1 };
				for (int32 K = 0; K < 4; ++K)
				{
					const int32 NX = X + DX4[K], NY = Y + DY4[K];
					if (!M.Inb(NX, NY) || M.Terrain[M.Idx(NX, NY)] != FRSMap::T_WATER || M.Elev[M.Idx(NX, NY)] == 1) continue;
					FVector A, B;
					if (K == 0) { A = FVector(X1, Y0, 0); B = FVector(X1, Y1, 0); }
					else if (K == 1) { A = FVector(X0, Y1, 0); B = FVector(X0, Y0, 0); }
					else if (K == 2) { A = FVector(X1, Y1, 0); B = FVector(X0, Y1, 0); }
					else { A = FVector(X0, Y0, 0); B = FVector(X1, Y0, 0); }
					FVector A2 = A, B2 = B;
					A2.Z = WATER_Z;
					B2.Z = WATER_Z;
					AddQuad(Sec(2200, RS::Darken(Th.Mini[FRSMap::T_ALT], 0.2f)), A2, B2, B, A);
				}
			}
		}
	}
	// 地圖外圍的深色地面
	{
		const float E = 6000.f;
		const float WW = M.W * RS_CELL, HH = M.H * RS_CELL;
		FSection& S = Sec(9000, RS::Darken(Th.Mini[0], 0.75f));
		const float Z = -40.f;
		AddQuad(S, FVector(-E, -E, Z), FVector(WW + E, -E, Z), FVector(WW + E, 0, Z), FVector(-E, 0, Z));
		AddQuad(S, FVector(-E, HH, Z), FVector(WW + E, HH, Z), FVector(WW + E, HH + E, Z), FVector(-E, HH + E, Z));
		AddQuad(S, FVector(-E, 0, Z), FVector(0, 0, Z), FVector(0, HH, Z), FVector(-E, HH, Z));
		AddQuad(S, FVector(WW, 0, Z), FVector(WW + E, 0, Z), FVector(WW + E, HH, Z), FVector(WW, HH, Z));
	}
	int32 SI = 0;
	for (auto& P : Sections)
	{
		FSection& S = P.Value;
		TArray<FLinearColor> VC;
		TArray<FProcMeshTangent> Tan;
		Ground->CreateMeshSection_LinearColor(SI, S.V, S.T, S.N, S.UV, VC, Tan, false);
		Ground->SetMaterial(SI, RSV::Mat(S.C));
		SI++;
	}
}

// ===================== 貼圖地表（M_RSTerrain：5 層原創貼圖，頂點色平滑混合） =====================
namespace
{
	struct FTexSection
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FVector> N;
		TArray<FVector2D> UV0, UV1;
		TArray<FLinearColor> VC;
		TArray<FProcMeshTangent> Tan;
		void Quad(const FVector P[4], const FVector2D UV[4], const FLinearColor W[4], const float Tone[4], const FVector& TanX)
		{
			FVector Nrm = FVector::CrossProduct(P[1] - P[0], P[3] - P[0]).GetSafeNormal();
			if (Nrm.Z < -0.01f) Nrm = -Nrm;
			const int32 B = V.Num();
			for (int32 K = 0; K < 4; ++K)
			{
				V.Add(P[K]);
				N.Add(Nrm);
				UV0.Add(UV[K]);
				UV1.Add(FVector2D(Tone[K], 0.f));
				VC.Add(W[K]);
				Tan.Add(FProcMeshTangent(TanX, false));
			}
			T.Append({ B, B + 1, B + 2, B, B + 2, B + 3 });
			T.Append({ B, B + 2, B + 1, B, B + 3, B + 2 });
		}
	};
}

bool ARSTerrain::BuildGroundTextured()
{
	UMaterialInterface* TM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedStorm/Terrain/M_RSTerrain.M_RSTerrain"));
	if (!TM) return false;
	const FRSMap& M = *Game->Map;
	const FRSTheme& Th = FRSDefs::Get().GetTheme(M.Theme);
	// 各主題的 5 層：基本地面、泥土、替代地面（沙灘 / 沙丘 / 冰）、岩石、道路
	const TCHAR* LayerNames[5] = { TEXT("Grass"), TEXT("Dirt"), TEXT("Sand"), TEXT("Rock"), TEXT("Asphalt") };
	if (M.Theme == TEXT("desert")) { LayerNames[0] = TEXT("Sand"); LayerNames[2] = TEXT("Dune"); LayerNames[3] = TEXT("RockTrop"); }
	else if (M.Theme == TEXT("tropical")) { LayerNames[0] = TEXT("GrassTrop"); LayerNames[1] = TEXT("DirtTrop"); LayerNames[2] = TEXT("Beach"); LayerNames[3] = TEXT("RockTrop"); }
	else if (M.Theme == TEXT("snow")) { LayerNames[0] = TEXT("Snow"); LayerNames[1] = TEXT("SnowDirt"); LayerNames[2] = TEXT("Ice"); }
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TM, this);
	for (int32 L = 0; L < 5; ++L)
	{
		for (const TCHAR* Suf : { TEXT("A"), TEXT("N") })
		{
			const FString Name = FString::Printf(TEXT("T_%s_%s"), LayerNames[L], Suf);
			UTexture* Tex = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/RedStorm/Terrain/Tex/%s.%s"), *Name, *Name));
			if (Tex) MID->SetTextureParameterValue(FName(*FString::Printf(TEXT("%s%d"), Suf[0] == 'A' ? TEXT("L") : TEXT("N"), L)), Tex);
			else UE_LOG(LogTemp, Warning, TEXT("RSTerrain: missing texture %s"), *Name);
		}
	}
	TerrainMID = MID;

	FRSNoise NTone, NWX, NWY;
	NTone.Init(Game->Settings.Seed + 11, 0.09f, 3);
	NWX.Init(Game->Settings.Seed + 41, 0.35f, 2);
	NWY.Init(Game->Settings.Seed + 53, 0.35f, 2);
	auto LayerOf = [&M](int32 CX, int32 CY) -> int32
	{
		CX = FMath::Clamp(CX, 0, M.W - 1);
		CY = FMath::Clamp(CY, 0, M.H - 1);
		const int32 I = M.Idx(CX, CY);
		if (M.Cliff[I] == 1) return 3;
		switch (M.Terrain[I])
		{
		case FRSMap::T_DIRT: return 1;
		case FRSMap::T_ALT: case FRSMap::T_WATER: return 2;
		case FRSMap::T_ROCK: return 3;
		case FRSMap::T_ROAD: return 4;
		default: return 0;
		}
	};
	// 某點（格子座標）的 5 層權重：鄰近格子以距離衰減混合，並以雜訊扭曲邊界，形成自然不規則的交界
	auto Weights = [&](float PX, float PY) -> FLinearColor
	{
		const float QX = PX + 0.32f * NWX.Get2D(PX, PY), QY = PY + 0.32f * NWY.Get2D(PX, PY);
		float W[5] = { 0, 0, 0, 0, 0 };
		const int32 CX = FMath::FloorToInt(QX), CY = FMath::FloorToInt(QY);
		for (int32 DY = -1; DY <= 1; ++DY)
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const int32 NX = CX + DX, NY = CY + DY;
				const float DDX = FMath::Max3(NX - QX, 0.f, QX - (NX + 1)), DDY = FMath::Max3(NY - QY, 0.f, QY - (NY + 1));
				const float D = FMath::Sqrt(DDX * DDX + DDY * DDY);
				const int32 Ly = LayerOf(NX, NY);
				const float R = Ly == 4 ? 0.22f : 0.45f;     // 道路邊緣比較銳利
				float K = FMath::Clamp(1.f - D / R, 0.f, 1.f);
				K = K * K * (3.f - 2.f * K);
				W[Ly] += K;
			}
		const float Sum = FMath::Max(W[0] + W[1] + W[2] + W[3] + W[4], 0.001f);
		return FLinearColor(W[0] / Sum, W[1] / Sum, W[2] / Sum, W[3] / Sum);
	};
	auto ToneAt = [&](float PX, float PY, bool bHigh) { return 0.86f + 0.26f * (0.5f + 0.5f * NTone.Get2D(PX, PY)) + (bHigh ? 0.04f : 0.f); };
	const float Tile = 300.f;

	// 水面材質與「離岸距離」場（決定深淺漸層與岸邊浪花）
	UMaterialInstanceDynamic* WaterMID = nullptr;
	if (UMaterialInterface* WM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedStorm/Terrain/M_RSWater.M_RSWater")))
	{
		WaterMID = UMaterialInstanceDynamic::Create(WM, this);
		FLinearColor Sh(0.07f, 0.28f, 0.30f), Dp(0.01f, 0.055f, 0.11f);
		if (M.Theme == TEXT("tropical")) { Sh = FLinearColor(0.08f, 0.45f, 0.42f); Dp = FLinearColor(0.008f, 0.09f, 0.2f); }
		else if (M.Theme == TEXT("desert")) { Sh = FLinearColor(0.12f, 0.36f, 0.32f); Dp = FLinearColor(0.012f, 0.07f, 0.12f); }
		else if (M.Theme == TEXT("snow")) { Sh = FLinearColor(0.1f, 0.22f, 0.26f); Dp = FLinearColor(0.012f, 0.045f, 0.08f); }
		WaterMID->SetVectorParameterValue(TEXT("Shallow"), Sh);
		WaterMID->SetVectorParameterValue(TEXT("Deep"), Dp);
		WaterTexMID = WaterMID;
	}
	TArray<float> Dist;
	Dist.Init(99.f, M.W * M.H);
	{
		TArray<int32> Q;
		for (int32 I = 0; I < M.W * M.H; ++I)
			if (M.Terrain[I] != FRSMap::T_WATER || M.Elev[I] == 1) { Dist[I] = 0.f; Q.Add(I); }
		for (int32 H = 0; H < Q.Num(); ++H)
		{
			const int32 C = Q[H], CX = C % M.W, CY = C / M.W;
			for (int32 DY = -1; DY <= 1; ++DY)
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const int32 NX = CX + DX, NY = CY + DY;
					if (!M.Inb(NX, NY)) continue;
					const int32 NI = M.Idx(NX, NY);
					const float ND = Dist[C] + ((DX != 0 && DY != 0) ? 1.414f : 1.f);
					if (ND < Dist[NI] && ND < 12.f) { Dist[NI] = ND; Q.Add(NI); }
				}
		}
	}
	// 某點（格子座標）的離岸距離：取四周格子中心的雙線性內插
	auto ShoreDist = [&](float PX, float PY) -> float
	{
		const float FX = PX - 0.5f, FY = PY - 0.5f;
		const int32 IX = FMath::FloorToInt(FX), IY = FMath::FloorToInt(FY);
		const float AX = FX - IX, AY = FY - IY;
		auto D = [&](int32 CX, int32 CY) { return FMath::Min(Dist[M.Idx(FMath::Clamp(CX, 0, M.W - 1), FMath::Clamp(CY, 0, M.H - 1))], 12.f); };
		return FMath::Lerp(FMath::Lerp(D(IX, IY), D(IX + 1, IY), AX), FMath::Lerp(D(IX, IY + 1), D(IX + 1, IY + 1), AX), AY);
	};
	FTexSection G, Walls, Water;
	auto EmitWater = [&](int32 X, int32 Y)
	{
		const float X0 = X * RS_CELL, Y0 = Y * RS_CELL;
		const int32 WS2 = 2;
					for (int32 SY = 0; SY < WS2; ++SY)
						for (int32 SX = 0; SX < WS2; ++SX)
						{
							FVector P[4];
							FVector2D UV[4];
							FLinearColor W[4];
							const float Tn[4] = { 1, 1, 1, 1 };
							const float Us[4] = { float(SX) / WS2, float(SX + 1) / WS2, float(SX + 1) / WS2, float(SX) / WS2 };
							const float Vs[4] = { float(SY) / WS2, float(SY) / WS2, float(SY + 1) / WS2, float(SY + 1) / WS2 };
							for (int32 K = 0; K < 4; ++K)
							{
								const float WX = X0 + Us[K] * RS_CELL, WY = Y0 + Vs[K] * RS_CELL;
								P[K] = FVector(WX, WY, WATER_Z);
								UV[K] = FVector2D(WX / 400.f, WY / 400.f);
								const float Dv = ShoreDist(X + Us[K], Y + Vs[K]);
								const float Fn = 0.15f * NWX.Get2D((X + Us[K]) * 2.f, (Y + Vs[K]) * 2.f);
								W[K] = FLinearColor(FMath::Clamp((Dv - 0.4f) / 3.2f, 0.f, 1.f), FMath::Clamp(1.25f - Dv * 1.1f + Fn, 0.f, 1.f), 0.f, 0.f);
							}
							Water.Quad(P, UV, W, Tn, FVector(1, 0, 0));
						}
	};
	// 離水距離場（陸地靠近水邊時往下傾斜成平滑沙灘）
	TArray<float> WDist;
	WDist.Init(99.f, M.W * M.H);
	{
		TArray<int32> Q;
		for (int32 I = 0; I < M.W * M.H; ++I)
			if (M.Terrain[I] == FRSMap::T_WATER && M.Elev[I] == 0) { WDist[I] = 0.f; Q.Add(I); }
		for (int32 H = 0; H < Q.Num(); ++H)
		{
			const int32 C = Q[H], CX = C % M.W, CY = C / M.W;
			for (int32 DY = -1; DY <= 1; ++DY)
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const int32 NX = CX + DX, NY = CY + DY;
					if (!M.Inb(NX, NY)) continue;
					const int32 NI = M.Idx(NX, NY);
					const float ND = WDist[C] + ((DX != 0 && DY != 0) ? 1.414f : 1.f);
					if (ND < WDist[NI] && ND < 4.f) { WDist[NI] = ND; Q.Add(NI); }
				}
		}
	}
	auto BeachZ = [&](float PX, float PY) -> float
	{
		const float FX = PX - 0.5f, FY = PY - 0.5f;
		const int32 IX = FMath::FloorToInt(FX), IY = FMath::FloorToInt(FY);
		const float AX = FX - IX, AY = FY - IY;
		auto D = [&](int32 CX, int32 CY) { return FMath::Min(WDist[M.Idx(FMath::Clamp(CX, 0, M.W - 1), FMath::Clamp(CY, 0, M.H - 1))], 4.f); };
		const float Wd = FMath::Lerp(FMath::Lerp(D(IX, IY), D(IX + 1, IY), AX), FMath::Lerp(D(IX, IY + 1), D(IX + 1, IY + 1), AX), AY)
			+ 0.3f * NWY.Get2D(PX * 1.7f, PY * 1.7f);
		if (Wd >= 1.2f) return 0.f;
		const float T = FMath::Clamp((Wd - 0.3f) / 0.8f, 0.f, 1.f);
		return FMath::Lerp(WATER_Z - 8.f, 0.f, T * T * (3.f - 2.f * T));
	};
	TMap<uint32, FSection> WaterSec;
	const int32 S = 3;   // 每格細分 3x3，讓混合更平滑
	for (int32 Y = 0; Y < M.H; ++Y)
	{
		for (int32 X = 0; X < M.W; ++X)
		{
			const int32 I = M.Idx(X, Y);
			const uint8 T = M.Terrain[I];
			const bool bHigh = M.Elev[I] == 1;
			const float X0 = X * RS_CELL, X1 = (X + 1) * RS_CELL, Y0 = Y * RS_CELL, Y1 = (Y + 1) * RS_CELL;
			if (T == FRSMap::T_WATER && !bHigh)
			{
				if (WaterMID)
				{
					EmitWater(X, Y);
					continue;
				}
				const uint32 Var = Hash2(X, Y) % 3;
				FSection& WS = WaterSec.FindOrAdd(100 + Var);
				WS.C = RS::Lighten(Th.Mini[FRSMap::T_WATER], 0.05f * Var);
				const float Z = WATER_Z;
				AddQuad(WS, FVector(X0, Y0, Z), FVector(X1, Y0, Z), FVector(X1, Y1, Z), FVector(X0, Y1, Z));
				continue;
			}
			float H00 = 0.f, H10 = 0.f, H11 = 0.f, H01 = 0.f;
			if (bHigh)
			{
				H00 = M.VtxH(X, Y) * RS_HIGH;
				H10 = M.VtxH(X + 1, Y) * RS_HIGH;
				H11 = M.VtxH(X + 1, Y + 1) * RS_HIGH;
				H01 = M.VtxH(X, Y + 1) * RS_HIGH;
			}
			auto HB = [&](float U, float V) { return FMath::Lerp(FMath::Lerp(H00, H10, U), FMath::Lerp(H01, H11, U), V); };
			for (int32 SY = 0; SY < S; ++SY)
				for (int32 SX = 0; SX < S; ++SX)
				{
					FVector P[4];
					FVector2D UV[4];
					FLinearColor W[4];
					float Tn[4];
					const float Us[4] = { float(SX) / S, float(SX + 1) / S, float(SX + 1) / S, float(SX) / S };
					const float Vs[4] = { float(SY) / S, float(SY) / S, float(SY + 1) / S, float(SY + 1) / S };
					for (int32 K = 0; K < 4; ++K)
					{
						const float WX = X0 + Us[K] * RS_CELL, WY = Y0 + Vs[K] * RS_CELL;
						P[K] = FVector(WX, WY, (bHigh || !WaterMID) ? HB(Us[K], Vs[K]) : BeachZ(X + Us[K], Y + Vs[K]));
						UV[K] = FVector2D(WX / Tile, WY / Tile);
						W[K] = Weights(X + Us[K], Y + Vs[K]);
						Tn[K] = ToneAt(X + Us[K], Y + Vs[K], bHigh);
					}
					G.Quad(P, UV, W, Tn, FVector(1, 0, 0));
				}
			// 崖面（岩石層）
			if (bHigh)
			{
				struct FEdge { int32 DX, DY; FVector A, B; float HA, HB; };
				const FEdge Edges[4] = {
					{ 1, 0, FVector(X1, Y0, 0), FVector(X1, Y1, 0), H10, H11 },
					{ -1, 0, FVector(X0, Y1, 0), FVector(X0, Y0, 0), H01, H00 },
					{ 0, 1, FVector(X1, Y1, 0), FVector(X0, Y1, 0), H11, H01 },
					{ 0, -1, FVector(X0, Y0, 0), FVector(X1, Y0, 0), H00, H10 } };
				for (const FEdge& E : Edges)
				{
					const int32 NX = X + E.DX, NY = Y + E.DY;
					const bool bLow = !M.Inb(NX, NY) || M.Elev[M.Idx(NX, NY)] == 0;
					if (!bLow || E.HA + E.HB < 1.f) continue;
					const bool bWaterN = M.Inb(NX, NY) && M.Terrain[M.Idx(NX, NY)] == FRSMap::T_WATER;
					const float ZB = bWaterN ? WATER_Z : 0.f;
					FVector P[4] = { E.A, E.B, E.B, E.A };
					P[0].Z = ZB; P[1].Z = ZB; P[2].Z = E.HB; P[3].Z = E.HA;
					const FVector Dir = (E.B - E.A).GetSafeNormal();
					auto Along = [&](const FVector& Q) { return (Q.X * Dir.X + Q.Y * Dir.Y) / Tile; };
					FVector2D UV[4];
					for (int32 K = 0; K < 4; ++K) UV[K] = FVector2D(Along(P[K]), -P[K].Z / Tile * 1.6f);
					const FLinearColor RockW(0, 0, 0, 1);
					const FLinearColor W[4] = { RockW, RockW, RockW, RockW };
					const float Tb = E.DY != 0 ? 0.9f : 0.78f;
					const float Tn[4] = { Tb * 0.8f, Tb * 0.8f, Tb, Tb };
					Walls.Quad(P, UV, W, Tn, Dir);
				}
			}
			if (WaterMID && !bHigh && WDist[I] <= 1.5f) EmitWater(X, Y);
			// 岸邊斜面（替代地面層）
			if (!bHigh && !WaterMID)
			{
				const int32 DX4[4] = { 1, -1, 0, 0 };
				const int32 DY4[4] = { 0, 0, 1, -1 };
				for (int32 K = 0; K < 4; ++K)
				{
					const int32 NX = X + DX4[K], NY = Y + DY4[K];
					if (!M.Inb(NX, NY) || M.Terrain[M.Idx(NX, NY)] != FRSMap::T_WATER || M.Elev[M.Idx(NX, NY)] == 1) continue;
					FVector A, B;
					if (K == 0) { A = FVector(X1, Y0, 0); B = FVector(X1, Y1, 0); }
					else if (K == 1) { A = FVector(X0, Y1, 0); B = FVector(X0, Y0, 0); }
					else if (K == 2) { A = FVector(X1, Y1, 0); B = FVector(X0, Y1, 0); }
					else { A = FVector(X0, Y0, 0); B = FVector(X1, Y0, 0); }
					FVector P[4] = { A, B, B, A };
					P[0].Z = WATER_Z; P[1].Z = WATER_Z;
					FVector2D UV[4];
					for (int32 Q = 0; Q < 4; ++Q) UV[Q] = FVector2D((P[Q].X + P[Q].Z) / Tile, (P[Q].Y + P[Q].Z) / Tile);
					const FLinearColor SandW(0, 0, 1, 0);
					const FLinearColor W[4] = { SandW, SandW, SandW, SandW };
					const float Tn[4] = { 0.7f, 0.7f, 0.9f, 0.9f };
					Walls.Quad(P, UV, W, Tn, (B - A).GetSafeNormal());
				}
			}
		}
	}
	// 地圖外圍：有水面材質時改為外海（近岸淺、外圍深）＋地圖邊緣的土壁
	if (WaterMID)
	{
		const float WW = M.W * RS_CELL, HH = M.H * RS_CELL;
		const float Z = WATER_Z;
		auto OQuad = [&](float AX0, float AY0, float AX1, float AY1, float DIn, float DOut, float FIn, int32 InSide)
		{
			// InSide：0=靠近地圖的一側在 Y1，1=在 Y0，2=在 X1，3=在 X0
			FVector P[4] = { FVector(AX0, AY0, Z), FVector(AX1, AY0, Z), FVector(AX1, AY1, Z), FVector(AX0, AY1, Z) };
			FVector2D UV[4];
			FLinearColor W[4];
			const float Tn[4] = { 1, 1, 1, 1 };
			for (int32 K = 0; K < 4; ++K)
			{
				UV[K] = FVector2D(P[K].X / 400.f, P[K].Y / 400.f);
				bool bIn = false;
				if (InSide == 0) bIn = K >= 2; else if (InSide == 1) bIn = K < 2; else if (InSide == 2) bIn = (K == 1 || K == 2); else bIn = (K == 0 || K == 3);
				W[K] = bIn ? FLinearColor(DIn, FIn, 0, 0) : FLinearColor(DOut, 0, 0, 0);
			}
			Water.Quad(P, UV, W, Tn, FVector(1, 0, 0));
		};
		const float R1 = 400.f, E = 9000.f;
		// 近岸環帶
		OQuad(0, -R1, WW, 0, 0.2f, 1.f, 0.4f, 0);
		OQuad(0, HH, WW, HH + R1, 0.2f, 1.f, 0.4f, 1);
		OQuad(-R1, 0, 0, HH, 0.2f, 1.f, 0.4f, 2);
		OQuad(WW, 0, WW + R1, HH, 0.2f, 1.f, 0.4f, 3);
		// 外海
		OQuad(-E, -E, WW + E, -R1, 1.f, 1.f, 0.f, 0);
		OQuad(-E, HH + R1, WW + E, HH + E, 1.f, 1.f, 0.f, 1);
		OQuad(-E, -R1, -R1, HH + R1, 1.f, 1.f, 0.f, 2);
		OQuad(WW + R1, -R1, WW + E, HH + R1, 1.f, 1.f, 0.f, 3);
		OQuad(-R1, -R1, 0, 0, 0.6f, 0.6f, 0.f, 0);
		OQuad(WW, -R1, WW + R1, 0, 0.6f, 0.6f, 0.f, 0);
		OQuad(-R1, HH, 0, HH + R1, 0.6f, 0.6f, 0.f, 0);
		OQuad(WW, HH, WW + R1, HH + R1, 0.6f, 0.6f, 0.f, 0);
		// 地圖邊緣土壁（避免看到地面底下）
		const FLinearColor DW(0, 1, 0, 0);
		const FLinearColor Wc[4] = { DW, DW, DW, DW };
		const float Tw[4] = { 0.6f, 0.6f, 0.8f, 0.8f };
		const FVector Cn[4] = { FVector(0, 0, 0), FVector(WW, 0, 0), FVector(WW, HH, 0), FVector(0, HH, 0) };
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector A = Cn[K], B = Cn[(K + 1) % 4];
			FVector P[4] = { A, B, B, A };
			P[0].Z = WATER_Z - 40.f; P[1].Z = WATER_Z - 40.f;
			const FVector Dir = (B - A).GetSafeNormal();
			FVector2D UV[4];
			for (int32 Q = 0; Q < 4; ++Q) UV[Q] = FVector2D((P[Q].X * Dir.X + P[Q].Y * Dir.Y) / Tile, -P[Q].Z / Tile);
			Walls.Quad(P, UV, Wc, Tw, Dir);
		}
	}
	else
	{
		const float E = 6000.f, Z = -40.f;
		const float WW = M.W * RS_CELL, HH = M.H * RS_CELL;
		const FVector Rects[4][2] = { { FVector(-E, -E, Z), FVector(WW + E, 0, Z) }, { FVector(-E, HH, Z), FVector(WW + E, HH + E, Z) },
			{ FVector(-E, 0, Z), FVector(0, HH, Z) }, { FVector(WW, 0, Z), FVector(WW + E, HH, Z) } };
		const FLinearColor OW(0.5f, 0.5f, 0, 0);
		const FLinearColor W[4] = { OW, OW, OW, OW };
		const float Tn[4] = { 0.2f, 0.2f, 0.2f, 0.2f };
		for (const auto& R : Rects)
		{
			FVector P[4] = { R[0], FVector(R[1].X, R[0].Y, Z), R[1], FVector(R[0].X, R[1].Y, Z) };
			FVector2D UV[4];
			for (int32 K = 0; K < 4; ++K) UV[K] = FVector2D(P[K].X / Tile, P[K].Y / Tile);
			G.Quad(P, UV, W, Tn, FVector(1, 0, 0));
		}
	}
	const TArray<FVector2D> Empty;
	int32 SI = 0;
	for (FTexSection* TS : { &G, &Walls })
	{
		if (TS->V.Num() == 0) continue;
		Ground->CreateMeshSection_LinearColor(SI, TS->V, TS->T, TS->N, TS->UV0, TS->UV1, Empty, Empty, TS->VC, TS->Tan, false, false);
		Ground->SetMaterial(SI, MID);
		SI++;
	}
	if (Water.V.Num() > 0)
	{
		Ground->CreateMeshSection_LinearColor(SI, Water.V, Water.T, Water.N, Water.UV0, Water.UV1, Empty, Empty, Water.VC, Water.Tan, false, false);
		Ground->SetMaterial(SI, WaterMID);
		SI++;
	}
	for (auto& P : WaterSec)
	{
		FSection& WS = P.Value;
		TArray<FLinearColor> VC;
		TArray<FProcMeshTangent> Tan;
		Ground->CreateMeshSection_LinearColor(SI, WS.V, WS.T, WS.N, WS.UV, VC, Tan, false);
		Ground->SetMaterial(SI, RSV::Mat(WS.C));
		SI++;
	}
	UE_LOG(LogTemp, Log, TEXT("RSTerrain: textured ground (%s) verts=%d walls=%d"), *M.Theme, G.V.Num(), Walls.V.Num());
	return true;
}

void ARSTerrain::BuildProps()
{
	const FRSMap& M = *Game->Map;
	const bool bSnow = M.Theme == TEXT("snow");
	// 樹木（v2：多球樹冠闊葉樹、三層針葉樹、彎曲樹幹的棕櫚，顏色隨機變化）
	UInstancedStaticMeshComponent* Trunk = MakeISM(RSV::Cylinder(), FLinearColor(0.3f, 0.22f, 0.14f));
	UInstancedStaticMeshComponent* PalmTrunk = MakeISM(RSV::Cylinder(), FLinearColor(0.47f, 0.38f, 0.26f));
	UInstancedStaticMeshComponent* LeafISM[3] = {
		MakeISM(RSV::Sphere(), FLinearColor(0.17f, 0.37f, 0.12f)),
		MakeISM(RSV::Sphere(), FLinearColor(0.24f, 0.45f, 0.15f)),
		MakeISM(RSV::Sphere(), bSnow ? FLinearColor(0.3f, 0.36f, 0.3f) : FLinearColor(0.13f, 0.3f, 0.11f)) };
	UInstancedStaticMeshComponent* PineISM[2] = {
		MakeISM(RSV::Cone(), bSnow ? FLinearColor(0.16f, 0.28f, 0.22f) : FLinearColor(0.09f, 0.25f, 0.12f)),
		MakeISM(RSV::Cone(), bSnow ? FLinearColor(0.2f, 0.32f, 0.26f) : FLinearColor(0.13f, 0.3f, 0.15f)) };
	UInstancedStaticMeshComponent* Frond = MakeISM(RSV::Sphere(), FLinearColor(0.25f, 0.48f, 0.15f));
	UInstancedStaticMeshComponent* Coconut = MakeISM(RSV::Sphere(), FLinearColor(0.32f, 0.24f, 0.12f));
	UInstancedStaticMeshComponent* Snowcap = bSnow ? MakeISM(RSV::Cone(), FLinearColor(0.9f, 0.92f, 0.95f)) : nullptr;
	for (const FRSTree& T : M.Trees)
	{
		const uint32 H = Hash2(T.C.X, T.C.Y);
		FRandomStream R(static_cast<int32>(H));
		const FVector2D P = RS::CellCenter(T.C) + FVector2D(R.FRandRange(-0.15f, 0.15f), R.FRandRange(-0.15f, 0.15f));
		const FVector Base = Game->ToWorld(P);
		const float Sc = R.FRandRange(0.8f, 1.3f);
		const float Yaw = R.FRandRange(0.f, 360.f);
		if (T.Kind == TEXT("palm"))
		{
			// 彎曲樹幹：三段逐漸傾斜
			const FVector LeanDir = FRotator(0, Yaw, 0).Vector();
			FVector Cur = Base;
			float Lean = 4.f;
			for (int32 K = 0; K < 3; ++K)
			{
				const float SegL = 40.f * Sc;
				const FVector Dir = (FVector(0, 0, 1) + LeanDir * FMath::Tan(FMath::DegreesToRadians(Lean))).GetSafeNormal();
				AddProp(PalmTrunk, FTransform(FRotationMatrix::MakeFromZ(Dir).Rotator(), Cur + Dir * SegL * 0.5f, FVector(0.075f - K * 0.012f, 0.075f - K * 0.012f, SegL / 100.f)), T.C);
				Cur += Dir * SegL;
				Lean += 9.f;
			}
			// 葉片：放射狀、下垂的扁長橢球
			const int32 NF = 7;
			for (int32 K = 0; K < NF; ++K)
			{
				const float A = Yaw + K * 360.f / NF + R.FRandRange(-12.f, 12.f);
				const FRotator FR(-28.f - R.FRandRange(0.f, 15.f), A, 0);
				AddProp(Frond, FTransform(FR, Cur + FR.Vector() * 26.f * Sc, FVector(0.62f, 0.16f, 0.05f) * Sc), T.C);
			}
			for (int32 K = 0; K < 3; ++K)
				AddProp(Coconut, FTransform(FRotator::ZeroRotator, Cur + FVector(FMath::Cos(K * 2.1f) * 5.f, FMath::Sin(K * 2.1f) * 5.f, -6.f) * Sc, FVector(0.08f) * Sc), T.C);
		}
		else if (T.Kind == TEXT("snowpine") || (T.Kind == TEXT("tree") && (H & 1)))
		{
			// 針葉樹：樹幹＋三層錐形枝葉，越上越小
			AddProp(Trunk, FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, 18 * Sc), FVector(0.09f, 0.09f, 0.36f * Sc)), T.C);
			UInstancedStaticMeshComponent* PineM = PineISM[R.RandRange(0, 1)];
			for (int32 K = 0; K < 3; ++K)
			{
				const float W = (0.7f - K * 0.17f) * Sc;
				const float Zc = (42.f + K * 30.f) * Sc;
				AddProp(PineM, FTransform(FRotator(0, Yaw + K * 37.f, 0), Base + FVector(0, 0, Zc), FVector(W, W, (0.55f - K * 0.06f) * Sc)), T.C);
				if (Snowcap) AddProp(Snowcap, FTransform(FRotator(0, Yaw, 0), Base + FVector(0, 0, Zc + 12.f * Sc), FVector(W * 0.6f, W * 0.6f, 0.22f * Sc)), T.C);
			}
		}
		else
		{
			// 闊葉樹：稍粗的樹幹＋兩根枝幹＋4～5 顆不同大小、顏色的球形樹冠
			const float Tilt = R.FRandRange(-5.f, 5.f);
			AddProp(Trunk, FTransform(FRotator(Tilt, Yaw, 0), Base + FVector(0, 0, 24 * Sc), FVector(0.13f, 0.13f, 0.48f * Sc)), T.C);
			for (int32 K = 0; K < 2; ++K)
				AddProp(Trunk, FTransform(FRotator(35.f, Yaw + K * 150.f, 0), Base + FVector(0, 0, 44 * Sc), FVector(0.06f, 0.06f, 0.3f * Sc)), T.C);
			const int32 NB = 4 + R.RandRange(0, 1);
			for (int32 K = 0; K < NB; ++K)
			{
				const float A = FMath::DegreesToRadians(Yaw + K * 360.f / NB);
				const float Rr = K == 0 ? 0.f : R.FRandRange(14.f, 22.f) * Sc;
				const float Zc = (K == 0 ? 74.f : R.FRandRange(56.f, 70.f)) * Sc;
				const float Bs = (K == 0 ? 0.62f : R.FRandRange(0.4f, 0.52f)) * Sc;
				AddProp(LeafISM[(K + H) % 3], FTransform(FRotator(R.FRandRange(0.f, 30.f), R.FRandRange(0.f, 360.f), 0),
					Base + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, Zc), FVector(Bs, Bs * R.FRandRange(0.85f, 1.05f), Bs * 0.82f)), T.C);
			}
		}
	}
	// 岩石
	UInstancedStaticMeshComponent* Rock = MakeISM(RSV::Sphere(), M.Theme == TEXT("desert") || M.Theme == TEXT("tropical") ? FLinearColor(0.5f, 0.36f, 0.24f) : FLinearColor(0.42f, 0.41f, 0.4f));
	for (const FRSRock& R : M.Rocks)
	{
		const uint32 H = Hash2(R.C.X + 7, R.C.Y + 3);
		const FVector Base = Game->ToWorld(RS::CellCenter(R.C));
		const float Sc = 0.6f + (H & 255) / 255.f * 0.6f;
		AddProp(Rock, FTransform(FRotator(float(H % 40), float((H >> 8) % 360), 0), Base + FVector(0, 0, 18 * Sc), FVector(1.1f, 0.95f, 0.6f + (R.Var % 3) * 0.25f) * Sc), R.C);
	}
	// 橋樑
	UInstancedStaticMeshComponent* Deck = MakeISM(RSV::Cube(), FLinearColor(0.55f, 0.53f, 0.5f));
	UInstancedStaticMeshComponent* Rail = MakeISM(RSV::Cube(), FLinearColor(0.3f, 0.29f, 0.27f));
	for (int32 Y = 0; Y < M.H; ++Y)
		for (int32 X = 0; X < M.W; ++X)
		{
			if (M.Bridge[M.Idx(X, Y)] == 0) continue;
			const FVector C(RS::CellCenter(FIntPoint(X, Y)).X * RS_CELL, RS::CellCenter(FIntPoint(X, Y)).Y * RS_CELL, -6.f);
			Deck->AddInstance(FTransform(FRotator::ZeroRotator, C, FVector(1.02f, 1.02f, 0.14f)));
			Rail->AddInstance(FTransform(FRotator::ZeroRotator, C + FVector(0, 0, -30), FVector(0.25f, 0.25f, 0.5f)));
		}
}

// 平面戰爭迷霧：貼在該格地面高度的黑色薄板（往下延伸蓋住懸崖面），不再是高牆
FTransform ARSTerrain::FogXf(int32 X, int32 Y) const
{
	const FVector G = Game->ToWorld(RS::CellCenter(FIntPoint(X, Y)));
	const float Top = FMath::Max(G.Z, ARSTerrain::WATER_Z) + 4.f;
	return FTransform(FRotator::ZeroRotator, FVector((X + 0.5f) * RS_CELL, (Y + 0.5f) * RS_CELL, Top - 40.f), FVector(1.02f, 1.02f, 0.8f));
}

// 未探索格子裡的樹木、岩石先藏起來，探索後才出現
void ARSTerrain::AddProp(UInstancedStaticMeshComponent* ISM, const FTransform& Xf, const FIntPoint& Cell)
{
	const int32 Idx = ISM->AddInstance(Xf);
	const int32 PI2 = Props.Num();
	Props.Add({ ISM, Idx, Xf });
	if (Game && Game->Map.IsValid() && Game->Map->Inb(Cell.X, Cell.Y)) CellProps.Add(Game->Map->Idx(Cell.X, Cell.Y), PI2);
}

void ARSTerrain::HideCellsForBuilding(const TArray<int32>& Cells)
{
	for (int32 C : Cells)
	{
		BuiltCells.Add(C);
		SetPropsShown(C, false);
	}
	for (UInstancedStaticMeshComponent* D : DirtyISM) if (D) D->MarkRenderStateDirty();
	DirtyISM.Reset();
}

void ARSTerrain::AddRubble(FIntPoint TL, FIntPoint Size, bool bWall, bool bShown)
{
	if (!Game || !Game->Map.IsValid()) return;
	const FRSMap& M = *Game->Map;
	if (RubbleISM.Num() == 0)
	{
		RubbleISM.Add(MakeISM(RSV::Cube(), FLinearColor(0.36f, 0.35f, 0.33f)));   // 水泥碎塊
		RubbleISM.Add(MakeISM(RSV::Cube(), FLinearColor(0.07f, 0.065f, 0.06f)));  // 燒焦殘骸
		RubbleISM.Add(MakeISM(RSV::Cube(), FLinearColor(0.32f, 0.2f, 0.12f)));    // 鏽蝕鋼材
		RubbleISM.Add(MakeISM(RSV::Cylinder(), FLinearColor(0.05f, 0.045f, 0.04f), false));   // 焦黑地面
	}
	TArray<int32> Cells;
	for (int32 Y = TL.Y; Y < TL.Y + Size.Y; ++Y)
		for (int32 X = TL.X; X < TL.X + Size.X; ++X)
			if (M.Inb(X, Y))
			{
				const int32 I = M.Idx(X, Y);
				BuiltCells.Remove(I);
				Cells.Add(I);
			}
	FRandomStream R(static_cast<int32>(Hash2(TL.X * 7 + 3, TL.Y * 13 + 5)) ^ Props.Num());
	auto CellOf = [&](const FVector2D& P) { return FIntPoint(FMath::Clamp(FMath::FloorToInt(P.X), 0, M.W - 1), FMath::Clamp(FMath::FloorToInt(P.Y), 0, M.H - 1)); };
	const FVector2D C0(TL.X + Size.X * 0.5f, TL.Y + Size.Y * 0.5f);
	if (!bWall)
	{
		const FVector G = Game->ToWorld(C0);
		AddProp(RubbleISM[3], FTransform(FRotator(0, R.FRandRange(0.f, 360.f), 0), G + FVector(0, 0, 1.2f),
			FVector(Size.X * 1.15f, Size.Y * 1.15f, 0.012f)), CellOf(C0));
	}
	const int32 N = bWall ? 5 : Size.X * Size.Y * 9;
	for (int32 K = 0; K < N; ++K)
	{
		// 越靠中心堆得越高
		const float U = R.FRandRange(-0.48f, 0.48f), V = R.FRandRange(-0.48f, 0.48f);
		const FVector2D P(C0.X + U * Size.X, C0.Y + V * Size.Y);
		const float Mid = 1.f - FMath::Max(FMath::Abs(U), FMath::Abs(V)) * 1.6f;
		const float Sx = R.FRandRange(0.06f, 0.26f), Sy = R.FRandRange(0.05f, 0.22f), Sz = R.FRandRange(0.03f, 0.12f) * (0.7f + Mid);
		const int32 Kind = R.FRand() < 0.5f ? 0 : (R.FRand() < 0.6f ? 1 : 2);
		const FVector G = Game->ToWorld(P);
		AddProp(RubbleISM[Kind], FTransform(FRotator(R.FRandRange(-35.f, 35.f), R.FRandRange(0.f, 360.f), R.FRandRange(-35.f, 35.f)),
			G + FVector(0, 0, Sz * 30.f + FMath::Max(0.f, Mid) * 10.f), FVector(Sx, Sy, Sz)), CellOf(P));
	}
	// 傾倒的牆板與鋼樑
	const int32 NS = bWall ? 1 : 2 + R.RandRange(0, 2);
	for (int32 K = 0; K < NS; ++K)
	{
		const FVector2D P(C0.X + R.FRandRange(-0.3f, 0.3f) * Size.X, C0.Y + R.FRandRange(-0.3f, 0.3f) * Size.Y);
		const bool bBeam = R.FRand() < 0.4f;
		const FVector Sc = bBeam ? FVector(0.6f * Size.X * 0.5f, 0.05f, 0.05f) : FVector(0.45f * Size.X * 0.5f + 0.15f, 0.05f, 0.28f);
		const FVector G = Game->ToWorld(P);
		AddProp(RubbleISM[bBeam ? 2 : 0], FTransform(FRotator(R.FRandRange(-40.f, 40.f), R.FRandRange(0.f, 360.f), R.FRandRange(-25.f, 25.f)),
			G + FVector(0, 0, 12.f), Sc), CellOf(P));
	}
	if (!bShown)
		for (int32 I : Cells) SetPropsShown(I, false);
	for (UInstancedStaticMeshComponent* D : DirtyISM) if (D) D->MarkRenderStateDirty();
	DirtyISM.Reset();
}

// 地面點綴：草叢、野花、碎石、乾灌木，讓地表不再是單色平面
void ARSTerrain::BuildClutter()
{
	const FRSMap& M = *Game->Map;
	const FRSTheme& Th = FRSDefs::Get().GetTheme(M.Theme);
	const bool bSnow = M.Theme == TEXT("snow"), bDesert = M.Theme == TEXT("desert");
	const FLinearColor G0 = Th.Mini[FRSMap::T_BASE];
	const FLinearColor TuftC = bSnow ? FLinearColor(0.4f, 0.47f, 0.4f) : (bDesert ? FLinearColor(0.62f, 0.55f, 0.32f) : RS::Darken(G0, 0.28f));
	UInstancedStaticMeshComponent* Tuft[2] = { MakeISM(RSV::Cone(), TuftC, false), MakeISM(RSV::Cone(), RS::Lighten(TuftC, 0.18f), false) };
	UInstancedStaticMeshComponent* Pebble = MakeISM(RSV::Sphere(), bDesert ? FLinearColor(0.6f, 0.5f, 0.38f) : FLinearColor(0.5f, 0.49f, 0.47f), false);
	UInstancedStaticMeshComponent* Shrub = MakeISM(RSV::Sphere(), bDesert ? FLinearColor(0.42f, 0.38f, 0.2f) : FLinearColor(0.2f, 0.33f, 0.14f), false);
	UInstancedStaticMeshComponent* Flower[3] = {
		MakeISM(RSV::Sphere(), FLinearColor(0.95f, 0.95f, 0.9f), false),
		MakeISM(RSV::Sphere(), FLinearColor(0.98f, 0.85f, 0.2f), false),
		MakeISM(RSV::Sphere(), FLinearColor(0.7f, 0.45f, 0.9f), false) };
	for (int32 Y = 0; Y < M.H; ++Y)
		for (int32 X = 0; X < M.W; ++X)
		{
			const int32 I = M.Idx(X, Y);
			const uint8 T = M.Terrain[I];
			if (T == FRSMap::T_WATER || T == FRSMap::T_ROCK || T == FRSMap::T_ROAD || M.Cliff[I] || M.TreeAt[I] || M.Bridge[I] || Game->BldAt(X, Y)) continue;
			FRandomStream R(static_cast<int32>(Hash2(X + 101, Y + 57)));
			const FIntPoint Cell(X, Y);
			auto At = [&](float Ox, float Oy) { return Game->ToWorld(FVector2D(X + Ox, Y + Oy)); };
			// 草叢：3～5 根小錐體組成一叢，微微傾斜
			const float TuftP = T == FRSMap::T_BASE ? (bDesert ? 0.18f : (bSnow ? 0.14f : 0.55f)) : (T == FRSMap::T_DIRT ? 0.2f : 0.08f);
			if (R.FRand() < TuftP)
			{
				const int32 NC = 1 + (R.FRand() < 0.4f ? 1 : 0);
				for (int32 Cn = 0; Cn < NC; ++Cn)
				{
					const FVector Base = At(R.FRandRange(0.15f, 0.85f), R.FRandRange(0.15f, 0.85f));
					const int32 NB = R.RandRange(3, 5);
					UInstancedStaticMeshComponent* TI = Tuft[R.RandRange(0, 1)];
					for (int32 K = 0; K < NB; ++K)
					{
						const float H = R.FRandRange(7.f, 13.f);
						AddProp(TI, FTransform(FRotator(R.FRandRange(-18.f, 18.f), R.FRandRange(0.f, 360.f), R.FRandRange(-18.f, 18.f)),
							Base + FVector(R.FRandRange(-5.f, 5.f), R.FRandRange(-5.f, 5.f), H * 0.5f), FVector(0.035f, 0.035f, H / 100.f)), Cell);
					}
				}
			}
			// 野花（溫帶 / 熱帶的草地）
			if (!bSnow && !bDesert && T == FRSMap::T_BASE && R.FRand() < 0.1f)
			{
				UInstancedStaticMeshComponent* FI = Flower[R.RandRange(0, 2)];
				const FVector Base = At(R.FRandRange(0.2f, 0.8f), R.FRandRange(0.2f, 0.8f));
				for (int32 K = 0; K < R.RandRange(3, 6); ++K)
					AddProp(FI, FTransform(FRotator::ZeroRotator, Base + FVector(R.FRandRange(-9.f, 9.f), R.FRandRange(-9.f, 9.f), 3.f), FVector(0.03f)), Cell);
			}
			// 碎石
			const float PebP = T == FRSMap::T_DIRT ? 0.45f : (T == FRSMap::T_ALT ? 0.25f : (bDesert || bSnow ? 0.3f : 0.12f));
			if (R.FRand() < PebP)
			{
				for (int32 K = 0; K < R.RandRange(1, 3); ++K)
				{
					const float S = R.FRandRange(0.04f, 0.09f);
					AddProp(Pebble, FTransform(FRotator(0, R.FRandRange(0.f, 360.f), 0), At(R.FRandRange(0.1f, 0.9f), R.FRandRange(0.1f, 0.9f)) + FVector(0, 0, S * 15.f),
						FVector(S, S * 0.8f, S * 0.45f)), Cell);
				}
			}
			// 小灌木
			if ((bDesert || T == FRSMap::T_BASE) && !bSnow && R.FRand() < (bDesert ? 0.1f : 0.05f))
			{
				const FVector Base = At(R.FRandRange(0.2f, 0.8f), R.FRandRange(0.2f, 0.8f));
				for (int32 K = 0; K < 3; ++K)
				{
					const float S = R.FRandRange(0.1f, 0.16f);
					AddProp(Shrub, FTransform(FRotator::ZeroRotator, Base + FVector(R.FRandRange(-7.f, 7.f), R.FRandRange(-7.f, 7.f), S * 35.f), FVector(S, S, S * 0.75f)), Cell);
				}
			}
		}
}

void ARSTerrain::SetPropsShown(int32 CellIdx, bool bShown)
{
	if (bShown && BuiltCells.Contains(CellIdx)) bShown = false;
	TArray<int32> Ids;
	CellProps.MultiFind(CellIdx, Ids);
	for (int32 PIdx : Ids)
	{
		const FRSPropInst& P = Props[PIdx];
		if (!P.ISM) continue;
		P.ISM->UpdateInstanceTransform(P.Index, bShown ? P.Xf : FTransform(FRotator::ZeroRotator, FVector(0, 0, -50000.f), FVector(0.001f)), true, false, true);
		DirtyISM.Add(P.ISM);
	}
}

void ARSTerrain::BuildFog()
{
	const FRSMap& M = *Game->Map;
	FogISM = NewObject<UInstancedStaticMeshComponent>(this);
	FogISM->SetupAttachment(RootScene);
	FogISM->SetStaticMesh(RSV::Cube());
	FogISM->SetMaterial(0, RSV::Mat(FLinearColor(0.004f, 0.004f, 0.006f)));
	FogISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FogISM->SetCastShadow(false);
	FogISM->SetMobility(EComponentMobility::Movable);
	FogISM->RegisterComponent();
	AddInstanceComponent(FogISM);
	FogIndex.Init(-1, M.W * M.H);
	TArray<FTransform> Xf;
	Xf.Reserve(M.W * M.H);
	for (int32 Y = 0; Y < M.H; ++Y)
		for (int32 X = 0; X < M.W; ++X)
		{
			FogIndex[M.Idx(X, Y)] = Xf.Num();
			Xf.Add(FogXf(X, Y));
		}
	FogISM->AddInstances(Xf, false, true);
	ResetFog(Game->Explored);
}

void ARSTerrain::RevealCells(const TArray<int32>& Cells)
{
	if (!FogISM) return;
	for (int32 I : Cells)
	{
		if (!FogIndex.IsValidIndex(I) || FogIndex[I] < 0) continue;
		const int32 X = I % Game->W, Y = I / Game->W;
		FogISM->UpdateInstanceTransform(FogIndex[I], FTransform(FRotator::ZeroRotator, FVector((X + 0.5f) * RS_CELL, (Y + 0.5f) * RS_CELL, -50000.f), FVector(0.01f)), true, false, true);
		SetPropsShown(I, true);
	}
	FogISM->MarkRenderStateDirty();
	for (UInstancedStaticMeshComponent* D : DirtyISM) if (D) D->MarkRenderStateDirty();
	DirtyISM.Reset();
	Game->Map->bOreDirty = true;
}

void ARSTerrain::ResetFog(const TArray<uint8>& Explored)
{
	if (!FogISM) return;
	for (int32 I = 0; I < Explored.Num() && I < FogIndex.Num(); ++I)
	{
		const int32 X = I % Game->W, Y = I / Game->W;
		const bool bOpen = Explored[I] != 0;
		FogISM->UpdateInstanceTransform(FogIndex[I], bOpen
			? FTransform(FRotator::ZeroRotator, FVector((X + 0.5f) * RS_CELL, (Y + 0.5f) * RS_CELL, -50000.f), FVector(0.01f))
			: FogXf(X, Y), true, false, true);
		SetPropsShown(I, bOpen);
	}
	FogISM->MarkRenderStateDirty();
	for (UInstancedStaticMeshComponent* D : DirtyISM) if (D) D->MarkRenderStateDirty();
	DirtyISM.Reset();
}

void ARSTerrain::RebuildOre()
{
	FRSMap& M = *Game->Map;
	if (!OreISM) OreISM = MakeISM(RSV::Sphere(), FLinearColor(0.95f, 0.72f, 0.12f));
	if (!GemISM) GemISM = MakeISM(RSV::Sphere(), FLinearColor(0.72f, 0.3f, 0.95f));
	OreISM->ClearInstances();
	GemISM->ClearInstances();
	TArray<FTransform> Ore, Gem;
	for (int32 I : M.OreCells)
	{
		const int32 Amt = M.Ore[I];
		if (Amt <= 0) continue;
		const int32 X = I % M.W, Y = I / M.W;
		if (!Game->IsExplored(X, Y)) continue;
		const float F = FMath::Clamp(Amt / float(FRSMap::ORE_MAX), 0.15f, 1.f);
		const uint32 H = Hash2(X, Y);
		const FVector Base = Game->ToWorld(RS::CellCenter(FIntPoint(X, Y)));
		TArray<FTransform>& Dst = M.Gem[I] ? Gem : Ore;
		const int32 N = 2 + (H % 2);
		for (int32 K = 0; K < N; ++K)
		{
			const float A = (H >> (K * 3)) % 360 * PI / 180.f + K * 2.1f;
			const FVector Off(FMath::Cos(A) * 24.f, FMath::Sin(A) * 24.f, 6.f);
			const float S = (0.16f + 0.1f * ((H >> (K * 5)) & 3) / 3.f) * (0.55f + 0.45f * F);
			Dst.Add(FTransform(FRotator(float((H >> 3) % 30), float(K * 50), 0), Base + Off, FVector(S, S, S * 1.4f)));
		}
	}
	if (Ore.Num() > 0) OreISM->AddInstances(Ore, false, true);
	if (Gem.Num() > 0) GemISM->AddInstances(Gem, false, true);
	M.bOreDirty = false;
}

void ARSTerrain::UpdateTerrain(float Dt)
{
	OreT -= Dt;
	if (OreT <= 0.f && Game && Game->Map.IsValid() && Game->Map->bOreDirty)
	{
		OreT = 1.f;
		RebuildOre();
	}
}
