#include "RSGlobals.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSMission.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TSharedPtr<FJsonObject> ReadJsonFile(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return nullptr;
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root)) return nullptr;
		return Root;
	}
	bool WriteJsonFile(const FString& Path, const TSharedRef<FJsonObject>& O)
	{
		FString Text;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(O, Writer);
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		PF.CreateDirectoryTree(*FPaths::GetPath(Path));
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	TArray<FString> StrList(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(K, A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A) Out.Add(V->AsString());
		}
		return Out;
	}
	TArray<TSharedPtr<FJsonValue>> ToVals(const TArray<FString>& L)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : L) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	}
}

// ───────────────────────── 使用者設定 ─────────────────────────
FRSUserSettings& FRSUserSettings::Get()
{
	static FRSUserSettings S;
	static bool bInit = false;
	if (!bInit)
	{
		bInit = true;
		S.Load();
	}
	return S;
}

void FRSUserSettings::Load()
{
	TSharedPtr<FJsonObject> O = ReadJsonFile(RS::SaveDir() / TEXT("settings.json"));
	if (!O.IsValid()) return;
	O->TryGetStringField(TEXT("faction"), Faction);
	O->TryGetStringField(TEXT("country"), Country);
	O->TryGetStringField(TEXT("campaign_done"), CampaignDone);
	double V;
	if (O->TryGetNumberField(TEXT("sfx_vol"), V)) SfxVol = (float)V;
	if (O->TryGetNumberField(TEXT("voice_vol"), V)) VoiceVol = (float)V;
	if (O->TryGetNumberField(TEXT("music_vol"), V)) MusicVol = (float)V;
	if (O->TryGetNumberField(TEXT("mapsize"), V)) MapSize = (int32)V;
	if (O->TryGetNumberField(TEXT("menu_bg"), V)) MenuBg = (int32)V;
	O->TryGetBoolField(TEXT("edge_scroll"), bEdgeScroll);
	O->TryGetBoolField(TEXT("intro_seen"), bIntroSeen);
	O->TryGetBoolField(TEXT("unlock_all"), bUnlockAll);
}

void FRSUserSettings::Save() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("faction"), Faction);
	O->SetStringField(TEXT("country"), Country);
	O->SetStringField(TEXT("campaign_done"), CampaignDone);
	O->SetNumberField(TEXT("sfx_vol"), SfxVol);
	O->SetNumberField(TEXT("voice_vol"), VoiceVol);
	O->SetNumberField(TEXT("music_vol"), MusicVol);
	O->SetNumberField(TEXT("mapsize"), MapSize);
	O->SetNumberField(TEXT("menu_bg"), MenuBg);
	O->SetBoolField(TEXT("edge_scroll"), bEdgeScroll);
	O->SetBoolField(TEXT("intro_seen"), bIntroSeen);
	O->SetBoolField(TEXT("unlock_all"), bUnlockAll);
	WriteJsonFile(RS::SaveDir() / TEXT("settings.json"), O);
}

bool FRSUserSettings::MissionDone(const FString& Mid) const
{
	TArray<FString> Parts;
	CampaignDone.ParseIntoArray(Parts, TEXT(","));
	return Parts.Contains(Mid);
}

void FRSUserSettings::MarkMissionDone(const FString& Mid)
{
	if (MissionDone(Mid)) return;
	CampaignDone = CampaignDone.IsEmpty() ? Mid : CampaignDone + TEXT(",") + Mid;
	Save();
}

bool FRSUserSettings::MissionUnlocked(const FString& Mid) const
{
	const FRSDefs& D = FRSDefs::Get();
	const FRSMissionDef* Target = D.FindMission(Mid);
	if (!Target) return false;
	FString Prev;
	for (const FRSMissionDef& M : D.Missions)
	{
		if (M.Id == Mid)
		{
			return Prev.IsEmpty() || MissionDone(Prev) || bUnlockAll;
		}
		if (M.Camp == Target->Camp) Prev = M.Id;
	}
	return true;
}

