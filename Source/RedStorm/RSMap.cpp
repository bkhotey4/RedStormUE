#include "RSMap.h"
#include "RSCommon.h"
#include "RSGame.h"

// ───────────────────────── 雜訊 ─────────────────────────
void FRSNoise::Init(int32 Seed, float Frequency, int32 Octaves)
{
	Freq = Frequency;
	Oct = FMath::Max(1, Octaves);
	FRandomStream R(Seed);
	uint8 P[256];
	for (int32 I = 0; I < 256; ++I) P[I] = (uint8)I;
	for (int32 I = 255; I > 0; --I)
	{
		const int32 J = R.RandRange(0, I);
		Swap(P[I], P[J]);
	}
	for (int32 I = 0; I < 512; ++I) Perm[I] = P[I & 255];
	Offset = FVector2D(R.FRandRange(0.f, 256.f), R.FRandRange(0.f, 256.f));
}

static float Fade(float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); }
static float Grad(uint8 H, float X, float Y)
{
	switch (H & 7)
	{
	case 0: return X + Y;
	case 1: return -X + Y;
	case 2: return X - Y;
	case 3: return -X - Y;
	case 4: return X;
	case 5: return -X;
	case 6: return Y;
	default: return -Y;
	}
}

float FRSNoise::Perlin(float X, float Y) const
{
	const int32 XI = FMath::FloorToInt(X);
	const int32 YI = FMath::FloorToInt(Y);
	const float XF = X - XI, YF = Y - YI;
	const int32 Xa = XI & 255, Ya = YI & 255;
	const uint8 AA = Perm[Perm[Xa] + Ya], AB = Perm[Perm[Xa] + Ya + 1];
	const uint8 BA = Perm[Perm[Xa + 1] + Ya], BB = Perm[Perm[Xa + 1] + Ya + 1];
	const float U = Fade(XF), V = Fade(YF);
	const float X1 = FMath::Lerp(Grad(AA, XF, YF), Grad(BA, XF - 1.f, YF), U);
	const float X2 = FMath::Lerp(Grad(AB, XF, YF - 1.f), Grad(BB, XF - 1.f, YF - 1.f), U);
	return FMath::Lerp(X1, X2, V);
}

float FRSNoise::Get2D(float X, float Y) const
{
	float Sum = 0.f, Amp = 1.f, Norm = 0.f, F = Freq;
	for (int32 O = 0; O < Oct; ++O)
	{
		Sum += Perlin(X * F + Offset.X, Y * F + Offset.Y) * Amp;
		Norm += Amp;
		Amp *= 0.5f;
		F *= 2.f;
	}
	// 拉伸到接近 [-1, 1]，讓門檻值與 Godot 版相近
	return FMath::Clamp(Sum / Norm * 1.45f, -1.f, 1.f);
}

// ───────────────────────── 基本查詢 ─────────────────────────
bool FRSMap::Passable(int32 X, int32 Y) const
{
	if (!Inb(X, Y)) return false;
	const int32 I = Idx(X, Y);
	const uint8 T = Terrain[I];
	return T != T_WATER && T != T_ROCK && TreeAt[I] == 0 && Cliff[I] == 0;
}

int32 FRSMap::ElevAt(const FVector2D& P) const
{
	const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y);
	if (!Inb(X, Y)) return 0;
	return Elev[Idx(X, Y)];
}

float FRSMap::VtxH(int32 VX, int32 VY) const
{
	if (VH.Num() == 0) return 0.f;
	return VH[FMath::Clamp(VY, 0, H) * (W + 1) + FMath::Clamp(VX, 0, W)];
}

float FRSMap::HeightAt(const FVector2D& P) const
{
	if (VH.Num() == 0) return 0.f;
	const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y);
	if (!Inb(X, Y) || Elev[Idx(X, Y)] == 0) return 0.f;
	const float FX = P.X - X, FY = P.Y - Y;
	const float H0 = VtxH(X, Y), H1 = VtxH(X + 1, Y), H2 = VtxH(X + 1, Y + 1), H3 = VtxH(X, Y + 1);
	return FMath::Lerp(FMath::Lerp(H0, H1, FX), FMath::Lerp(H3, H2, FX), FY);
}

bool FRSMap::IsRamp(int32 X, int32 Y) const
{
	const int32 I = Idx(X, Y);
	if (Elev[I] == 0 || Cliff[I] != 0) return false;
	const FIntPoint D[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (const FIntPoint& O : D)
	{
		const int32 NX = X + O.X, NY = Y + O.Y;
		if (!Inb(NX, NY) || Elev[Idx(NX, NY)] == 0) return true;
	}
	return false;
}

void FRSMap::BuildVH()
{
	VH.SetNumZeroed((W + 1) * (H + 1));
	TArray<uint8> Cls;
	Cls.SetNumZeroed(W * H);
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Y * W + X;
			Cls[I] = Elev[I] == 0 ? 0 : (IsRamp(X, Y) ? 2 : 1);
		}
	for (int32 VY = 0; VY <= H; ++VY)
		for (int32 VX = 0; VX <= W; ++VX)
		{
			bool bHigh = false, bLow = false, bRamp = false;
			for (int32 YY = VY - 1; YY <= VY; ++YY)
				for (int32 XX = VX - 1; XX <= VX; ++XX)
				{
					if (XX < 0 || YY < 0 || XX >= W || YY >= H) { bLow = true; continue; }
					const uint8 C = Cls[YY * W + XX];
					if (C == 0) bLow = true;
					else if (C == 2) bRamp = true;
					else bHigh = true;
				}
			float Hh = 0.f;
			if (bHigh) Hh = 1.f;
			else if (bRamp && !bLow) Hh = 1.f;
			VH[VY * (W + 1) + VX] = Hh;
		}
}

