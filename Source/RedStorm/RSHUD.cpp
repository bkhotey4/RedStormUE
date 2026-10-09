#include "RSHUD.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGlobals.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSGameMode.h"
#include "RSPlayerController.h"
#include "RSAudio.h"
#include "RSMap.h"
#include "RSSaveLoad.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "GlobalRenderResources.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "ImageUtils.h"
#include "MediaTexture.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/CompositeFont.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"

ARSHUD::ARSHUD()
{
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

// ───────────────────────── 字型 ─────────────────────────
static UFont* MakeRuntimeFont(UObject* Outer, const TArray<FString>& Candidates)
{
	FString WinDir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
	if (WinDir.IsEmpty()) WinDir = TEXT("C:/Windows");
	TArray<FString> Paths;
	for (const FString& C : Candidates)
	{
		Paths.Add(WinDir / TEXT("Fonts") / C);
		Paths.Add(RS::ContentDir() / TEXT("Fonts") / C);
	}
	for (const FString& P : Paths)
	{
		if (!FPaths::FileExists(P)) continue;
		UFont* F = NewObject<UFont>(Outer);
		F->FontCacheType = EFontCacheType::Runtime;
		F->LegacyFontSize = 16;
		FTypefaceEntry Entry;
		Entry.Name = TEXT("Regular");
		Entry.Font = FFontData(P, EFontHinting::Default, EFontLoadingPolicy::LazyLoad, 0);
		F->CompositeFont.DefaultTypeface.Fonts.Add(Entry);
		UE_LOG(LogRS, Log, TEXT("UI font: %s"), *P);
		return F;
	}
	return nullptr;
}

void ARSHUD::BeginPlay()
{
	Super::BeginPlay();
	// 自訂字型可放在 Content/RedStorm/Fonts/（例如 NotoSansTC-Regular.ttf）
	Font = MakeRuntimeFont(this, { TEXT("NotoSansTC-Regular.ttf"), TEXT("msjh.ttc"), TEXT("msjh.ttf"), TEXT("msyh.ttc"), TEXT("mingliu.ttc"), TEXT("simsun.ttc") });
	FontBold = MakeRuntimeFont(this, { TEXT("NotoSansTC-Bold.ttf"), TEXT("msjhbd.ttc"), TEXT("msjhbd.ttf"), TEXT("msyhbd.ttc"), TEXT("msjh.ttc"), TEXT("mingliu.ttc") });
	if (!FontBold) FontBold = Font;
	const FRSUserSettings& US = FRSUserSettings::Get();
	const int32 CI = FRSDefs::Get().CountryOrder.IndexOfByKey(US.Country);
	OptCountry = CI >= 0 ? CI : 0;
	OptMapSize = FMath::Clamp(US.MapSize, 0, 3);
	BgStyle = US.MenuBg >= 0 ? US.MenuBg % 4 : FMath::RandRange(0, 3);
	PreviewSeed = FMath::Rand();
}

ARSGame* ARSHUD::Game() const
{
	ARSGameMode* G = GM();
	return G ? G->Game.Get() : nullptr;
}

ARSGameMode* ARSHUD::GM() const
{
	return GetWorld() ? Cast<ARSGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
}

ARSPlayerController* ARSHUD::PC() const
{
	return Cast<ARSPlayerController>(GetOwningPlayerController());
}

// ───────────────────────── 繪圖基本功 ─────────────────────────
void ARSHUD::Rect(float X, float Y, float W, float H, const FLinearColor& C)
{
	if (!Canvas || W <= 0.f || H <= 0.f || C.A <= 0.f) return;
	FCanvasTileItem Tile(FVector2D(X * S, Y * S), FVector2D(W * S, H * S), C);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ARSHUD::Frame(float X, float Y, float W, float H, const FLinearColor& C, float Th)
{
	Rect(X, Y, W, Th, C);
	Rect(X, Y + H - Th, W, Th, C);
	Rect(X, Y, Th, H, C);
	Rect(X + W - Th, Y, Th, H, C);
}

void ARSHUD::Line(float X0, float Y0, float X1, float Y1, const FLinearColor& C, float Th)
{
	if (!Canvas || C.A <= 0.f) return;
	FCanvasLineItem L(FVector2D(X0 * S, Y0 * S), FVector2D(X1 * S, Y1 * S));
	L.SetColor(C);
	L.LineThickness = FMath::Max(1.f, Th * S);
	L.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(L);
}

void ARSHUD::Tri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Col)
{
	if (!Canvas || Col.A <= 0.f) return;
	FCanvasTriangleItem TI(A * S, B * S, C * S, GWhiteTexture);
	TI.SetColor(Col);
	TI.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TI);
}

void ARSHUD::Poly(const TArray<FVector2D>& Pts, const FLinearColor& C)
{
	for (int32 I = 1; I + 1 < Pts.Num(); ++I) Tri(Pts[0], Pts[I], Pts[I + 1], C);
}

void ARSHUD::Circle(const FVector2D& C, float R, const FLinearColor& Col, int32 Seg)
{
	for (int32 I = 0; I < Seg; ++I)
	{
		const float A0 = 2.f * PI * I / Seg, A1 = 2.f * PI * (I + 1) / Seg;
		Tri(C, C + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R, C + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R, Col);
	}
}

void ARSHUD::Ring(const FVector2D& C, float R, const FLinearColor& Col, float Th, int32 Seg, float Sy)
{
	for (int32 I = 0; I < Seg; ++I)
	{
		const float A0 = 2.f * PI * I / Seg, A1 = 2.f * PI * (I + 1) / Seg;
		Line(C.X + FMath::Cos(A0) * R, C.Y + FMath::Sin(A0) * R * Sy, C.X + FMath::Cos(A1) * R, C.Y + FMath::Sin(A1) * R * Sy, Col, Th);
	}
}

void ARSHUD::Star(const FVector2D& C, float R, const FLinearColor& Col)
{
	TArray<FVector2D> P;
	for (int32 K = 0; K < 10; ++K)
	{
		const float Rr = K % 2 == 0 ? R : R * 0.42f;
		const float A = -PI / 2.f + K * PI / 5.f;
		P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr);
	}
	for (int32 K = 0; K < 10; ++K) Tri(C, P[K], P[(K + 1) % 10], Col);
}

void ARSHUD::Tex(UTexture* Tx, float X, float Y, float W, float H, const FLinearColor& Tint)
{
	if (!Canvas || !Tx || !Tx->GetResource()) return;
	FCanvasTileItem Tile(FVector2D(X * S, Y * S), Tx->GetResource(), FVector2D(W * S, H * S), Tint);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

float ARSHUD::TextW(const FString& Txt, float Size, bool bBold)
{
	UFont* F = bBold ? FontBold.Get() : Font.Get();
	if (!F) F = GEngine->GetMediumFont();
	const int32 Pt = FMath::Max(1, FMath::RoundToInt(Size * S * 0.75f));
	float W = 0.f;
	if (!FSlateApplication::IsInitialized()) return Txt.Len() * Size * 0.6f;
	TSharedRef<FSlateFontMeasure> MS = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo FI(F, Pt);
	for (TCHAR Ch : Txt)
	{
		const uint64 Key = (uint64(Ch) << 24) | (uint64(Pt) << 1) | (bBold ? 1 : 0);
		if (const float* G = GlyphW.Find(Key))
		{
			W += *G;
			continue;
		}
		const float GW = MS->Measure(FString(1, &Ch), FI).X / S;
		GlyphW.Add(Key, GW);
		W += GW;
	}
	return W;
}

void ARSHUD::Text(const FString& Txt, float X, float Y, float Size, const FLinearColor& C, bool bBold, int32 Align, float W, bool bShadow)
{
	if (!Canvas || Txt.IsEmpty() || C.A <= 0.01f) return;
	UFont* F = bBold ? FontBold.Get() : Font.Get();
	if (!F) F = GEngine->GetMediumFont();
	float DX = X;
	if (Align != 0)
	{
		const float TW = TextW(Txt, Size, bBold);
		DX = Align == 1 ? X + (W - TW) * 0.5f : X + W - TW;
	}
	const FSlateFontInfo FI(F, FMath::Max(1, FMath::RoundToInt(Size * S * 0.75f)));
	FCanvasTextItem TI(FVector2D(DX * S, Y * S), FText::FromString(Txt), FI, C);
	if (bShadow) TI.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.75f * C.A), FVector2D(1.f, 1.f) * FMath::Max(1.f, S));
	TI.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(TI);
}

