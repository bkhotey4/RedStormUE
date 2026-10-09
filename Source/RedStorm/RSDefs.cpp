#include "RSDefs.h"
#include "RSCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogRS);

namespace
{
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0)
	{
		double V = Def;
		if (O.IsValid() && O->TryGetNumberField(K, V)) return V;
		return Def;
	}
	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K, const FString& Def = FString())
	{
		FString V;
		if (O.IsValid() && O->TryGetStringField(K, V)) return V;
		return Def;
	}
	bool Bool(const TSharedPtr<FJsonObject>& O, const TCHAR* K, bool Def = false)
	{
		bool V = Def;
		if (O.IsValid() && O->TryGetBoolField(K, V)) return V;
		return Def;
	}
	FLinearColor Col(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid() || V->Type != EJson::Array) return FLinearColor::White;
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		FLinearColor C(1, 1, 1, 1);
		if (A.Num() > 0) C.R = (float)A[0]->AsNumber();
		if (A.Num() > 1) C.G = (float)A[1]->AsNumber();
		if (A.Num() > 2) C.B = (float)A[2]->AsNumber();
		if (A.Num() > 3) C.A = (float)A[3]->AsNumber();
		return C;
	}
	TMap<FString, float> FloatMap(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		TMap<FString, float> Out;
		const TSharedPtr<FJsonObject>* Sub = nullptr;
		if (O.IsValid() && O->TryGetObjectField(K, Sub) && Sub && Sub->IsValid())
		{
			for (const auto& Pair : (*Sub)->Values)
			{
				Out.Add(Pair.Key, (float)Pair.Value->AsNumber());
			}
		}
		return Out;
	}
	TArray<FString> StrArray(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (O.IsValid() && O->TryGetArrayField(K, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) Out.Add(V->AsString());
		}
		return Out;
	}
	FRSMapPreset ParsePreset(const TSharedPtr<FJsonObject>& O)
	{
		FRSMapPreset P;
		P.Name = Str(O, TEXT("name"));
		P.Theme = Str(O, TEXT("theme"), TEXT("temperate"));
		P.Layout = Str(O, TEXT("layout"), TEXT("plains"));
		P.Desc = Str(O, TEXT("desc"));
		P.Size = (int32)Num(O, TEXT("size"), 72);
		return P;
	}
}

FRSDefs& FRSDefs::Get()
{
	static FRSDefs Instance;
	if (!Instance.bLoaded) Instance.Load();
	return Instance;
}

TSharedPtr<FJsonObject> FRSDefs::LoadJson(const FString& File)
{
	const FString Path = RS::ContentDir() / TEXT("Data") / File;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogRS, Error, TEXT("找不到資料檔 %s"), *Path);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogRS, Error, TEXT("資料檔格式錯誤 %s"), *Path);
		return nullptr;
	}
	return Root;
}

