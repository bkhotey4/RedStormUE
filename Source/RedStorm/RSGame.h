// 遊戲主控：世界、指令、戰鬥規則、外交、超級武器（對應 Godot game.gd）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RSDefs.h"
#include "RSGlobals.h"
#include "RSMap.h"
#include "RSPath.h"
#include "RSGame.generated.h"

class URSPlayer;
class ARSUnit;
class ARSBuilding;
class ARSEntity;
class URSAI;
class URSMission;
class URSWorldEvents;
class ARSTerrain;
class ARSFx;
class URSAudio;
class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class ARSCameraPawn;
class UStaticMeshComponent;

struct FRSMessage
{
	FString Text;
	float T = 0.f;
};

struct FRSPing
{
	FVector2D P;
	float T = 0.f;
};

struct FRSMarker
{
	FVector2D P;
	float T = 0.f;
	FLinearColor C;
};

struct FRSCrate
{
	FIntPoint Cell = FIntPoint::ZeroValue;
	FString Kind;
	UStaticMeshComponent* Mesh = nullptr;
	float T = 0.f;
};

struct FRSTimed
{
	float T = 0.f;
	TFunction<void()> Fn;
};

UCLASS()
class REDSTORM_API ARSGame : public AActor
{
	GENERATED_BODY()
public:
	ARSGame();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	static constexpr int32 BUILD_GAP = 3;

	int32 W = 72, H = 72;
	TUniquePtr<FRSMap> Map;
	UPROPERTY() TObjectPtr<ARSTerrain> Terrain = nullptr;
	UPROPERTY() TObjectPtr<ARSFx> Fx = nullptr;
	UPROPERTY() TObjectPtr<URSAudio> AudioRef = nullptr;
	UPROPERTY() TObjectPtr<ARSCameraPawn> Cam = nullptr;
	FRSGridPath Astar;
	FRSGridPath AstarW;
	UPROPERTY() TArray<TObjectPtr<URSPlayer>> Players;
	UPROPERTY() TObjectPtr<URSPlayer> Human = nullptr;
	UPROPERTY() TObjectPtr<URSPlayer> Neutral = nullptr;
	UPROPERTY() TArray<TObjectPtr<URSAI>> AIs;
	UPROPERTY() TArray<TObjectPtr<ARSUnit>> Units;
	UPROPERTY() TArray<TObjectPtr<ARSBuilding>> Buildings;
	TArray<ARSBuilding*> BGrid;
	TMap<int32, TArray<ARSUnit*>> Buckets;
	TArray<TWeakObjectPtr<ARSEntity>> Selection;
	TMap<int32, TArray<TWeakObjectPtr<ARSUnit>>> Groups;
	float Time = 0.f;
	bool bPaused = false;
	bool bGameOver = false;
	FString Result;
	float Speed = 1.f;
	int32 NextId = 1;
	TArray<uint8> Explored;
	float RevealT = 0.f, CheckT = 0.f;
	FString PlaceId;
	FString CmdMode;
	bool bDragging = false;
	FVector2D DragStart = FVector2D::ZeroVector;    // 螢幕座標
	TWeakObjectPtr<ARSEntity> Hover;
	TArray<FRSTimed> Events;
	TArray<FRSMarker> Markers;
	int32 LastGroupKey = -1;
	double LastGroupTime = 0.0;
	FRSGameSettings Settings;
	float AckT = 0.f;
	UPROPERTY() TObjectPtr<URSMission> Mission = nullptr;
	UPROPERTY() TObjectPtr<URSWorldEvents> WorldEv = nullptr;
	bool bLoading = false;
	TArray<FRSCrate> Crates;
	float CrateT = 30.f;
	FString Objective;
	TArray<FRSMessage> Msgs;
	TArray<FRSPing> Pings;
	TArray<FRSAchDef> FreshAch;
	float DiploT = 0.f;
	float EwT = 0.f;
	TMap<FString, int32> CycleIdx;
	bool bShowFps = false;
	// UI 面板狀態（由 HUD 讀寫）
	bool bPausePanel = false, bHelpPanel = false, bDiploPanel = false, bSavePanel = false;

	URSAudio* Audio() const { return AudioRef; }
	void Start(const FRSGameSettings& S, URSAudio* InAudio, ARSCameraPawn* InCam);