float ARSHUD::TextWrap(const FString& Txt, float X, float Y, float W, float Size, const FLinearColor& C, float LineH, bool bBold, int32 MaxChars)
{
	if (LineH <= 0.f) LineH = Size * 1.45f;
	const FString Src = MaxChars >= 0 ? Txt.Left(MaxChars) : Txt;
	FString Cur;
	float CurW = 0.f;
	float YY = Y;
	auto Flush = [&]()
	{
		Text(Cur, X, YY, Size, C, bBold);
		Cur.Empty();
		CurW = 0.f;
		YY += LineH;
	};
	for (int32 I = 0; I < Src.Len(); ++I)
	{
		const TCHAR Ch = Src[I];
		if (Ch == TCHAR('\n'))
		{
			Flush();
			continue;
		}
		const float GW = TextW(FString(1, &Ch), Size, bBold);
		if (CurW + GW > W && Cur.Len() > 0)
		{
			// 英文單字盡量不拆開
			int32 Sp = INDEX_NONE;
			if (Ch != TCHAR(' ') && Cur.FindLastChar(TCHAR(' '), Sp) && Sp > 0 && Cur.Len() - Sp < 16 && FChar::IsAlpha(Ch))
			{
				const FString Tail = Cur.Mid(Sp + 1);
				Cur = Cur.Left(Sp);
				Flush();
				Cur = Tail;
				CurW = TextW(Cur, Size, bBold);
			}
			else
			{
				Flush();
			}
			if (Ch == TCHAR(' ')) continue;
		}
		Cur.AppendChar(Ch);
		CurW += GW;
	}
	if (Cur.Len() > 0) Flush();
	return YY;
}

void ARSHUD::Brackets(float X, float Y, float W, float H, const FLinearColor& C, float Len, float Th)
{
	const float L = FMath::Min(Len, FMath::Min(W, H) * 0.45f);
	Rect(X, Y, L, Th, C); Rect(X, Y, Th, L, C);
	Rect(X + W - L, Y, L, Th, C); Rect(X + W - Th, Y, Th, L, C);
	Rect(X, Y + H - Th, L, Th, C); Rect(X, Y + H - L, Th, L, C);
	Rect(X + W - L, Y + H - Th, L, Th, C); Rect(X + W - Th, Y + H - L, Th, L, C);
}

// 戰術面板：深色玻璃底、細框、切角、四角括號
void ARSHUD::Bevel(float X, float Y, float W, float H, const FLinearColor& Base, bool bInset)
{
	const float Cc = FMath::Min(7.f, FMath::Min(W, H) * 0.3f);
	const FLinearColor Fill = bInset ? RS::Darken(Base, 0.3f) : Base;
	Poly({ FVector2D(X + Cc, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H - Cc), FVector2D(X + W - Cc, Y + H), FVector2D(X, Y + H), FVector2D(X, Y + Cc) }, RS::WithA(Fill, 0.88f));
	// 上緣微光
	Rect(X + Cc, Y + 1, W - Cc - 1, FMath::Min(H * 0.35f, 10.f), FLinearColor(1, 1, 1, bInset ? 0.015f : 0.035f));
	const FLinearColor L = RS::Lighten(Accent, 0.2f);
	const float La = bInset ? 0.28f : 0.45f;
	Line(X + Cc, Y, X + W, Y, RS::WithA(L, La));
	Line(X + W, Y, X + W, Y + H - Cc, RS::WithA(L, La));
	Line(X + W, Y + H - Cc, X + W - Cc, Y + H, RS::WithA(L, La));
	Line(X + W - Cc, Y + H, X, Y + H, RS::WithA(L, La));
	Line(X, Y + H, X, Y + Cc, RS::WithA(L, La));
	Line(X, Y + Cc, X + Cc, Y, RS::WithA(L, La));
	if (!bInset)
	{
		Line(X + Cc, Y, X + Cc + 18, Y, L, 2.f);
		Line(X + W - Cc - 18, Y + H, X + W - Cc, Y + H, L, 2.f);
		Rect(X + W - 5, Y + 3, 2, 2, RS::WithA(L, 0.8f));
	}
}

bool ARSHUD::Button(float X, float Y, float W, float H, const FString& Label, float Size, bool bEnabled, bool bOn)
{
	const bool bHov = bEnabled && Hovered(X, Y, W, H);
	FLinearColor BC = bOn ? RS::Darken(Accent, 0.55f) : FLinearColor(0.035f, 0.06f, 0.08f);
	if (bHov) BC = FMath::Lerp(BC, RS::Darken(Accent, 0.45f), 0.6f);
	Bevel(X, Y, W, H, BC, false);
	if (bHov) Brackets(X - 3, Y - 3, W + 6, H + 6, RS::WithA(Accent, 0.9f), 9.f, 2.f);
	Text(Label, X, Y + (H - Size * 1.25f) * 0.5f, Size, bEnabled ? (bHov ? FLinearColor(0.85f, 1.f, 1.f) : FLinearColor(0.82f, 0.9f, 0.92f)) : FLinearColor(0.38f, 0.42f, 0.44f), true, 1, W);
	if (bHov && bClick)
	{
		bClick = false;
		if (ARSGameMode* G = GM())
			if (G->Audio) G->Audio->Play(TEXT("click"), -6.f);
		return true;
	}
	return false;
}

bool ARSHUD::Selector(float X, float Y, float W, float H, const FString& Label, const TArray<FString>& Opts, int32& Idx)
{
	if (Opts.Num() == 0) return false;
	Idx = FMath::Clamp(Idx, 0, Opts.Num() - 1);
	Text(Label, X, Y + (H - 22.f) * 0.5f, 18.f, FLinearColor(0.9f, 0.9f, 0.86f));
	const float BX = X + 120.f, BW = W - 120.f;
	const bool bHov = Hovered(BX, Y, BW, H);
	Bevel(BX, Y, BW, H, bHov ? RS::Darken(Accent, 0.6f) : FLinearColor(0.035f, 0.06f, 0.08f), false);
	Text(TEXT("<"), BX + 8, Y + (H - 20.f) * 0.5f, 16.f, RS::Lighten(Accent, 0.2f), true);
	Text(TEXT(">"), BX + BW - 24, Y + (H - 20.f) * 0.5f, 16.f, RS::Lighten(Accent, 0.2f), true);
	Text(Opts[Idx], BX + 28, Y + (H - 21.f) * 0.5f, 16.f, FLinearColor(0.95f, 0.94f, 0.9f), false, 1, BW - 56);
	bool bChanged = false;
	if (bHov && bClick)
	{
		bClick = false;
		Idx = (M.X < BX + BW * 0.5f) ? (Idx + Opts.Num() - 1) % Opts.Num() : (Idx + 1) % Opts.Num();
		bChanged = true;
	}
	else if (bHov && Wheel != 0.f)
	{
		Idx = (Idx + (Wheel > 0.f ? Opts.Num() - 1 : 1)) % Opts.Num();
		Wheel = 0.f;
		bChanged = true;
	}
	if (bChanged)
	{
		if (ARSGameMode* G = GM())
			if (G->Audio) G->Audio->Play(TEXT("click"), -6.f);
	}
	return bChanged;
}

bool ARSHUD::Slider(float X, float Y, float W, float H, float& V)
{
	Rect(X, Y + H * 0.4f, W, H * 0.2f, FLinearColor(0.2f, 0.2f, 0.22f));
	Rect(X, Y + H * 0.4f, W * V, H * 0.2f, Accent);
	Rect(X + W * V - 5, Y, 10, H, FLinearColor(0.9f, 0.9f, 0.85f));
	ARSPlayerController* P = PC();
	if (P && P->bLeftDown && Hovered(X - 6, Y, W + 12, H))
	{
		const float NV = FMath::Clamp((M.X - X) / W, 0.f, 1.f);
		if (!FMath::IsNearlyEqual(NV, V))
		{
			V = NV;
			return true;
		}
	}
	return false;
}

void ARSHUD::Panel(float X, float Y, float W, float H, const FString& Title)
{
	Rect(0, 0, VS.X, VS.Y, FLinearColor(0, 0, 0, 0.55f));
	Rect(X, Y, W, H, FLinearColor(0.015f, 0.03f, 0.045f, 0.95f));
	for (float GX = X + 24; GX < X + W; GX += 24) Rect(GX, Y, 1, H, RS::WithA(Accent, 0.035f));
	for (float GY = Y + 24; GY < Y + H; GY += 24) Rect(X, GY, W, 1, RS::WithA(Accent, 0.035f));
	const float Scan = FMath::Fmod(T * 90.f, H);
	Rect(X, Y + Scan, W, 2, RS::WithA(Accent, 0.08f));
	Frame(X, Y, W, H, RS::WithA(Accent, 0.45f), 1.f);
	Brackets(X - 4, Y - 4, W + 8, H + 8, Accent, 22.f, 3.f);
	if (!Title.IsEmpty())
	{
		Rect(X + 1, Y + 1, W - 2, 64, RS::WithA(RS::Darken(Accent, 0.6f), 0.55f));
		Rect(X + 1, Y + 65, W - 2, 1, RS::WithA(Accent, 0.7f));
		Text(Title, X, Y + 16, 30.f, FLinearColor(0.85f, 0.98f, 1.f), true, 1, W);
	}
}

UTexture2D* ARSHUD::Cameo(const FString& Key)
{
	static TMap<FString, UTexture2D*> Cache;
	if (UTexture2D** F = Cache.Find(Key)) return *F;
	const FString Path = RS::ContentDir() / TEXT("Cameos") / Key + TEXT(".png");
	UTexture2D* Tx = FPaths::FileExists(Path) ? FImageUtils::ImportFileAsTexture2D(Path) : nullptr;
	if (Tx) Tx->AddToRoot();
	Cache.Add(Key, Tx);
	return Tx;
}

bool ARSHUD::Project(const FVector& W, FVector2D& Out) const
{
	APlayerController* P = GetOwningPlayerController();
	FVector2D Sc;
	if (!P || !P->ProjectWorldLocationToScreen(W, Sc, false)) return false;
	Out = Sc / S;
	return true;
}