// ───────────────────────── 生成 ─────────────────────────
void FRSMap::Generate(const FRSMapPreset& Preset, int32 Players, int32 Seed)
{
	W = FMath::Clamp(Preset.Size, 32, 400);
	H = W;
	Theme = Preset.Theme;
	Layout = Preset.Layout;
	const int32 NPlayers = FMath::Clamp(Players, 2, 4);
	Rng.Initialize(Seed);
	const int32 N = W * H;
	Terrain.Init(T_BASE, N);
	Ore.Init(0, N);
	Gem.Init(0, N);
	TreeAt.Init(0, N);
	Bridge.Init(0, N);
	Elev.Init(0, N);
	Cliff.Init(0, N);
	Trees.Reset();
	Rocks.Reset();
	Starts.Reset();
	Oils.Reset();
	Civs.Reset();
	OreCells.Reset();

	FRSNoise HN, PN, TN, RN;
	HN.Init(Rng.RandRange(0, 1 << 30), 0.045f, 4);
	PN.Init(Rng.RandRange(0, 1 << 30), 0.07f, 3);
	TN.Init(Rng.RandRange(0, 1 << 30), 0.09f, 3);
	RN.Init(Rng.RandRange(0, 1 << 30), 0.035f, 3);
	const float CX = W * 0.5f, CY = H * 0.5f;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			const float Hh = HN.Get2D(X, Y);
			const float DC = FVector2D(X - CX, Y - CY).Size() / (W * 0.5f);
			if (Layout == TEXT("plains"))
			{
				if (Hh < -0.5f) Terrain[I] = T_WATER;
				else if (Hh > 0.55f) Terrain[I] = T_ROCK;
			}
			else if (Layout == TEXT("lake"))
			{
				const float Lim = 0.36f + Hh * 0.18f;
				if (DC < Lim) Terrain[I] = T_WATER;
				else if (Hh < -0.52f) Terrain[I] = T_WATER;
				else if (Hh > 0.6f) Terrain[I] = T_ROCK;
			}
			else if (Layout == TEXT("canyon"))
			{
				const float R = FMath::Abs(RN.Get2D(X, Y));
				if (R < 0.075f && DC > 0.12f) Terrain[I] = T_ROCK;
				else if (Hh > 0.62f) Terrain[I] = T_ROCK;
			}
			else if (Layout == TEXT("river"))
			{
				if (Hh > 0.6f) Terrain[I] = T_ROCK;
			}
			else if (Layout == TEXT("coast"))
			{
				const float Side = float(Y - X) / W;
				if (Side > 0.16f + Hh * 0.08f || Side < -0.62f + Hh * 0.06f) Terrain[I] = T_WATER;
				else if (Hh > 0.62f) Terrain[I] = T_ROCK;
			}
			else if (Layout == TEXT("strait"))
			{
				const float U = float(X) / W, V = float(Y) / H;
				const float Coast = 0.30f + Hh * 0.05f + FMath::Sin(V * 9.f) * 0.02f;
				const float IU = (U - 0.70f) / 0.12f;
				const float IV = (V - 0.50f) / 0.34f;
				const float Lean = IU + IV * 0.35f;
				const bool bIsland = Lean * Lean + IV * IV * (1.f + 0.4f * FMath::Abs(IV)) < 1.f + Hh * 0.12f;
				const bool bPeng = FVector2D(U - 0.50f, V - 0.58f).Size() < 0.035f + Hh * 0.015f || FVector2D(U - 0.47f, V - 0.62f).Size() < 0.02f;
				if (U > Coast && !bIsland && !bPeng) Terrain[I] = T_WATER;
				else if (bIsland && Lean > 0.35f && FMath::Abs(IV) < 0.8f) Terrain[I] = T_ROCK;
				else if (Hh > 0.66f) Terrain[I] = T_ROCK;
			}
			else if (Layout == TEXT("gulf"))
			{
				const float U = float(X) / W, V = float(Y) / H;
				const float T = FMath::Clamp(((U - 0.05f) + (0.85f - V)) / 1.6f, 0.f, 1.f);
				const float DLine = FMath::Abs((U - 0.05f) - (0.85f - V)) / FMath::Sqrt(2.f);
				float Wdt = 0.12f * (1.f - T * 0.55f) + Hh * 0.03f;
				if (T > 0.82f) Wdt = 0.04f + Hh * 0.01f;
				if (DLine < Wdt && T > 0.03f) Terrain[I] = T_WATER;
				else if (Hh > 0.6f) Terrain[I] = T_ROCK;
			}
			if (Terrain[I] == T_BASE)
			{
				const float PV = PN.Get2D(X, Y);
				if (PV > 0.3f) Terrain[I] = T_DIRT;
				else if (PV < -0.45f) Terrain[I] = T_ALT;
			}
		}
	}
	if (Layout == TEXT("river")) CarveRiver(HN);
	const FRSTheme& Th = FRSDefs::Get().GetTheme(Theme);
	if (Theme == TEXT("temperate") || Theme == TEXT("tropical") || Layout == TEXT("coast"))
	{
		const TArray<uint8> WM = DilateMask(T_WATER, Th.Beach);
		for (int32 I = 0; I < N; ++I)
		{
			if (WM[I] == 1 && Terrain[I] != T_WATER && Terrain[I] != T_ROCK && Terrain[I] != T_ROAD) Terrain[I] = T_ALT;
		}
	}
	MakeRoads();
	MakePlateaus();
	BuildVH();
	// 出生點
	const int32 M = 12;
	TArray<FIntPoint> Cand = { {W - M, H - M}, {M - 1, M - 1}, {W - M, M - 1}, {M - 1, H - M} };
	if (Layout == TEXT("strait"))
	{
		Cand = { {int32(W * 0.64f), int32(H * 0.52f)}, {int32(W * 0.14f), int32(H * 0.30f)}, {int32(W * 0.14f), int32(H * 0.72f)}, {int32(W * 0.70f), int32(H * 0.27f)} };
	}
	else if (Layout == TEXT("gulf"))
	{
		Cand = { {int32(W * 0.18f), int32(H * 0.22f)}, {int32(W * 0.80f), int32(H * 0.78f)}, {int32(W * 0.62f), int32(H * 0.15f)}, {int32(W * 0.30f), int32(H * 0.88f)} };
	}
	for (int32 K = 0; K < NPlayers; ++K) Starts.Add(Cand[K]);
	for (const FIntPoint& S : Starts) ClearArea(S, 9.f);
	// 出生點附近的礦區
	const FVector2D Center(CX, CY);
	const bool bSpecial = Layout == TEXT("strait") || Layout == TEXT("gulf");
	for (const FIntPoint& S : Starts)
	{
		const FVector2D TOC = (Center - RS::Cell2(S)).GetSafeNormal();
		TArray<float> Angs = { -0.75f, 0.75f };
		if (bSpecial) Angs = { -0.75f, 0.75f, 2.4f, -2.4f };
		for (float A : Angs)
		{
			const FVector2D FP = RS::Cell2(S) + RS::Rot(TOC, A) * Rng.FRandRange(7.5f, 9.f);
			OreBlob(FP, Rng.FRandRange(2.6f, 3.2f), false);
		}
	}
	if (bSpecial)
	{
		for (int32 K = 0; K < 10; ++K)
		{
			const FVector2D Q(Rng.FRandRange(0.08f, 0.92f) * W, Rng.FRandRange(0.08f, 0.92f) * H);
			const int32 QX = (int32)Q.X, QY = (int32)Q.Y;
			if (Inb(QX, QY) && Terrain[Idx(QX, QY)] == T_BASE && !NearStart(QX, QY, 14.f)) OreBlob(Q, Rng.FRandRange(2.4f, 3.f), K % 3 == 0);
		}
	}
	OreBlob(Center, 2.8f, true);
	for (int32 K = 0; K < 4; ++K)
	{
		const float A = 2.f * PI * K / 4.f + PI * 0.25f;
		OreBlob(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * W * 0.24f, 2.6f, K % 2 == 0 && W >= 90);
	}
	const int32 Extra = int32(W * H / 12000.f) - 1;
	if (Extra > 0 && !bSpecial)
	{
		for (int32 K = 0; K < Extra * 3; ++K)
		{
			const FVector2D Q(Rng.FRandRange(0.08f, 0.92f) * W, Rng.FRandRange(0.08f, 0.92f) * H);
			const int32 QX = (int32)Q.X, QY = (int32)Q.Y;
			if (Inb(QX, QY) && Terrain[Idx(QX, QY)] != T_WATER && !NearStart(QX, QY, 16.f)) OreBlob(Q, Rng.FRandRange(2.4f, 3.1f), K % 3 == 0);
		}
	}
	// 油井
	TArray<FVector2D> OilSpots = { Center + FVector2D(-W * 0.14f, W * 0.14f), Center + FVector2D(W * 0.14f, -W * 0.14f) };
	if (NPlayers > 2 || W >= 90)
	{
		OilSpots.Add(Center + FVector2D(W * 0.14f, W * 0.14f));
		OilSpots.Add(Center + FVector2D(-W * 0.14f, -W * 0.14f));
	}
	if (Extra > 0 && !bSpecial)
	{
		for (int32 K = 0; K < Extra * 4; ++K)
		{
			const FVector2D Q(Rng.FRandRange(0.1f, 0.9f) * W, Rng.FRandRange(0.1f, 0.9f) * H);
			const int32 QX = (int32)Q.X, QY = (int32)Q.Y;
			bool bOk = Inb(QX, QY) && (Terrain[Idx(QX, QY)] == T_BASE || Terrain[Idx(QX, QY)] == T_DIRT || Terrain[Idx(QX, QY)] == T_ALT) && !NearStart(QX, QY, 18.f);
			for (const FVector2D& SP : OilSpots)
			{
				if (FVector2D::Distance(SP, Q) < 20.f) bOk = false;
			}
			if (bOk)
			{
				OilSpots.Add(Q);
				if (OilSpots.Num() >= 4 + Extra * 2) break;
			}
		}
	}
	if (Layout == TEXT("gulf"))
	{
		OilSpots.Reset();
		const FVector2D Q[] = { {0.12f, 0.62f}, {0.30f, 0.40f}, {0.48f, 0.30f}, {0.36f, 0.72f}, {0.58f, 0.60f}, {0.74f, 0.46f}, {0.80f, 0.22f}, {0.55f, 0.85f} };
		for (const FVector2D& V : Q) OilSpots.Add(FVector2D(V.X * W, V.Y * H));
	}
	else if (Layout == TEXT("strait"))
	{
		OilSpots.Reset();
		const FVector2D Q[] = { {0.20f, 0.50f}, {0.10f, 0.12f}, {0.10f, 0.90f}, {0.66f, 0.30f}, {0.66f, 0.70f}, {0.50f, 0.58f} };
		for (const FVector2D& V : Q) OilSpots.Add(FVector2D(V.X * W, V.Y * H));
	}
	for (const FVector2D& SP : OilSpots)
	{
		const FIntPoint C((int32)SP.X, (int32)SP.Y);
		ClearArea(C + FIntPoint(1, 1), 2.2f);
		for (int32 YY = C.Y - 1; YY < C.Y + 3; ++YY)
			for (int32 XX = C.X - 1; XX < C.X + 3; ++XX)
				if (Inb(XX, YY)) Ore[Idx(XX, YY)] = 0;
		Oils.Add(C);
	}
	MakeTowns();
	// 樹木
	TArray<uint8> Block = CircleMask(Starts, 10.5f);
	const TArray<uint8> OilM = CircleMask(Oils, 3.f, FVector2D(1, 1));
	for (const FRSCivSpot& C : Civs)
	{
		const FIntPoint Sz = FRSDefs::Get().GetDef(C.Id).Size;
		for (int32 YY = FMath::Max(0, C.TL.Y - 1); YY <= FMath::Min(H - 1, C.TL.Y + Sz.Y); ++YY)
			for (int32 XX = FMath::Max(0, C.TL.X - 1); XX <= FMath::Min(W - 1, C.TL.X + Sz.X); ++XX)
				Block[YY * W + XX] = 1;
	}
	const FString TreeKind = Th.bSnowTrees ? TEXT("snowpine") : (Theme == TEXT("desert") || Theme == TEXT("tropical") ? TEXT("palm") : TEXT("tree"));
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			if (Terrain[I] == T_WATER || Terrain[I] == T_ROCK || Terrain[I] == T_ROAD || Ore[I] > 0 || Block[I] == 1 || OilM[I] == 1) continue;
			const bool bBeachPalm = Theme == TEXT("tropical") && Terrain[I] == T_ALT && Rng.FRand() < 0.14f;
			if (bBeachPalm || (TN.Get2D(X, Y) > 0.28f && Rng.FRand() < Th.TreeDensity))
			{
				TreeAt[I] = 1;
				FRSTree T;
				T.C = FIntPoint(X, Y);
				T.Var = Rng.RandRange(0, 7);
				T.Kind = bBeachPalm ? FString(TEXT("palm")) : TreeKind;
				if (Theme == TEXT("desert") && Rng.FRand() < 0.15f) T.Kind = TEXT("tree");
				Trees.Add(T);
			}
		}
	}
	// 岩石
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
			if (Terrain[Idx(X, Y)] == T_ROCK)
			{
				const bool bInner = NearType(X, Y, T_ROCK, 1) && !(NearType(X, Y, T_BASE, 1) || NearType(X, Y, T_DIRT, 1));
				FRSRock R;
				R.C = FIntPoint(X, Y);
				R.Var = (bInner || Rng.FRand() < 0.6f) ? Rng.RandRange(0, 4) : 5 + Rng.RandRange(0, 2);
				Rocks.Add(R);
			}
	EnsureConnectivity();
	for (int32 I = 0; I < N; ++I)
		if (Ore[I] > 0) OreCells.Add(I);
	bOreDirty = true;
}