	// 生成 / 建築
	ARSUnit* SpawnUnit(URSPlayer* P, const FString& Id, const FVector2D& At);
	ARSBuilding* BldAt(int32 X, int32 Y) const;
	bool CanPlace(const FString& Id, FIntPoint TL, URSPlayer* P, const ARSUnit* Ignore = nullptr, bool bNeedNear = true) const;
	bool CellBuildable(const FString& Id, int32 X, int32 Y) const;
	bool NearOwn(FIntPoint TL, FIntPoint Sz, URSPlayer* P, int32 Extra = 0) const;
	ARSBuilding* PlaceBuilding(URSPlayer* P, const FString& Id, FIntPoint TL, bool bInstant = false);
	void OnBuildingComplete(ARSBuilding* B);
	void RemoveBuildingCells(ARSBuilding* B);
	void SpawnProduced(URSPlayer* P, const FString& Id);
	FIntPoint NearestFree(FIntPoint C, int32 MaxR) const;
	ARSBuilding* NearestRefinery(URSPlayer* P, const FVector2D& From) const;
	ARSBuilding* NearestAirfield(URSPlayer* P, const FVector2D& From) const;

	// 查詢
	bool PassablePos(const FVector2D& P, bool bWater = false) const;
	bool Los(const FVector2D& A, const FVector2D& B, bool bWater = false) const;
	FIntPoint NearestWater(FIntPoint C, int32 MaxR) const;
	TArray<FVector2D> FindPath(const FVector2D& From, const FVector2D& To, bool bWater = false);
	void RebuildBuckets();
	void Neighbors(const FVector2D& P, TArray<ARSUnit*>& Out) const;
	ARSEntity* FindTarget(const ARSEntity* Ent, const FRSWeapon& Wp, float Rng, bool bPreferUnits = false, bool bAirOnly = false) const;
	void DamageArea(const FVector2D& P, float R, float Dmg, const FString& Wh, ARSEntity* Att, ARSEntity* Primary = nullptr, URSPlayer* InOwner = nullptr, bool bFriendly = false);

	// 事件
	void OnEntityDied(ARSEntity* E);
	void QuickSave();
	void After(float Delay, TFunction<void()> Fn);

	// 外交
	TArray<URSPlayer*> OthersOf(const URSPlayer* P) const;
	float Strength(const URSPlayer* P) const;
	void UpdateDiplomacy(float Dt);
	bool Propose(URSPlayer* From, URSPlayer* To, const FString& Kind);
	void ApplyTreaty(URSPlayer* A, URSPlayer* B, const FString& Kind);
	void BreakTreaty(URSPlayer* A, URSPlayer* B);
	TArray<URSPlayer*> HumanAllies() const;
	void NotifyAttack(ARSEntity* Victim, ARSEntity* Attacker);

	// 特殊行動
	void SpyEnter(ARSUnit* U, ARSBuilding* B);
	void EngineerEnter(ARSUnit* U, ARSBuilding* B);
	void PlantC4(ARSUnit* U, ARSEntity* T);
	void TryDeploy(ARSUnit* U);
	void RemoveUnitSilent(ARSUnit* U);
	void Sell(ARSBuilding* B);
	void FinishSell(ARSBuilding* B);
	void OnPowerReady(URSPlayer* P, const FString& Kind);
	URSPlayer* PlayerAt(const FVector2D& Target, URSPlayer* Attacker) const;
	bool FirePower(URSPlayer* P, const FString& Kind, const FVector2D& Target);
	void FireNuke(URSPlayer* P, const FVector2D& Target);
	void FireStorm(const FVector2D& Target);
	bool CanGarrison(const ARSUnit* U, const ARSBuilding* B) const;
	void GarrisonEnter(ARSUnit* U, ARSBuilding* B);
	void GarrisonExit(ARSBuilding* B);
	void UpdateCrates(float Dt);
	void PickCrate(ARSUnit* U, int32 CrateIndex);
	void DroneDetonate(ARSUnit* U, ARSEntity* T);
	void UpdateEw(float Dt);
	void Message(const FString& T);
	void Ping(const FVector2D& P);

	// 戰爭迷霧
	bool IsExplored(int32 X, int32 Y) const;
	void Reveal(const FVector2D& C, float R);
	void RevealEntity(ARSEntity* E);
	void RevealAllNow();
	void UpdateVisibility();

	void Step(float Dt);
	void CheckVictory();
	void EndGame(bool bWin);

