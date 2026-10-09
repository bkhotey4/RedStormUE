// 使用者設定、戰役進度、戰績與成就（存在 Saved/RedStorm/*.json）
#pragma once

#include "CoreMinimal.h"
#include "RSDefs.h"

struct REDSTORM_API FRSUserSettings
{
	FString Faction = TEXT("allies");
	FString Country = TEXT("usa");
	FString CampaignDone;
	float SfxVol = 0.8f, VoiceVol = 1.0f, MusicVol = 0.5f;
	bool bEdgeScroll = true;
	bool bIntroSeen = false;
	bool bUnlockAll = false;
	int32 MapSize = 0;
	int32 MenuBg = -1;

	static FRSUserSettings& Get();
	void Load();
	void Save() const;

	bool MissionDone(const FString& Mid) const;
	void MarkMissionDone(const FString& Mid);
	bool MissionUnlocked(const FString& Mid) const;
	FString NextMission(const FString& Mid) const;
};

// 開局設定（對應 Godot game.settings 字典）
struct REDSTORM_API FRSGameSettings
{
	FString Faction = TEXT("allies");
	FString Country = TEXT("random");
	FString EnemyFaction = TEXT("random");
	FString EnemyCountry = TEXT("random");
	int32 Enemies = 1;
	bool bAiAllied = true;
	int32 Difficulty = 1;
	float Money = 10000.f;
	FRSMapPreset Map;
	int32 Seed = 1;
	float Speed = 1.f;
	bool bCrates = true;
	bool bEvents = true;
	FString Mission;
	bool bBriefShown = false;
	bool bFromSave = false;
	TArray<FString> FixedCountries;     // 已決定的各玩家國家（存檔用，避免讀檔時重新隨機）

	TSharedRef<class FJsonObject> ToJson() const;
	static FRSGameSettings FromJson(const TSharedPtr<class FJsonObject>& O);
	static FRSGameSettings FromMission(const FRSMissionDef& M, int32 InDifficulty);
};

struct REDSTORM_API FRSRecords
{
	int32 Games = 0, Wins = 0, Losses = 0, Built = 0, Killed = 0, Lost = 0, BldLost = 0;
	double Harvested = 0.0, PlayTime = 0.0, BestWin = 0.0;
	int32 Spies = 0, Powers = 0, Treaties = 0, MaxKills = 0, Carriers = 0, Drones = 0;
	int32 Streak = 0, BestStreak = 0;
	TArray<FString> Countries, Maps, Ach;

	static FRSRecords& Get();
	void Load();
	void Save() const;
	bool HasAch(const FString& Id) const { return Ach.Contains(Id); }
	void Bump(const FString& Key, int32 N = 1);
	FString WinRate() const;
	static FString FmtTime(double T);
	// 回傳本次新解鎖的成就
	TArray<FRSAchDef> OnGameEnd(class ARSGame* Game, bool bWin);
};
