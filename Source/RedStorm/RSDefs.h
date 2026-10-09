// 遊戲數值設定表 —— 從 Content/RedStorm/Data/*.json 讀取（可直接用記事本修改，不需重新編譯）
#pragma once

#include "CoreMinimal.h"

class FJsonObject;

struct FRSWeapon
{
	FString Id;
	float Dmg = 0.f, Rof = 1.f, Range = 0.f, MinRange = 0.f, Splash = 0.f;
	FString Proj, Wh = TEXT("ap"), Snd;
	bool bAir = false, bAirOnly = false, bPower = false;
	int32 Burst = 0, Ammo = 0;
	bool IsValid() const { return !Id.IsEmpty(); }
};

// 單位與建築共用的定義（對應 Godot 的 Defs.get_def 回傳字典）
struct FRSDef
{
	FString Id, Name, Faction, Country, Cat, Kind, Role, Desc, Armor = TEXT("heavy");
	FString Weapon, Weapon2, Mcv, Carrier, Super, PowerSw;
	float Cost = 0.f, Hp = 100.f, Speed = 1.f, Sight = 5.f, Ew = 0.f, Power = 0.f, Income = 0.f;
	FIntPoint Size = FIntPoint(1, 1);
	TArray<FString> Req;
	int32 Limit = 0, Hangar = 3, Garrison = 0;
	bool bIsUnit = false, bBuildable = true;
	bool bTurret = false, bEngineer = false, bHarvester = false, bJet = false, bDrone = false, bKamikaze = false;
	bool bLoiter = false, bHeli = false, bAirship = false, bPreferBld = false, bHeal = false, bC4 = false, bSpy = false, bSub = false;
	bool bNaval = false, bWall = false, bCiv = false, bExplode = false, bAirfield = false, bRunway = false;
	bool IsValid() const { return !Id.IsEmpty(); }
};

struct FRSSuper
{
	FString Id, Name, Target, Desc;
	float Charge = 100.f, Radius = 0.f;
};

struct FRSFaction
{
	FString Id, Name, Infantry;
	FLinearColor Color = FLinearColor::White;
};

struct FRSCountry
{
	FString Id, Name, Faction, Power, Desc;
	FLinearColor Flag[3];
	TMap<FString, float> Cost, Time, Hp, Speed, Charge;
	float DmgVsShip = 1.f;
};

struct FRSMapPreset
{
	FString Name, Theme = TEXT("temperate"), Layout = TEXT("plains"), Desc;
	int32 Size = 72;
};

struct FRSTheme
{
	FString Id;
	FLinearColor Mini[6];
	FLinearColor Clear = FLinearColor::Black;
	float TreeDensity = 0.5f;
	int32 Beach = 1;
	bool bPalms = false, bSnowTrees = false;
};

struct FRSMissionDef
{
	FString Id, Name, Camp = TEXT("basic"), Faction, Enemy, Country, EnemyCountry, Headline, Brief;
	FRSMapPreset Map;
	int32 Seed = 1, Money = 10000, Enemies = 1;
	bool bFfa = false;
};

struct FRSEventDef
{
	FString Id, Name, News;
	float Dur = 0.f;
	bool bBad = false, bGlobal = false;
};

struct FRSAchDef
{
	FString Id, Name, Desc, Icon;
};

struct FRSIntroLine
{
	float T = 0.f;
	FString En, Zh;
};

class REDSTORM_API FRSDefs
{
public:
	static FRSDefs& Get();
	bool Load();
	bool IsLoaded() const { return bLoaded; }

	TMap<FString, FRSFaction> Factions;
	TMap<FString, TMap<FString, float>> Warheads;
	TMap<FString, FRSWeapon> Weapons;
	TMap<FString, FRSDef> Units;
	TMap<FString, FRSDef> Buildings;
	TMap<FString, TArray<FString>> BuildOrder;
	TMap<FString, FRSSuper> Supers;
	TArray<FString> PowerOrder;
	TMap<FString, FRSCountry> Countries;
	TArray<FString> CountryOrder;
	TArray<FRSMapPreset> Presets;
	TMap<FString, FRSTheme> Themes;
	TArray<FRSMissionDef> Missions;
	TArray<FRSEventDef> Events;
	TArray<FRSAchDef> Achievements;
	TArray<FRSIntroLine> IntroLines;

	const FRSDef& GetDef(const FString& Id) const;
	const FRSWeapon& GetWeapon(const FString& Id) const;
	const FRSSuper& GetSuper(const FString& Id) const;
	const FRSCountry* GetCountry(const FString& Id) const { return Countries.Find(Id); }
	const FRSMissionDef* FindMission(const FString& Id) const;
	const FRSTheme& GetTheme(const FString& Id) const;
	float WarheadMult(const FString& Wh, const FString& Armor) const;
	float CountryMult(const FString& Country, const FString& Table, const FString& Id) const;
	float BuildTime(const FString& Id) const;
	FString FactionName(const FString& F) const { const FRSFaction* P = Factions.Find(F); return P ? P->Name : F; }

private:
	bool bLoaded = false;
	FRSDef EmptyDef;
	FRSWeapon EmptyWeapon;
	FRSSuper EmptySuper;
	FRSTheme DefaultTheme;
	static TSharedPtr<FJsonObject> LoadJson(const FString& File);
	void ParseDef(const FString& Id, const TSharedPtr<FJsonObject>& O, bool bUnit, FRSDef& D);
};