void FRSMap::MakePlateaus()
{
	if (Layout == TEXT("strait") || Layout == TEXT("gulf") || Layout == TEXT("coast")) return;
	FRSNoise EN;
	EN.Init(Rng.RandRange(0, 1 << 30), 0.028f, 3);
	const float CX = W * 0.5f, CY = H * 0.5f;
	const TArray<uint8> WM = DilateMask(T_WATER, 2);
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			const uint8 T = Terrain[I];
			if (T == T_WATER || T == T_ROCK || WM[I] == 1) continue;
			if (EN.Get2D(X, Y) > 0.3f && FVector2D::Distance(FVector2D(X, Y), FVector2D(CX, CY)) >= 7.f) Elev[I] = 1;
		}
	const int32 M = 12;
	const FIntPoint SS[4] = { {W - M, H - M}, {M - 1, M - 1}, {W - M, M - 1}, {M - 1, H - M} };
	for (const FIntPoint& S : SS)
		for (int32 YY = S.Y - 11; YY < S.Y + 12; ++YY)
			for (int32 XX = S.X - 11; XX < S.X + 12; ++XX)
				if (Inb(XX, YY) && FVector2D::Distance(FVector2D(XX, YY), RS::Cell2(S)) < 11.f) Elev[Idx(XX, YY)] = 0;
	// 移除太小的高地
	TArray<uint8> Seen;
	Seen.Init(0, W * H);
	const FIntPoint D4[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I0 = Idx(X, Y);
			if (Elev[I0] == 0 || Seen[I0] != 0) continue;
			TArray<FIntPoint> Q = { FIntPoint(X, Y) };
			Seen[I0] = 1;
			int32 Head = 0;
			while (Head < Q.Num())
			{
				const FIntPoint C = Q[Head++];
				for (const FIntPoint& Dd : D4)
				{
					const FIntPoint Nn = C + Dd;
					if (Inb(Nn.X, Nn.Y))
					{
						const int32 J = Idx(Nn.X, Nn.Y);
						if (Elev[J] == 1 && Seen[J] == 0) { Seen[J] = 1; Q.Add(Nn); }
					}
				}
			}
			if (Q.Num() < 40)
				for (const FIntPoint& C : Q) Elev[Idx(C.X, C.Y)] = 0;
		}
	for (int32 Y = 1; Y < H - 1; ++Y)
		for (int32 X = 1; X < W - 1; ++X)
		{
			const int32 I = Idx(X, Y);
			if (Elev[I] == 0 && Elev[Idx(X + 1, Y)] == 1 && Elev[Idx(X - 1, Y)] == 1 && Elev[Idx(X, Y + 1)] == 1 && Elev[Idx(X, Y - 1)] == 1) Elev[I] = 1;
		}
	// 懸崖邊緣
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Idx(X, Y);
			if (Elev[I] == 0) continue;
			bool bEdge = false;
			for (const FIntPoint& Dd : D4)
			{
				const int32 NX = X + Dd.X, NY = Y + Dd.Y;
				if (!Inb(NX, NY) || Elev[Idx(NX, NY)] == 0) bEdge = true;
			}
			if (bEdge)
			{
				Cliff[I] = Terrain[I] == T_ROAD ? 0 : 1;
				TreeAt[I] = 0;
				Ore[I] = 0;
			}
		}
	// 每塊高地至少 2 條坡道
	Seen.Init(0, W * H);
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I0 = Idx(X, Y);
			if (Elev[I0] == 0 || Seen[I0] != 0) continue;
			TArray<FIntPoint> Q = { FIntPoint(X, Y) };
			TArray<FIntPoint> Edges;
			Seen[I0] = 1;
			int32 Head = 0;
			while (Head < Q.Num())
			{
				const FIntPoint C = Q[Head++];
				if (Cliff[Idx(C.X, C.Y)] == 1) Edges.Add(C);
				for (const FIntPoint& Dd : D4)
				{
					const FIntPoint Nn = C + Dd;
					if (Inb(Nn.X, Nn.Y))
					{
						const int32 J = Idx(Nn.X, Nn.Y);
						if (Elev[J] == 1 && Seen[J] == 0) { Seen[J] = 1; Q.Add(Nn); }
					}
				}
			}
			if (Edges.Num() == 0) continue;
			const int32 NRamp = 2 + Q.Num() / 200;
			TArray<FIntPoint> Picked;
			for (int32 K = 0; K < NRamp * 6; ++K)
			{
				const FIntPoint E = Edges[Rng.RandRange(0, Edges.Num() - 1)];
				bool bOk = true;
				for (const FIntPoint& Pk : Picked)
					if (FVector2D::Distance(RS::Cell2(Pk), RS::Cell2(E)) < 10.f) bOk = false;
				if (bOk) Picked.Add(E);
				if (Picked.Num() >= NRamp) break;
			}
			for (const FIntPoint& E : Picked)
				for (int32 YY = E.Y - 1; YY <= E.Y + 1; ++YY)
					for (int32 XX = E.X - 1; XX <= E.X + 1; ++XX)
						if (Inb(XX, YY))
						{
							const int32 J = Idx(XX, YY);
							Cliff[J] = 0;
							if (Terrain[J] == T_WATER || Terrain[J] == T_ROCK) Terrain[J] = T_DIRT;
							TreeAt[J] = 0;
						}
		}
}