void FRSDefs::ParseDef(const FString& Id, const TSharedPtr<FJsonObject>& O, bool bUnit, FRSDef& D)
{
	D.Id = Id;
	D.bIsUnit = bUnit;
	D.Name = Str(O, TEXT("name"), Id);
	D.Faction = Str(O, TEXT("faction"));
	D.Country = Str(O, TEXT("country"));
	D.Cat = Str(O, TEXT("cat"));
	D.Kind = Str(O, TEXT("kind"));
	D.Role = Str(O, TEXT("role"));
	D.Desc = Str(O, TEXT("desc"));
	D.Armor = Str(O, TEXT("armor"), bUnit ? TEXT("heavy") : TEXT("bld"));
	D.Weapon = Str(O, TEXT("weapon"));
	D.Weapon2 = Str(O, TEXT("weapon2"));
	D.Mcv = Str(O, TEXT("mcv"));
	D.Carrier = Str(O, TEXT("carrier"));
	D.Super = Str(O, TEXT("super"));
	D.PowerSw = Str(O, TEXT("power_sw"));
	D.Cost = (float)Num(O, TEXT("cost"), 0);
	D.Hp = (float)Num(O, TEXT("hp"), 100);
	D.Speed = (float)Num(O, TEXT("speed"), 1);
	D.Sight = (float)Num(O, TEXT("sight"), 5);
	D.Ew = (float)Num(O, TEXT("ew"), 0);
	D.Power = (float)Num(O, TEXT("power"), 0);
	D.Income = (float)Num(O, TEXT("income"), 0);
	const TArray<TSharedPtr<FJsonValue>>* SizeArr = nullptr;
	if (O->TryGetArrayField(TEXT("size"), SizeArr) && SizeArr && SizeArr->Num() >= 2)
	{
		D.Size = FIntPoint((int32)(*SizeArr)[0]->AsNumber(), (int32)(*SizeArr)[1]->AsNumber());
	}
	D.Req = StrArray(O, TEXT("req"));
	D.Limit = (int32)Num(O, TEXT("limit"), 0);
	D.Hangar = (int32)Num(O, TEXT("hangar"), 3);
	D.Garrison = (int32)Num(O, TEXT("garrison"), 0);
	D.bBuildable = Bool(O, TEXT("buildable"), true);
	D.bTurret = Bool(O, TEXT("turret"));
	D.bEngineer = Bool(O, TEXT("engineer"));
	D.bHarvester = Bool(O, TEXT("harvester"));
	D.bJet = Bool(O, TEXT("jet"));
	D.bDrone = Bool(O, TEXT("drone"));
	D.bKamikaze = Bool(O, TEXT("kamikaze"));
	D.bLoiter = Bool(O, TEXT("loiter"));
	D.bHeli = Bool(O, TEXT("heli"));
	D.bAirship = Bool(O, TEXT("airship"));
	D.bPreferBld = Bool(O, TEXT("prefer_bld"));
	D.bHeal = Bool(O, TEXT("heal"));
	D.bC4 = Bool(O, TEXT("c4"));
	D.bSpy = Bool(O, TEXT("spy"));
	D.bSub = Bool(O, TEXT("sub"));
	D.bNaval = Bool(O, TEXT("naval"));
	D.bWall = Bool(O, TEXT("wall"));
	D.bCiv = Bool(O, TEXT("civ"));
	D.bExplode = Bool(O, TEXT("explode"));
	D.bAirfield = Bool(O, TEXT("airfield"));
	D.bRunway = Bool(O, TEXT("runway"));
}