// ───────────────────────── 國旗（簡化但可辨識的設計） ─────────────────────────
void ARSHUD::Flag(float X, float Y, float W, float H, const FString& Id)
{
	const FLinearColor Red(0.8f, 0.1f, 0.12f), Blue(0.1f, 0.2f, 0.55f), White(0.97f, 0.97f, 0.97f);
	if (Id == TEXT("usa"))
	{
		for (int32 K = 0; K < 13; ++K) Rect(X, Y + H * K / 13.f, W, H / 13.f + 0.5f, K % 2 == 0 ? Red : White);
		Rect(X, Y, W * 0.42f, H * 7.f / 13.f, Blue);
		for (int32 YY = 0; YY < 4; ++YY)
			for (int32 XX = 0; XX < 5; ++XX) Circle(FVector2D(X + W * 0.42f * (XX + 0.5f) / 5.f, Y + H * 7.f / 13.f * (YY + 0.5f) / 4.f), H * 0.035f, White, 6);
	}
	else if (Id == TEXT("japan"))
	{
		Rect(X, Y, W, H, White);
		Circle(FVector2D(X + W * 0.5f, Y + H * 0.5f), H * 0.3f, FLinearColor(0.75f, 0.05f, 0.1f));
	}
	else if (Id == TEXT("korea"))
	{
		Rect(X, Y, W, H, White);
		const FVector2D C(X + W * 0.5f, Y + H * 0.5f);
		const float R = H * 0.26f;
		Circle(C, R, FLinearColor(0.8f, 0.1f, 0.2f));
		TArray<FVector2D> P = { C };
		for (int32 K = 0; K <= 16; ++K)
		{
			const float A = PI * 0.15f + PI * K / 16.f;
			P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		Poly(P, FLinearColor(0.1f, 0.25f, 0.6f));
		Circle(C + RS::Rot(FVector2D(-R * 0.5f, R * 0.1f), -0.15f), R * 0.5f, FLinearColor(0.8f, 0.1f, 0.2f), 16);
		Circle(C + RS::Rot(FVector2D(R * 0.5f, -R * 0.1f), -0.15f), R * 0.5f, FLinearColor(0.1f, 0.25f, 0.6f), 16);
		const int32 Q[4][2] = { {-1, -1}, {1, 1}, {1, -1}, {-1, 1} };
		for (const auto& Qd : Q)
		{
			const FVector2D B = C + FVector2D(Qd[0] * W * 0.3f, Qd[1] * H * 0.28f);
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector2D Pp = B + FVector2D(0, (K - 1) * H * 0.075f);
				Rect(Pp.X - W * 0.07f, Pp.Y - H * 0.0225f, W * 0.14f, H * 0.045f, FLinearColor(0.08f, 0.08f, 0.08f));
			}
		}
	}
	else if (Id == TEXT("taiwan"))
	{
		Rect(X, Y, W, H, FLinearColor(0.85f, 0.08f, 0.12f));
		Rect(X, Y, W * 0.5f, H * 0.5f, FLinearColor(0.f, 0.15f, 0.6f));
		const FVector2D C(X + W * 0.25f, Y + H * 0.25f);
		for (int32 K = 0; K < 12; ++K)
		{
			const float A = K * 2.f * PI / 12.f;
			Tri(C + FVector2D(FMath::Cos(A - 0.18f), FMath::Sin(A - 0.18f)) * H * 0.08f, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * H * 0.18f,
				C + FVector2D(FMath::Cos(A + 0.18f), FMath::Sin(A + 0.18f)) * H * 0.08f, White);
		}
		Circle(C, H * 0.09f, White, 12);
	}
	else if (Id == TEXT("ukraine"))
	{
		Rect(X, Y, W, H * 0.5f, FLinearColor(0.f, 0.35f, 0.73f));
		Rect(X, Y + H * 0.5f, W, H * 0.5f, FLinearColor(1.f, 0.84f, 0.f));
	}
	else if (Id == TEXT("russia"))
	{
		Rect(X, Y, W, H / 3.f, White);
		Rect(X, Y + H / 3.f, W, H / 3.f, FLinearColor(0.f, 0.2f, 0.6f));
		Rect(X, Y + H * 2.f / 3.f, W, H / 3.f, FLinearColor(0.85f, 0.1f, 0.12f));
	}
	else if (Id == TEXT("nkorea"))
	{
		Rect(X, Y, W, H, FLinearColor(0.f, 0.2f, 0.6f));
		Rect(X, Y + H * 0.16f, W, H * 0.04f, White);
		Rect(X, Y + H * 0.2f, W, H * 0.6f, FLinearColor(0.85f, 0.1f, 0.12f));
		Rect(X, Y + H * 0.8f, W, H * 0.04f, White);
		const FVector2D C(X + W * 0.33f, Y + H * 0.5f);
		Circle(C, H * 0.2f, White);
		Star(C, H * 0.17f, FLinearColor(0.85f, 0.1f, 0.12f));
	}
	else if (Id == TEXT("iran"))
	{
		Rect(X, Y, W, H / 3.f, FLinearColor(0.15f, 0.6f, 0.25f));
		Rect(X, Y + H / 3.f, W, H / 3.f, White);
		Rect(X, Y + H * 2.f / 3.f, W, H / 3.f, FLinearColor(0.85f, 0.1f, 0.12f));
		const FVector2D C(X + W * 0.5f, Y + H * 0.5f);
		Ring(C, H * 0.11f, FLinearColor(0.85f, 0.1f, 0.12f), H * 0.03f, 12);
		Line(C.X, C.Y - H * 0.14f, C.X, C.Y + H * 0.1f, FLinearColor(0.85f, 0.1f, 0.12f), H * 0.035f);
	}
	else if (Id == TEXT("cuba"))
	{
		for (int32 K = 0; K < 5; ++K) Rect(X, Y + H * K / 5.f, W, H / 5.f + 0.5f, K % 2 == 0 ? FLinearColor(0.f, 0.25f, 0.6f) : White);
		Tri(FVector2D(X, Y), FVector2D(X + W * 0.45f, Y + H * 0.5f), FVector2D(X, Y + H), FLinearColor(0.8f, 0.1f, 0.12f));
		Star(FVector2D(X + W * 0.15f, Y + H * 0.5f), H * 0.14f, White);
	}
	else if (Id == TEXT("china"))
	{
		Rect(X, Y, W, H, FLinearColor(0.87f, 0.1f, 0.1f));
		const FLinearColor Yw(1.f, 0.87f, 0.f);
		Star(FVector2D(X + W * 0.17f, Y + H * 0.32f), H * 0.19f, Yw);
		const FVector2D Sm[4] = { {0.34f, 0.12f}, {0.42f, 0.27f}, {0.42f, 0.45f}, {0.34f, 0.58f} };
		for (const FVector2D& P : Sm) Star(FVector2D(X + W * P.X, Y + H * P.Y), H * 0.07f, Yw);
	}
	else if (Id == TEXT("yemen"))
	{
		Rect(X, Y, W, H / 3.f, FLinearColor(0.85f, 0.1f, 0.12f));
		Rect(X, Y + H / 3.f, W, H / 3.f, White);
		Rect(X, Y + H * 2.f / 3.f, W, H / 3.f, FLinearColor(0.08f, 0.08f, 0.08f));
	}
	else if (const FRSCountry* C = FRSDefs::Get().GetCountry(Id))
	{
		for (int32 K = 0; K < 3; ++K) Rect(X, Y + H * K / 3.f, W, H / 3.f + 0.5f, C->Flag[K]);
	}
	else
	{
		Rect(X, Y, W, H, FLinearColor(0.3f, 0.3f, 0.3f));
	}
	Frame(X, Y, W, H, FLinearColor(0, 0, 0, 0.7f), 1.f);
}

// ───────────────────────── 主繪製流程 ─────────────────────────
void ARSHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	const float DT = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	T += DT;
	S = FMath::Max(0.4f, Canvas->ClipY / 1080.f);
	VS = FVector2D(Canvas->ClipX / S, Canvas->ClipY / S);
	ARSPlayerController* P = PC();
	if (P)
	{
		M = P->Mouse / S;
		bClick = P->bLeftClick;
		bRClick = P->bRightClick;
		Wheel = P->Wheel;
	}
	ARSGameMode* G = GM();
	if (!G) return;
	switch (G->State)
	{
	case ERSState::Intro: DrawIntro(); break;
	case ERSState::Menu: DrawMenu(); break;
	case ERSState::Briefing: DrawBriefing(); break;
	case ERSState::Game: DrawGame(); break;
	}
}

void ARSHUD::OnEnterMenu()
{
	MenuPanel = 0;
	PreviewKey = -1;
}

void ARSHUD::OnEnterGame()
{
	Tab = 0;
	for (int32& Sc : Scroll) Sc = 0;
	MiniTex = nullptr;
	MiniBase.Reset();
	HoverItem.Empty();
	if (ARSGame* G = Game())
		if (G->Human) Accent = G->Human->Faction == TEXT("allies") ? FLinearColor(0.16f, 0.78f, 0.86f) : FLinearColor(0.95f, 0.42f, 0.12f);
}