FString FRSUserSettings::NextMission(const FString& Mid) const
{
	const FRSDefs& D = FRSDefs::Get();
	const FRSMissionDef* Cur = D.FindMission(Mid);
	if (!Cur) return FString();
	bool bFound = false;
	for (const FRSMissionDef& M : D.Missions)
	{
		if (bFound && M.Camp == Cur->Camp) return M.Id;
		if (M.Id == Mid) bFound = true;
	}
	return FString();
}

// ───────────────────────── 開局設定 ─────────────────────────
TSharedRef<FJsonObject> FRSGameSettings::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("faction"), Faction);
	O->SetStringField(TEXT("country"), Country);
	O->SetStringField(TEXT("enemy_faction"), EnemyFaction);
	O->SetStringField(TEXT("enemy_country"), EnemyCountry);
	{
		TArray<TSharedPtr<FJsonValue>> FC;
		for (const FString& C : FixedCountries) FC.Add(MakeShared<FJsonValueString>(C));
		O->SetArrayField(TEXT("fixed_countries"), FC);
	}
	O->SetNumberField(TEXT("enemies"), Enemies);
	O->SetBoolField(TEXT("ai_allied"), bAiAllied);
	O->SetNumberField(TEXT("difficulty"), Difficulty);
	O->SetNumberField(TEXT("money"), Money);
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("name"), Map.Name);
	M->SetStringField(TEXT("theme"), Map.Theme);
	M->SetStringField(TEXT("layout"), Map.Layout);
	M->SetStringField(TEXT("desc"), Map.Desc);
	M->SetNumberField(TEXT("size"), Map.Size);
	O->SetObjectField(TEXT("map"), M);
	O->SetNumberField(TEXT("seed"), Seed);
	O->SetNumberField(TEXT("speed"), Speed);
	O->SetBoolField(TEXT("crates"), bCrates);
	O->SetBoolField(TEXT("events"), bEvents);
	O->SetStringField(TEXT("mission"), Mission);
	return O;
}

FRSGameSettings FRSGameSettings::FromJson(const TSharedPtr<FJsonObject>& O)
{
	FRSGameSettings S;
	if (!O.IsValid()) return S;
	O->TryGetStringField(TEXT("faction"), S.Faction);
	O->TryGetStringField(TEXT("country"), S.Country);
	O->TryGetStringField(TEXT("enemy_faction"), S.EnemyFaction);
	O->TryGetStringField(TEXT("enemy_country"), S.EnemyCountry);
	{
		const TArray<TSharedPtr<FJsonValue>>* FC = nullptr;
		if (O->TryGetArrayField(TEXT("fixed_countries"), FC))
			for (const TSharedPtr<FJsonValue>& V : *FC) S.FixedCountries.Add(V->AsString());
	}
	double V;
	if (O->TryGetNumberField(TEXT("enemies"), V)) S.Enemies = (int32)V;
	O->TryGetBoolField(TEXT("ai_allied"), S.bAiAllied);
	if (O->TryGetNumberField(TEXT("difficulty"), V)) S.Difficulty = (int32)V;
	if (O->TryGetNumberField(TEXT("money"), V)) S.Money = (float)V;
	const TSharedPtr<FJsonObject>* M = nullptr;
	if (O->TryGetObjectField(TEXT("map"), M))
	{
		(*M)->TryGetStringField(TEXT("name"), S.Map.Name);
		(*M)->TryGetStringField(TEXT("theme"), S.Map.Theme);
		(*M)->TryGetStringField(TEXT("layout"), S.Map.Layout);
		(*M)->TryGetStringField(TEXT("desc"), S.Map.Desc);
		if ((*M)->TryGetNumberField(TEXT("size"), V)) S.Map.Size = (int32)V;
	}
	if (O->TryGetNumberField(TEXT("seed"), V)) S.Seed = (int32)V;
	if (O->TryGetNumberField(TEXT("speed"), V)) S.Speed = (float)V;
	O->TryGetBoolField(TEXT("crates"), S.bCrates);
	O->TryGetBoolField(TEXT("events"), S.bEvents);
	O->TryGetStringField(TEXT("mission"), S.Mission);
	return S;
}