void FRSMap::MakeRoads()
{
	const bool bSpecial = Layout == TEXT("strait") || Layout == TEXT("gulf");
	for (int32 Dir = 0; Dir < 2; ++Dir)
	{
		const float Off = Rng.FRandRange(-0.12f, 0.12f) * W;
		for (int32 K = 0; K < W * 4; ++K)
		{
			const float T = float(K) / (W * 4);
			const float Along = T * W;
			const float Side = W * 0.5f + Off + FMath::Sin(T * 2.f * PI * 1.3f + Dir) * W * 0.06f;
			const FVector2D C = Dir == 0 ? FVector2D(Along, Side) : FVector2D(Side, Along);
			for (int32 YY = (int32)C.Y - 1; YY < (int32)C.Y + 2; ++YY)
				for (int32 XX = (int32)C.X - 1; XX < (int32)C.X + 2; ++XX)
				{
					if (!Inb(XX, YY)) continue;
					if (FVector2D::Distance(FVector2D(XX + 0.5f, YY + 0.5f), C) <= 1.15f)
					{
						const int32 I = Idx(XX, YY);
						if (Terrain[I] == T_WATER)
						{
							if (bSpecial) continue;
							Bridge[I] = 1;
						}
						Terrain[I] = T_ROAD;
					}
				}
		}
	}
}