void ARSHUD::MenuBack()
{
	MenuPanel = 0;
}

bool ARSHUD::IsModalOpen() const
{
	const ARSGame* G = Game();
	return G && (G->bPausePanel || G->bDiploPanel || G->bSavePanel || G->bGameOver);
}

bool ARSHUD::IsOverUI(const FVector2D& MouseScreen) const
{
	const ARSGameMode* Gm = GM();
	if (!Gm || Gm->State != ERSState::Game) return true;
	const ARSGame* G = Game();
	if (!G) return true;
	if (IsModalOpen() || G->bHelpPanel) return true;
	const FVector2D Mv = MouseScreen / S;
	return Mv.X >= VS.X - SW;
}

void ARSHUD::Escape()
{
	ARSGame* G = Game();
	if (!G || G->bGameOver) return;
	if (G->bHelpPanel) { G->bHelpPanel = false; return; }
	if (G->bSavePanel) { G->bSavePanel = false; return; }
	if (G->bDiploPanel) { G->bDiploPanel = false; return; }
	G->bPausePanel = !G->bPausePanel;
}

void ARSHUD::ToggleHelp()
{
	if (ARSGame* G = Game()) G->bHelpPanel = !G->bHelpPanel;
	else if (MenuPanel == 4) MenuPanel = 0;
	else MenuPanel = 4;
}

void ARSHUD::ToggleDiplomacy()
{
	ARSGame* G = Game();
	if (!G || G->bGameOver) return;
	G->bDiploPanel = !G->bDiploPanel;
}

// ───────────────────────── 片頭 ─────────────────────────
void ARSHUD::DrawVideoFull(float Alpha)
{
	ARSGameMode* G = GM();
	if (!G || !G->bVideoOk || !G->MediaTexture) return;
	UMediaTexture* MT = G->MediaTexture;
	const float TW = FMath::Max(1, MT->GetWidth()), TH = FMath::Max(1, MT->GetHeight());
	const float K = FMath::Max(VS.X / TW, VS.Y / TH);
	const float W = TW * K, H = TH * K;
	Tex(MT, (VS.X - W) * 0.5f, (VS.Y - H) * 0.5f, W, H, FLinearColor(1, 1, 1, Alpha));
}

void ARSHUD::DrawIntro()
{
	ARSGameMode* G = GM();
	const float Tt = G->StateT;
	Rect(0, 0, VS.X, VS.Y, FLinearColor(0.01f, 0.012f, 0.02f));
	if (G->bVideoOk)
	{
		DrawVideoFull();
	}
	else
	{
		// 無影片時的程序化片頭：網格、雷達點、快訊
		for (float GX = 0; GX < VS.X; GX += 60) Line(GX, 0, GX, VS.Y, FLinearColor(0.15f, 0.35f, 0.5f, 0.12f));
		for (float GY = 0; GY < VS.Y; GY += 60) Line(0, GY, VS.X, GY, FLinearColor(0.15f, 0.35f, 0.5f, 0.12f));
		FRandomStream R(7);
		for (int32 K = 0; K < 40; ++K)
		{
			const float X = FMath::Fmod(R.FRandRange(-1400.f, 0.f) + Tt * R.FRandRange(300.f, 500.f), VS.X + 400.f) - 200.f;
			const float Y = R.FRandRange(200.f, 700.f) + FMath::Sin(Tt + K) * 10.f;
			Tri(FVector2D(X + 14, Y), FVector2D(X - 8, Y - 7), FVector2D(X - 8, Y + 7), K % 3 ? FLinearColor(0.3f, 0.55f, 1.f, 0.8f) : FLinearColor(1.f, 0.3f, 0.2f, 0.8f));
		}
		const float SY = FMath::Fmod(Tt * 160.f, VS.Y);
		Rect(0, SY, VS.X, 3, FLinearColor(0.4f, 0.8f, 1.f, 0.2f));
		Rect(60, 70, 160, 44, FLinearColor(0.75f, 0.08f, 0.06f));
		Text(TEXT("RSN"), 60, 76, 30, FLinearColor::White, true, 1, 160);
		Text(TEXT("WORLD NEWS  •  LIVE"), 236, 82, 20, FLinearColor(1, 1, 1, 0.85f), true);
		if (Tt > 55.f)
		{
			const float A = FMath::Clamp((Tt - 55.f) / 1.5f, 0.f, 1.f);
			Text(TEXT("紅色風暴"), 0, VS.Y * 0.38f, 110, FLinearColor(0.95f, 0.18f, 0.1f, A), true, 1, VS.X);
			Text(TEXT("R E D   S T O R M"), 0, VS.Y * 0.38f + 140, 28, FLinearColor(1.f, 0.85f, 0.5f, A), true, 1, VS.X);
		}
	}
	// 字幕
	const TArray<FRSIntroLine>& L = FRSDefs::Get().IntroLines;
	int32 Cur = -1;
	for (int32 I = 0; I < L.Num(); ++I)
		if (L[I].T <= Tt) Cur = I;
	if (Cur >= 0)
	{
		const float Age = Tt - L[Cur].T;
		const float A = FMath::Clamp(Age / 0.3f, 0.f, 1.f);
		// 影片下方有自帶的新聞跑馬燈，字幕改放在畫面中段並加深底色，避免重疊
		const float SubY = G->bVideoOk ? VS.Y * 0.56f : VS.Y - 200.f;
		const float BoxW = FMath::Max(TextW(L[Cur].En, 40, true), TextW(L[Cur].Zh, 30, true)) + 80.f;
		Rect((VS.X - BoxW) * 0.5f, SubY, BoxW, 126, FLinearColor(0, 0, 0, 0.62f * A));
		Text(L[Cur].En, 0, SubY + 10, 40, FLinearColor(1, 1, 1, A), true, 1, VS.X);
		Text(L[Cur].Zh, 0, SubY + 72, 30, FLinearColor(1.f, 0.85f, 0.5f, A), true, 1, VS.X);
	}
	const float Blink = 0.5f + 0.5f * FMath::Sin(T * 4.f);
	if (G->bVideoOk)
	{
		// 影片底部有跑馬燈：提示改放右上角
		Rect(VS.X - 330, 22, 300, 34, FLinearColor(0, 0, 0, 0.55f));
		Text(TEXT("按 Esc / 空白鍵 / 點擊 跳過片頭"), VS.X - 330, 29, 18, FLinearColor(0.9f, 0.9f, 0.9f, 0.5f + 0.3f * Blink), false, 1, 300);
	}
	else
	{
		Text(TEXT("按 Esc / 空白鍵 / 點擊 跳過片頭"), 0, VS.Y - 40, 18, FLinearColor(0.8f, 0.8f, 0.8f, 0.4f + 0.3f * Blink), false, 1, VS.X);
	}
}

