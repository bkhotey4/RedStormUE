// 主流程：片頭 → 主選單 → （任務簡報）→ 遊戲（對應 Godot main.gd）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RSGlobals.h"
#include "RSGameMode.generated.h"

class ARSGame;
class URSAudio;
class UMediaPlayer;
class UMediaTexture;
class UFileMediaSource;
class UMediaSoundComponent;

UENUM()
enum class ERSState : uint8
{
	Intro,
	Menu,
	Briefing,
	Game
};

UCLASS()
class REDSTORM_API ARSGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARSGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	ERSState State = ERSState::Menu;
	float StateT = 0.f;
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	UPROPERTY() TObjectPtr<URSAudio> Audio = nullptr;
	FRSGameSettings LastSettings;
	FRSGameSettings PendingSettings;     // 簡報結束後要開始的任務
	const struct FRSMissionDef* BriefMission = nullptr;

	// 影片（片頭 / 選單背景）
	UPROPERTY() TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;
	UPROPERTY() TObjectPtr<UMediaTexture> MediaTexture = nullptr;
	UPROPERTY() TObjectPtr<UFileMediaSource> MediaSource = nullptr;
	UPROPERTY() TObjectPtr<UMediaSoundComponent> MediaSound = nullptr;
	bool bVideoOk = false;
	FString VideoName;

	void StartGame(const FRSGameSettings& S);
	void ToMenu();
	void RestartGame();
	bool LoadGame(int32 Slot);
	void PlayIntro();
	void FinishIntro();
	void FinishBriefing();
	void QuitGame();
	FRSGameSettings DefaultSettings() const;

	bool PlayVideo(const FString& File, bool bLoop, bool bSound);
	void StopVideo();
	bool VideoPlaying() const;

	// 自動測試（命令列 -RSAutoTest）：跑過片頭、選單、遭遇戰、存讀檔、任務簡報並截圖到 Saved/AutoTest
	bool bAutoTest = false;
	bool bWarShot = false;
	float AutoT = 0.f;
	int32 AutoStep = 0;
	float AutoNextStat = 0.f;
	int32 AutoQ = 0;          // 全部任務 / 地圖巡檢的索引
	float AutoQT = 0.f;
	bool bAutoShotDone = false;
	// 外觀展示（命令列 -RSGallery）：擺出全部建築與單位並截圖
	bool bGallery = false;
	TArray<FVector2D> GalleryShots;
	void GalleryTick(float Dt);
	void BuildGallery();
	void AutoTestTick(float Dt);
	void AutoShot(const FString& Name);
	void AutoStats(const TCHAR* Tag);

private:
	void DestroyGame();
	void SpawnGame(const FRSGameSettings& S);
	class ARSCameraPawn* GetCam() const;
	float VideoStartT = 0.f;
};