FRSGameSettings FRSGameSettings::FromMission(const FRSMissionDef& M, int32 InDifficulty)
{
	FRSGameSettings S;
	S.Faction = M.Faction;
	S.EnemyFaction = M.Enemy;
	S.Enemies = M.Enemies;
	S.bAiAllied = !M.bFfa;
	S.Difficulty = InDifficulty;
	S.Map = M.Map;
	S.Seed = M.Seed;
	S.Money = (float)M.Money;
	S.Speed = 1.f;
	S.Mission = M.Id;
	S.Country = M.Country.IsEmpty() ? FString() : M.Country;
	S.EnemyCountry = M.Country.IsEmpty() ? FString() : (M.EnemyCountry.IsEmpty() ? TEXT("random") : M.EnemyCountry);
	return S;
}

// ───────────────────────── 戰績 ─────────────────────────
FRSRecords& FRSRecords::Get()
{
	static FRSRecords R;
	static bool bInit = false;
	if (!bInit)
	{
		bInit = true;
		R.Load();
	}
	return R;
}

void FRSRecords::Load()
{
	TSharedPtr<FJsonObject> O = ReadJsonFile(RS::SaveDir() / TEXT("records.json"));
	if (!O.IsValid()) return;
	auto I = [&O](const TCHAR* K, int32& Out) { double V; if (O->TryGetNumberField(K, V)) Out = (int32)V; };
	auto D = [&O](const TCHAR* K, double& Out) { double V; if (O->TryGetNumberField(K, V)) Out = V; };
	I(TEXT("games"), Games); I(TEXT("wins"), Wins); I(TEXT("losses"), Losses); I(TEXT("built"), Built);
	I(TEXT("killed"), Killed); I(TEXT("lost"), Lost); I(TEXT("bld_lost"), BldLost);
	D(TEXT("harvested"), Harvested); D(TEXT("play_time"), PlayTime); D(TEXT("best_win"), BestWin);
	I(TEXT("spies"), Spies); I(TEXT("powers"), Powers); I(TEXT("treaties"), Treaties); I(TEXT("max_kills"), MaxKills);
	I(TEXT("carriers"), Carriers); I(TEXT("drones"), Drones); I(TEXT("streak"), Streak); I(TEXT("best_streak"), BestStreak);
	Countries = StrList(O, TEXT("countries"));
	Maps = StrList(O, TEXT("maps"));
	Ach = StrList(O, TEXT("ach"));
}

void FRSRecords::Save() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("games"), Games); O->SetNumberField(TEXT("wins"), Wins); O->SetNumberField(TEXT("losses"), Losses);
	O->SetNumberField(TEXT("built"), Built); O->SetNumberField(TEXT("killed"), Killed); O->SetNumberField(TEXT("lost"), Lost);
	O->SetNumberField(TEXT("bld_lost"), BldLost); O->SetNumberField(TEXT("harvested"), Harvested); O->SetNumberField(TEXT("play_time"), PlayTime);
	O->SetNumberField(TEXT("best_win"), BestWin); O->SetNumberField(TEXT("spies"), Spies); O->SetNumberField(TEXT("powers"), Powers);
	O->SetNumberField(TEXT("treaties"), Treaties); O->SetNumberField(TEXT("max_kills"), MaxKills); O->SetNumberField(TEXT("carriers"), Carriers);
	O->SetNumberField(TEXT("drones"), Drones); O->SetNumberField(TEXT("streak"), Streak); O->SetNumberField(TEXT("best_streak"), BestStreak);
	O->SetArrayField(TEXT("countries"), ToVals(Countries));
	O->SetArrayField(TEXT("maps"), ToVals(Maps));
	O->SetArrayField(TEXT("ach"), ToVals(Ach));
	WriteJsonFile(RS::SaveDir() / TEXT("records.json"), O);
}