// ───────────────────────── 任務簡報（RSN 新聞播報風格） ─────────────────────────
void ARSHUD::DrawBriefing()
{
	ARSGameMode* G = GM();
	const FRSMissionDef* MD = G->BriefMission;
	if (!MD) return;
	const float Tt = G->StateT;
	const float W = VS.X, H = VS.Y;
	Rect(0, 0, W, H, FLinearColor(0.02f, 0.03f, 0.05f));
	const float A = FMath::Clamp(Tt / 0.8f, 0.f, 1.f);
	for (float GX = 0; GX < W; GX += 60) Line(GX, 0, GX, H, FLinearColor(0.15f, 0.35f, 0.5f, 0.15f * A));
	for (float GY = 0; GY < H; GY += 60) Line(0, GY, W, GY, FLinearColor(0.15f, 0.35f, 0.5f, 0.15f * A));
	FRandomStream R(MD->Seed);
	TArray<FVector2D> Pts;
	for (int32 K = 0; K < 7; ++K) Pts.Add(FVector2D(300 + R.FRand() * 1300, 160 + R.FRand() * 220));
	for (int32 K = 0; K < Pts.Num(); ++K)
	{
		const float Ph = FMath::Fmod(Tt * 1.3f + K * 0.6f, 1.f);
		Ring(Pts[K], 8 + Ph * 44, FLinearColor(1.f, 0.25f, 0.15f, (1.f - Ph) * A * 0.8f), 2.f, 24);
		Circle(Pts[K], 5, FLinearColor(1.f, 0.3f, 0.2f, A), 10);
		if (K > 0 && Tt > 1.f + K * 0.3f) Line(Pts[K - 1].X, Pts[K - 1].Y, Pts[K].X, Pts[K].Y, FLinearColor(1.f, 0.4f, 0.2f, 0.4f * A), 2.f);
	}
	const float SY = FMath::Fmod(Tt * 160.f, H);
	Rect(0, SY, W, 3, FLinearColor(0.4f, 0.8f, 1.f, 0.25f * A));
	Rect(60, 70, 160, 44, FLinearColor(0.75f, 0.08f, 0.06f, A));
	Text(TEXT("RSN"), 60, 74, 30, FLinearColor(1, 1, 1, A), true, 1, 160);
	Text(TEXT("WORLD NEWS  •  LIVE"), 236, 82, 20, FLinearColor(1, 1, 1, A * 0.85f), true);
	Circle(FVector2D(560, 92), 7, FLinearColor(1.f, 0.15f, 0.1f, A * (0.5f + 0.5f * FMath::Sin(Tt * 6.f))), 12);
	Text(MD->Name, 0, 78, 26, FLinearColor(1.f, 0.85f, 0.4f, A), true, 2, W - 60);
	// 標題逐字
	const FString& Head = MD->Headline;
	const int32 N = FMath::Min(Head.Len(), int32(FMath::Max(0.f, Tt - 0.6f) * 30.f));
	Rect(60, 400, 260, 36, FLinearColor(0.75f, 0.08f, 0.06f, A));
	Text(TEXT("BREAKING NEWS"), 60, 405, 22, FLinearColor(1, 1, 1, A), true, 1, 260);
	Rect(0, 440, W, 80, FLinearColor(0.05f, 0.05f, 0.08f, 0.92f * A));
	Rect(0, 440, W, 5, FLinearColor(0.75f, 0.08f, 0.06f, A));
	FString Shown = Head.Left(N);
	if (N < Head.Len() && int32(Tt * 6.f) % 2 == 0) Shown += TEXT("_");
	Text(Shown, 70, 455, 40, FLinearColor(1, 1, 1, A), true);
	// 跑馬燈
	Rect(0, 520, W, 30, FLinearColor(0.12f, 0.12f, 0.16f, 0.95f * A));
	const FString Tick = TEXT("GRID FAILURES REPORTED  •  DRONE SWARMS CROSS THE STRAIT  •  CENTRAL BANK FREEZES RESERVES  •  ENERGY EXPORTS HALTED  •  ALLIANCE TALKS COLLAPSE  •  CARRIER GROUP DEPARTS PORT  •  ");
	const float X = FMath::Fmod(-Tt * 160.f, 2200.f);
	Text(Tick + Tick, X, 524, 18, FLinearColor(1.f, 0.9f, 0.7f, A), true, 0, 0.f, false);
	// 簡報內文
	Rect(100, 570, 1140, 300, FLinearColor(0, 0, 0, 0.45f * A));
	const int32 NB = int32(FMath::Max(0.f, Tt - 2.2f) * 28.f);
	TextWrap(MD->Brief, 124, 590, 1090, 23, FLinearColor(0.95f, 0.95f, 0.9f, A), 34.f, false, NB);
	// 右側：陣營資訊
	if (const FRSCountry* C = FRSDefs::Get().GetCountry(MD->Country))
	{
		Rect(1300, 570, 540, 300, FLinearColor(0.05f, 0.06f, 0.1f, 0.8f * A));
		Frame(1300, 570, 540, 300, FLinearColor(0.3f, 0.5f, 0.8f, 0.5f * A), 1.f);
		Flag(1320, 590, 90, 60, MD->Country);
		Text(FString::Printf(TEXT("我方：%s（%s）"), *C->Name, *FRSDefs::Get().FactionName(C->Faction)), 1430, 594, 24, FLinearColor(1, 1, 1, A), true);
		const FRSCountry* EC = FRSDefs::Get().GetCountry(MD->EnemyCountry);
		Text(FString::Printf(TEXT("敵方：%s"), EC ? *EC->Name : TEXT("隨機")), 1430, 630, 22, FLinearColor(1.f, 0.6f, 0.5f, A), true);
		TextWrap(C->Desc, 1320, 680, 500, 16, FLinearColor(0.85f, 0.85f, 0.8f, A), 24.f);
	}
	if (Tt > 1.f)
	{
		const float Blink = 0.5f + 0.5f * FMath::Sin(Tt * 4.f);
		Text(TEXT("按任意鍵開始任務  ·  Press any key to begin"), 0, H - 64, 22, FLinearColor(1.f, 0.85f, 0.4f, Blink), true, 1, W);
	}
	Rect(0, 0, W, 50, FLinearColor::Black);
	Rect(0, H - 30, W, 30, FLinearColor::Black);
}

// ───────────────────────── 主選單 ─────────────────────────
FRSMapPreset ARSHUD::SizedMap() const
{
	const TArray<FRSMapPreset>& P = FRSDefs::Get().Presets;
	FRSMapPreset Mp = P.IsValidIndex(OptMap) ? P[OptMap] : FRSMapPreset();
	const float Mult[4] = { 1.f, 1.5f, 2.f, 2.5f };
	const float K = Mult[FMath::Clamp(OptMapSize, 0, 3)];
	if (K > 1.f) Mp.Size = FMath::Min(400, FMath::RoundToInt(Mp.Size * K / 8.f) * 8);
	return Mp;
}

void ARSHUD::RefreshPreview()
{
	const int32 Key = OptMap * 1000 + OptMapSize * 100 + OptEnemies * 10;
	if (Key == PreviewKey && PreviewTex) return;
	PreviewKey = Key;
	const FRSMapPreset Mp = SizedMap();
	FRSMap Mm;
	Mm.Generate(Mp, 2 + OptEnemies, PreviewSeed);
	TArray<FColor> Px = Mm.MakePreview(true);
	PreviewW = Mm.W;
	PreviewH = Mm.H;
	PreviewTex = UTexture2D::CreateTransient(PreviewW, PreviewH, PF_B8G8R8A8);
	if (PreviewTex)
	{
		PreviewTex->Filter = TF_Nearest;
		PreviewTex->SRGB = true;
		FTexture2DMipMap& Mip = PreviewTex->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Px.GetData(), Px.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		PreviewTex->UpdateResource();
	}
	static const TMap<FString, FString> ThName = { {TEXT("temperate"), TEXT("溫帶")}, {TEXT("desert"), TEXT("沙漠")}, {TEXT("snow"), TEXT("雪地")}, {TEXT("tropical"), TEXT("熱帶")} };
	const FString* Tn = ThName.Find(Mp.Theme);
	PreviewDesc = FString::Printf(TEXT("%s\n%s ・ %dx%d ・ 最多 4 名玩家\n白色 = 你的出生點，紅色 = 敵人，黃/紫 = 礦區"), *Mp.Desc, Tn ? **Tn : *Mp.Theme, Mp.Size, Mp.Size);
}

void ARSHUD::DrawDiamondTex(UTexture2D* Tx, int32 TW, int32 TH, const FVector2D& Center, float K)
{
	if (!Tx || !Tx->GetResource() || !Canvas) return;
	// 與鏡頭方向一致：x-y 為畫面右方，x+y 為畫面下方
	auto P = [&](float X, float Y) { return (Center + FVector2D((X - Y) * K, (X + Y) * K * 0.5f - (TW + TH) * K * 0.25f)) * S; };
	TArray<FCanvasUVTri> Tris;
	FCanvasUVTri A, B;
	A.V0_Pos = P(0, 0); A.V0_UV = FVector2D(0, 0); A.V0_Color = FLinearColor::White;
	A.V1_Pos = P(TW, 0); A.V1_UV = FVector2D(1, 0); A.V1_Color = FLinearColor::White;
	A.V2_Pos = P(TW, TH); A.V2_UV = FVector2D(1, 1); A.V2_Color = FLinearColor::White;
	B.V0_Pos = P(0, 0); B.V0_UV = FVector2D(0, 0); B.V0_Color = FLinearColor::White;
	B.V1_Pos = P(TW, TH); B.V1_UV = FVector2D(1, 1); B.V1_Color = FLinearColor::White;
	B.V2_Pos = P(0, TH); B.V2_UV = FVector2D(0, 1); B.V2_Color = FLinearColor::White;
	Tris.Add(A);
	Tris.Add(B);
	FCanvasTriangleItem TI(Tris, Tx->GetResource());
	TI.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(TI);
}