bool FRSDefs::Load()
{
	bLoaded = true;
	for (int32 I = 0; I < 6; ++I) DefaultTheme.Mini[I] = FLinearColor(0.3f, 0.4f, 0.2f);
	DefaultTheme.Id = TEXT("temperate");

	TSharedPtr<FJsonObject> D = LoadJson(TEXT("defs.json"));
	if (!D.IsValid()) return false;

	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (D->TryGetObjectField(TEXT("FACTIONS"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			FRSFaction F;
			F.Id = P.Key;
			F.Name = Str(O, TEXT("name"));
			F.Infantry = Str(O, TEXT("infantry"));
			F.Color = Col(O->TryGetField(TEXT("color")));
			Factions.Add(P.Key, F);
		}
	}
	if (D->TryGetObjectField(TEXT("WARHEADS"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			TMap<FString, float> M;
			for (const auto& Q : P.Value->AsObject()->Values) M.Add(Q.Key, (float)Q.Value->AsNumber());
			Warheads.Add(P.Key, M);
		}
	}
	if (D->TryGetObjectField(TEXT("WEAPONS"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			FRSWeapon W;
			W.Id = P.Key;
			W.Dmg = (float)Num(O, TEXT("dmg"));
			W.Rof = (float)Num(O, TEXT("rof"), 1);
			W.Range = (float)Num(O, TEXT("range"));
			W.MinRange = (float)Num(O, TEXT("min_range"));
			W.Splash = (float)Num(O, TEXT("splash"));
			W.Proj = Str(O, TEXT("proj"));
			W.Wh = Str(O, TEXT("wh"), TEXT("ap"));
			W.Snd = Str(O, TEXT("snd"));
			W.bAir = Bool(O, TEXT("air"));
			W.bAirOnly = Bool(O, TEXT("air_only"));
			W.bPower = Bool(O, TEXT("power"));
			W.Burst = (int32)Num(O, TEXT("burst"));
			W.Ammo = (int32)Num(O, TEXT("ammo"));
			Weapons.Add(P.Key, W);
		}
	}
	if (D->TryGetObjectField(TEXT("UNITS"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			FRSDef Def;
			ParseDef(P.Key, P.Value->AsObject(), true, Def);
			Units.Add(P.Key, Def);
		}
	}
	if (D->TryGetObjectField(TEXT("BUILDINGS"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			FRSDef Def;
			ParseDef(P.Key, P.Value->AsObject(), false, Def);
			Buildings.Add(P.Key, Def);
		}
	}
	if (D->TryGetObjectField(TEXT("BUILD_ORDER"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			TArray<FString> L;
			for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray()) L.Add(V->AsString());
			BuildOrder.Add(P.Key, L);
		}
	}
	if (D->TryGetObjectField(TEXT("SUPER"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			FRSSuper S;
			S.Id = P.Key;
			S.Name = Str(O, TEXT("name"));
			S.Charge = (float)Num(O, TEXT("charge"), 100);
			S.Target = Str(O, TEXT("target"), TEXT("area"));
			S.Radius = (float)Num(O, TEXT("radius"));
			S.Desc = Str(O, TEXT("desc"));
			Supers.Add(P.Key, S);
		}
	}
	PowerOrder = StrArray(D, TEXT("POWER_ORDER"));
	if (D->TryGetObjectField(TEXT("COUNTRIES"), Sub))
	{
		for (const auto& P : (*Sub)->Values)
		{
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			FRSCountry C;
			C.Id = P.Key;
			C.Name = Str(O, TEXT("name"));
			C.Faction = Str(O, TEXT("faction"));
			C.Power = Str(O, TEXT("power"));
			C.Desc = Str(O, TEXT("desc"));
			const TArray<TSharedPtr<FJsonValue>>* Fl = nullptr;
			if (O->TryGetArrayField(TEXT("flag"), Fl) && Fl)
			{
				for (int32 K = 0; K < 3 && K < Fl->Num(); ++K) C.Flag[K] = Col((*Fl)[K]);
			}
			C.Cost = FloatMap(O, TEXT("cost"));
			C.Time = FloatMap(O, TEXT("time"));
			C.Hp = FloatMap(O, TEXT("hp"));
			C.Speed = FloatMap(O, TEXT("speed"));
			C.Charge = FloatMap(O, TEXT("charge"));
			C.DmgVsShip = (float)Num(O, TEXT("dmg_vs_ship"), 1.0);
			Countries.Add(P.Key, C);
		}
	}
	CountryOrder = StrArray(D, TEXT("COUNTRY_ORDER"));

	// 地圖
	if (TSharedPtr<FJsonObject> M = LoadJson(TEXT("maps.json")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (M->TryGetArrayField(TEXT("PRESETS"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) Presets.Add(ParsePreset(V->AsObject()));
		}
		if (M->TryGetObjectField(TEXT("THEMES"), Sub))
		{
			for (const auto& P : (*Sub)->Values)
			{
				const TSharedPtr<FJsonObject> O = P.Value->AsObject();
				FRSTheme T;
				T.Id = P.Key;
				const TArray<TSharedPtr<FJsonValue>>* Mini = nullptr;
				if (O->TryGetArrayField(TEXT("mini"), Mini))
				{
					for (int32 K = 0; K < 6 && K < Mini->Num(); ++K) T.Mini[K] = Col((*Mini)[K]);
				}
				T.Clear = Col(O->TryGetField(TEXT("clear")));
				T.TreeDensity = (float)Num(O, TEXT("tree_density"), 0.5);
				T.Beach = (int32)Num(O, TEXT("beach"), 1);
				const TArray<TSharedPtr<FJsonValue>>* Trees = nullptr;
				if (O->TryGetArrayField(TEXT("trees"), Trees))
				{
					for (const TSharedPtr<FJsonValue>& TV : *Trees)
					{
						const TArray<TSharedPtr<FJsonValue>>& TA = TV->AsArray();
						if (TA.Num() > 0)
						{
							const FString K = TA[0]->AsString();
							if (K == TEXT("palm")) T.bPalms = true;
							if (K == TEXT("snowpine")) T.bSnowTrees = true;
						}
					}
				}
				Themes.Add(P.Key, T);
			}
		}
	}
	// 任務
	if (TSharedPtr<FJsonObject> M = LoadJson(TEXT("missions.json")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (M->TryGetArrayField(TEXT("LIST"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FRSMissionDef MD;
				MD.Id = Str(O, TEXT("id"));
				MD.Name = Str(O, TEXT("name"));
				MD.Camp = Str(O, TEXT("camp"), TEXT("basic"));
				MD.Faction = Str(O, TEXT("faction"), TEXT("allies"));
				MD.Enemy = Str(O, TEXT("enemy"), TEXT("soviet"));
				MD.Country = Str(O, TEXT("country"));
				MD.EnemyCountry = Str(O, TEXT("enemy_country"));
				MD.Headline = Str(O, TEXT("headline"));
				MD.Brief = Str(O, TEXT("brief"));
				const TSharedPtr<FJsonObject>* MapO = nullptr;
				if (O->TryGetObjectField(TEXT("map"), MapO)) MD.Map = ParsePreset(*MapO);
				MD.Seed = (int32)Num(O, TEXT("seed"), 1);
				MD.Money = (int32)Num(O, TEXT("money"), 10000);
				MD.Enemies = (int32)Num(O, TEXT("enemies"), 1);
				MD.bFfa = Bool(O, TEXT("ffa"));
				Missions.Add(MD);
			}
		}
	}
	// 時事事件
	if (TSharedPtr<FJsonObject> M = LoadJson(TEXT("events.json")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (M->TryGetArrayField(TEXT("EVENTS"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FRSEventDef E;
				E.Id = Str(O, TEXT("id"));
				E.Name = Str(O, TEXT("name"));
				E.News = Str(O, TEXT("news"));
				E.Dur = (float)Num(O, TEXT("dur"));
				E.bBad = Bool(O, TEXT("bad"));
				E.bGlobal = Bool(O, TEXT("global"));
				Events.Add(E);
			}
		}
	}
	// 成就
	if (TSharedPtr<FJsonObject> M = LoadJson(TEXT("achievements.json")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (M->TryGetArrayField(TEXT("ACH"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FRSAchDef A;
				A.Id = Str(O, TEXT("id"));
				A.Name = Str(O, TEXT("name"));
				A.Desc = Str(O, TEXT("desc"));
				A.Icon = Str(O, TEXT("icon"));
				Achievements.Add(A);
			}
		}
	}
	// 片頭字幕
	if (TSharedPtr<FJsonObject> M = LoadJson(TEXT("intro.json")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (M->TryGetArrayField(TEXT("LINES"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TArray<TSharedPtr<FJsonValue>>& L = V->AsArray();
				if (L.Num() >= 3)
				{
					FRSIntroLine IL;
					IL.T = (float)L[0]->AsNumber();
					IL.En = L[1]->AsString();
					IL.Zh = L[2]->AsString();
					IntroLines.Add(IL);
				}
			}
		}
	}
	UE_LOG(LogRS, Log, TEXT("Defs loaded: %d units, %d buildings, %d weapons, %d countries, %d missions"),
		Units.Num(), Buildings.Num(), Weapons.Num(), Countries.Num(), Missions.Num());
	return true;
}

const FRSDef& FRSDefs::GetDef(const FString& Id) const
{
	if (const FRSDef* U = Units.Find(Id)) return *U;
	if (const FRSDef* B = Buildings.Find(Id)) return *B;
	return EmptyDef;
}

const FRSWeapon& FRSDefs::GetWeapon(const FString& Id) const
{
	if (const FRSWeapon* W = Weapons.Find(Id)) return *W;
	return EmptyWeapon;
}

const FRSSuper& FRSDefs::GetSuper(const FString& Id) const
{
	if (const FRSSuper* S = Supers.Find(Id)) return *S;
	return EmptySuper;
}

const FRSMissionDef* FRSDefs::FindMission(const FString& Id) const
{
	for (const FRSMissionDef& M : Missions)
	{
		if (M.Id == Id) return &M;
	}
	return nullptr;
}

const FRSTheme& FRSDefs::GetTheme(const FString& Id) const
{
	if (const FRSTheme* T = Themes.Find(Id)) return *T;
	return DefaultTheme;
}

float FRSDefs::WarheadMult(const FString& Wh, const FString& Armor) const
{
	const TMap<FString, float>* M = Warheads.Find(Wh);
	if (!M) M = Warheads.Find(TEXT("ap"));
	if (!M) return 1.f;
	const float* V = M->Find(Armor);
	return V ? *V : 1.f;
}

float FRSDefs::CountryMult(const FString& Country, const FString& Table, const FString& Id) const
{
	const FRSCountry* C = Countries.Find(Country);
	if (!C) return 1.f;
	const TMap<FString, float>* T = nullptr;
	if (Table == TEXT("cost")) T = &C->Cost;
	else if (Table == TEXT("time")) T = &C->Time;
	else if (Table == TEXT("hp")) T = &C->Hp;
	else if (Table == TEXT("speed")) T = &C->Speed;
	if (!T || T->Num() == 0) return 1.f;
	const FRSDef& D = GetDef(Id);
	float M = 1.f;
	const FString Keys[3] = { TEXT("id:") + Id, TEXT("kind:") + D.Kind, TEXT("cat:") + D.Cat };
	for (const FString& K : Keys)
	{
		if (const float* V = T->Find(K)) M *= *V;
	}
	return M;
}

float FRSDefs::BuildTime(const FString& Id) const
{
	const FRSDef& D = GetDef(Id);
	return FMath::Max(3.f, (D.IsValid() ? D.Cost : 1000.f) / 1000.f * 11.f);
}