bool FRSMap::CivAt(int32 X, int32 Y) const
{
	for (const FRSCivSpot& C : Civs)
	{
		const FIntPoint Sz = FRSDefs::Get().GetDef(C.Id).Size;
		if (X >= C.TL.X - 1 && Y >= C.TL.Y - 1 && X <= C.TL.X + Sz.X && Y <= C.TL.Y + Sz.Y) return true;
	}
	return false;
}

void FRSMap::MakeTowns()
{
	static const TCHAR* CIVS[] = { TEXT("civ_house"), TEXT("civ_house"), TEXT("civ_shop"), TEXT("civ_office"), TEXT("civ_gas"), TEXT("civ_tower"), TEXT("civ_barn") };
	TArray<FIntPoint> RoadCells;
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
			if (Terrain[Idx(X, Y)] == T_ROAD && Bridge[Idx(X, Y)] == 0) RoadCells.Add(FIntPoint(X, Y));
	if (RoadCells.Num() == 0) return;
	const int32 NTown = 3 + W / 32;
	for (int32 T = 0; T < NTown; ++T)
	{
		const FIntPoint RC = RoadCells[Rng.RandRange(0, RoadCells.Num() - 1)];
		if (NearStart(RC.X, RC.Y, 15.f) || NearOil(RC.X, RC.Y)) continue;
		int32 Placed = 0;
		const int32 Want = Rng.RandRange(3, 5);
		for (int32 Attempt = 0; Attempt < 40 && Placed < Want; ++Attempt)
		{
			const FString Id = CIVS[Rng.RandRange(0, 6)];
			const FIntPoint Sz = FRSDefs::Get().GetDef(Id).Size;
			const FIntPoint TL = RC + FIntPoint(Rng.RandRange(-5, 4), Rng.RandRange(-5, 4));
			bool bOk = true;
			for (int32 YY = TL.Y - 1; YY < TL.Y + Sz.Y + 1 && bOk; ++YY)
				for (int32 XX = TL.X - 1; XX < TL.X + Sz.X + 1; ++XX)
				{
					if (!Inb(XX, YY)) { bOk = false; break; }
					const bool bInside = XX >= TL.X && YY >= TL.Y && XX < TL.X + Sz.X && YY < TL.Y + Sz.Y;
					const uint8 Tt = Terrain[Idx(XX, YY)];
					if (bInside && (Tt == T_WATER || Tt == T_ROCK || Tt == T_ROAD || Ore[Idx(XX, YY)] > 0 || Cliff[Idx(XX, YY)] != 0)) { bOk = false; break; }
				}
			if (!bOk || CivAt(TL.X, TL.Y) || CivAt(TL.X + Sz.X - 1, TL.Y + Sz.Y - 1) || NearStart(TL.X, TL.Y, 13.f) || NearOil(TL.X, TL.Y)) continue;
			FRSCivSpot S;
			S.Id = Id;
			S.TL = TL;
			S.Var = Rng.RandRange(0, 1);
			Civs.Add(S);
			Placed++;
		}
	}
}

