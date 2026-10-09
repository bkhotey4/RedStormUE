#include "RSSaveLoad.h"
#include "RSCommon.h"
#include "RSGame.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSMission.h"
#include "RSAI.h"
#include "RSTerrain.h"
#include "RSCameraPawn.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> StrVals(const TArray<FString>& L)
	{
		TArray<TSharedPtr<FJsonValue>> O;
		for (const FString& S : L) O.Add(MakeShared<FJsonValueString>(S));
		return O;
	}
	double N(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0)
	{
		double V = Def;
		if (O.IsValid()) O->TryGetNumberField(K, V);
		return V;
	}
	FString S(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		FString V;
		if (O.IsValid()) O->TryGetStringField(K, V);
		return V;
	}
	bool B(const TSharedPtr<FJsonObject>& O, const TCHAR* K, bool Def = false)
	{
		bool V = Def;
		if (O.IsValid()) O->TryGetBoolField(K, V);
		return V;
	}
	TSharedRef<FJsonObject> IntFloatMap(const TMap<int32, float>& M)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (const auto& P : M) O->SetNumberField(FString::FromInt(P.Key), P.Value);
		return O;
	}
}

namespace RSSave
{
	FString SlotPath(int32 Nn)
	{
		return RS::SaveDir() / TEXT("saves") / FString::Printf(TEXT("slot%d.json"), Nn);
	}