void FRSRecords::Bump(const FString& Key, int32 N)
{
	if (Key == TEXT("spies")) Spies += N;
	else if (Key == TEXT("powers")) Powers += N;
	else if (Key == TEXT("treaties")) Treaties += N;
	else if (Key == TEXT("carriers")) Carriers += N;
	else if (Key == TEXT("drones")) Drones += N;
}

FString FRSRecords::WinRate() const
{
	if (Games == 0) return TEXT("—");
	return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.f * Wins / Games));
}

FString FRSRecords::FmtTime(double T)
{
	const int32 S = (int32)T;
	if (S >= 3600) return FString::Printf(TEXT("%d 時 %02d 分"), S / 3600, (S % 3600) / 60);
	return FString::Printf(TEXT("%d 分 %02d 秒"), S / 60, S % 60);
}

TArray<FRSAchDef> FRSRecords::OnGameEnd(ARSGame* Game, bool bWin)
{
	TArray<FRSAchDef> Fresh;
	if (!Game || !Game->Human) return Fresh;
	const FRSStats& St = Game->Human->Stats;
	Games++;
	if (bWin)
	{
		Wins++;
		Streak++;
		BestStreak = FMath::Max(BestStreak, Streak);
		if (BestWin <= 0.0 || Game->Time < BestWin) BestWin = Game->Time;
	}
	else
	{
		Losses++;
		Streak = 0;
	}
	Built += St.Built;
	Killed += St.Killed;
	Lost += St.Lost;
	BldLost += St.BldLost;
	Harvested += St.Harvested;
	PlayTime += Game->Time;
	MaxKills = FMath::Max(MaxKills, St.Killed);
	if (Game->Mission && bWin) Maps.AddUnique(TEXT("mission_") + Game->Mission->Id);
	else Maps.AddUnique(Game->Settings.Map.Name);
	Countries.AddUnique(Game->Human->Country);

	const FRSUserSettings& US = FRSUserSettings::Get();
	bool bBasicAll = true, bHybridAll = true;
	for (const FRSMissionDef& M : FRSDefs::Get().Missions)
	{
		if (!US.MissionDone(M.Id))
		{
			if (M.Camp == TEXT("hybrid")) bHybridAll = false;
			else bBasicAll = false;
		}
	}
	TMap<FString, bool> Cond;
	Cond.Add(TEXT("first_blood"), Wins >= 1);
	Cond.Add(TEXT("veteran"), Games >= 10);
	Cond.Add(TEXT("commander"), Wins >= 25);
	Cond.Add(TEXT("hundred"), Killed >= 100);
	Cond.Add(TEXT("thousand"), Killed >= 1000);
	Cond.Add(TEXT("builder"), Built >= 500);
	Cond.Add(TEXT("tycoon"), Harvested >= 500000.0);
	Cond.Add(TEXT("blitz"), bWin && Game->Time <= 600.f);
	Cond.Add(TEXT("spymaster"), Spies >= 10);
	Cond.Add(TEXT("super"), Powers >= 20);
	Cond.Add(TEXT("diplomat"), Treaties >= 5);
	Cond.Add(TEXT("admiral"), Carriers >= 5);
	Cond.Add(TEXT("swarm"), Drones >= 100);
	Cond.Add(TEXT("globetrotter"), Countries.Num() >= 6);
	Cond.Add(TEXT("explorer"), Maps.Num() >= 10);
	Cond.Add(TEXT("campaign_basic"), bBasicAll);
	Cond.Add(TEXT("campaign_hybrid"), bHybridAll);
	Cond.Add(TEXT("streak5"), Streak >= 5);
	Cond.Add(TEXT("ironwall"), bWin && St.BldLost == 0 && Game->Time > 120.f);
	Cond.Add(TEXT("massacre"), St.Killed >= 150);
	for (const FRSAchDef& A : FRSDefs::Get().Achievements)
	{
		const bool* C = Cond.Find(A.Id);
		if (!HasAch(A.Id) && C && *C)
		{
			Ach.Add(A.Id);
			Fresh.Add(A);
		}
	}
	Save();
	return Fresh;
}