void FRSMap::CarveRiver(const FRSNoise& HN)
{
	const int32 L = int32(W * 1.45f);
	const float Fords[3] = { 0.3f, 0.5f, 0.7f };
	const FVector2D Perp = FVector2D(1, 1).GetSafeNormal();
	for (int32 K = 0; K < L; ++K)
	{
		const float T = float(K) / L;
		const FVector2D Base(T * W, H - T * H);
		const float Off = FMath::Sin(T * 2.f * PI * 1.5f + 0.7f) * W * 0.07f + HN.Get1D(T * 60.f) * 3.f;
		const FVector2D C = Base + Perp * Off;
		const float Wdt = 2.2f + HN.Get1D(T * 40.f + 9.f) * 0.8f;
		bool bFord = false;
		for (float F : Fords)
			if (FMath::Abs(T - F) < 0.025f) bFord = true;
		for (int32 YY = int32(C.Y - 5); YY < int32(C.Y + 6); ++YY)
			for (int32 XX = int32(C.X - 5); XX < int32(C.X + 6); ++XX)
			{
				if (!Inb(XX, YY)) continue;
				const float Dd = FVector2D::Distance(FVector2D(XX + 0.5f, YY + 0.5f), C);
				const int32 I = Idx(XX, YY);
				if (Dd < Wdt) Terrain[I] = bFord ? T_DIRT : T_WATER;
				else if (Dd < Wdt + 1.2f && Terrain[I] != T_WATER && bFord) Terrain[I] = T_DIRT;
			}
	}
}

TArray<uint8> FRSMap::DilateMask(uint8 T, int32 R) const
{
	TArray<uint8> A, B;
	A.Init(0, W * H);
	B.Init(0, W * H);
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
			if (Terrain[Y * W + X] == T)
				for (int32 XX = FMath::Max(0, X - R); XX <= FMath::Min(W - 1, X + R); ++XX) A[Y * W + XX] = 1;
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
			if (A[Y * W + X] == 1)
				for (int32 YY = FMath::Max(0, Y - R); YY <= FMath::Min(H - 1, Y + R); ++YY) B[YY * W + X] = 1;
	return B;
}