void ARSHUD::DrawMenuBackground()
{
	ARSGameMode* G = GM();
	if (G && G->bVideoOk && G->MediaTexture)
	{
		Rect(0, 0, VS.X, VS.Y, FLinearColor::Black);
		DrawVideoFull(0.85f);
		Rect(0, 0, VS.X, VS.Y, FLinearColor(0, 0, 0, 0.25f));
		return;
	}
	const float W = VS.X, H = VS.Y;
	switch (BgStyle)
	{
	case 1:
	{
		for (int32 I = 0; I < 24; ++I)
			Rect(0, H * I / 24.f, W, H / 24.f + 1, FMath::Lerp(FLinearColor(0.01f, 0.02f, 0.06f), FLinearColor(0.03f, 0.08f, 0.16f), I / 24.f));
		for (int32 K = 0; K < 90; ++K)
			Circle(FVector2D(FMath::Fmod(K * 173.7f, W), FMath::Fmod(K * 97.3f, H * 0.5f)), 1.2f, FLinearColor(0.9f, 0.95f, 1.f, (0.4f + 0.6f * FMath::Abs(FMath::Sin(T * 1.3f + K))) * 0.8f), 6);
		const FVector2D MC(W - 420, 150);
		Circle(MC, 46, FLinearColor(0.95f, 0.95f, 0.85f, 0.95f), 32);
		const float Hz = H * 0.56f;
		Rect(0, Hz, W, H - Hz, FLinearColor(0.03f, 0.09f, 0.18f));
		for (int32 K = 0; K < 60; ++K)
		{
			const float F = FMath::Pow(K / 60.f, 1.6f);
			const float Y = Hz + (H - Hz) * F;
			Line(0, Y, W, Y, FLinearColor(0.3f, 0.6f, 0.9f, 0.05f + 0.08f * F * (0.5f + 0.5f * FMath::Sin(T * 1.2f + K * 0.7f))), 1.f + F * 2.f);
		}
		break;
	}
	case 2:
	{
		for (int32 I = 0; I < 24; ++I)
			Rect(0, H * I / 24.f, W, H / 24.f + 1, FMath::Lerp(FLinearColor(0.55f, 0.22f, 0.08f), FLinearColor(0.95f, 0.65f, 0.3f), I / 24.f));
		const FVector2D SC(W * 0.62f, H * 0.4f);
		for (int32 K = 0; K < 6; ++K) Circle(SC, 60.f + K * 30.f, FLinearColor(1.f, 0.85f, 0.5f, 0.06f), 32);
		Circle(SC, 58, FLinearColor(1.f, 0.93f, 0.7f, 0.95f), 32);
		const FLinearColor Dunes[4] = { FLinearColor(0.82f, 0.55f, 0.25f), FLinearColor(0.7f, 0.42f, 0.18f), FLinearColor(0.55f, 0.3f, 0.12f), FLinearColor(0.4f, 0.22f, 0.1f) };
		for (int32 D = 0; D < 4; ++D)
		{
			const float BaseY = H * (0.42f + D * 0.12f);
			for (int32 K = 0; K < 40; ++K)
			{
				const float X0 = W * K / 40.f, X1 = W * (K + 1) / 40.f;
				const float Y0 = BaseY + FMath::Sin(X0 * 0.004f + D * 1.7f + T * 0.05f) * 40.f + FMath::Sin(X0 * 0.011f + D) * 18.f;
				const float Y1 = BaseY + FMath::Sin(X1 * 0.004f + D * 1.7f + T * 0.05f) * 40.f + FMath::Sin(X1 * 0.011f + D) * 18.f;
				Poly({ FVector2D(X0, Y0), FVector2D(X1, Y1), FVector2D(X1, H), FVector2D(X0, H) }, Dunes[D]);
			}
		}
		break;
	}
	case 3:
	{
		Rect(0, 0, W, H, FLinearColor(0.01f, 0.02f, 0.03f));
		for (int32 K = 0; K < 60; ++K)
		{
			const float X = K * (W / 60.f) + 8.f;
			const float Sp = 60.f + (K * 37) % 90;
			const float Y = FMath::Fmod(T * Sp + K * 211.f, H + 200.f) - 100.f;
			for (int32 J = 0; J < 12; ++J)
			{
				const FString Ch = FString::FromInt((K * J + int32(T * 4.f)) % 10);
				Text(Ch, X, Y - J * 18.f, 14, J > 0 ? FLinearColor(0.2f, 1.f, 0.5f, (1.f - J / 12.f) * 0.6f) : FLinearColor(0.8f, 1.f, 0.9f, 0.9f), true, 0, 0.f, false);
			}
		}
		TArray<FVector2D> Curve;
		for (int32 K = 0; K < 80; ++K)
		{
			const float X = W * 0.42f + K * (W * 0.56f / 79.f);
			const float V = FMath::Sin(K * 0.3f + T * 0.8f) * 30.f + FMath::Sin(K * 0.9f - T * 1.3f) * 14.f + K * 1.2f;
			Curve.Add(FVector2D(X, H * 0.36f - V));
		}
		for (int32 K = 1; K < Curve.Num(); ++K) Line(Curve[K - 1].X, Curve[K - 1].Y, Curve[K].X, Curve[K].Y, FLinearColor(1.f, 0.75f, 0.2f, 0.8f), 2.f);
		Text(FString::Printf(TEXT("GLOBAL MARKETS  ▲ %.2f%%   RESERVES FROZEN   SANCTIONS ACTIVE"), 3.f + FMath::Sin(T) * 2.f), W * 0.62f, H * 0.28f, 14, FLinearColor(1.f, 0.75f, 0.2f, 0.7f), true);
		break;
	}
	default:
	{
		for (int32 I = 0; I < 24; ++I)
			Rect(0, H * I / 24.f, W, H / 24.f + 1, FMath::Lerp(FLinearColor(0.16f, 0.03f, 0.02f), FLinearColor(0.02f, 0.02f, 0.03f), I / 24.f));
		const float Hz = H * 0.55f;
		const FLinearColor GC(0.9f, 0.2f, 0.1f, 0.18f);
		for (int32 K = 0; K < 24; ++K)
		{
			const float F = FMath::Pow(FMath::Fmod(K / 24.f + T * 0.04f, 1.f), 2.2f);
			const float Y = Hz + (H - Hz) * F;
			Line(0, Y, W, Y, RS::WithA(GC, GC.A * F * 1.5f));
		}
		for (int32 K = -20; K <= 20; ++K) Line(W * 0.5f + K * 40.f, Hz, W * 0.5f + K * 260.f, H, GC);
		for (int32 K = 0; K < 3; ++K)
		{
			const float OX = W * (0.2f + 0.3f * K);
			const float Ang = -PI * 0.5f + FMath::Sin(T * 0.4f + K * 2.f) * 0.5f;
			const FVector2D Tip = FVector2D(OX, H) + RS::FromAngle(Ang) * H * 1.3f;
			const FVector2D Side = RS::Ortho(RS::FromAngle(Ang)) * 110.f;
			Tri(FVector2D(OX, H), Tip + Side, Tip - Side, FLinearColor(1.f, 0.95f, 0.8f, 0.06f));
		}
		break;
	}
	}
	// 底部兵種卡片遊行（使用原創兵種圖示）
	static const TCHAR* Parade[4][7] = {
		{ TEXT("a_tank"), TEXT("s_tank"), TEXT("s_mammoth"), TEXT("a_ifv"), TEXT("s_arty"), TEXT("a_beam"), TEXT("s_flaktrk") },
		{ TEXT("a_destroyer"), TEXT("s_sub"), TEXT("a_cvn"), TEXT("s_dread"), TEXT("a_carrier"), TEXT("s_fastboat"), TEXT("a_seadrone") },
		{ TEXT("a_tank"), TEXT("a_himars"), TEXT("s_tos"), TEXT("a_k9"), TEXT("s_bmpt"), TEXT("a_type10"), TEXT("s_technical") },
		{ TEXT("a_ew"), TEXT("s_ew"), TEXT("a_harv"), TEXT("s_iskander"), TEXT("a_mcv"), TEXT("s_hwasong"), TEXT("a_hf") } };
	Rect(0, H - 120, W, 120, FLinearColor(0.03f, 0.03f, 0.03f, 0.75f));
	for (int32 K = 0; K < 7; ++K)
	{
		const FString Id = Parade[FMath::Clamp(BgStyle, 0, 3)][K];
		const float X = FMath::Fmod(K * 230.f + T * 40.f, 7 * 230.f) - 150.f;
		if (UTexture2D* Cm = Cameo(Id))
		{
			Tex(Cm, X, H - 108, 136, 100, FLinearColor(1, 1, 1, 0.85f));
			if (UTexture2D* Tm = Cameo(Id + TEXT("_team"))) Tex(Tm, X, H - 108, 136, 100, Id.StartsWith(TEXT("a")) ? FLinearColor(0.25f, 0.55f, 1.f, 0.85f) : FLinearColor(0.92f, 0.18f, 0.12f, 0.85f));
		}
	}
	for (float Y = 0; Y < H; Y += 4.f) Rect(0, Y, W, 1, FLinearColor(0, 0, 0, 0.1f));
}

void ARSHUD::DrawMenu()
{
	DrawMenuBackground();
	// 標題
	Text(TEXT("紅色風暴"), 60, 40, 84, FLinearColor(0.95f, 0.18f, 0.1f), true);
	Text(TEXT("R E D   S T O R M  ·  混合戰爭即時戰略"), 64, 150, 22, FLinearColor(1.f, 0.85f, 0.5f, 0.85f), true);
	const FVector2D SC(VS.X - 140, 110);
	TArray<FVector2D> SP;
	for (int32 I = 0; I < 10; ++I)
	{
		const float R = I % 2 == 0 ? 70.f : 28.f;
		SP.Add(SC + RS::FromAngle(-PI * 0.5f + I * PI / 5.f + FMath::Sin(T * 0.3f) * 0.05f) * R);
	}
	for (int32 I = 0; I < 10; ++I) Tri(SC, SP[I], SP[(I + 1) % 10], FLinearColor(0.85f, 0.12f, 0.08f, 0.85f));
	Text(TEXT("v2.0 Unreal  ·  同人即時戰略遊戲（原創素材）"), VS.X - 420, VS.Y - 24, 12, FLinearColor(0.6f, 0.6f, 0.6f), false, 2, 400);
	switch (MenuPanel)
	{
	case 1: DrawCampaign(); break;
	case 2: DrawRecords(); break;
	case 3: DrawLoads(); break;
	case 4: DrawMenuHelp(); break;
	default: DrawMenuMain(); break;
	}
}

