// 介面：片頭字幕、主選單、戰役 / 戰績 / 讀檔、任務簡報、遊戲 HUD（建造欄、雷達、資訊）、暫停 / 外交 / 存檔 / 結算面板
// 全部以 Canvas 即時繪製（1920x1080 虛擬座標，依視窗縮放），中文字型直接讀取 Windows 系統字型。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RSDefs.h"
#include "RSHUD.generated.h"

class ARSGame;
class ARSGameMode;
class ARSPlayerController;
class URSPlayer;
class ARSEntity;
class ARSUnit;
class ARSBuilding;
class UFont;
class UTexture2D;
class UTexture;

UCLASS()
class REDSTORM_API ARSHUD : public AHUD
{
	GENERATED_BODY()
public:
	ARSHUD();
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	void OnEnterMenu();
	void OnEnterGame();
	void MenuBack();
	void Escape();
	void ToggleHelp();
	void ToggleDiplomacy();
	bool IsModalOpen() const;
	bool IsOverUI(const FVector2D& MouseScreen) const;

private:
	// ───── 基本繪圖（虛擬座標）─────
	float S = 1.f;
	FVector2D VS = FVector2D(1920, 1080);
	FVector2D M = FVector2D::ZeroVector;     // 滑鼠（虛擬座標）
	bool bClick = false, bRClick = false;
	float Wheel = 0.f;
	float T = 0.f;
	UPROPERTY() TObjectPtr<UFont> Font = nullptr;
	UPROPERTY() TObjectPtr<UFont> FontBold = nullptr;
	TMap<uint64, float> GlyphW;

	void Rect(float X, float Y, float W, float H, const FLinearColor& C);
	void Frame(float X, float Y, float W, float H, const FLinearColor& C, float Th = 1.f);
	void Line(float X0, float Y0, float X1, float Y1, const FLinearColor& C, float Th = 1.f);
	void Tri(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Col);
	void Poly(const TArray<FVector2D>& Pts, const FLinearColor& C);
	void Circle(const FVector2D& C, float R, const FLinearColor& Col, int32 Seg = 24);
	void Ring(const FVector2D& C, float R, const FLinearColor& Col, float Th = 1.5f, int32 Seg = 32, float Sy = 1.f);
	void Star(const FVector2D& C, float R, const FLinearColor& Col);
	void Tex(UTexture* Tx, float X, float Y, float W, float H, const FLinearColor& Tint = FLinearColor::White);
	float TextW(const FString& Txt, float Size, bool bBold = false);
	// Align: 0 左, 1 中, 2 右（以 W 為寬）
	void Text(const FString& Txt, float X, float Y, float Size, const FLinearColor& C, bool bBold = false, int32 Align = 0, float W = 0.f, bool bShadow = true);
	float TextWrap(const FString& Txt, float X, float Y, float W, float Size, const FLinearColor& C, float LineH = 0.f, bool bBold = false, int32 MaxChars = -1);
	bool Hovered(float X, float Y, float W, float H) const { return M.X >= X && M.Y >= Y && M.X < X + W && M.Y < Y + H; }
	bool Button(float X, float Y, float W, float H, const FString& Label, float Size = 17.f, bool bEnabled = true, bool bOn = false);
	bool Selector(float X, float Y, float W, float H, const FString& Label, const TArray<FString>& Opts, int32& Idx);
	bool Slider(float X, float Y, float W, float H, float& V);
	void Bevel(float X, float Y, float W, float H, const FLinearColor& Base, bool bInset = true);
	void Panel(float X, float Y, float W, float H, const FString& Title);
	void Flag(float X, float Y, float W, float H, const FString& Id);
	UTexture2D* Cameo(const FString& Key);
	bool Project(const FVector& W, FVector2D& Out) const;    // 世界 → 虛擬座標
	ARSGame* Game() const;
	ARSGameMode* GM() const;
	ARSPlayerController* PC() const;
	FLinearColor Accent = FLinearColor(0.16f, 0.78f, 0.86f);   // 戰術介面主色（青）
	void Brackets(float X, float Y, float W, float H, const FLinearColor& C, float Len = 12.f, float Th = 2.f);