TArray<uint8> FRSMap::CircleMask(const TArray<FIntPoint>& Centers, float R, FVector2D Off) const
{
	TArray<uint8> Mk;
	Mk.Init(0, W * H);
	const float R2 = R * R;
	for (const FIntPoint& C : Centers)
	{
		const FVector2D CV = RS::Cell2(C) + Off;
		for (int32 YY = FMath::Max(0, int32(CV.Y - R)); YY <= FMath::Min(H - 1, int32(CV.Y + R) + 1); ++YY)
			for (int32 XX = FMath::Max(0, int32(CV.X - R)); XX <= FMath::Min(W - 1, int32(CV.X + R) + 1); ++XX)
				if (FVector2D::DistSquared(FVector2D(XX, YY), CV) < R2) Mk[YY * W + XX] = 1;
	}
	return Mk;
}

bool FRSMap::NearType(int32 X, int32 Y, uint8 T, int32 R) const
{
	for (int32 YY = Y - R; YY <= Y + R; ++YY)
		for (int32 XX = X - R; XX <= X + R; ++XX)
			if (Inb(XX, YY) && Terrain[Idx(XX, YY)] == T) return true;
	return false;
}

bool FRSMap::NearStart(int32 X, int32 Y, float R) const
{
	for (const FIntPoint& S : Starts)
		if (FVector2D::Distance(FVector2D(X, Y), RS::Cell2(S)) < R) return true;
	return false;
}

bool FRSMap::NearOil(int32 X, int32 Y) const
{
	for (const FIntPoint& O : Oils)
		if (FVector2D::Distance(FVector2D(X, Y), RS::Cell2(O) + FVector2D(1, 1)) < 3.f) return true;
	return false;
}

void FRSMap::ClearArea(FIntPoint C, float R)
{
	for (int32 YY = int32(C.Y - R - 1); YY < int32(C.Y + R + 2); ++YY)
		for (int32 XX = int32(C.X - R - 1); XX < int32(C.X + R + 2); ++XX)
			if (Inb(XX, YY) && FVector2D::Distance(FVector2D(XX, YY), RS::Cell2(C)) <= R)
			{
				const int32 I = Idx(XX, YY);
				if (Terrain[I] == T_WATER || Terrain[I] == T_ROCK) Terrain[I] = T_BASE;
				TreeAt[I] = 0;
				Cliff[I] = 0;
			}
}

void FRSMap::OreBlob(FVector2D C, float R, bool bGem)
{
	for (int32 YY = int32(C.Y - R - 1); YY < int32(C.Y + R + 2); ++YY)
		for (int32 XX = int32(C.X - R - 1); XX < int32(C.X + R + 2); ++XX)
		{
			if (!Inb(XX, YY)) continue;
			const float Dd = FVector2D::Distance(FVector2D(XX + 0.5f, YY + 0.5f), C);
			const float Jit = Rng.FRandRange(-0.6f, 0.6f);
			if (Dd + Jit < R)
			{
				const int32 I = Idx(XX, YY);
				if (Terrain[I] == T_WATER || Terrain[I] == T_ROCK || Terrain[I] == T_ROAD) continue;
				if (NearStart(XX, YY, 4.5f)) continue;
				TreeAt[I] = 0;
				Ore[I] = (int32)FMath::Clamp(ORE_MAX * (1.1f - Dd / R * 0.7f) * Rng.FRandRange(0.7f, 1.f), 120.f, (float)ORE_MAX);
				Gem[I] = bGem ? 1 : 0;
			}
		}
}

void FRSMap::EnsureConnectivity()
{
	if (Layout == TEXT("strait")) return;
	TArray<FIntPoint> Targets;
	for (int32 K = 1; K < Starts.Num(); ++K) Targets.Add(Starts[K]);
	for (const FIntPoint& O : Oils) Targets.Add(O + FIntPoint(-1, 0));
	TArray<uint8> Seen = Flood(Starts[0]);
	for (const FIntPoint& T : Targets)
	{
		if (!Inb(T.X, T.Y)) continue;
		if (Seen[Idx(T.X, T.Y)] == 0)
		{
			CarveLine(Starts[0], T);
			Seen = Flood(Starts[0]);
		}
	}
}

TArray<uint8> FRSMap::Flood(FIntPoint A) const
{
	TArray<uint8> Seen;
	Seen.Init(0, W * H);
	if (!Inb(A.X, A.Y)) return Seen;
	TArray<int32> Q;
	Q.Reserve(W * H);
	Q.Add(Idx(A.X, A.Y));
	Seen[Idx(A.X, A.Y)] = 1;
	int32 Head = 0;
	while (Head < Q.Num())
	{
		const int32 C = Q[Head++];
		const int32 X = C % W, Y = C / W;
		if (X + 1 < W && Seen[C + 1] == 0 && Passable(X + 1, Y)) { Seen[C + 1] = 1; Q.Add(C + 1); }
		if (X > 0 && Seen[C - 1] == 0 && Passable(X - 1, Y)) { Seen[C - 1] = 1; Q.Add(C - 1); }
		if (Y + 1 < H && Seen[C + W] == 0 && Passable(X, Y + 1)) { Seen[C + W] = 1; Q.Add(C + W); }
		if (Y > 0 && Seen[C - W] == 0 && Passable(X, Y - 1)) { Seen[C - W] = 1; Q.Add(C - W); }
	}
	return Seen;
}