void ARSHUD::DrawMenuMain()
{
	const FRSDefs& D = FRSDefs::Get();
	const float X = 60, Y = 200, W = 560;
	Rect(X - 20, Y - 20, W + 40, 760, FLinearColor(0.015f, 0.03f, 0.045f, 0.84f));
	Frame(X - 20, Y - 20, W + 40, 760, RS::WithA(Accent, 0.4f), 1.f);
	Brackets(X - 24, Y - 24, W + 48, 768, Accent, 22.f, 3.f);
	Rect(X - 20, Y - 20, 4, 760, RS::WithA(Accent, 0.6f));
	TArray<FString> CNames;
	for (const FString& C : D.CountryOrder)
	{
		const FRSCountry& Co = D.Countries[C];
		CNames.Add(FString::Printf(TEXT("%s（%s）"), *Co.Name, *D.FactionName(Co.Faction)));
	}
	float YY = Y;
	const float RH = 40.f, Gap = 46.f;
	Selector(X, YY, W, RH, TEXT("我方國家"), CNames, OptCountry); YY += Gap;
	TArray<FString> ENames = { TEXT("隨機") };
	ENames.Append(CNames);
	Selector(X, YY, W, RH, TEXT("敵方國家"), ENames, OptEnemyCountry); YY += Gap;
	if (D.CountryOrder.IsValidIndex(OptCountry))
	{
		const FString Cid = D.CountryOrder[OptCountry];
		const FRSCountry& Co = D.Countries[Cid];
		Flag(X, YY + 4, 64, 40, Cid);
		FString Desc = Co.Desc;
		if (!Co.Power.IsEmpty()) Desc += FString::Printf(TEXT("\n特殊能力（需雷達/航空作戰中心）：%s"), *D.GetSuper(Co.Power).Name);
		TextWrap(Desc, X + 80, YY, W - 80, 14, FLinearColor(1.f, 0.85f, 0.45f), 20.f);
		YY += 70;
	}
	if (Selector(X, YY, W, RH, TEXT("敵人數量"), { TEXT("1 名對手"), TEXT("2 名對手"), TEXT("3 名對手") }, OptEnemies)) PreviewKey = -1;
	YY += Gap;
	Selector(X, YY, W, RH, TEXT("敵軍關係"), { TEXT("敵軍互相結盟"), TEXT("各自為戰(混戰)") }, OptRelation); YY += Gap;
	Selector(X, YY, W, RH, TEXT("電腦難度"), { TEXT("簡單（AI 收入 -25%，不騷擾）"), TEXT("普通（會騷擾採礦車）"), TEXT("困難（AI 收入 +35%，猛攻超武）") }, OptDiff); YY += Gap;
	TArray<FString> MNames;
	for (const FRSMapPreset& P : D.Presets) MNames.Add(FString::Printf(TEXT("%s (%dx%d)"), *P.Name, P.Size, P.Size));
	if (Selector(X, YY, W, RH, TEXT("戰場地圖"), MNames, OptMap)) { PreviewSeed = FMath::Rand(); PreviewKey = -1; }
	YY += Gap;
	if (Selector(X, YY, W, RH, TEXT("地圖尺寸"), { TEXT("標準"), TEXT("大 (×1.5)"), TEXT("特大 (×2)"), TEXT("超巨 (×2.5)") }, OptMapSize))
	{
		FRSUserSettings::Get().MapSize = OptMapSize;
		FRSUserSettings::Get().Save();
		PreviewKey = -1;
	}
	YY += Gap;
	Selector(X, YY, W, RH, TEXT("起始資金"), { TEXT("$5,000"), TEXT("$10,000"), TEXT("$20,000"), TEXT("$50,000") }, OptMoney); YY += Gap;
	Selector(X, YY, W, RH, TEXT("遊戲速度"), { TEXT("慢速"), TEXT("正常"), TEXT("快速") }, OptSpeed); YY += Gap;
	Selector(X, YY, W, RH, TEXT("隨機補給箱"), { TEXT("開啟"), TEXT("關閉") }, OptCrates); YY += Gap;
	Selector(X, YY, W, RH, TEXT("時事事件"), { TEXT("開啟（制裁/駭客/軍援…）"), TEXT("關閉") }, OptEvents); YY += Gap + 6;
	if (Button(X + 120, YY, W - 120, 52, TEXT("開始作戰"), 22)) StartSkirmish();
	YY += 58;
	if (Button(X + 120, YY, W - 120, 46, TEXT("戰役任務"), 19)) MenuPanel = 1;
	YY += 54;
	const float BW = (W - 120 - 5 * 8) / 6.f;
	const FString Labels[6] = { TEXT("說明"), TEXT("戰績"), TEXT("讀檔"), TEXT("片頭"), TEXT("背景"), TEXT("離開") };
	for (int32 K = 0; K < 6; ++K)
	{
		if (Button(X + 120 + K * (BW + 8), YY, BW, 38, Labels[K], 15))
		{
			ARSGameMode* G = GM();
			switch (K)
			{
			case 0: MenuPanel = 4; break;
			case 1: MenuPanel = 2; break;
			case 2: MenuPanel = 3; break;
			case 3: if (G) G->PlayIntro(); break;
			case 4:
				BgStyle = (BgStyle + 1) % 4;
				FRSUserSettings::Get().MenuBg = BgStyle;
				FRSUserSettings::Get().Save();
				if (G) G->StopVideo();
				break;
			case 5: if (G) G->QuitGame(); break;
			}
		}
	}
	// 右側：戰場預覽
	RefreshPreview();
	const float PX = 680, PY = 200, PW = 440;
	Rect(PX - 20, PY - 20, PW + 40, 520, FLinearColor(0.015f, 0.03f, 0.045f, 0.84f));
	Frame(PX - 20, PY - 20, PW + 40, 520, RS::WithA(Accent, 0.4f), 1.f);
	Brackets(PX - 24, PY - 24, PW + 48, 528, Accent, 22.f, 3.f);
	Text(TEXT("戰場預覽"), PX, PY, 20, FLinearColor(1.f, 0.85f, 0.35f), true);
	const float K = 400.f / float(PreviewW + PreviewH);
	DrawDiamondTex(PreviewTex, PreviewW, PreviewH, FVector2D(PX + PW * 0.5f, PY + 150), K);
	TextWrap(PreviewDesc, PX, PY + 320, PW, 15, FLinearColor(0.9f, 0.9f, 0.86f), 22.f);
	if (Button(PX, PY + 420, 200, 40, TEXT("重新產生地形"), 15))
	{
		PreviewSeed = FMath::Rand();
		PreviewKey = -1;
	}
}

void ARSHUD::StartSkirmish()
{
	const FRSDefs& D = FRSDefs::Get();
	ARSGameMode* G = GM();
	if (!G || !D.CountryOrder.IsValidIndex(OptCountry)) return;
	const FString Cid = D.CountryOrder[OptCountry];
	const FString Fac = D.Countries[Cid].Faction;
	FRSUserSettings& US = FRSUserSettings::Get();
	US.Faction = Fac;
	US.Country = Cid;
	US.Save();
	FRSGameSettings GS;
	GS.Faction = Fac;
	GS.Country = Cid;
	GS.EnemyCountry = OptEnemyCountry == 0 ? FString(TEXT("random")) : D.CountryOrder[OptEnemyCountry - 1];
	GS.EnemyFaction = OptEnemyCountry == 0 ? FString(TEXT("random")) : D.Countries[D.CountryOrder[OptEnemyCountry - 1]].Faction;
	GS.Enemies = OptEnemies + 1;
	GS.bAiAllied = OptRelation == 0;
	GS.Difficulty = OptDiff;
	GS.Map = SizedMap();
	GS.Seed = PreviewSeed;
	const float Money[4] = { 5000.f, 10000.f, 20000.f, 50000.f };
	const float Spd[3] = { 0.75f, 1.f, 1.35f };
	GS.Money = Money[FMath::Clamp(OptMoney, 0, 3)];
	GS.Speed = Spd[FMath::Clamp(OptSpeed, 0, 2)];
	GS.bCrates = OptCrates == 0;
	GS.bEvents = OptEvents == 0;
	G->StartGame(GS);
}