	// ───── 片頭 / 簡報 ─────
	void DrawIntro();
	void DrawBriefing();
	void DrawVideoFull(float Alpha = 1.f);

	// ───── 主選單 ─────
	int32 OptCountry = 0, OptEnemyCountry = 0, OptEnemies = 0, OptRelation = 0, OptDiff = 1, OptMap = 0, OptMapSize = 0, OptMoney = 1, OptSpeed = 1, OptCrates = 0, OptEvents = 0;
	int32 MenuPanel = 0;     // 0 主畫面 1 戰役 2 戰績 3 讀檔 4 說明
	int32 CampSel = 0;
	int32 CampScroll = 0;
	int32 PreviewSeed = 1;
	int32 PreviewKey = -1;
	FString PreviewDesc;
	UPROPERTY() TObjectPtr<UTexture2D> PreviewTex = nullptr;
	int32 PreviewW = 1, PreviewH = 1;
	int32 BgStyle = 0;
	void DrawMenu();
	void DrawMenuBackground();
	void DrawMenuMain();
	void DrawCampaign();
	void DrawRecords();
	void DrawLoads();
	void DrawMenuHelp();
	void RefreshPreview();
	FRSMapPreset SizedMap() const;
	void StartSkirmish();
	void DrawDiamondTex(UTexture2D* Tx, int32 TW, int32 TH, const FVector2D& Center, float K);

	// ───── 遊戲 HUD ─────
	static constexpr float SW = 300.f;
	static constexpr float CW = 134.f;
	static constexpr float CH = 98.f;
	static constexpr float MMH = 160.f;
	int32 Tab = 0;
	int32 Scroll[5] = { 0, 0, 0, 0, 0 };
	FString HoverItem;
	bool bMMDrag = false;
	UPROPERTY() TObjectPtr<UTexture2D> MiniTex = nullptr;
	TArray<FColor> MiniBase;
	float MiniT = 0.f;
	float FpsT = 0.f, FpsShow = 60.f;
	int32 FpsFrames = 0;

	void DrawGame();
	void DrawWorldOverlay(ARSGame* G);
	void DrawUnitBox(ARSEntity* E, const FLinearColor& Col, bool bHp);
	void DrawHpBar(float CX, float Y, float W, float R, int32 Segs, const FLinearColor& Override = FLinearColor(-1, 0, 0, 0));
	void DrawGroundQuad(ARSGame* G, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Col, float Z = 3.f);
	void DrawGroundCircle(ARSGame* G, const FVector2D& C, float R, const FLinearColor& Col, float Th = 2.f);
	void DrawTop(ARSGame* G);
	void DrawSidebar(ARSGame* G);
	void DrawCameo(ARSGame* G, float X, float Y, const FString& Id);
	void DrawTooltip(ARSGame* G, const FString& Id, float Y);
	void DrawMinimap(ARSGame* G, float X, float Y, float W, float H);
	void UpdateMinimap(ARSGame* G);
	FVector2D MMOrigin(ARSGame* G) const;
	float MMK(ARSGame* G) const;
	FVector2D WorldToMM(ARSGame* G, const FVector2D& P) const;
	FVector2D MMToWorld(ARSGame* G, const FVector2D& P) const;
	bool TabEnabled(ARSGame* G, int32 I) const;
	TArray<FString> TabItems(ARSGame* G, int32 I) const;
	void ClickItem(ARSGame* G, const FString& Id);
	void PressPower(ARSGame* G, const FString& K);
	FLinearColor HpColor(float R) const;

	// 面板
	void DrawPausePanel(ARSGame* G);
	void DrawSavePanel(ARSGame* G);
	void DrawHelpPanel();
	void DrawDiploPanel(ARSGame* G);
	void DrawEndPanel(ARSGame* G);
};