	// 鏡頭與座標
	FVector ToWorld(const FVector2D& P, float ZUp = 0.f) const;
	float GroundHeight(const FVector2D& P) const;
	void CenterOn(const FVector2D& P);
	bool MouseWorld(FVector2D& Out) const;
	bool ScreenToCell(const FVector2D& Screen, FVector2D& Out) const;
	bool HighGround(const ARSEntity* Ent, const ARSEntity* T) const;
	float SoundAttenuation(const FVector2D& WP) const;
	bool Project(const FVector& World, FVector2D& Screen) const;

	// 選取與輸入（由 PlayerController 呼叫）
	ARSEntity* EntityAtScreen(const FVector2D& SP) const;
	void UpdateHover();
	void SelectList(const TArray<ARSEntity*>& List, bool bAdd = false);
	TArray<ARSUnit*> OwnSelectedUnits() const;
	TArray<ARSEntity*> ValidSelection() const;
	void OnLeftPress(bool bShift, bool bDouble);
	void OnLeftRelease(bool bShift);
	void OnRightPress(bool bCtrl, bool bShift);
	void OnKey(FKey Key, bool bCtrl, bool bShift);
	void ClickSelect(bool bShift, bool bDbl);
	void AckSelect();
	void BoxSelect(const FVector2D& A, const FVector2D& B, bool bAdd);
	void ContextOrder(bool bForceFire, bool bShift);
	void UnitAck(const TArray<ARSUnit*>& List, bool bAttack);
	void CommandMove(const TArray<ARSUnit*>& List, const FVector2D& Dest, bool bAmove = false);
	FIntPoint PopNearest(TArray<FIntPoint>& Cells, const FVector2D& P) const;
	// 電影鏡頭：自動導播追最激烈的戰鬥，低角度環繞
	struct FRSHot { FVector2D P; float T = 0.f; float W = 1.f; FString Label; };
	TArray<FRSHot> Hot;
	bool bCinema = false;
	float CineShotT = 0.f, CineSaveZoom = 1.1f;
	FVector2D CineTarget = FVector2D::ZeroVector, CineFocus = FVector2D::ZeroVector;
	FString CineLabel;
	void AddHot(const FVector2D& P, float W, const FString& Label = FString());
	void ToggleCinema();
	void UpdateCinema(float RealDt);
	struct FRSWreck { TWeakObjectPtr<ARSUnit> U; float T = 0.f; float SmokeT = 0.f; };
	TArray<FRSWreck> Wrecks;
	void MakeWreck(ARSUnit* U);
	void UpdateWrecks(float Dt);
	void FormationMove(const TArray<ARSUnit*>& Group, const FVector2D& Dest, bool bAmove);
	bool bFormation = true;   // 陣型移動開關（F 鍵）
	TArray<FIntPoint> FreeCellsNear(FIntPoint C, int32 N) const;
	void CycleSelect(const TArray<ARSEntity*>& List, const FString& Key, const FString& EmptyMsg);
	void BeginPlace(const FString& Id);
	FIntPoint PlaceTL() const;
	void TryPlace();
	void HomeKey();

	// 日夜與天氣
	float Tod = 0.3f;          // 一天的進度 0~1（0.25 日出、0.5 正午、0.75 日落）
	int32 Weather = 0;         // 0 晴、1 雨、2 霧（沙塵）、3 雪
	float WeatherT = 90.f;
	float Darkness = 0.f;      // 0 白天 ~ 1 深夜
	float SightMult() const;
	FString EnvLabel() const;
	// 掩體：直射武器的彈道被敵方 / 中立建築與圍牆擋住時傳回該建築
	ARSEntity* FireBlocker(const FVector2D& From, const FVector2D& To, const ARSEntity* Att, const ARSEntity* T) const;

	// 除錯
	void EnableAutopilot();

private:
	void SetupPlayers(const FRSGameSettings& S);
	void SpawnStart(URSPlayer* P);
	void SetupEnvironment();
	void UpdateCrateVisuals(float Dt);
	void CleanupDead();
	TArray<TObjectPtr<AActor>> EnvActors;
	TWeakObjectPtr<UDirectionalLightComponent> SunL;
	TWeakObjectPtr<USkyLightComponent> SkyLC;
	TWeakObjectPtr<UExponentialHeightFogComponent> FogC;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PrecipISM = nullptr;
	TArray<FVector> PrecipP;
	float SunBaseI = 9.f, FogBase = 0.008f, NightLightT = 0.f, EnvT = 0.f;
	void UpdateEnvironment(float Dt, float VDt);
	TArray<TPair<float, TObjectPtr<ARSEntity>>> Graveyard;
};
