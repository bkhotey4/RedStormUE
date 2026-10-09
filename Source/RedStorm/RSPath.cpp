#include "RSPath.h"
#include "Algo/Reverse.h"

void FRSGridPath::Init(int32 InW, int32 InH)
{
	W = InW;
	H = InH;
	Solid.Init(0, W * H);
	G.Init(0.f, W * H);
	Parent.Init(-1, W * H);
	Stamp.Init(0, W * H);
	Closed.Init(0, W * H);
	CurStamp = 1;
}

namespace
{
	struct FNode
	{
		float F;
		int32 I;
	};
	struct FNodeLess
	{
		bool operator()(const FNode& A, const FNode& B) const { return A.F < B.F; }
	};
	inline float Octile(int32 DX, int32 DY)
	{
		DX = FMath::Abs(DX);
		DY = FMath::Abs(DY);
		const int32 Mn = FMath::Min(DX, DY), Mx = FMath::Max(DX, DY);
		return (float)(Mx - Mn) + 1.41421356f * Mn;
	}
}

TArray<FIntPoint> FRSGridPath::FindPath(FIntPoint A, FIntPoint B, bool bPartial)
{
	TArray<FIntPoint> Out;
	if (W <= 0 || A.X < 0 || A.Y < 0 || A.X >= W || A.Y >= H) return Out;
	B.X = FMath::Clamp(B.X, 0, W - 1);
	B.Y = FMath::Clamp(B.Y, 0, H - 1);
	if (IsSolid(A.X, A.Y)) return Out;
	if (++CurStamp == 0)
	{
		Stamp.Init(0, W * H);
		CurStamp = 1;
	}
	const int32 Start = A.Y * W + A.X;
	const int32 Goal = B.Y * W + B.X;
	TArray<FNode> Open;
	Open.Reserve(1024);
	Stamp[Start] = CurStamp;
	G[Start] = 0.f;
	Parent[Start] = -1;
	Closed[Start] = 0;
	Open.HeapPush(FNode{ Octile(B.X - A.X, B.Y - A.Y), Start }, FNodeLess());
	int32 BestI = Start;
	float BestH = Octile(B.X - A.X, B.Y - A.Y);
	int32 Found = -1;
	int32 Iter = 0;
	static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	while (Open.Num() > 0 && Iter++ < 250000)
	{
		FNode N;
		Open.HeapPop(N, FNodeLess());
		const int32 Ci = N.I;
		if (Stamp[Ci] == CurStamp && Closed[Ci]) continue;
		Closed[Ci] = 1;
		if (Ci == Goal)
		{
			Found = Ci;
			break;
		}
		const int32 CX = Ci % W, CY = Ci / W;
		const float Hh = Octile(B.X - CX, B.Y - CY);
		if (Hh < BestH)
		{
			BestH = Hh;
			BestI = Ci;
		}
		for (int32 K = 0; K < 8; ++K)
		{
			const int32 NX = CX + DX[K], NY = CY + DY[K];
			if (IsSolid(NX, NY)) continue;
			if (K >= 4 && (IsSolid(CX + DX[K], CY) || IsSolid(CX, CY + DY[K]))) continue;
			const int32 Ni = NY * W + NX;
			const float Step = K >= 4 ? 1.41421356f : 1.f;
			const float NG = G[Ci] + Step;
			if (Stamp[Ni] != CurStamp)
			{
				Stamp[Ni] = CurStamp;
				Closed[Ni] = 0;
				G[Ni] = NG;
				Parent[Ni] = Ci;
				Open.HeapPush(FNode{ NG + Octile(B.X - NX, B.Y - NY), Ni }, FNodeLess());
			}
			else if (!Closed[Ni] && NG < G[Ni])
			{
				G[Ni] = NG;
				Parent[Ni] = Ci;
				Open.HeapPush(FNode{ NG + Octile(B.X - NX, B.Y - NY), Ni }, FNodeLess());
			}
		}
	}
	int32 End = Found;
	if (End < 0)
	{
		if (!bPartial) return Out;
		End = BestI;
	}
	for (int32 I = End; I >= 0; I = Parent[I])
	{
		Out.Add(FIntPoint(I % W, I / W));
		if (I == Start) break;
	}
	Algo::Reverse(Out);
	return Out;
}