void FRSMap::CarveLine(FIntPoint A, FIntPoint B)
{
	const int32 Steps = int32(FVector2D::Distance(RS::Cell2(A), RS::Cell2(B)) * 2) + 1;
	for (int32 K = 0; K <= Steps; ++K)
	{
		const FVector2D P = RS::Lerp2(RS::Cell2(A), RS::Cell2(B), float(K) / Steps);
		for (int32 YY = int32(P.Y) - 1; YY < int32(P.Y) + 2; ++YY)
			for (int32 XX = int32(P.X) - 1; XX < int32(P.X) + 2; ++XX)
				if (Inb(XX, YY))
				{
					const int32 I = Idx(XX, YY);
					if (Terrain[I] == T_WATER || Terrain[I] == T_ROCK) Terrain[I] = T_DIRT;
					TreeAt[I] = 0;
					Cliff[I] = 0;
				}
	}
	Trees.RemoveAll([this](const FRSTree& T) { return TreeAt[Idx(T.C.X, T.C.Y)] == 0; });
	Rocks.RemoveAll([this](const FRSRock& R) { return Terrain[Idx(R.C.X, R.C.Y)] != T_ROCK; });
}

// ───────────────────────── 預覽 / 小地圖 ─────────────────────────
FLinearColor FRSMap::BaseColor(int32 I) const
{
	const FRSTheme& Th = FRSDefs::Get().GetTheme(Theme);
	FLinearColor C = Th.Mini[FMath::Clamp<int32>(Terrain[I], 0, 5)];
	if (Elev[I] == 1) C = RS::Lighten(C, 0.18f);
	if (Cliff[I] == 1) C = FLinearColor(0.45f, 0.38f, 0.3f);
	if (TreeAt[I] != 0) C = Theme != TEXT("snow") ? FLinearColor(0.12f, 0.25f, 0.1f) : FLinearColor(0.2f, 0.32f, 0.25f);
	if (Ore[I] > 0) C = Gem[I] == 0 ? FLinearColor(0.9f, 0.7f, 0.15f) : FLinearColor(0.7f, 0.35f, 0.9f);
	return C;
}

TArray<FColor> FRSMap::MakePreview(bool bShowStarts) const
{
	TArray<FColor> Out;
	Out.SetNumUninitialized(W * H);
	for (int32 I = 0; I < W * H; ++I) Out[I] = BaseColor(I).ToFColor(false);
	if (bShowStarts)
	{
		for (int32 K = 0; K < Starts.Num(); ++K)
		{
			const FIntPoint S = Starts[K];
			for (int32 YY = S.Y - 2; YY <= S.Y + 2; ++YY)
				for (int32 XX = S.X - 2; XX <= S.X + 2; ++XX)
					if (Inb(XX, YY)) Out[Idx(XX, YY)] = K == 0 ? FColor::White : FColor(255, 77, 51);
		}
	}
	for (const FIntPoint& O : Oils)
		if (Inb(O.X, O.Y)) Out[Idx(O.X, O.Y)] = FColor(25, 25, 25);
	return Out;
}

// ───────────────────────── 礦石 ─────────────────────────
void FRSMap::Update(float Dt)
{
	RegrowT += Dt;
	if (RegrowT <= 6.f) return;
	RegrowT = 0.f;
	TArray<int32> Add;
	for (int32 I : OreCells)
	{
		if (Ore[I] > 0 && Ore[I] < ORE_MAX)
		{
			Ore[I] = FMath::Min(ORE_MAX, Ore[I] + 12);
			bOreDirty = true;
		}
		if (Ore[I] > ORE_MAX * 0.8f && Rng.FRand() < 0.02f)
		{
			const int32 X = I % W + Rng.RandRange(-1, 1);
			const int32 Y = I / W + Rng.RandRange(-1, 1);
			if (Inb(X, Y))
			{
				const int32 J = Idx(X, Y);
				if (Ore[J] == 0 && Passable(X, Y) && (!Game || Game->BldAt(X, Y) == nullptr))
				{
					Ore[J] = 60;
					Gem[J] = Gem[I];
					Add.Add(J);
				}
			}
		}
	}
	for (int32 J : Add) OreCells.Add(J);
}

int32 FRSMap::TakeOre(int32 X, int32 Y, int32 Amt)
{
	const int32 I = Idx(X, Y);
	const int32 Got = FMath::Min(Ore[I], Amt);
	Ore[I] -= Got;
	if (Ore[I] <= 0)
	{
		Ore[I] = 0;
		OreCells.Remove(I);
	}
	bOreDirty = true;
	return Got * (Gem[I] == 1 ? 2 : 1);
}

FIntPoint FRSMap::FindOreNear(const FVector2D& From, float MaxD) const
{
	FIntPoint Best(-1, -1);
	float BD = MaxD;
	for (int32 I : OreCells)
	{
		if (Ore[I] <= 0) continue;
		const int32 X = I % W, Y = I / W;
		if (Game && Game->BldAt(X, Y) != nullptr) continue;
		float D = FVector2D::Distance(From, FVector2D(X + 0.5f, Y + 0.5f));
		if (Gem[I] == 1) D -= 2.f;
		if (D < BD)
		{
			BD = D;
			Best = FIntPoint(X, Y);
		}
	}
	return Best;
}

void FRSMap::ClearOre(int32 X, int32 Y)
{
	const int32 I = Idx(X, Y);
	if (Ore[I] > 0)
	{
		Ore[I] = 0;
		OreCells.Remove(I);
		bOreDirty = true;
	}
}