	TSharedPtr<FJsonObject> LoadData(int32 Nn)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *SlotPath(Nn))) return nullptr;
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(R, Root)) return nullptr;
		return Root;
	}

	FRSSlotInfo SlotInfo(int32 Nn)
	{
		FRSSlotInfo I;
		TSharedPtr<FJsonObject> D = LoadData(Nn);
		if (!D.IsValid()) return I;
		I.bValid = true;
		I.Title = S(D, TEXT("title"));
		I.Date = S(D, TEXT("date"));
		I.MapName = S(D, TEXT("map_name"));
		I.Time = (float)N(D, TEXT("time"));
		return I;
	}

	bool SaveGame(ARSGame* Game, int32 Nn)
	{
		if (!Game) return false;
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("version"), 1);
		D->SetStringField(TEXT("title"), FString::Printf(TEXT("%s — %s"), *Game->Settings.Map.Name, *RS::MMSS(Game->Time)));
		D->SetStringField(TEXT("date"), FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")));
		D->SetStringField(TEXT("map_name"), Game->Settings.Map.Name);
		D->SetObjectField(TEXT("settings"), Game->Settings.ToJson());
		D->SetNumberField(TEXT("time"), Game->Time);
		D->SetNumberField(TEXT("next_id"), Game->NextId);
		D->SetNumberField(TEXT("speed"), Game->Speed);
		if (Game->Cam)
		{
			const FVector F = Game->Cam->GetFocusWorld();
			D->SetNumberField(TEXT("cam_x"), F.X);
			D->SetNumberField(TEXT("cam_y"), F.Y);
			D->SetNumberField(TEXT("zoom"), Game->Cam->Zoom);
		}
		D->SetStringField(TEXT("explored"), FBase64::Encode(Game->Explored));
		{
			TArray<uint8> OreBytes;
			OreBytes.SetNumUninitialized(Game->Map->Ore.Num() * 4);
			FMemory::Memcpy(OreBytes.GetData(), Game->Map->Ore.GetData(), OreBytes.Num());
			D->SetStringField(TEXT("ore"), FBase64::Encode(OreBytes));
			D->SetStringField(TEXT("gem"), FBase64::Encode(Game->Map->Gem));
		}
		TArray<TSharedPtr<FJsonValue>> Ps;
		for (URSPlayer* P : Game->Players)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("index"), P->Index);
			O->SetNumberField(TEXT("money"), P->Money);
			TSharedRef<FJsonObject> Pw = MakeShared<FJsonObject>();
			for (const auto& K : P->Powers) Pw->SetNumberField(K.Key, K.Value);
			O->SetObjectField(TEXT("powers"), Pw);
			O->SetBoolField(TEXT("alive"), P->bAlive);
			TSharedRef<FJsonObject> St = MakeShared<FJsonObject>();
			St->SetNumberField(TEXT("built"), P->Stats.Built);
			St->SetNumberField(TEXT("killed"), P->Stats.Killed);
			St->SetNumberField(TEXT("lost"), P->Stats.Lost);
			St->SetNumberField(TEXT("bld_lost"), P->Stats.BldLost);
			St->SetNumberField(TEXT("harvested"), P->Stats.Harvested);
			O->SetObjectField(TEXT("stats"), St);
			O->SetObjectField(TEXT("relations"), IntFloatMap(P->Relations));
			TArray<FString> Al;
			for (int32 A : P->Allies) Al.Add(FString::FromInt(A));
			O->SetArrayField(TEXT("allies"), StrVals(Al));
			O->SetObjectField(TEXT("ceasefire"), IntFloatMap(P->Ceasefire));
			TSharedRef<FJsonObject> Pr = MakeShared<FJsonObject>();
			for (const auto& K : P->Proposal) Pr->SetStringField(FString::FromInt(K.Key), K.Value);
			O->SetObjectField(TEXT("proposal"), Pr);
			O->SetNumberField(TEXT("sanctioned_until"), P->SanctionedUntil);
			O->SetNumberField(TEXT("energy_cut_until"), P->EnergyCutUntil);
			O->SetNumberField(TEXT("sabotage_until"), P->SabotageUntil);
			O->SetBoolField(TEXT("vet_bonus"), P->bVetBonus);
			O->SetNumberField(TEXT("team"), P->Team);
			TSharedRef<FJsonObject> Qs = MakeShared<FJsonObject>();
			for (const auto& Q : P->Queues)
			{
				TArray<TSharedPtr<FJsonValue>> Items;
				for (const FRSQueueItem& It : Q.Value)
				{
					TSharedRef<FJsonObject> IO = MakeShared<FJsonObject>();
					IO->SetStringField(TEXT("id"), It.Id);
					IO->SetNumberField(TEXT("progress"), It.Progress);
					IO->SetNumberField(TEXT("spent"), It.Spent);
					Items.Add(MakeShared<FJsonValueObject>(IO));
				}
				Qs->SetArrayField(Q.Key, Items);
			}
			O->SetObjectField(TEXT("queues"), Qs);
			O->SetStringField(TEXT("ready_building"), P->GetReady(TEXT("building")));
			O->SetStringField(TEXT("ready_defense"), P->GetReady(TEXT("defense")));
			Ps.Add(MakeShared<FJsonValueObject>(O));
		}
		D->SetArrayField(TEXT("players"), Ps);
		TArray<TSharedPtr<FJsonValue>> Bs;
		for (ARSBuilding* Bd : Game->Buildings)
		{
			if (!Bd || Bd->bDead) continue;
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("id"), Bd->EntId);
			O->SetStringField(TEXT("t"), Bd->TypeId);
			O->SetNumberField(TEXT("p"), Bd->Player->Index);
			O->SetNumberField(TEXT("cx"), Bd->Cell.X);
			O->SetNumberField(TEXT("cy"), Bd->Cell.Y);
			O->SetNumberField(TEXT("hp"), Bd->Hp);
			O->SetNumberField(TEXT("bp"), Bd->BuildP);
			O->SetNumberField(TEXT("rx"), Bd->Rally.X);
			O->SetNumberField(TEXT("ry"), Bd->Rally.Y);
			O->SetNumberField(TEXT("vf"), Bd->VarFrame);
			O->SetNumberField(TEXT("tur"), Bd->Turret);
			O->SetNumberField(TEXT("kills"), Bd->Kills);
			O->SetNumberField(TEXT("rank"), Bd->Rank);
			O->SetBoolField(TEXT("repairing"), Bd->bRepairing);
			TArray<TSharedPtr<FJsonValue>> Occ;
			for (const FRSOccupant& Oc : Bd->Occupants)
			{
				TSharedRef<FJsonObject> OO = MakeShared<FJsonObject>();
				OO->SetStringField(TEXT("id"), Oc.Id);
				OO->SetNumberField(TEXT("hp"), Oc.Hp);
				OO->SetNumberField(TEXT("rank"), Oc.Rank);
				OO->SetNumberField(TEXT("kills"), Oc.Kills);
				Occ.Add(MakeShared<FJsonValueObject>(OO));
			}
			O->SetArrayField(TEXT("occ"), Occ);
			Bs.Add(MakeShared<FJsonValueObject>(O));
		}
		D->SetArrayField(TEXT("buildings"), Bs);
		TArray<TSharedPtr<FJsonValue>> Us;
		for (ARSUnit* U : Game->Units)
		{
			if (!U || U->bDead || U->bFromCarrier || U->bSwarm) continue;
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("id"), U->EntId);
			O->SetStringField(TEXT("t"), U->TypeId);
			O->SetNumberField(TEXT("p"), U->Player->Index);
			O->SetNumberField(TEXT("x"), U->Pos.X);
			O->SetNumberField(TEXT("y"), U->Pos.Y);
			O->SetNumberField(TEXT("hp"), U->Hp);
			O->SetNumberField(TEXT("f"), U->Facing);
			O->SetNumberField(TEXT("rank"), U->Rank);
			O->SetNumberField(TEXT("kills"), U->Kills);
			O->SetNumberField(TEXT("cargo"), U->Cargo);
			O->SetNumberField(TEXT("ammo"), U->Ammo);
			O->SetNumberField(TEXT("alt"), U->Alt);
			O->SetBoolField(TEXT("landed"), U->bLanded);
			O->SetBoolField(TEXT("harv"), U->Def->bHarvester);
			Us.Add(MakeShared<FJsonValueObject>(O));
		}
		D->SetArrayField(TEXT("units"), Us);
		if (Game->Mission)
		{
			TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetStringField(TEXT("id"), Game->Mission->Id);
			M->SetNumberField(TEXT("spy_hits"), Game->Mission->SpyHits);
			M->SetBoolField(TEXT("sanction_fired"), Game->Mission->bSanctionFired);
			M->SetNumberField(TEXT("waves"), Game->Mission->Waves);
			M->SetNumberField(TEXT("wave_t"), Game->Mission->WaveT);
			D->SetObjectField(TEXT("mission"), M);
		}
		FString Text;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(D, W);
		const FString Path = SlotPath(Nn);
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(Path));
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	static URSPlayer* PlayerByIndex(ARSGame* Game, int32 Idx)
	{
		for (URSPlayer* P : Game->Players)
			if (P->Index == Idx) return P;
		return Game->Players[0];
	}

	void Apply(ARSGame* Game, const TSharedPtr<FJsonObject>& D)
	{
		if (!Game || !D.IsValid()) return;
		// 清掉開局自動產生的所有單位與建築
		for (ARSUnit* U : TArray<ARSUnit*>(Game->Units)) Game->RemoveUnitSilent(U);
		for (ARSBuilding* Bd : TArray<ARSBuilding*>(Game->Buildings))
		{
			Bd->bDead = true;
			Game->RemoveBuildingCells(Bd);
			Bd->Destroy();
		}
		Game->Buildings.Reset();
		Game->Units.Reset();
		Game->Selection.Reset();
		for (FRSCrate& C : Game->Crates)
			if (C.Mesh) C.Mesh->DestroyComponent();
		Game->Crates.Reset();
		Game->Time = (float)N(D, TEXT("time"));
		Game->bLoading = true;
		Game->Speed = (float)N(D, TEXT("speed"), 1.0);
		// 地圖資源
		TArray<uint8> OreB;
		if (FBase64::Decode(S(D, TEXT("ore")), OreB) && OreB.Num() == Game->Map->Ore.Num() * 4)
		{
			FMemory::Memcpy(Game->Map->Ore.GetData(), OreB.GetData(), OreB.Num());
		}
		TArray<uint8> GemB;
		if (FBase64::Decode(S(D, TEXT("gem")), GemB) && GemB.Num() == Game->Map->Gem.Num()) Game->Map->Gem = GemB;
		Game->Map->OreCells.Reset();
		for (int32 I = 0; I < Game->Map->Ore.Num(); ++I)
			if (Game->Map->Ore[I] > 0) Game->Map->OreCells.Add(I);
		Game->Map->bOreDirty = true;
		// 玩家
		const TArray<TSharedPtr<FJsonValue>>* Ps = nullptr;
		if (D->TryGetArrayField(TEXT("players"), Ps))
		{
			for (int32 I = 0; I < FMath::Min(Ps->Num(), Game->Players.Num()); ++I)
			{
				URSPlayer* P = Game->Players[I];
				const TSharedPtr<FJsonObject> Q = (*Ps)[I]->AsObject();
				if (P->bNeutral) continue;
				P->Money = N(Q, TEXT("money"), P->Money);
				P->bAlive = B(Q, TEXT("alive"), true);
				P->SanctionedUntil = (float)N(Q, TEXT("sanctioned_until"), -100);
				P->EnergyCutUntil = (float)N(Q, TEXT("energy_cut_until"), -100);
				P->SabotageUntil = (float)N(Q, TEXT("sabotage_until"), -100);
				P->bVetBonus = B(Q, TEXT("vet_bonus"));
				P->Team = (int32)N(Q, TEXT("team"), P->Team);
				P->Powers.Reset();
				const TSharedPtr<FJsonObject>* Pw = nullptr;
				if (Q->TryGetObjectField(TEXT("powers"), Pw))
					for (const auto& K : (*Pw)->Values) P->Powers.Add(K.Key, (float)K.Value->AsNumber());
				const TSharedPtr<FJsonObject>* St = nullptr;
				if (Q->TryGetObjectField(TEXT("stats"), St))
				{
					P->Stats.Built = (int32)N(*St, TEXT("built"));
					P->Stats.Killed = (int32)N(*St, TEXT("killed"));
					P->Stats.Lost = (int32)N(*St, TEXT("lost"));
					P->Stats.BldLost = (int32)N(*St, TEXT("bld_lost"));
					P->Stats.Harvested = N(*St, TEXT("harvested"));
				}
				P->Relations.Reset();
				const TSharedPtr<FJsonObject>* Rel = nullptr;
				if (Q->TryGetObjectField(TEXT("relations"), Rel))
					for (const auto& K : (*Rel)->Values) P->Relations.Add(FCString::Atoi(*K.Key), (float)K.Value->AsNumber());
				P->Ceasefire.Reset();
				const TSharedPtr<FJsonObject>* Cf = nullptr;
				if (Q->TryGetObjectField(TEXT("ceasefire"), Cf))
					for (const auto& K : (*Cf)->Values) P->Ceasefire.Add(FCString::Atoi(*K.Key), (float)K.Value->AsNumber());
				P->Proposal.Reset();
				const TSharedPtr<FJsonObject>* Pr = nullptr;
				if (Q->TryGetObjectField(TEXT("proposal"), Pr))
					for (const auto& K : (*Pr)->Values) P->Proposal.Add(FCString::Atoi(*K.Key), K.Value->AsString());
				P->Allies.Reset();
				const TArray<TSharedPtr<FJsonValue>>* Al = nullptr;
				if (Q->TryGetArrayField(TEXT("allies"), Al))
					for (const TSharedPtr<FJsonValue>& V : *Al) P->Allies.Add(FCString::Atoi(*V->AsString()));
				P->InitQueues();
				const TSharedPtr<FJsonObject>* Qs = nullptr;
				if (Q->TryGetObjectField(TEXT("queues"), Qs))
				{
					for (const auto& K : (*Qs)->Values)
					{
						TArray<FRSQueueItem>& Arr = P->Queues.FindOrAdd(K.Key);
						for (const TSharedPtr<FJsonValue>& V : K.Value->AsArray())
						{
							const TSharedPtr<FJsonObject> IO = V->AsObject();
							FRSQueueItem It;
							It.Id = S(IO, TEXT("id"));
							It.Progress = (float)N(IO, TEXT("progress"));
							It.Spent = (float)N(IO, TEXT("spent"));
							Arr.Add(It);
						}
					}
				}
				P->Ready.Add(TEXT("building"), S(Q, TEXT("ready_building")));
				P->Ready.Add(TEXT("defense"), S(Q, TEXT("ready_defense")));
				P->bPowerDirty = true;
			}
		}
		// 建築
		const TArray<TSharedPtr<FJsonValue>>* Bs = nullptr;
		if (D->TryGetArrayField(TEXT("buildings"), Bs))
		{
			for (const TSharedPtr<FJsonValue>& V : *Bs)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				URSPlayer* P = PlayerByIndex(Game, (int32)N(O, TEXT("p")));
				const FString T = S(O, TEXT("t"));
				if (!FRSDefs::Get().Buildings.Contains(T)) continue;
				ARSBuilding* Bd = Game->PlaceBuilding(P, T, FIntPoint((int32)N(O, TEXT("cx")), (int32)N(O, TEXT("cy"))), true);
				if (!Bd) continue;
				Bd->EntId = (int32)N(O, TEXT("id"));
				Bd->Hp = FMath::Min((float)N(O, TEXT("hp"), Bd->MaxHp), Bd->MaxHp);
				Bd->BuildP = (float)N(O, TEXT("bp"), 1.0);
				Bd->Rally = FVector2D(N(O, TEXT("rx"), -1), N(O, TEXT("ry"), -1));
				Bd->VarFrame = (int32)N(O, TEXT("vf"));
				Bd->Turret = (float)N(O, TEXT("tur"));
				Bd->Kills = (int32)N(O, TEXT("kills"));
				Bd->Rank = (int32)N(O, TEXT("rank"));
				Bd->bRepairing = B(O, TEXT("repairing"));
				const TArray<TSharedPtr<FJsonValue>>* Occ = nullptr;
				if (O->TryGetArrayField(TEXT("occ"), Occ))
				{
					for (const TSharedPtr<FJsonValue>& OV : *Occ)
					{
						const TSharedPtr<FJsonObject> OO = OV->AsObject();
						FRSOccupant Oc;
						Oc.Id = S(OO, TEXT("id"));
						Oc.Hp = (float)N(OO, TEXT("hp"), 100);
						Oc.Rank = (int32)N(OO, TEXT("rank"));
						Oc.Kills = (int32)N(OO, TEXT("kills"));
						const FRSDef& UD = FRSDefs::Get().GetDef(Oc.Id);
						Oc.W = UD.Weapon.IsEmpty() ? nullptr : &FRSDefs::Get().GetWeapon(UD.Weapon);
						Bd->Occupants.Add(Oc);
					}
				}
			}
		}
		// 單位
		const TArray<TSharedPtr<FJsonValue>>* Us = nullptr;
		if (D->TryGetArrayField(TEXT("units"), Us))
		{
			for (const TSharedPtr<FJsonValue>& V : *Us)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				URSPlayer* P = PlayerByIndex(Game, (int32)N(O, TEXT("p")));
				const FString T = S(O, TEXT("t"));
				if (!FRSDefs::Get().Units.Contains(T)) continue;
				ARSUnit* U = Game->SpawnUnit(P, T, FVector2D(N(O, TEXT("x")), N(O, TEXT("y"))));
				if (!U) continue;
				U->EntId = (int32)N(O, TEXT("id"));
				U->Hp = FMath::Min((float)N(O, TEXT("hp"), U->MaxHp), U->MaxHp);
				U->Facing = (float)N(O, TEXT("f"));
				U->Turret = U->Facing;
				U->Rank = (int32)N(O, TEXT("rank"));
				U->Kills = (int32)N(O, TEXT("kills"));
				U->Cargo = (float)N(O, TEXT("cargo"));
				U->Ammo = (int32)N(O, TEXT("ammo"), U->Ammo);
				if (U->IsAir())
				{
					U->Alt = (float)N(O, TEXT("alt"), U->Alt);
					U->bLanded = B(O, TEXT("landed"));
				}
				if (B(O, TEXT("harv"))) U->CmdHarvest();
			}
		}
		Game->bLoading = false;
		Game->NextId = (int32)N(D, TEXT("next_id"), Game->NextId);
		// 探索範圍
		TArray<uint8> Ex;
		if (FBase64::Decode(S(D, TEXT("explored")), Ex) && Ex.Num() == Game->Explored.Num())
		{
			Game->Explored = Ex;
			if (Game->Terrain) Game->Terrain->ResetFog(Game->Explored);
		}
		// 任務
		const TSharedPtr<FJsonObject>* M = nullptr;
		if (Game->Mission && D->TryGetObjectField(TEXT("mission"), M))
		{
			Game->Mission->SpyHits = (int32)N(*M, TEXT("spy_hits"));
			Game->Mission->bSanctionFired = B(*M, TEXT("sanction_fired"));
			Game->Mission->Waves = (int32)N(*M, TEXT("waves"));
			Game->Mission->WaveT = (float)N(*M, TEXT("wave_t"));
		}
		for (URSAI* AI : Game->AIs)
		{
			AI->WaterOk = -1;
			AI->AttackGroup.Reset();
			AI->NextAttack = FMath::Max(AI->NextAttack, Game->Time + 90.f);
		}
		if (Game->Cam)
		{
			Game->Cam->SetFocusWorld(FVector(N(D, TEXT("cam_x")), N(D, TEXT("cam_y")), 0));
			Game->Cam->SetZoom((float)N(D, TEXT("zoom"), 1.1));
		}
		Game->Message(TEXT("存檔已讀取"));
	}
}