void ARSHUD::DrawCampaign()
{
	const FRSDefs& D = FRSDefs::Get();
	const FRSUserSettings& US = FRSUserSettings::Get();
	const float X = 420, Y = 150, W = 1080, H = 640;
	Rect(X, Y, W, H, FLinearColor(0.06f, 0.05f, 0.06f, 0.98f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.12f, 0.08f), 2.f);
	Text(TEXT("戰役任務"), X + 22, Y + 18, 26, FLinearColor(1.f, 0.85f, 0.35f), true);
	float YY = Y + 66;
	FString LastCamp;
	const int32 Rows = 13;
	int32 Row = 0;
	if (Hovered(X, Y, 360, H) && Wheel != 0.f)
	{
		CampScroll = FMath::Clamp(CampScroll - int32(Wheel), 0, FMath::Max(0, D.Missions.Num() + 2 - Rows));
		Wheel = 0.f;
	}
	for (int32 I = 0; I < D.Missions.Num(); ++I)
	{
		const FRSMissionDef& Md = D.Missions[I];
		if (Md.Camp != LastCamp)
		{
			LastCamp = Md.Camp;
			if (Row >= CampScroll && Row < CampScroll + Rows)
			{
				Text(Md.Camp == TEXT("hybrid") ? TEXT("— 混合戰爭戰役 —") : TEXT("— 基礎戰役 —"), X + 22, YY + 6, 15, FLinearColor(1.f, 0.6f, 0.4f));
				YY += 34;
			}
			Row++;
		}
		if (Row >= CampScroll && Row < CampScroll + Rows)
		{
			const bool bUnl = US.MissionUnlocked(Md.Id);
			const FString Lbl = (US.MissionDone(Md.Id) ? TEXT("✓ ") : (!bUnl ? TEXT("（未解鎖）") : TEXT(""))) + Md.Name;
			if (Button(X + 22, YY, 330, 36, Lbl, 15, bUnl, CampSel == I)) CampSel = I;
			YY += 40;
		}
		Row++;
	}
	if (Button(X + 22, Y + H - 96, 160, 38, TEXT("返回"), 16)) MenuPanel = 0;
	if (Button(X + 192, Y + H - 96, 160, 38, US.bUnlockAll ? TEXT("☑ 解鎖全部") : TEXT("☐ 解鎖全部"), 15))
	{
		FRSUserSettings::Get().bUnlockAll = !US.bUnlockAll;
		FRSUserSettings::Get().Save();
	}
	if (D.Missions.IsValidIndex(CampSel))
	{
		const FRSMissionDef& Md = D.Missions[CampSel];
		const float BX = X + 390;
		FString Br = Md.Headline.IsEmpty() ? Md.Brief : TEXT("RSN BREAKING: ") + Md.Headline + TEXT("\n\n") + Md.Brief;
		TextWrap(Br, BX, Y + 70, W - 420, 18, FLinearColor(0.92f, 0.92f, 0.88f), 28.f);
		if (!Md.Country.IsEmpty()) Flag(BX, Y + H - 160, 72, 48, Md.Country);
		if (Button(BX, Y + H - 96, W - 420, 52, TEXT("開始任務"), 22, US.MissionUnlocked(Md.Id)))
		{
			if (ARSGameMode* G = GM()) G->StartGame(FRSGameSettings::FromMission(Md, OptDiff));
		}
	}
}

void ARSHUD::DrawRecords()
{
	const FRSRecords& R = FRSRecords::Get();
	const FRSDefs& D = FRSDefs::Get();
	const float X = 300, Y = 110, W = 1320, H = 800;
	Rect(X, Y, W, H, FLinearColor(0.06f, 0.05f, 0.06f, 0.98f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.12f, 0.08f), 2.f);
	Text(TEXT("戰績統計"), X + 22, Y + 18, 26, FLinearColor(1.f, 0.85f, 0.35f), true);
	int32 MDone = 0;
	for (const FRSMissionDef& Md : D.Missions)
		if (FRSUserSettings::Get().MissionDone(Md.Id)) MDone++;
	const TArray<TPair<FString, FString>> Rows = {
		{TEXT("出戰場次"), FString::Printf(TEXT("%d（勝 %d / 敗 %d）"), R.Games, R.Wins, R.Losses)},
		{TEXT("勝率"), R.WinRate()},
		{TEXT("目前連勝 / 最佳連勝"), FString::Printf(TEXT("%d / %d"), R.Streak, R.BestStreak)},
		{TEXT("最快勝利"), R.BestWin > 0.0 ? FRSRecords::FmtTime(R.BestWin) : FString(TEXT("—"))},
		{TEXT("總作戰時間"), FRSRecords::FmtTime(R.PlayTime)},
		{TEXT("戰役任務完成"), FString::Printf(TEXT("%d / %d"), MDone, D.Missions.Num())},
		{TEXT("生產單位"), RS::FmtMoney(R.Built)},
		{TEXT("消滅敵軍"), RS::FmtMoney(R.Killed)},
		{TEXT("單場最多擊殺"), FString::FromInt(R.MaxKills)},
		{TEXT("損失單位 / 建築"), FString::Printf(TEXT("%s / %s"), *RS::FmtMoney(R.Lost), *RS::FmtMoney(R.BldLost))},
		{TEXT("採礦總收入"), TEXT("$") + RS::FmtMoney(R.Harvested)},
		{TEXT("特殊武器發射"), FString::FromInt(R.Powers)},
		{TEXT("間諜滲透成功"), FString::FromInt(R.Spies)},
		{TEXT("締結條約"), FString::FromInt(R.Treaties)},
		{TEXT("航母 / 無人機生產"), FString::Printf(TEXT("%d / %d"), R.Carriers, R.Drones)},
		{TEXT("使用過的國家"), FString::FromInt(R.Countries.Num())},
		{TEXT("作戰過的地圖"), FString::FromInt(R.Maps.Num())} };
	float YY = Y + 70;
	for (const auto& Rw : Rows)
	{
		Text(Rw.Key, X + 22, YY, 16, FLinearColor(0.8f, 0.8f, 0.75f));
		Text(Rw.Value, X + 230, YY, 16, FLinearColor(1.f, 0.95f, 0.8f));
		YY += 34;
	}
	if (Button(X + 22, Y + H - 60, 200, 40, TEXT("返回"), 16)) MenuPanel = 0;
	int32 NUn = 0;
	for (const FRSAchDef& A : D.Achievements)
		if (R.HasAch(A.Id)) NUn++;
	const float AX = X + 460;
	Text(FString::Printf(TEXT("成就  %d / %d"), NUn, D.Achievements.Num()), AX, Y + 18, 26, FLinearColor(1.f, 0.85f, 0.35f), true);
	for (int32 I = 0; I < D.Achievements.Num(); ++I)
	{
		const FRSAchDef& A = D.Achievements[I];
		const bool bUn = R.HasAch(A.Id);
		const float CX = AX + (I % 2) * 420, CY = Y + 70 + (I / 2) * 70;
		Rect(CX, CY, 400, 60, bUn ? FLinearColor(0.25f, 0.18f, 0.05f, 0.9f) : FLinearColor(0.1f, 0.1f, 0.12f, 0.9f));
		Frame(CX, CY, 400, 60, bUn ? FLinearColor(1.f, 0.75f, 0.2f) : FLinearColor(0.3f, 0.3f, 0.32f), 1.f);
		Text(bUn ? A.Icon : TEXT("？"), CX + 10, CY + 12, 26, bUn ? FLinearColor(1.f, 0.8f, 0.25f) : FLinearColor(0.5f, 0.5f, 0.5f), true);
		Text(A.Name, CX + 56, CY + 6, 16, bUn ? FLinearColor(1.f, 0.95f, 0.8f) : FLinearColor(0.6f, 0.6f, 0.6f), true);
		Text(A.Desc, CX + 56, CY + 32, 13, bUn ? FLinearColor(0.85f, 0.8f, 0.7f) : FLinearColor(0.5f, 0.5f, 0.5f));
	}
}

void ARSHUD::DrawLoads()
{
	const float X = 600, Y = 300, W = 720, H = 360;
	Rect(X, Y, W, H, FLinearColor(0.06f, 0.05f, 0.06f, 0.98f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.12f, 0.08f), 2.f);
	Text(TEXT("讀取存檔"), X + 22, Y + 18, 24, FLinearColor(1.f, 0.85f, 0.35f), true);
	for (int32 N = 0; N < RSSave::SLOTS; ++N)
	{
		const FRSSlotInfo Info = RSSave::SlotInfo(N);
		const FString Lbl = Info.bValid ? FString::Printf(TEXT("槽位 %d：%s（%s）"), N + 1, *Info.Title, *Info.Date) : FString::Printf(TEXT("槽位 %d：（空）"), N + 1);
		if (Button(X + 22, Y + 70 + N * 56, W - 44, 46, Lbl, 16, Info.bValid))
		{
			if (ARSGameMode* G = GM()) G->LoadGame(N);
		}
	}
	if (Button(X + 22, Y + H - 60, 200, 40, TEXT("返回"), 16)) MenuPanel = 0;
}

void ARSHUD::DrawMenuHelp()
{
	const float X = 300, Y = 180, W = 900, H = 560;
	Rect(X, Y, W, H, FLinearColor(0.06f, 0.05f, 0.06f, 0.98f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.12f, 0.08f), 2.f);
	const FString Txt = TEXT("《紅色風暴》玩法\n\n1. 選取基地車，按 D（或雙擊）展開成建造廠。\n2. 右側建造欄：發電廠 → 精煉廠（附採礦車）→ 兵營 → 戰車工廠。\n3. 採礦車自動採集金礦（紫色寶石價值加倍）換取資金。\n4. 雷達站開啟小地圖；科技中心解鎖高級單位與超級武器。\n5. 用工程師佔領中立油井，可持續獲得資金；也能佔領敵方建築！\n6. 摧毀所有敵方建築即可獲勝。\n\n滑鼠：左鍵選取/框選，右鍵移動/攻擊，滾輪縮放，中鍵拖曳平移\n鍵盤：A 攻擊移動、S 停止、Ctrl+數字 編隊、Z 出售、X 修理、T 外交\n遊戲中按 F1 可隨時查看完整說明。");
	TextWrap(Txt, X + 30, Y + 30, W - 60, 18, FLinearColor(0.92f, 0.92f, 0.88f), 30.f);
	if (Button(X + 30, Y + H - 60, 200, 40, TEXT("返回"), 16)) MenuPanel = 0;
}
