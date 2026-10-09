#include "RSGame.h"
#include "RSCommon.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "RSAI.h"
#include "RSMission.h"
#include "RSWorldEvents.h"
#include "RSTerrain.h"
#include "RSFx.h"
#include "RSAudio.h"
#include "RSCameraPawn.h"
#include "RSVisuals.h"
#include "RSSaveLoad.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"

ARSGame::ARSGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	USceneComponent* R = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = R;
}

void ARSGame::EndPlay(const EEndPlayReason::Type Reason)
{
	for (ARSUnit* U : Units)
		if (IsValid(U)) U->Destroy();
	for (ARSBuilding* B : Buildings)
		if (IsValid(B)) B->Destroy();
	for (FRSWreck& Wk : Wrecks)
		if (ARSUnit* WU = Wk.U.Get()) WU->Destroy();
	Wrecks.Reset();
	for (AActor* A : EnvActors)
		if (IsValid(A)) A->Destroy();
	if (IsValid(Terrain)) Terrain->Destroy();
	if (IsValid(Fx)) Fx->Destroy();
	Super::EndPlay(Reason);
}

// ───────────────────────── 開局 ─────────────────────────
void ARSGame::SetupEnvironment()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 太陽
	ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-48.f, 35.f, 0.f)));
	if (Sun)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
		if (L)
		{
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensity(Settings.Map.Theme == TEXT("snow") ? 7.f : 9.f);
			L->SetLightColor(Settings.Map.Theme == TEXT("desert") ? FLinearColor(1.f, 0.92f, 0.8f) : FLinearColor(1.f, 0.97f, 0.92f));
			L->SetAtmosphereSunLight(true);
			L->SetDynamicShadowDistanceMovableLight(20000.f);
			SunL = L;
			SunBaseI = Settings.Map.Theme == TEXT("snow") ? 7.f : 9.f;
		}
		UGameplayStatics::FinishSpawningActor(Sun, FTransform(FRotator(-48.f, 35.f, 0.f)));
		EnvActors.Add(Sun);
	}
	// 天空大氣
	AActor* Sky = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SP);
	if (Sky)
	{
		USceneComponent* SR = NewObject<USceneComponent>(Sky);
		Sky->SetRootComponent(SR);
		SR->RegisterComponent();
		USkyAtmosphereComponent* Atm = NewObject<USkyAtmosphereComponent>(Sky);
		Atm->SetupAttachment(SR);
		Atm->RegisterComponent();
		// 降雨 / 降雪粒子（鏡頭附近的細長方塊）
		PrecipISM = NewObject<UInstancedStaticMeshComponent>(Sky);
		PrecipISM->SetupAttachment(SR);
		PrecipISM->SetStaticMesh(RSV::Cube());
		PrecipISM->SetMaterial(0, RSV::Mat(FLinearColor(0.75f, 0.82f, 0.9f)));
		PrecipISM->SetCastShadow(false);
		PrecipISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PrecipISM->SetMobility(EComponentMobility::Movable);
		PrecipISM->RegisterComponent();
		EnvActors.Add(Sky);
	}
	// 天光
	ASkyLight* SkyL = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
	if (SkyL)
	{
		USkyLightComponent* C = SkyL->GetLightComponent();
		if (C)
		{
			C->SetMobility(EComponentMobility::Movable);
			C->bRealTimeCapture = true;
			C->SetIntensity(1.f);
			SkyLC = C;
		}
		UGameplayStatics::FinishSpawningActor(SkyL, FTransform::Identity);
		if (C) C->RecaptureSky();
		EnvActors.Add(SkyL);
	}
	// 高度霧
	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform(FVector(0, 0, -200)), SP);
	if (Fog)
	{
		if (UExponentialHeightFogComponent* F = Fog->GetComponent())
		{
			FogBase = Settings.Map.Theme == TEXT("desert") ? 0.012f : 0.008f;
			F->SetFogDensity(FogBase);
			FogC = F;
			F->SetFogHeightFalloff(0.05f);
			F->SetFogInscatteringColor(Settings.Map.Theme == TEXT("snow") ? FLinearColor(0.6f, 0.68f, 0.8f) : FLinearColor(0.45f, 0.5f, 0.6f));
		}
		EnvActors.Add(Fog);
	}
}

void ARSGame::Start(const FRSGameSettings& S, URSAudio* InAudio, ARSCameraPawn* InCam)
{
	Settings = S;
	AudioRef = InAudio;
	if (AudioRef) AudioRef->Game = this;
	Cam = InCam;
	Speed = S.Speed;
	RSV::SetTheme(S.Map.Theme);
	Map = MakeUnique<FRSMap>();
	Map->Game = this;
	Map->Generate(S.Map, 1 + S.Enemies, S.Seed);
	W = Map->W;
	H = Map->H;
	SetupEnvironment();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Terrain = GetWorld()->SpawnActor<ARSTerrain>(ARSTerrain::StaticClass(), FTransform::Identity, SP);
	Fx = GetWorld()->SpawnActor<ARSFx>(ARSFx::StaticClass(), FTransform::Identity, SP);
	Fx->Setup(this);
	// 尋路
	Astar.Init(W, H);
	AstarW.Init(W, H);
	for (int32 Y = 0; Y < H; ++Y)
		for (int32 X = 0; X < W; ++X)
		{
			AstarW.SetSolid(X, Y, Map->Terrain[Map->Idx(X, Y)] != FRSMap::T_WATER);
			Astar.SetSolid(X, Y, !Map->Passable(X, Y));
		}
	BGrid.Init(nullptr, W * H);
	Explored.Init(0, W * H);
	Terrain->Build(this);
	if (Cam)
	{
		Cam->BoundsMin = FVector2D(-2.f * RS_CELL, -2.f * RS_CELL);
		Cam->BoundsMax = FVector2D((W + 2) * RS_CELL, (H + 2) * RS_CELL);
		Cam->SetZoom(1.1f);
	}
	SetupPlayers(S);
	Settings.FixedCountries.Reset();
	for (int32 K = 0; K <= S.Enemies && K < Players.Num(); ++K) Settings.FixedCountries.Add(Players[K]->Country);
	for (const FIntPoint& O : Map->Oils) PlaceBuilding(Neutral, TEXT("oil"), O, true);
	for (const FRSCivSpot& C : Map->Civs)
	{
		ARSBuilding* B = PlaceBuilding(Neutral, C.Id, C.TL, true);
		if (B) B->VarFrame = C.Var;
	}
	if (!S.Mission.IsEmpty())
	{
		Mission = NewObject<URSMission>(this);
		Mission->Setup(this, S.Mission);
	}
	else
	{
		for (URSPlayer* P : Players)
			if (!P->bNeutral) SpawnStart(P);
	}
	WorldEv = NewObject<URSWorldEvents>(this);
	WorldEv->Setup(this);
	CenterOn(RS::CellCenter(Human->StartCell));
	RevealAllNow();
	if (AudioRef)
	{
		AudioRef->Say(TEXT("construction_options"), 0.f);
		AudioRef->Music(TEXT("battle"));
	}
	if (!S.bFromSave && S.Mission.IsEmpty()) Message(TEXT("指揮官，展開基地車 (選取後按 D 或雙擊) 開始建設基地！"));
}

void ARSGame::SetupPlayers(const FRSGameSettings& S)
{
	const FRSDefs& D = FRSDefs::Get();
	TArray<FLinearColor> Colors = { FLinearColor(0.92f, 0.16f, 0.12f), FLinearColor(0.22f, 0.52f, 1.f), FLinearColor(0.95f, 0.8f, 0.12f),
		FLinearColor(0.2f, 0.8f, 0.3f), FLinearColor(1.f, 0.5f, 0.1f), FLinearColor(0.7f, 0.3f, 0.9f) };
	FString HF = S.Faction.IsEmpty() ? TEXT("allies") : S.Faction;
	FString HC = S.FixedCountries.IsValidIndex(0) && !S.FixedCountries[0].IsEmpty() ? S.FixedCountries[0] : S.Country;
	if (HC == TEXT("random"))
	{
		TArray<FString> Cands;
		for (const FString& C : D.CountryOrder)
			if (D.Countries[C].Faction == HF) Cands.Add(C);
		HC = Cands.Num() > 0 ? Cands[RS::RandI(0, Cands.Num() - 1)] : FString();
	}
	if (const FRSCountry* C = D.GetCountry(HC)) HF = C->Faction;
	auto FacColor = [&D](const FString& F) { const FRSFaction* Fa = D.Factions.Find(F); return Fa ? Fa->Color : FLinearColor::White; };
	Human = NewObject<URSPlayer>(this);
	Human->InitQueues();
	Human->Country = HC;
	Human->Index = 0;
	Human->Game = this;
	Human->Name = TEXT("指揮官");
	Human->Faction = HF;
	Human->Color = FacColor(HF);
	Human->Team = 0;
	Human->Money = S.Money;
	Human->StartCell = Map->Starts[0];
	Players.Add(Human);
	TArray<FLinearColor> Used = { Human->Color };
	const int32 N = S.Enemies;
	const int32 Diff = S.Difficulty;
	for (int32 K = 0; K < N; ++K)
	{
		URSPlayer* P = NewObject<URSPlayer>(this);
		P->InitQueues();
		P->Index = K + 1;
		P->Game = this;
		FString EF = S.EnemyFaction;
		if (EF == TEXT("random") || EF.IsEmpty()) EF = RS::RandI(0, 1) == 0 ? TEXT("allies") : TEXT("soviet");
		FString EC = S.FixedCountries.IsValidIndex(K + 1) && !S.FixedCountries[K + 1].IsEmpty() ? S.FixedCountries[K + 1] : S.EnemyCountry;
		if (EC == TEXT("random"))
		{
			TArray<FString> Cands;
			for (const FString& C : D.CountryOrder)
			{
				if ((S.EnemyFaction == TEXT("random") || S.EnemyFaction.IsEmpty() || D.Countries[C].Faction == EF) && C != HC) Cands.Add(C);
			}
			for (URSPlayer* Q : Players) Cands.Remove(Q->Country);
			EC = Cands.Num() > 0 ? Cands[RS::RandI(0, Cands.Num() - 1)] : FString();
		}
		if (const FRSCountry* C = D.GetCountry(EC)) EF = C->Faction;
		P->Country = EC;
		P->Faction = EF;
		FLinearColor Col = FacColor(EF);
		if (Used.Contains(Col))
		{
			for (const FLinearColor& CC : Colors)
			{
				if (!Used.Contains(CC))
				{
					Col = CC;
					break;
				}
			}
		}
		Used.Add(Col);
		P->Color = Col;
		P->Team = S.bAiAllied ? 1 : K + 1;
		P->bAI = true;
		const FRSCountry* Cn = D.GetCountry(EC);
		P->Name = Cn ? FString::Printf(TEXT("%s 電腦 %d"), *Cn->Name, K + 1) : FString::Printf(TEXT("%s電腦 %d"), *D.FactionName(EF), K + 1);
		P->Money = S.Money;
		P->StartCell = Map->Starts.IsValidIndex(K + 1) ? Map->Starts[K + 1] : Map->Starts.Last();
		if (Diff == 0) { P->IncomeMult = 0.75f; P->BuildMult = 0.75f; }
		else if (Diff == 2) { P->IncomeMult = 1.35f; P->BuildMult = 1.3f; }
		Players.Add(P);
		URSAI* AI = NewObject<URSAI>(this);
		AI->Setup(this, P, Diff);
		AIs.Add(AI);
	}
	Neutral = NewObject<URSPlayer>(this);
	Neutral->InitQueues();
	Neutral->Index = 99;
	Neutral->Game = this;
	Neutral->bNeutral = true;
	Neutral->Name = TEXT("中立");
	Neutral->Color = FLinearColor(0.6f, 0.6f, 0.6f);
	Neutral->Team = -1;
	Neutral->Faction = TEXT("neutral");
	Players.Add(Neutral);
}

void ARSGame::SpawnStart(URSPlayer* P)
{
	const FVector2D C = RS::CellCenter(P->StartCell);
	const bool bA = P->Faction == TEXT("allies");
	if (ARSUnit* Mcv = SpawnUnit(P, bA ? TEXT("a_mcv") : TEXT("s_mcv"), C)) Mcv->Facing = PI * 1.25f;
	const FString Inf = bA ? TEXT("a_gi") : TEXT("s_con");
	const FString Tank = bA ? TEXT("a_tank") : TEXT("s_tank");
	const FVector2D TOC = (FVector2D(W, H) * 0.5f - C).GetSafeNormal();
	for (int32 K = 0; K < 5; ++K)
	{
		const FVector2D Off = RS::Rot(TOC, RS::Rand(-1.2f, 1.2f)) * RS::Rand(2.f, 3.5f);
		SpawnUnit(P, Inf, C + Off);
	}
	for (int32 K = 0; K < 2; ++K)
	{
		if (ARSUnit* U = SpawnUnit(P, Tank, C + TOC * 3.2f + RS::Ortho(TOC) * (K * 1.4f - 0.7f)))
		{
			U->Facing = RS::AngleOf(TOC);
			U->Turret = U->Facing;
		}
	}
}

// ───────────────────────── 生成 / 建築 ─────────────────────────
ARSUnit* ARSGame::SpawnUnit(URSPlayer* P, const FString& Id, const FVector2D& At)
{
	if (!FRSDefs::Get().Units.Contains(Id) || !P) return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARSUnit* U = GetWorld()->SpawnActor<ARSUnit>(ARSUnit::StaticClass(), FTransform(ToWorld(At)), SP);
	if (!U) return nullptr;
	U->EntId = NextId++;
	U->Setup(this, P, Id, At);
	if (!P->Country.IsEmpty())
	{
		const FRSDefs& D = FRSDefs::Get();
		U->MaxHp *= D.CountryMult(P->Country, TEXT("hp"), Id);
		U->Hp = U->MaxHp;
		U->Speed *= D.CountryMult(P->Country, TEXT("speed"), Id);
	}
	Units.Add(U);
	const bool bOwn = P == Human || (Human && Human->Allies.Contains(P->Index));
	U->SetShown(bOwn || IsExplored(int32(At.X), int32(At.Y)));
	return U;
}

ARSBuilding* ARSGame::BldAt(int32 X, int32 Y) const
{
	if (X < 0 || Y < 0 || X >= W || Y >= H || BGrid.Num() == 0) return nullptr;
	return BGrid[Y * W + X];
}

bool ARSGame::CellBuildable(const FString& Id, int32 X, int32 Y) const
{
	if (!Map->Inb(X, Y) || BldAt(X, Y) != nullptr) return false;
	if (FRSDefs::Get().GetDef(Id).bNaval) return Map->Terrain[Map->Idx(X, Y)] == FRSMap::T_WATER;
	return Map->Buildable(X, Y);
}

bool ARSGame::CanPlace(const FString& Id, FIntPoint TL, URSPlayer* P, const ARSUnit* Ignore, bool bNeedNear) const
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	if (!Def.IsValid()) return false;
	const FIntPoint Sz = Def.Size;
	for (int32 Y = TL.Y; Y < TL.Y + Sz.Y; ++Y)
		for (int32 X = TL.X; X < TL.X + Sz.X; ++X)
			if (!CellBuildable(Id, X, Y)) return false;
	for (ARSUnit* U : Units)
	{
		if (!U || U == Ignore || U->bDead || U->UKind == TEXT("air")) continue;
		if (U->Pos.X > TL.X - 0.1f && U->Pos.X < TL.X + Sz.X + 0.1f && U->Pos.Y > TL.Y - 0.1f && U->Pos.Y < TL.Y + Sz.Y + 0.1f)
		{
			if (U->Player != P || (U->UKind == TEXT("veh") && U->bMoving)) return false;
		}
	}
	if (bNeedNear && !NearOwn(TL, Sz, P, Def.bNaval ? 5 : 0)) return false;
	return true;
}

bool ARSGame::NearOwn(FIntPoint TL, FIntPoint Sz, URSPlayer* P, int32 Extra) const
{
	const FIntRect R(TL, TL + Sz);
	for (ARSBuilding* B : Buildings)
	{
		if (!B || B->Player != P || B->bDead || B->Def->bWall) continue;
		const int32 Gap = BUILD_GAP + Extra;
		const FIntRect BR(B->Cell - FIntPoint(Gap, Gap), B->Cell + B->Size + FIntPoint(Gap, Gap));
		if (BR.Intersect(R)) return true;
	}
	return false;
}

ARSBuilding* ARSGame::PlaceBuilding(URSPlayer* P, const FString& Id, FIntPoint TL, bool bInstant)
{
	if (!FRSDefs::Get().Buildings.Contains(Id) || !P) return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARSBuilding* B = GetWorld()->SpawnActor<ARSBuilding>(ARSBuilding::StaticClass(), FTransform(ToWorld(RS::Cell2(TL))), SP);
	if (!B) return nullptr;
	B->EntId = NextId++;
	B->Setup(this, P, Id, TL, bInstant);
	if (!P->Country.IsEmpty())
	{
		B->MaxHp *= FRSDefs::Get().CountryMult(P->Country, TEXT("hp"), Id);
		B->Hp = B->MaxHp;
	}
	Buildings.Add(B);
	TArray<int32> Covered;
	for (int32 Y = TL.Y; Y < TL.Y + B->Size.Y; ++Y)
		for (int32 X = TL.X; X < TL.X + B->Size.X; ++X)
		{
			if (!Map->Inb(X, Y)) continue;
			Covered.Add(Map->Idx(X, Y));
			BGrid[Y * W + X] = B;
			Astar.SetSolid(X, Y, true);
			AstarW.SetSolid(X, Y, true);
			Map->ClearOre(X, Y);
		}
	if (Terrain && Covered.Num() > 0) Terrain->HideCellsForBuilding(Covered);
	for (ARSUnit* U : Units)
	{
		if (!U || U->bDead || U->UKind == TEXT("air")) continue;
		if (BldAt(int32(U->Pos.X), int32(U->Pos.Y)) == B)
		{
			const FIntPoint C = NearestFree(RS::ToCell(U->Pos), 6);
			if (C.X >= 0)
			{
				U->Pos = RS::CellCenter(C);
				U->Path.Reset();
			}
		}
	}
	P->bPowerDirty = true;
	if (bInstant) OnBuildingComplete(B);
	const bool bOwn = P == Human || (Human && Human->Allies.Contains(P->Index));
	if (bOwn) RevealEntity(B);
	B->SetShown(bOwn || IsExplored(TL.X, TL.Y) || IsExplored(TL.X + B->Size.X - 1, TL.Y + B->Size.Y - 1));
	return B;
}

void ARSGame::OnBuildingComplete(ARSBuilding* B)
{
	if (bLoading) return;
	const FRSDefs& D = FRSDefs::Get();
	if (B->Def->Role == TEXT("ref"))
	{
		const FString Hid = B->Def->Faction == TEXT("allies") ? TEXT("a_harv") : TEXT("s_harv");
		const FIntPoint C = NearestFree(B->ExitCell(), 5);
		if (C.X >= 0)
		{
			if (ARSUnit* U = SpawnUnit(B->Player, Hid, RS::CellCenter(C)))
			{
				U->Facing = PI * 0.5f;
				U->CmdHarvest();
			}
		}
	}
	if (B->Player == Human)
	{
		Audio()->Play(TEXT("build"), -4.f);
		if (B->Def->Role == TEXT("yard")) Message(TEXT("造船廠完工：右側「海軍」分頁可生產艦艇"));
		const FString& R = B->Def->Role;
		if (R == TEXT("radar") || R == TEXT("tech") || R == TEXT("fac") || R == TEXT("bar") || R == TEXT("ref")) Audio()->Say(TEXT("construction_options"), 3.f);
	}
	if (!B->Def->Super.IsEmpty() && B->Player != Human && Human->IsEnemyOf(B->Player))
	{
		Message(FString::Printf(TEXT("警告：%s 已建成超級武器「%s」，倒數開始！"), *B->Player->Name, *D.GetSuper(B->Def->Super).Name));
		Audio()->Play(TEXT("alarm"), -4.f);
		Ping(B->Pos);
		Reveal(B->Pos, 3.f);
	}
	else if (B->Def->Role == TEXT("radar") && B->Player != Human && Human->IsEnemyOf(B->Player))
	{
		const FRSCountry* C = D.GetCountry(B->Player->Country);
		if (C && !C->Power.IsEmpty()) Message(FString::Printf(TEXT("情報：%s 的特殊能力「%s」開始充能"), *B->Player->Name, *D.GetSuper(C->Power).Name));
	}
}

void ARSGame::RemoveBuildingCells(ARSBuilding* B)
{
	for (int32 Y = B->Cell.Y; Y < B->Cell.Y + B->Size.Y; ++Y)
		for (int32 X = B->Cell.X; X < B->Cell.X + B->Size.X; ++X)
		{
			if (!Map->Inb(X, Y)) continue;
			if (BGrid[Y * W + X] == B)
			{
				BGrid[Y * W + X] = nullptr;
				Astar.SetSolid(X, Y, !Map->Passable(X, Y));
				AstarW.SetSolid(X, Y, Map->Terrain[Map->Idx(X, Y)] != FRSMap::T_WATER);
			}
		}
}

void ARSGame::SpawnProduced(URSPlayer* P, const FString& Id)
{
	const FRSDef& Def = FRSDefs::Get().GetDef(Id);
	if (P == Human)
	{
		if (Def.bDrone) FRSRecords::Get().Bump(TEXT("drones"));
		if (!Def.Carrier.IsEmpty()) FRSRecords::Get().Bump(TEXT("carriers"));
	}
	const bool bVet = P->bVetBonus;
	const FString RoleId = URSPlayer::ProducerRole(Def);
	ARSBuilding* Prod = nullptr;
	if (TWeakObjectPtr<ARSBuilding>* PB = P->Primary.Find(RoleId))
	{
		ARSBuilding* B = PB->Get();
		if (B && !B->bDead && B->Player == P) Prod = B;
	}
	if (!Prod)
	{
		for (ARSBuilding* B : Buildings)
		{
			if (B && B->Player == P && !B->bDead && B->Built() && (B->Def->Role == RoleId || (RoleId == TEXT("airfield") && B->Def->bAirfield)))
			{
				if (!Prod || B->Def->Faction == Def.Faction) Prod = B;
			}
		}
	}
	if (!Prod)
	{
		P->Money += Def.Cost;
		return;
	}
	ARSUnit* U = nullptr;
	if (Def.Kind == TEXT("ship"))
	{
		FIntPoint WC = NearestWater(Prod->ExitCell(), 6);
		if (WC.X < 0) WC = NearestWater(RS::ToCell(Prod->Pos), 8);
		if (WC.X < 0)
		{
			P->Money += Def.Cost;
			return;
		}
		U = SpawnUnit(P, Id, RS::CellCenter(WC));
		if (U && Prod->Rally.X >= 0) U->CmdMove(Prod->Rally);
	}
	else if (Def.Kind == TEXT("air"))
	{
		U = SpawnUnit(P, Id, Prod->Pos + FVector2D(0.3f, 0.3f));
		if (U)
		{
			if (Def.bJet)
			{
				U->bLanded = true;
				U->Alt = 16.f;
			}
			else
			{
				U->CmdMove(Prod->Pos + FVector2D(1.5f, 1.5f));
			}
		}
	}
	else
	{
		FIntPoint C = NearestFree(Prod->ExitCell(), 8);
		if (C.X < 0) C = Prod->ExitCell();
		U = SpawnUnit(P, Id, RS::CellCenter(C) + FVector2D(RS::Rand(-0.15f, 0.15f), RS::Rand(-0.15f, 0.15f)));
		if (U)
		{
			U->Facing = PI * 0.5f;
			U->Turret = U->Facing;
			if (Def.bHarvester) U->CmdHarvest();
			else if (Prod->Rally.X >= 0) U->CmdMove(Prod->Rally + FVector2D(RS::Rand(-0.6f, 0.6f), RS::Rand(-0.6f, 0.6f)));
			else
			{
				const FIntPoint Out = NearestFree(C + FIntPoint(RS::RandI(-1, 1), 2), 4);
				if (Out.X >= 0) U->CmdMove(RS::CellCenter(Out));
			}
		}
	}
	if (!U) return;
	if (bVet && U->Rank < 1 && !Def.bHarvester) U->Rank = 1;
	if (P == Human) Audio()->Say(TEXT("unit_ready"), 2.5f);
	for (URSAI* AI : AIs)
		if (AI->P == P) AI->OnUnitBuilt(U);
}

FIntPoint ARSGame::NearestFree(FIntPoint C, int32 MaxR) const
{
	for (int32 R = 0; R <= MaxR; ++R)
	{
		FIntPoint Best(-1, -1);
		float BD = 1e9f;
		for (int32 Y = C.Y - R; Y <= C.Y + R; ++Y)
			for (int32 X = C.X - R; X <= C.X + R; ++X)
			{
				if (FMath::Max(FMath::Abs(X - C.X), FMath::Abs(Y - C.Y)) != R) continue;
				if (Map->Inb(X, Y) && Map->Passable(X, Y) && BldAt(X, Y) == nullptr)
				{
					const float D = float((X - C.X) * (X - C.X) + (Y - C.Y) * (Y - C.Y));
					if (D < BD)
					{
						BD = D;
						Best = FIntPoint(X, Y);
					}
				}
			}
		if (Best.X >= 0) return Best;
	}
	return FIntPoint(-1, -1);
}

ARSBuilding* ARSGame::NearestRefinery(URSPlayer* P, const FVector2D& From) const
{
	ARSBuilding* Best = nullptr;
	float BD = 1e9f;
	for (ARSBuilding* B : Buildings)
	{
		if (B && B->Player == P && !B->bDead && B->Def->Role == TEXT("ref") && B->Built())
		{
			const float D = FVector2D::Distance(B->Pos, From);
			if (D < BD) { BD = D; Best = B; }
		}
	}
	return Best;
}

ARSBuilding* ARSGame::NearestAirfield(URSPlayer* P, const FVector2D& From) const
{
	ARSBuilding* Best = nullptr;
	float BD = 1e9f;
	for (ARSBuilding* B : Buildings)
	{
		if (B && B->Player == P && !B->bDead && B->Def->bAirfield && B->Built())
		{
			const float D = FVector2D::Distance(B->Pos, From);
			if (D < BD) { BD = D; Best = B; }
		}
	}
	return Best;
}

// ───────────────────────── 查詢 ─────────────────────────
bool ARSGame::PassablePos(const FVector2D& P, bool bWater) const
{
	const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y);
	if (X < 0 || Y < 0 || X >= W || Y >= H) return false;
	return bWater ? !AstarW.IsSolid(X, Y) : !Astar.IsSolid(X, Y);
}

bool ARSGame::Los(const FVector2D& A, const FVector2D& B, bool bWater) const
{
	const float D = FVector2D::Distance(A, B);
	const int32 N = int32(D / 0.25f) + 1;
	const FVector2D Side = RS::Ortho((B - A).GetSafeNormal()) * (bWater ? 0.4f : 0.3f);
	for (int32 K = 0; K <= N; ++K)
	{
		const FVector2D P = RS::Lerp2(A, B, float(K) / N);
		if (!PassablePos(P, bWater)) return false;
		if (!PassablePos(P + Side, bWater) || !PassablePos(P - Side, bWater)) return false;
	}
	return true;
}

FIntPoint ARSGame::NearestWater(FIntPoint C, int32 MaxR) const
{
	for (int32 R = 0; R <= MaxR; ++R)
		for (int32 Y = C.Y - R; Y <= C.Y + R; ++Y)
			for (int32 X = C.X - R; X <= C.X + R; ++X)
			{
				if (FMath::Max(FMath::Abs(X - C.X), FMath::Abs(Y - C.Y)) != R) continue;
				if (Map->Inb(X, Y) && !AstarW.IsSolid(X, Y)) return FIntPoint(X, Y);
			}
	return FIntPoint(-1, -1);
}

TArray<FVector2D> ARSGame::FindPath(const FVector2D& From, const FVector2D& To, bool bWater)
{
	TArray<FVector2D> Out;
	FRSGridPath& Grid = bWater ? AstarW : Astar;
	FIntPoint A(FMath::Clamp(FMath::FloorToInt(From.X), 0, W - 1), FMath::Clamp(FMath::FloorToInt(From.Y), 0, H - 1));
	if (Grid.IsSolid(A.X, A.Y))
	{
		A = bWater ? NearestWater(A, 3) : NearestFree(A, 3);
		if (A.X < 0) return Out;
	}
	const FIntPoint B(FMath::Clamp(FMath::FloorToInt(To.X), 0, W - 1), FMath::Clamp(FMath::FloorToInt(To.Y), 0, H - 1));
	const TArray<FIntPoint> Ids = Grid.FindPath(A, B, true);
	if (Ids.Num() == 0) return Out;
	TArray<FVector2D> Pts;
	for (int32 K = 1; K < Ids.Num(); ++K) Pts.Add(RS::CellCenter(Ids[K]));
	const bool bExact = Ids.Last() == B && !Grid.IsSolid(B.X, B.Y);
	if (bExact)
	{
		const FVector2D TP(FMath::Clamp(To.X, B.X + 0.15f, B.X + 0.85f), FMath::Clamp(To.Y, B.Y + 0.15f, B.Y + 0.85f));
		if (Pts.Num() == 0) Pts.Add(TP);
		else Pts.Last() = TP;
	}
	if (Pts.Num() == 0) return Out;
	// 拉直路徑
	FVector2D Cur = From;
	int32 I = 0;
	while (I < Pts.Num())
	{
		int32 J = FMath::Min(I + 10, Pts.Num() - 1);
		while (J > I && !Los(Cur, Pts[J], bWater)) J--;
		Out.Add(Pts[J]);
		Cur = Pts[J];
		I = J + 1;
	}
	return Out;
}

void ARSGame::RebuildBuckets()
{
	Buckets.Reset();
	for (ARSUnit* U : Units)
	{
		if (!U || U->bDead || U->UKind == TEXT("air")) continue;
		Buckets.FindOrAdd(int32(U->Pos.X) + int32(U->Pos.Y) * 1000).Add(U);
	}
}

void ARSGame::Neighbors(const FVector2D& P, TArray<ARSUnit*>& Out) const
{
	const int32 CX = int32(P.X), CY = int32(P.Y);
	for (int32 Y = CY - 1; Y <= CY + 1; ++Y)
		for (int32 X = CX - 1; X <= CX + 1; ++X)
			if (const TArray<ARSUnit*>* L = Buckets.Find(X + Y * 1000)) Out.Append(*L);
}

ARSEntity* ARSGame::FindTarget(const ARSEntity* Ent, const FRSWeapon& Wp, float Rng, bool bPreferUnits, bool bAirOnly) const
{
	ARSEntity* Best = nullptr;
	float BestS = 1e9f;
	const FRSDefs& D = FRSDefs::Get();
	const FVector2D MP = Ent->Pos;
	const float MinR = Wp.MinRange;
	const ARSUnit* EU = Cast<ARSUnit>(Ent);
	for (ARSUnit* U : Units)
	{
		if (!U || U->bDead || !Ent->Player->IsEnemyOf(U->Player)) continue;
		if (U->Def->bSpy)
		{
			if (!EU || EU->UKind != TEXT("inf") || FVector2D::Distance(MP, U->Pos) > 2.2f) continue;
		}
		if (U->IsAir())
		{
			if (!Wp.bAir) continue;
		}
		else if (bAirOnly || Wp.bAirOnly)
		{
			continue;
		}
		const float Eff = D.WarheadMult(Wp.Wh, U->Armor);
		if (Eff < 0.02f) continue;
		const float Dd = FVector2D::Distance(MP, U->Pos);
		if (Dd > Rng || Dd < MinR) continue;
		if (Ent->Player == Human && !IsExplored(int32(U->Pos.X), int32(U->Pos.Y))) continue;
		float S = Dd - Eff * 2.f;
		if (U->HasWeapon()) S -= 1.f;
		if (S < BestS)
		{
			BestS = S;
			Best = U;
		}
	}
	if (bAirOnly || Wp.bAirOnly) return Best;
	for (ARSBuilding* B : Buildings)
	{
		if (!B || B->bDead || !Ent->Player->IsEnemyOf(B->Player) || B->Def->bWall) continue;
		const float Eff = D.WarheadMult(Wp.Wh, TEXT("bld"));
		if (Eff < 0.02f) continue;
		const float Dd = B->DistTo(MP);
		if (Dd > Rng || Dd < MinR) continue;
		float S = Dd + 1.5f - Eff;
		if (B->HasWeapon()) S -= 3.f;
		if (bPreferUnits) S += 6.f;
		if (S < BestS)
		{
			BestS = S;
			Best = B;
		}
	}
	return Best;
}

void ARSGame::DamageArea(const FVector2D& P, float R, float Dmg, const FString& Wh, ARSEntity* Att, ARSEntity* Primary, URSPlayer* InOwner, bool bFriendly)
{
	URSPlayer* Own = InOwner;
	if (!Own && Att && IsValid(Att)) Own = Att->Player;
	ARSEntity* A = (Att && IsValid(Att) && !Att->bDead) ? Att : nullptr;
	for (ARSUnit* U : TArray<ARSUnit*>(Units))
	{
		if (!U || U->bDead) continue;
		if (!bFriendly && Own && U->Player == Own) continue;
		if (U->IsAir() && Wh != TEXT("flak")) continue;
		const float Dd = FVector2D::Distance(U->Pos, P);
		if (Dd <= R || U == Primary)
		{
			const float F = 1.f - 0.5f * FMath::Clamp(Dd / FMath::Max(R, 0.01f), 0.f, 1.f);
			U->ApplyDamage(Dmg * F, A, Wh);
		}
	}
	for (ARSBuilding* B : TArray<ARSBuilding*>(Buildings))
	{
		if (!B || B->bDead) continue;
		if (!bFriendly && Own && B->Player == Own) continue;
		const float Dd = B->DistTo(P);
		if (Dd <= R || B == Primary)
		{
			const float F = 1.f - 0.5f * FMath::Clamp(Dd / FMath::Max(R, 0.01f), 0.f, 1.f);
			B->ApplyDamage(Dmg * F, A, Wh);
		}
	}
}

// ───────────────────────── 事件 ─────────────────────────
void ARSGame::After(float Delay, TFunction<void()> Fn)
{
	FRSTimed T;
	T.T = Time + Delay;
	T.Fn = MoveTemp(Fn);
	Events.Add(MoveTemp(T));
}

void ARSGame::OnEntityDied(ARSEntity* E)
{
	Selection.RemoveAll([E](const TWeakObjectPtr<ARSEntity>& Wk) { return Wk.Get() == E; });
	for (auto& G : Groups) G.Value.RemoveAll([E](const TWeakObjectPtr<ARSUnit>& Wk) { return Wk.Get() == E; });
	if (Hover.Get() == E) Hover = nullptr;
	if (ARSUnit* U = Cast<ARSUnit>(E))
	{
		Units.Remove(U);
		U->Player->Stats.Lost++;
		if (U->bShown)
		{
			if (U->UKind == TEXT("inf"))
			{
				bool bFried = false;
				if (ARSEntity* LA = U->LastAttacker.Get())
				{
					if (ARSUnit* LU = Cast<ARSUnit>(LA)) bFried = LU->HasWeapon() && LU->Weapon->Proj == TEXT("tesla");
					else if (ARSBuilding* LB = Cast<ARSBuilding>(LA)) bFried = LB->HasWeapon() && LB->Weapon->Proj == TEXT("tesla");
				}
				Fx->InfDeath(U->Pos, bFried);
			}
			else if (U->UKind == TEXT("veh"))
			{
				Fx->Explosion(U->Pos, (U->Def->bHarvester || !U->Def->Mcv.IsEmpty()) ? 1.8f : 1.3f);
				MakeWreck(U);
				return;
			}
			else if (U->UKind == TEXT("air"))
			{
				Fx->Explosion(U->Pos, 1.6f, U->Alt * 0.5f);
				Fx->Explosion(U->Pos, 1.2f);
			}
			else
			{
				Fx->Explosion(U->Pos, 1.6f);
			}
		}
		U->Destroy();
		return;
	}
	ARSBuilding* B = Cast<ARSBuilding>(E);
	if (!B) return;
	Buildings.Remove(B);
	RemoveBuildingCells(B);
	B->Player->Stats.BldLost++;
	B->Player->bPowerDirty = true;
	const float S = (B->Size.X + B->Size.Y) * 0.5f;
	Fx->Explosion(B->Pos, 1.2f + S * 0.5f);
	if (B->bShown) AddHot(B->Pos, 12.f, FString::Printf(TEXT("%s 被摧毀"), *B->Def->Name));
	if (Terrain && !B->Def->bCiv) Terrain->AddRubble(B->Cell, B->Size, B->Def->bWall, B->bShown);
	else if (Terrain) Terrain->AddRubble(B->Cell, B->Size, false, B->bShown);
	for (int32 K = 0; K < int32(S * 2); ++K)
	{
		const FVector2D TP = B->Pos + FVector2D(RS::Rand(-0.5f, 0.5f) * B->Size.X, RS::Rand(-0.5f, 0.5f) * B->Size.Y);
		After(RS::Rand(0.1f, 0.8f), [this, TP]() { Fx->Explosion(TP, 1.f + RS::Rand01() * 0.6f); });
	}
	if (B->Occupants.Num() > 0)
	{
		const TArray<FRSOccupant> Occ = B->Occupants;
		B->Occupants.Reset();
		for (const FRSOccupant& O : Occ)
		{
			const FIntPoint C = NearestFree(B->ExitCell(), 4);
			if (C.X >= 0)
			{
				if (ARSUnit* U = SpawnUnit(B->Player, O.Id, RS::Cell2(C) + FVector2D(RS::Rand(0.2f, 0.8f), RS::Rand(0.2f, 0.8f)))) U->Hp = FMath::Max(10.f, O.Hp * 0.5f);
			}
		}
	}
	if (B->Def->bExplode)
	{
		Fx->Explosion(B->Pos, 3.f);
		DamageArea(B->Pos, 2.5f, 400.f, TEXT("he"), nullptr, nullptr, nullptr, true);
	}
	if (!B->Def->bWall && !B->Def->bCiv && B->Def->Role != TEXT("def") && !B->Player->bNeutral)
	{
		const FRSFaction* F = FRSDefs::Get().Factions.Find(B->Def->Faction);
		const FString InfId = F ? F->Infantry : FString(TEXT("a_gi"));
		const int32 N = RS::RandI(0, 2);
		for (int32 K = 0; K < N; ++K)
		{
			const FIntPoint C = NearestFree(B->ExitCell(), 3);
			if (C.X >= 0) SpawnUnit(B->Player, InfId, RS::Cell2(C) + FVector2D(RS::Rand01(), RS::Rand01()) * 0.8f + FVector2D(0.1f, 0.1f));
		}
	}
	if (B->Player == Human) Message(FString::Printf(TEXT("%s 被摧毀了"), *B->Def->Name));
	B->Destroy();
}

void ARSGame::QuickSave()
{
	if (bGameOver) return;
	if (RSSave::SaveGame(this, 0))
	{
		Message(TEXT("已快速存檔（F9 讀取）"));
		Audio()->Play(TEXT("click"), -4.f);
	}
	else
	{
		Message(TEXT("存檔失敗"));
	}
}

// ───────────────────────── 外交 ─────────────────────────
TArray<URSPlayer*> ARSGame::OthersOf(const URSPlayer* P) const
{
	TArray<URSPlayer*> Out;
	for (URSPlayer* Q : Players)
		if (Q != P && !Q->bNeutral && Q->bAlive) Out.Add(Q);
	return Out;
}

float ARSGame::Strength(const URSPlayer* P) const
{
	float S = 0.f;
	for (ARSUnit* U : Units)
		if (U && U->Player == P && !U->bDead) S += U->Def->Cost * 0.5f;
	for (ARSBuilding* B : Buildings)
		if (B && B->Player == P && !B->bDead) S += B->Def->Cost * 0.3f;
	return S;
}

void ARSGame::UpdateDiplomacy(float Dt)
{
	DiploT -= Dt;
	if (DiploT > 0.f) return;
	DiploT = 10.f;
	for (URSPlayer* P : Players)
	{
		if (P->bNeutral || !P->bAlive) continue;
		for (URSPlayer* Q : OthersOf(P)) P->AddRelation(Q, P->IsEnemyOf(Q) ? 0.3f : 1.f);
		TArray<int32> Keys;
		P->Ceasefire.GetKeys(Keys);
		for (int32 K : Keys)
		{
			if (Time >= P->Ceasefire[K])
			{
				P->Ceasefire.Remove(K);
				if (P == Human)
				{
					for (URSPlayer* Q : Players)
						if (Q->Index == K) Message(FString::Printf(TEXT("與 %s 的停火協議到期"), *Q->Name));
				}
			}
		}
	}
	for (URSAI* AI : AIs)
	{
		URSPlayer* P = AI->P;
		if (!P->bAlive || P->Allies.Contains(Human->Index) || Human->Proposal.Contains(P->Index)) continue;
		if (RS::Rand01() > 0.25f) continue;
		int32 EnemiesN = 0;
		for (URSPlayer* Q : OthersOf(P))
			if (P->IsEnemyOf(Q)) EnemiesN++;
		if (EnemiesN >= 2 && P->Relation(Human) >= 45.f && Strength(Human) > Strength(P) * 0.8f)
		{
			Human->Proposal.Add(P->Index, TEXT("ally"));
			Message(FString::Printf(TEXT("%s 提議與你結盟！按 T 開啟外交面板回應"), *P->Name));
			Audio()->Play(TEXT("radio"), -4.f);
		}
		else if (P->Relation(Human) >= 30.f && P->Relation(Human) < 45.f && Strength(Human) > Strength(P) * 1.5f && Human->IsEnemyOf(P))
		{
			Human->Proposal.Add(P->Index, TEXT("ceasefire"));
			Message(FString::Printf(TEXT("%s 提議停火 3 分鐘。按 T 開啟外交面板回應"), *P->Name));
			Audio()->Play(TEXT("radio"), -4.f);
		}
	}
}

bool ARSGame::Propose(URSPlayer* From, URSPlayer* To, const FString& Kind)
{
	if (Time < From->NoTreatyUntil || Time < To->NoTreatyUntil)
	{
		Message(TEXT("輿論風向不利，目前無法締結條約"));
		return false;
	}
	const float Rel = To->Relation(From);
	int32 EnemiesN = 0;
	for (URSPlayer* Q : OthersOf(To))
		if (To->IsEnemyOf(Q) && Q != From) EnemiesN++;
	bool bOk = false;
	if (Kind == TEXT("ally")) bOk = Rel >= 50.f && (EnemiesN >= 1 || Strength(From) >= Strength(To) * 1.3f || Rel >= 70.f);
	else if (Kind == TEXT("ceasefire")) bOk = Rel >= 35.f;
	if (bOk)
	{
		ApplyTreaty(From, To, Kind);
	}
	else
	{
		To->AddRelation(From, -3.f);
		if (From == Human) Message(FString::Printf(TEXT("%s 拒絕了提議（好感度 %d%%，需要 %d%%）"), *To->Name, int32(Rel), Kind == TEXT("ally") ? 50 : 35));
	}
	return bOk;
}

void ARSGame::ApplyTreaty(URSPlayer* A, URSPlayer* B, const FString& Kind)
{
	if (A == Human || B == Human) FRSRecords::Get().Bump(TEXT("treaties"));
	if (Kind == TEXT("ally"))
	{
		A->Allies.Add(B->Index);
		B->Allies.Add(A->Index);
		A->Ceasefire.Remove(B->Index);
		B->Ceasefire.Remove(A->Index);
		A->AddRelation(B, 20.f);
		B->AddRelation(A, 20.f);
		Message(FString::Printf(TEXT("%s 與 %s 締結同盟！共享視野，可呼叫盟友支援"), *A->Name, *B->Name));
		Audio()->Say(TEXT("building_ready"), 1.f);
		if (A == Human || B == Human)
		{
			URSPlayer* Other = A == Human ? B : A;
			for (ARSUnit* U : Units)
				if (U && U->Player == Other && !U->bDead) RevealEntity(U);
			for (ARSBuilding* Bd : Buildings)
				if (Bd && Bd->Player == Other && !Bd->bDead) RevealEntity(Bd);
			Human->Powers.FindOrAdd(TEXT("allyaid"));
		}
		for (ARSUnit* U : Units)
		{
			if (!U) continue;
			ARSEntity* T = U->Target.Get();
			if (T && ((U->Player == A && T->Player == B) || (U->Player == B && T->Player == A))) U->CmdStop();
		}
	}
	else if (Kind == TEXT("ceasefire"))
	{
		A->Ceasefire.Add(B->Index, Time + 180.f);
		B->Ceasefire.Add(A->Index, Time + 180.f);
		Message(FString::Printf(TEXT("%s 與 %s 停火 3 分鐘"), *A->Name, *B->Name));
	}
	A->Proposal.Remove(B->Index);
	B->Proposal.Remove(A->Index);
}

void ARSGame::BreakTreaty(URSPlayer* A, URSPlayer* B)
{
	A->Allies.Remove(B->Index);
	B->Allies.Remove(A->Index);
	A->Ceasefire.Remove(B->Index);
	B->Ceasefire.Remove(A->Index);
	A->AddRelation(B, -40.f);
	B->AddRelation(A, -40.f);
	Message(FString::Printf(TEXT("%s 撕毀了與 %s 的同盟！"), *A->Name, *B->Name));
	Audio()->Play(TEXT("alarm"), -4.f);
	if (A == Human || B == Human)
	{
		if (HumanAllies().Num() == 0) Human->Powers.Remove(TEXT("allyaid"));
	}
}

TArray<URSPlayer*> ARSGame::HumanAllies() const
{
	TArray<URSPlayer*> Out;
	for (URSPlayer* Q : OthersOf(Human))
		if (Human->Allies.Contains(Q->Index)) Out.Add(Q);
	return Out;
}

void ARSGame::NotifyAttack(ARSEntity* Victim, ARSEntity* Attacker)
{
	if (Attacker && IsValid(Attacker) && Attacker->Player != Victim->Player)
	{
		URSPlayer* AP = Attacker->Player;
		URSPlayer* VP = Victim->Player;
		if (VP->Allies.Contains(AP->Index) && AP == Human)
		{
			VP->AllyHits++;
			if (VP->AllyHits > 6)
			{
				VP->AllyHits = 0;
				BreakTreaty(AP, VP);
			}
		}
	}
	for (URSAI* AI : AIs)
		if (AI->P == Victim->Player) AI->OnAttacked(Victim, Attacker);
	if (Victim && Victim->bShown) AddHot(Victim->Pos, Victim->bIsBuilding ? 1.5f : 1.f);
}

// ───────────────────────── 特殊行動 ─────────────────────────
void ARSGame::SpyEnter(ARSUnit* U, ARSBuilding* B)
{
	URSPlayer* TP = B->Player;
	URSPlayer* Me = U->Player;
	FString Msg;
	if (Me == Human) FRSRecords::Get().Bump(TEXT("spies"));
	const FString& R = B->Def->Role;
	if (R == TEXT("power"))
	{
		TP->EnergyCutUntil = FMath::Max(TP->EnergyCutUntil, Time + 45.f);
		TP->bPowerDirty = true;
		Msg = FString::Printf(TEXT("間諜破壞了 %s 的發電廠，敵方停電 45 秒"), *TP->Name);
	}
	else if (R == TEXT("ref") || R == TEXT("bank"))
	{
		double Amt = FMath::Max(500.0, TP->Money * 0.2);
		Amt = FMath::Min(Amt, TP->Money);
		TP->Money -= Amt;
		Me->Money += Amt;
		Msg = FString::Printf(TEXT("間諜從 %s 竊取了 $%d"), *TP->Name, int32(Amt));
		Fx->Coins(B->Pos);
	}
	else if (R == TEXT("radar"))
	{
		if (Me == Human)
			for (ARSBuilding* O : Buildings)
				if (O && O->Player == TP && !O->bDead) Reveal(O->Pos, 5.f);
		Msg = FString::Printf(TEXT("間諜取得 %s 的雷達情報，敵方基地已揭露"), *TP->Name);
	}
	else if (R == TEXT("sw"))
	{
		for (auto& K : TP->Powers) K.Value = 0.f;
		Msg = FString::Printf(TEXT("間諜重置了 %s 的超級武器倒數！"), *TP->Name);
	}
	else if (R == TEXT("tech"))
	{
		Me->bVetBonus = true;
		Msg = TEXT("間諜竊取科技：我方新生產的單位將直接晉升老兵");
	}
	else if (R == TEXT("cy") || R == TEXT("bar") || R == TEXT("fac") || R == TEXT("yard") || R == TEXT("dronehub") || B->Def->bAirfield)
	{
		TP->SabotageUntil = FMath::Max(TP->SabotageUntil, Time + 60.f);
		Msg = FString::Printf(TEXT("間諜癱瘓了 %s 的生產線 60 秒"), *TP->Name);
	}
	else
	{
		Msg = FString::Printf(TEXT("間諜滲透了 %s，但沒有取得有用情報"), *B->Def->Name);
	}
	if (Me == Human)
	{
		Message(Msg);
		Audio()->Play(TEXT("cash"), -4.f);
		if (Mission) Mission->OnSpy(B);
	}
	else if (TP == Human)
	{
		Message(FString::Printf(TEXT("警告：敵方間諜滲透了我方 %s！"), *B->Def->Name));
		Audio()->Play(TEXT("alarm"), -4.f);
	}
	Fx->ScanRing(B->Pos, FLinearColor(1.f, 0.9f, 0.3f));
	RemoveUnitSilent(U);
}

void ARSGame::EngineerEnter(ARSUnit* U, ARSBuilding* B)
{
	if (U->Def->bSpy)
	{
		if (B->Player != U->Player && U->Player->IsEnemyOf(B->Player)) SpyEnter(U, B);
		else U->CmdStop();
		return;
	}
	if (B->Player == U->Player)
	{
		if (B->Hp < B->MaxHp)
		{
			B->Hp = B->MaxHp;
			if (U->Player == Human) Message(FString::Printf(TEXT("%s 已修復"), *B->Def->Name));
		}
	}
	else if (B->Def->bWall)
	{
		U->CmdStop();
		return;
	}
	else
	{
		URSPlayer* Old = B->Player;
		B->Player = U->Player;
		B->bRepairing = false;
		B->CaptureFlash = 1.2f;
		B->Target = nullptr;
		B->RefreshTeamColor();
		Old->bPowerDirty = true;
		U->Player->bPowerDirty = true;
		if (B->Def->Role == TEXT("oil")) U->Player->Money += 500;
		if (U->Player == Human)
		{
			Message(FString::Printf(TEXT("已佔領 %s！"), *B->Def->Name));
			Audio()->Say(TEXT("building_ready"), 1.f);
			RevealEntity(B);
		}
		else if (Old == Human)
		{
			Message(FString::Printf(TEXT("警告：%s 被敵軍佔領！"), *B->Def->Name));
			Audio()->Play(TEXT("alarm"), -4.f);
		}
	}
	RemoveUnitSilent(U);
}

void ARSGame::PlantC4(ARSUnit* U, ARSEntity* T)
{
	Audio()->Play(TEXT("tick"), -2.f, &T->Pos);
	TWeakObjectPtr<ARSEntity> WT = T;
	TWeakObjectPtr<ARSUnit> WU = U;
	After(1.2f, [WT, WU]()
	{
		ARSEntity* TT = WT.Get();
		ARSUnit* UU = WU.Get();
		if (TT && !TT->bDead)
		{
			TT->Hp = 1.f;
			TT->ApplyDamage(1e6f, (UU && !UU->bDead) ? UU : nullptr, TEXT("he"));
		}
	});
}

void ARSGame::TryDeploy(ARSUnit* U)
{
	const FIntPoint TL(FMath::FloorToInt(U->Pos.X) - 1, FMath::FloorToInt(U->Pos.Y) - 1);
	const FString Id = U->Def->Mcv;
	const FIntPoint Offs[] = { {0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, -1} };
	for (const FIntPoint& O : Offs)
	{
		if (CanPlace(Id, TL + O, U->Player, U, false))
		{
			URSPlayer* P = U->Player;
			const FVector2D At = U->Pos;
			RemoveUnitSilent(U);
			PlaceBuilding(P, Id, TL + O);
			Audio()->Play(TEXT("deploy"), 0.f, &At);
			if (P == Human) Message(TEXT("基地車展開中……"));
			return;
		}
	}
	if (U->Player == Human)
	{
		Message(TEXT("這裡無法展開基地車，請移到較空曠的地方"));
		Audio()->Play(TEXT("click"), -2.f);
	}
}

void ARSGame::RemoveUnitSilent(ARSUnit* U)
{
	if (!U) return;
	U->bDead = true;
	Units.Remove(U);
	Selection.RemoveAll([U](const TWeakObjectPtr<ARSEntity>& Wk) { return Wk.Get() == U; });
	for (auto& G : Groups) G.Value.RemoveAll([U](const TWeakObjectPtr<ARSUnit>& Wk) { return Wk.Get() == U; });
	U->Destroy();
}

void ARSGame::Sell(ARSBuilding* B)
{
	if (B->bSelling || B->bDead || !B->Built()) return;
	B->bSelling = true;
	B->SellT = 0.f;
	B->bRepairing = false;
	Audio()->Play(TEXT("sell"), -2.f);
}

void ARSGame::FinishSell(ARSBuilding* B)
{
	const float Refund = B->Def->Cost * 0.5f * B->Hp / B->MaxHp;
	B->Player->Money += Refund;
	const FString F = B->Def->Faction;
	B->bDead = true;
	Buildings.Remove(B);
	RemoveBuildingCells(B);
	if (!B->Def->bWall)
	{
		if (const FRSFaction* Fa = FRSDefs::Get().Factions.Find(F))
		{
			const int32 N = RS::RandI(1, 3);
			for (int32 K = 0; K < N; ++K)
			{
				const FIntPoint C = NearestFree(B->ExitCell(), 4);
				if (C.X >= 0) SpawnUnit(B->Player, Fa->Infantry, RS::Cell2(C) + FVector2D(RS::Rand(0.2f, 0.8f), RS::Rand(0.2f, 0.8f)));
			}
		}
	}
	B->Player->bPowerDirty = true;
	Selection.RemoveAll([B](const TWeakObjectPtr<ARSEntity>& Wk) { return Wk.Get() == B; });
	if (B->Player == Human) Audio()->Play(TEXT("cash"), -2.f);
	B->Destroy();
}

void ARSGame::OnPowerReady(URSPlayer* P, const FString& Kind)
{
	if (P == Human)
	{
		Message(FString::Printf(TEXT("特殊武器「%s」已就緒！"), *FRSDefs::Get().GetSuper(Kind).Name));
		Audio()->Play(TEXT("alarm"), -8.f);
	}
	for (URSAI* AI : AIs)
		if (AI->P == P) AI->PowerReady(Kind);
}

URSPlayer* ARSGame::PlayerAt(const FVector2D& Target, URSPlayer* Attacker) const
{
	URSPlayer* Best = nullptr;
	float BD = 1e9f;
	for (ARSBuilding* B : Buildings)
	{
		if (!B || B->bDead || !Attacker->IsEnemyOf(B->Player)) continue;
		const float D = B->DistTo(Target);
		if (D < BD)
		{
			BD = D;
			Best = B->Player;
		}
	}
	if (!Best)
	{
		for (URSPlayer* Q : Players)
			if (Attacker->IsEnemyOf(Q) && Q->bAlive) return Q;
	}
	return Best;
}

bool ARSGame::FirePower(URSPlayer* P, const FString& Kind, const FVector2D& Target)
{
	float* Charge = P->Powers.Find(Kind);
	if (!Charge || *Charge < 1.f) return false;
	const FRSSuper& Sup = FRSDefs::Get().GetSuper(Kind);
	URSPlayer* TP = nullptr;
	if (Sup.Target == TEXT("enemy"))
	{
		TP = PlayerAt(Target, P);
		if (!TP) return false;
	}
	*Charge = 0.f;
	if (P == Human) FRSRecords::Get().Bump(TEXT("powers"));
	AddHot(Target, 25.f, FString::Printf(TEXT("%s 發動「%s」"), *P->Name, *Sup.Name));
	if (P == Human && Mission) Mission->OnPower(Kind);
	const bool bMe = P == Human;
	if (Kind == TEXT("nuke"))
	{
		FireNuke(P, Target);
	}
	else if (Kind == TEXT("storm"))
	{
		FireStorm(Target);
	}
	else if (Kind == TEXT("sanction"))
	{
		const double Frozen = TP->Money * 0.3;
		TP->Money -= Frozen;
		TP->SanctionedUntil = Time + 90.f;
		Message(bMe ? FString::Printf(TEXT("金融制裁生效！%s 被凍結 $%d 資金，收入與生產大幅下降 90 秒"), *TP->Name, int32(Frozen))
			: FString::Printf(TEXT("警告：我方遭受金融制裁！資金被凍結 $%d，收入與生產下降"), int32(Frozen)));
		Audio()->Play(bMe ? TEXT("cash") : TEXT("alarm"), 0.f);
		for (ARSBuilding* B : Buildings)
			if (B && B->Player == TP && !B->bDead) Fx->Coins(B->Pos);
	}
	else if (Kind == TEXT("energy"))
	{
		TP->EnergyCutUntil = Time + 60.f;
		TP->bPowerDirty = true;
		Message(bMe ? FString::Printf(TEXT("能源斷供！%s 全面停電 60 秒"), *TP->Name) : FString(TEXT("警告：敵方切斷能源供應！我方全面停電 60 秒")));
		Audio()->Play(TEXT("power_down"), 0.f);
		for (ARSBuilding* B : Buildings)
			if (B && B->Player == TP && !B->bDead && B->Def->Role == TEXT("power")) Fx->Smoke(B->Pos, 30.f);
	}
	else if (Kind == TEXT("cyber"))
	{
		// 網路作戰：電網停擺、防禦系統癱瘓、雷達被駭、竊取資金
		TP->EnergyCutUntil = FMath::Max(TP->EnergyCutUntil, Time + 20.f);
		TP->bPowerDirty = true;
		TP->RadarJamUntil = Time + 40.f;
		const double Stolen = FMath::Min(TP->Money * 0.1, 3000.0);
		TP->Money -= Stolen;
		P->Money += Stolen;
		int32 NB = 0;
		for (ARSBuilding* B : Buildings)
		{
			if (!B || B->bDead || B->Player != TP) continue;
			if (B->Def->Role == TEXT("def")) B->JamUntil = FMath::Max(B->JamUntil, Time + 15.f);
			if (NB < 6 && (B->Def->Role == TEXT("power") || B->Def->Role == TEXT("radar") || B->Def->Role == TEXT("def")))
			{
				Fx->ScanRing(B->Pos, FLinearColor(0.2f, 1.f, 0.55f));
				++NB;
			}
		}
		Message(bMe ? FString::Printf(TEXT("網路作戰成功！%s 電網停擺 20 秒、防禦癱瘓 15 秒、雷達失效 40 秒，竊得 $%d"), *TP->Name, int32(Stolen))
			: FString::Printf(TEXT("警告：我方網路遭入侵！停電 20 秒、防禦失效、雷達被駭，損失 $%d"), int32(Stolen)));
		Audio()->Play(bMe ? TEXT("zap") : TEXT("alarm"), 0.f);
	}
	else if (Kind == TEXT("swarm"))
	{
		ARSBuilding* Hub = nullptr;
		for (ARSBuilding* B : Buildings)
			if (B && B->Player == P && !B->bDead && B->Def->PowerSw == TEXT("swarm")) Hub = B;
		const FVector2D Src = Hub ? Hub->Pos : Target;
		const FString Did = P->Faction == TEXT("allies") ? TEXT("a_fpv") : TEXT("s_lancet");
		const int32 N = Did == TEXT("a_fpv") ? 8 : 6;
		for (int32 K = 0; K < N; ++K)
		{
			if (ARSUnit* U = SpawnUnit(P, Did, Src + FVector2D(RS::Rand(-0.8f, 0.8f), RS::Rand(-0.8f, 0.8f))))
			{
				U->CmdMove(Target + FVector2D(RS::Rand(-1.5f, 1.5f), RS::Rand(-1.5f, 1.5f)));
				U->bSwarm = true;
			}
		}
		Message(bMe ? TEXT("無人機蜂群已出擊！") : TEXT("警告：偵測到敵方無人機蜂群！"));
		Audio()->Play(TEXT("jet"), -2.f);
	}
	else if (Kind == TEXT("scan"))
	{
		if (bMe)
		{
			Reveal(Target, 11.f);
			Fx->ScanRing(Target);
			Message(TEXT("星鏈衛星掃描完成"));
		}
	}
	else if (Kind == TEXT("jam"))
	{
		for (ARSUnit* U : TArray<ARSUnit*>(Units))
			if (U && !U->bDead && U->Def->bDrone && P->IsEnemyOf(U->Player) && FVector2D::Distance(U->Pos, Target) <= 6.f) U->ApplyDamage(1e5f, nullptr, TEXT("ap"));
		for (ARSBuilding* B : Buildings)
			if (B && !B->bDead && P->IsEnemyOf(B->Player) && B->DistTo(Target) <= 6.f) B->JamUntil = Time + 25.f;
		Fx->ScanRing(Target, FLinearColor(0.3f, 1.f, 0.5f));
		Message(bMe ? TEXT("GPS 干擾啟動：區域內敵方無人機墜毀、防禦失效") : TEXT("警告：敵方實施電子干擾！"));
		Audio()->Play(TEXT("zap"), 0.f);
	}
	else if (Kind == TEXT("paradrop"))
	{
		const TCHAR* Ids[] = { TEXT("a_gi"), TEXT("a_gi"), TEXT("a_gi"), TEXT("a_gi"), TEXT("a_gi"), TEXT("a_jav"), TEXT("a_jav"), TEXT("a_rocket"), TEXT("a_rocket") };
		for (int32 K = 0; K < 9; ++K)
		{
			const FVector2D At = Target + FVector2D(RS::Rand(-1.6f, 1.6f), RS::Rand(-1.6f, 1.6f));
			const FIntPoint C = NearestFree(RS::ToCell(At), 5);
			if (C.X < 0) continue;
			if (ARSUnit* U = SpawnUnit(P, Ids[K], RS::Cell2(C) + FVector2D(RS::Rand(0.2f, 0.8f), RS::Rand(0.2f, 0.8f)))) U->ParaT = 2.2f + K * 0.18f;
		}
		if (bMe) Reveal(Target, 5.f);
		Message(bMe ? TEXT("空降部隊出發！") : TEXT("警告：敵方空降部隊！"));
		Audio()->Play(TEXT("jet"), -2.f);
	}
	else if (Kind == TEXT("barrage") || Kind == TEXT("strait") || Kind == TEXT("df"))
	{
		const FVector2D Src = RS::Cell2(P->StartCell);
		const bool bB = Kind == TEXT("barrage"), bS = Kind == TEXT("strait");
		const int32 N = bB ? 10 : (bS ? 8 : 6);
		const float R = Sup.Radius;
		for (int32 K = 0; K < N; ++K)
		{
			FRSProj Pj;
			Pj.Kind = TEXT("ballistic");
			Pj.From = Src + FVector2D(RS::Rand(-1, 1), RS::Rand(-1, 1));
			Pj.To = Target + RS::FromAngle(RS::Rand(0.f, 2.f * PI)) * RS::Rand01() * R;
			Pj.Z0 = 20.f;
			Pj.Z1 = 0.f;
			Pj.T = -K * (Kind != TEXT("df") ? 0.22f : 0.12f);
			Pj.Dur = bB ? 2.4f : (bS ? 1.6f : 1.1f);
			Pj.Dmg = bB ? 260.f : (bS ? 200.f : 420.f);
			Pj.Wh = bB ? TEXT("he") : (bS ? TEXT("topatk") : TEXT("antiship"));
			Pj.Splash = bB ? 1.4f : (bS ? 1.f : 1.6f);
			Pj.Arc = bB ? 420.f : (bS ? 160.f : 300.f);
			Pj.Owner = P;
			Fx->AddProj(Pj);
		}
		Message(bMe ? FString::Printf(TEXT("%s 發射！"), *Sup.Name) : FString::Printf(TEXT("警告：偵測到敵方%s！"), *Sup.Name));
		Audio()->Play(TEXT("missile"), 0.f);
	}
	else if (Kind == TEXT("militia") || Kind == TEXT("armor"))
	{
		ARSBuilding* Home = nullptr;
		const FString HR = Kind == TEXT("armor") ? TEXT("fac") : TEXT("cy");
		for (ARSBuilding* B : Buildings)
			if (B && B->Player == P && !B->bDead && B->Def->Role == HR) Home = B;
		if (!Home)
			for (ARSBuilding* B : Buildings)
				if (B && B->Player == P && !B->bDead) Home = B;
		if (!Home) return true;
		TArray<FString> Ids;
		if (Kind == TEXT("armor")) Ids = { TEXT("s_tank"), TEXT("s_tank"), TEXT("s_mammoth") };
		else Ids = { TEXT("s_con"), TEXT("s_con"), TEXT("s_con"), TEXT("s_con"), TEXT("s_con"), TEXT("s_con"), TEXT("s_flak"), TEXT("s_flak"), TEXT("s_con"), TEXT("s_con") };
		for (const FString& Id : Ids)
		{
			const FVector2D Ex = Home->Pos + FVector2D(0.f, Home->Size.Y * 0.5f + 1.f);
			const FIntPoint C = NearestFree(RS::ToCell(Ex), 6);
			if (C.X >= 0)
			{
				if (ARSUnit* U = SpawnUnit(P, Id, RS::CellCenter(C))) U->CmdMove(RS::CellCenter(C) + FVector2D(RS::Rand(-2, 2), RS::Rand(0, 3)));
			}
		}
		Message(bMe ? FString::Printf(TEXT("%s！部隊已在基地集結"), *Sup.Name) : FString(TEXT("警告：敵方大規模動員！")));
		Audio()->Play(TEXT("deploy"), 0.f);
	}
	else if (Kind == TEXT("allyaid"))
	{
		bool bFired = false;
		for (URSPlayer* Q : HumanAllies())
		{
			TArray<FString> Ks;
			Q->Powers.GetKeys(Ks);
			for (const FString& K : Ks)
			{
				if (bFired) break;
				if ((K == TEXT("nuke") || K == TEXT("storm") || K == TEXT("barrage") || K == TEXT("df") || K == TEXT("strait") || K == TEXT("swarm")) && Q->Powers[K] >= 1.f)
				{
					FirePower(Q, K, Target);
					bFired = true;
					Message(FString::Printf(TEXT("盟友 %s 發動「%s」支援！"), *Q->Name, *FRSDefs::Get().GetSuper(K).Name));
				}
			}
			if (bFired) break;
		}
		if (!bFired)
		{
			int32 N = 0;
			for (URSPlayer* Q : HumanAllies())
			{
				TArray<FString> Ids;
				if (Q->Faction == TEXT("allies")) Ids = { TEXT("a_tank"), TEXT("a_tank"), TEXT("a_ifv"), TEXT("a_rocket"), TEXT("a_gi") };
				else Ids = { TEXT("s_tank"), TEXT("s_tank"), TEXT("s_flaktrk"), TEXT("s_tesla"), TEXT("s_con") };
				for (const FString& Id : Ids)
				{
					const FIntPoint C = NearestFree(Q->StartCell + FIntPoint(RS::RandI(-3, 3), RS::RandI(-3, 3)), 6);
					if (C.X >= 0)
					{
						if (ARSUnit* U = SpawnUnit(Q, Id, RS::CellCenter(C)))
						{
							U->CmdMove(Target, true);
							N++;
						}
					}
				}
			}
			Message(FString::Printf(TEXT("盟友派出 %d 個單位支援！"), N));
		}
		Audio()->Play(TEXT("radio"), -2.f);
	}
	else if (Kind == TEXT("blockade"))
	{
		const float R = Sup.Radius;
		for (ARSUnit* U : TArray<ARSUnit*>(Units))
		{
			if (U && !U->bDead && P->IsEnemyOf(U->Player) && FVector2D::Distance(U->Pos, Target) <= R)
			{
				const FVector2D UP = U->Pos;
				U->ApplyDamage(U->UKind == TEXT("ship") ? 450.f : 120.f, nullptr, TEXT("he"));
				Fx->Explosion(UP, 1.2f);
			}
		}
		for (int32 K = 0; K < 6; ++K)
		{
			const FVector2D MP = Target + RS::FromAngle(RS::Rand(0.f, 2.f * PI)) * RS::Rand01() * R;
			After(RS::Rand(0.1f, 1.2f), [this, MP]() { Fx->Explosion(MP, 1.f); });
		}
		if (URSPlayer* TP2 = PlayerAt(Target, P)) TP2->SanctionedUntil = FMath::Max(TP2->SanctionedUntil, Time + 60.f);
		Message(bMe ? TEXT("海峽封鎖！敵方航運中斷，收入下降 60 秒") : TEXT("警告：敵方實施海峽封鎖！收入下降"));
		Audio()->Play(TEXT("big_explode"), 0.f);
	}
	Ping(Target);
	return true;
}

void ARSGame::FireNuke(URSPlayer* P, const FVector2D& Target)
{
	Message(TEXT("警告：偵測到核彈發射！"));
	Audio()->Play(TEXT("alarm"), 0.f);
	Audio()->Play(TEXT("missile"), 0.f);
	ARSBuilding* Silo = nullptr;
	for (ARSBuilding* B : Buildings)
		if (B && B->Player == P && B->Def->Super == TEXT("nuke") && !B->bDead) Silo = B;
	if (Silo)
	{
		FRSProj Up;
		Up.Kind = TEXT("nuke_up");
		Up.From = Silo->Pos;
		Up.To = Silo->Pos;
		Up.Z0 = 20.f;
		Up.Z1 = 900.f;
		Up.Dur = 1.5f;
		Fx->AddProj(Up);
	}
	FRSProj Down;
	Down.Kind = TEXT("nuke_down");
	Down.From = Target + FVector2D(-2, -2);
	Down.To = Target;
	Down.Z0 = 900.f;
	Down.Z1 = 0.f;
	Down.T = -3.f;
	Down.Dur = 1.f;
	Fx->AddProj(Down);
	After(4.f, [this, Target]()
	{
		Fx->NukeBlast(Target);
		DamageArea(Target, 4.5f, 1400.f, TEXT("he"), nullptr, nullptr, nullptr, true);
		for (int32 K = 0; K < 10; ++K)
		{
			const FVector2D TP = Target + FVector2D(RS::Rand(-3, 3), RS::Rand(-3, 3));
			After(RS::Rand(0.2f, 1.5f), [this, TP]() { Fx->Explosion(TP, 1.5f); });
		}
	});
}

void ARSGame::FireStorm(const FVector2D& Target)
{
	Message(TEXT("警告：雷暴打擊即將降臨！"));
	Audio()->Play(TEXT("alarm"), 0.f);
	Audio()->Play(TEXT("thunder"), 0.f);
	for (int32 K = 0; K < 34; ++K)
	{
		After(1.5f + K * 0.25f, [this, Target]()
		{
			const FVector2D TP = Target + RS::FromAngle(RS::Rand(0.f, 2.f * PI)) * FMath::Sqrt(RS::Rand01()) * 3.8f;
			Fx->Lightning(TP);
			Fx->Explosion(TP, 0.8f);
			DamageArea(TP, 1.4f, 260.f, TEXT("elec"), nullptr, nullptr, nullptr, true);
			if (RS::Rand01() < 0.35f) Audio()->Play(TEXT("thunder"), -4.f, &TP);
		});
	}
}

bool ARSGame::CanGarrison(const ARSUnit* U, const ARSBuilding* B) const
{
	if (!U || !B || B->Def->Garrison <= 0 || U->UKind != TEXT("inf") || !U->HasWeapon()) return false;
	if (B->Occupants.Num() >= B->Def->Garrison) return false;
	return B->Player->bNeutral || B->Player == U->Player;
}

void ARSGame::GarrisonEnter(ARSUnit* U, ARSBuilding* B)
{
	if (!CanGarrison(U, B))
	{
		U->CmdStop();
		return;
	}
	FRSOccupant O;
	O.Id = U->TypeId;
	O.Hp = U->Hp;
	O.Rank = U->Rank;
	O.Kills = U->Kills;
	O.W = U->Weapon;
	B->Occupants.Add(O);
	if (B->Player != U->Player)
	{
		B->Player = U->Player;
		B->CaptureFlash = 0.6f;
		B->RefreshTeamColor();
	}
	if (U->Player == Human) Audio()->Play(TEXT("click"), -4.f);
	RemoveUnitSilent(U);
}

void ARSGame::GarrisonExit(ARSBuilding* B)
{
	URSPlayer* P = B->Player;
	for (const FRSOccupant& O : B->Occupants)
	{
		const FIntPoint C = NearestFree(B->ExitCell(), 4);
		if (C.X < 0) continue;
		if (ARSUnit* U = SpawnUnit(P, O.Id, RS::Cell2(C) + FVector2D(RS::Rand(0.2f, 0.8f), RS::Rand(0.2f, 0.8f))))
		{
			U->Hp = FMath::Min(U->MaxHp, O.Hp);
			U->Rank = O.Rank;
			U->Kills = O.Kills;
		}
	}
	B->Occupants.Reset();
	B->Player = Neutral;
	B->RefreshTeamColor();
}

void ARSGame::UpdateCrates(float Dt)
{
	if (Mission || !Settings.bCrates) return;
	CrateT -= Dt;
	if (CrateT <= 0.f && Crates.Num() < 6)
	{
		CrateT = RS::Rand(35.f, 60.f);
		for (int32 Attempt = 0; Attempt < 30; ++Attempt)
		{
			const FIntPoint C(RS::RandI(4, W - 5), RS::RandI(4, H - 5));
			if (Map->Passable(C.X, C.Y) && !BldAt(C.X, C.Y) && !Map->HasOre(C.X, C.Y))
			{
				static const TCHAR* Kinds[] = { TEXT("money"), TEXT("money"), TEXT("money"), TEXT("heal"), TEXT("veteran"), TEXT("reveal"), TEXT("unit") };
				FRSCrate Cr;
				Cr.Cell = C;
				Cr.Kind = Kinds[RS::RandI(0, 6)];
				Cr.Mesh = RSV::AddPart(this, RootComponent, RSV::Cube(), ToWorld(RS::CellCenter(C), 20.f), FVector(30, 30, 30),
					Cr.Kind == TEXT("money") || Cr.Kind == TEXT("unit") ? FLinearColor(0.75f, 0.6f, 0.25f) : FLinearColor(0.3f, 0.8f, 0.9f));
				Cr.Mesh->SetVisibility(IsExplored(C.X, C.Y));
				Crates.Add(Cr);
				break;
			}
		}
	}
	for (int32 I = Crates.Num() - 1; I >= 0; --I)
	{
		for (ARSUnit* U : Units)
		{
			if (!U || U->bDead || U->UKind == TEXT("air") || U->UKind == TEXT("ship") || U->Player->bNeutral) continue;
			if (int32(U->Pos.X) == Crates[I].Cell.X && int32(U->Pos.Y) == Crates[I].Cell.Y)
			{
				PickCrate(U, I);
				break;
			}
		}
	}
}

void ARSGame::UpdateCrateVisuals(float Dt)
{
	for (FRSCrate& C : Crates)
	{
		C.T += Dt;
		if (!C.Mesh) continue;
		C.Mesh->SetWorldLocation(ToWorld(RS::CellCenter(C.Cell), 22.f + FMath::Sin(C.T * 3.f) * 6.f));
		C.Mesh->SetWorldRotation(FRotator(0.f, C.T * 60.f, 0.f));
		C.Mesh->SetVisibility(IsExplored(C.Cell.X, C.Cell.Y));
	}
}

void ARSGame::PickCrate(ARSUnit* U, int32 CrateIndex)
{
	const FRSCrate Cr = Crates[CrateIndex];
	Crates.RemoveAt(CrateIndex);
	if (Cr.Mesh) Cr.Mesh->DestroyComponent();
	URSPlayer* P = U->Player;
	FString Txt;
	if (Cr.Kind == TEXT("money"))
	{
		const int32 Amt = RS::RandI(8, 20) * 100;
		P->Money += Amt;
		Txt = FString::Printf(TEXT("補給箱：獲得 $%d"), Amt);
		Audio()->Play(TEXT("cash"), -2.f, &U->Pos);
	}
	else if (Cr.Kind == TEXT("heal"))
	{
		for (ARSUnit* O : Units)
			if (O && O->Player == P && FVector2D::Distance(O->Pos, U->Pos) < 5.f) O->Hp = O->MaxHp;
		Txt = TEXT("補給箱：附近單位完全修復");
		Fx->ScanRing(U->Pos, FLinearColor(0.4f, 1.f, 0.5f));
	}
	else if (Cr.Kind == TEXT("veteran"))
	{
		for (ARSUnit* O : Units)
		{
			if (O && O->Player == P && FVector2D::Distance(O->Pos, U->Pos) < 4.f && O->Rank < 2)
			{
				O->Rank++;
				O->Kills = FMath::Max(O->Kills, 3 * O->Rank);
			}
		}
		Txt = TEXT("補給箱：附近單位晉升！");
		Fx->ScanRing(U->Pos, FLinearColor(1.f, 0.85f, 0.3f));
	}
	else if (Cr.Kind == TEXT("reveal"))
	{
		if (P == Human) Reveal(U->Pos, 30.f);
		Txt = TEXT("補給箱：揭露地圖");
	}
	else
	{
		SpawnUnit(P, P->Faction == TEXT("allies") ? TEXT("a_tank") : TEXT("s_tank"), U->Pos + FVector2D(0.8f, 0.8f));
		Txt = TEXT("補給箱：獲得一輛坦克！");
	}
	if (P == Human) Message(Txt);
}

void ARSGame::DroneDetonate(ARSUnit* U, ARSEntity* T)
{
	const FVector2D TP = T->bIsBuilding ? T->ClosestPoint(U->Pos) : T->Pos;
	const FRSWeapon* Wp = U->Weapon;
	Fx->Explosion(TP, U->TypeId != TEXT("a_fpv") ? 1.3f : 0.9f, 4.f);
	if (Wp) DamageArea(TP, Wp->Splash > 0.f ? Wp->Splash : 0.6f, Wp->Dmg * U->DmgMult(), Wp->Wh, U, T);
	RemoveUnitSilent(U);
}

void ARSGame::UpdateEw(float Dt)
{
	EwT -= Dt;
	if (EwT > 0.f) return;
	EwT = 0.25f;
	struct FSrc { URSPlayer* P; FVector2D Pos; float R; };
	TArray<FSrc> Srcs;
	for (ARSUnit* U : Units)
		if (U && !U->bDead && U->Def->Ew > 0.f) Srcs.Add({ U->Player, U->Pos, U->Def->Ew });
	for (ARSBuilding* B : Buildings)
		if (B && !B->bDead && B->Def->Ew > 0.f && B->Built() && !B->Player->bLowPower) Srcs.Add({ B->Player, B->Pos, B->Def->Ew });
	if (Srcs.Num() == 0) return;
	for (ARSUnit* U : TArray<ARSUnit*>(Units))
	{
		if (!U || U->bDead || !U->Def->bDrone) continue;
		for (const FSrc& S : Srcs)
		{
			if (S.P->IsEnemyOf(U->Player) && FVector2D::Distance(U->Pos, S.Pos) <= S.R)
			{
				U->JamT = 0.3f;
				U->Hp -= 45.f * 0.25f;
				if (RS::Rand01() < 0.3f && U->bShown) Fx->Spark(U->Pos, U->Alt, FLinearColor(0.5f, 1.f, 0.6f));
				if (U->Hp <= 0.f)
				{
					U->Hp = 0.f;
					U->Die();
				}
				break;
			}
		}
	}
}

void ARSGame::Message(const FString& T)
{
	FRSMessage M;
	M.Text = T;
	Msgs.Add(M);
	if (Msgs.Num() > 6) Msgs.RemoveAt(0);
}

void ARSGame::Ping(const FVector2D& P)
{
	FRSPing Pg;
	Pg.P = P;
	Pings.Add(Pg);
}

// ───────────────────────── 迷霧 ─────────────────────────
bool ARSGame::IsExplored(int32 X, int32 Y) const
{
	if (X < 0 || Y < 0 || X >= W || Y >= H || Explored.Num() == 0) return false;
	return Explored[Y * W + X] == 1;
}

void ARSGame::Reveal(const FVector2D& C, float R)
{
	const float R2 = R * R;
	TArray<int32> Changed;
	for (int32 Y = int32(C.Y - R); Y <= int32(C.Y + R); ++Y)
	{
		if (Y < 0 || Y >= H) continue;
		for (int32 X = int32(C.X - R); X <= int32(C.X + R); ++X)
		{
			if (X < 0 || X >= W) continue;
			const int32 I = Y * W + X;
			if (Explored[I] == 0 && FVector2D(X + 0.5f - C.X, Y + 0.5f - C.Y).SizeSquared() <= R2)
			{
				Explored[I] = 1;
				Changed.Add(I);
			}
		}
	}
	if (Changed.Num() > 0 && Terrain) Terrain->RevealCells(Changed);
}

void ARSGame::RevealEntity(ARSEntity* E)
{
	const float R = E->bIsBuilding ? E->Sight + 1.5f : E->Sight * SightMult();
	if (ARSBuilding* B = Cast<ARSBuilding>(E)) Reveal(E->Pos, R + B->Size.X * 0.5f);
	else Reveal(E->Pos, R);
}

void ARSGame::RevealAllNow()
{
	for (ARSUnit* U : Units)
		if (U && U->Player == Human) RevealEntity(U);
	for (ARSBuilding* B : Buildings)
		if (B && B->Player == Human) RevealEntity(B);
	Map->bOreDirty = true;
}

void ARSGame::UpdateVisibility()
{
	for (ARSUnit* U : Units)
	{
		if (!U || U->bDead) continue;
		if (U->Player == Human || Human->Allies.Contains(U->Player->Index))
		{
			const int32 K = int32(U->Pos.X) * 1000 + int32(U->Pos.Y);
			if (U->RevealKey != K)
			{
				U->RevealKey = K;
				RevealEntity(U);
			}
			U->SetShown(true);
		}
		else
		{
			U->SetShown(IsExplored(int32(U->Pos.X), int32(U->Pos.Y)));
		}
	}
	for (ARSBuilding* B : Buildings)
	{
		if (!B || B->bDead) continue;
		if (B->Player == Human || Human->Allies.Contains(B->Player->Index))
		{
			if (!B->bRevealed)
			{
				B->bRevealed = true;
				RevealEntity(B);
			}
			B->SetShown(true);
		}
		else
		{
			B->SetShown(IsExplored(B->Cell.X, B->Cell.Y) || IsExplored(B->Cell.X + B->Size.X - 1, B->Cell.Y + B->Size.Y - 1));
		}
	}
}

// ───────────────────────── 主迴圈 ─────────────────────────
void ARSGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Map.IsValid()) return;
	for (FRSMessage& M : Msgs) M.T += DeltaSeconds;
	Msgs.RemoveAll([](const FRSMessage& M) { return M.T >= 7.f; });
	for (FRSPing& P : Pings) P.T += DeltaSeconds;
	Pings.RemoveAll([](const FRSPing& P) { return P.T >= 3.f; });
	const bool bModal = bPausePanel || bDiploPanel || bSavePanel;
	if (!bPaused && !bGameOver && !bModal)
	{
		const float Dt = FMath::Min(DeltaSeconds, 0.05f) * Speed;
		Step(Dt);
	}
	// 外觀更新（暫停時也維持鏡頭與畫面）
	const float VDt = (bPaused || bGameOver || bModal) ? 0.f : FMath::Min(DeltaSeconds, 0.05f) * Speed;
	for (ARSUnit* U : Units)
		if (U && !U->bDead && U->bShown) U->UpdateVisual(VDt);
	for (ARSBuilding* B : Buildings)
		if (B && !B->bDead) B->UpdateVisual(VDt);
	UpdateCrateVisuals(VDt);
	if (Fx && VDt > 0.f) Fx->UpdateFx(VDt);
	if (VDt > 0.f) UpdateWrecks(VDt);
	if (bCinema && !bGameOver) UpdateCinema(FMath::Min(DeltaSeconds, 0.05f));
	UpdateEnvironment((bPaused || bGameOver || bModal) ? 0.f : FMath::Min(DeltaSeconds, 0.05f) * Speed, VDt);
	if (Terrain) Terrain->UpdateTerrain(DeltaSeconds);
	if (Cam && Fx)
	{
		Cam->ShakeOffset = Fx->ShakeOffset();
		Cam->ApplyCamera();
	}
}

void ARSGame::Step(float Dt)
{
	Time += Dt;
	RebuildBuckets();
	for (URSPlayer* P : Players) P->Update(Dt);
	for (ARSUnit* U : TArray<ARSUnit*>(Units))
		if (IsValid(U) && !U->bDead) U->Update(Dt);
	for (ARSBuilding* B : TArray<ARSBuilding*>(Buildings))
		if (IsValid(B) && !B->bDead) B->Update(Dt);
	UpdateEw(Dt);
	UpdateDiplomacy(Dt);
	if (WorldEv) WorldEv->Update(Dt);
	Map->Update(Dt);
	for (URSAI* AI : AIs) AI->Update(Dt);
	for (int32 I = Events.Num() - 1; I >= 0; --I)
	{
		if (I < Events.Num() && Events[I].T <= Time)
		{
			TFunction<void()> Fn = MoveTemp(Events[I].Fn);
			Events.RemoveAt(I);
			if (Fn) Fn();
		}
	}
	for (int32 I = Markers.Num() - 1; I >= 0; --I)
	{
		Markers[I].T += Dt;
		if (Markers[I].T > 0.8f) Markers.RemoveAt(I);
	}
	RevealT -= Dt;
	if (RevealT <= 0.f)
	{
		RevealT = 0.25f;
		UpdateVisibility();
	}
	CheckT -= Dt;
	if (CheckT <= 0.f)
	{
		CheckT = 1.f;
		CheckVictory();
		UpdateCrates(1.f);
	}
	AckT -= Dt;
}

void ARSGame::CheckVictory()
{
	for (URSPlayer* P : Players)
	{
		if (P->bNeutral || !P->bAlive) continue;
		if (Mission && Mission->Id == TEXT("m1") && P == Human) continue;
		bool bHas = false;
		for (ARSBuilding* B : Buildings)
			if (B && B->Player == P && !B->bDead && !B->Def->bWall && !B->Def->bCiv) { bHas = true; break; }
		if (!bHas)
			for (ARSUnit* U : Units)
				if (U && U->Player == P && !U->bDead && !U->Def->Mcv.IsEmpty()) { bHas = true; break; }
		if (!bHas)
		{
			P->bAlive = false;
			if (P != Human) Message(FString::Printf(TEXT("%s 已被消滅！"), *P->Name));
			for (ARSUnit* U : TArray<ARSUnit*>(Units))
				if (U && U->Player == P && !U->bDead) U->ApplyDamage(1e6f, nullptr, TEXT("he"));
			for (ARSBuilding* B : TArray<ARSBuilding*>(Buildings))
				if (B && B->Player == P && !B->bDead) B->ApplyDamage(1e6f, nullptr, TEXT("he"));
		}
	}
	if (Mission)
	{
		const FString R = Mission->Check();
		if (!R.IsEmpty()) EndGame(R == TEXT("win"));
		return;
	}
	if (!Human->bAlive)
	{
		EndGame(false);
		return;
	}
	bool bAnyEnemy = false;
	for (URSPlayer* P : Players)
		if (P->bAlive && !P->bNeutral && P->Team != Human->Team && !Human->Allies.Contains(P->Index)) bAnyEnemy = true;
	if (!bAnyEnemy) EndGame(true);
}

void ARSGame::EndGame(bool bWin)
{
	if (bGameOver) return;
	bGameOver = true;
	Result = bWin ? TEXT("win") : TEXT("lose");
	CmdMode.Empty();
	PlaceId.Empty();
	if (bWin && Mission) FRSUserSettings::Get().MarkMissionDone(Mission->Id);
	FreshAch = FRSRecords::Get().OnGameEnd(this, bWin);
	if (FreshAch.Num() > 0) Audio()->Play(TEXT("cash"), -4.f);
	Audio()->Play(bWin ? TEXT("cash") : TEXT("alarm"), -2.f);
}

void ARSGame::EnableAutopilot()
{
	URSAI* AI = NewObject<URSAI>(this);
	AI->Setup(this, Human, 1);
	AIs.Add(AI);
}

// ───────────────────────── 日夜循環與天氣 ─────────────────────────
float ARSGame::SightMult() const
{
	const float Wm = Weather == 2 ? 0.72f : ((Weather == 1 || Weather == 3) ? 0.87f : 1.f);
	return (1.f - 0.28f * Darkness) * Wm;
}

FString ARSGame::EnvLabel() const
{
	const int32 Mins = int32(FMath::Fmod(Tod, 1.f) * 24.f * 60.f);
	static const TCHAR* WN[] = { TEXT("晴"), TEXT("雨"), TEXT("霧"), TEXT("雪") };
	const TCHAR* Part = Darkness > 0.6f ? TEXT("夜") : (Darkness > 0.15f ? TEXT("昏") : TEXT("晝"));
	FString S = FString::Printf(TEXT("%02d:%02d %s %s"), Mins / 60, Mins % 60, Part, WN[FMath::Clamp(Weather, 0, 3)]);
	if (Settings.Map.Theme == TEXT("desert") && Weather == 2) S = FString::Printf(TEXT("%02d:%02d %s 沙塵"), Mins / 60, Mins % 60, Part);
	return S;
}

void ARSGame::UpdateEnvironment(float Dt, float VDt)
{
	// 一天 = 10 分鐘遊戲時間
	Tod = FMath::Fmod(Tod + Dt / 600.f, 1.f);
	const float Ang = (Tod - 0.25f) * 2.f * PI;
	const float Elev = FMath::Sin(Ang);                 // 太陽高度 -1..1
	Darkness = FMath::Clamp(1.f - (Elev + 0.12f) / 0.32f, 0.f, 1.f);
	if (UDirectionalLightComponent* L = SunL.Get())
	{
		AActor* SunA = L->GetOwner();
		const bool bDay = Elev > -0.05f;
		const float Pitch = bDay ? -(12.f + 70.f * FMath::Clamp(Elev, 0.f, 1.f)) : -50.f;
		const float Yaw = bDay ? 35.f + (Tod - 0.5f) * 220.f : 215.f;
		if (SunA) SunA->SetActorRotation(FRotator(Pitch, Yaw, 0.f));
		const float DayK = FMath::Clamp(Elev * 2.5f, 0.f, 1.f);
		const float Ws = Weather == 0 ? 1.f : (Weather == 2 ? 0.55f : 0.65f);
		L->SetIntensity(FMath::Lerp(1.1f, SunBaseI * Ws, DayK));
		const FLinearColor Dusk(1.f, 0.58f, 0.32f), Night(0.5f, 0.6f, 1.f), Noon(1.f, 0.97f, 0.92f);
		L->SetLightColor(Darkness > 0.5f ? Night : FMath::Lerp(Dusk, Noon, FMath::Clamp(Elev * 3.f, 0.f, 1.f)));
	}
	if (USkyLightComponent* SL = SkyLC.Get()) SL->SetIntensity(FMath::Lerp(1.f, 0.3f, Darkness) * (Weather == 0 ? 1.f : 0.85f));
	// 自動曝光會把夜晚拉亮，用曝光補償與色調把夜色壓下來
	if (Cam && Cam->Camera)
	{
		FPostProcessSettings& PP = Cam->Camera->PostProcessSettings;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = -2.4f * Darkness - (Weather == 0 ? 0.f : 0.3f);
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f - 0.4f * Darkness);
		PP.bOverride_SceneColorTint = true;
		PP.SceneColorTint = FMath::Lerp(FLinearColor::White, FLinearColor(0.72f, 0.82f, 1.f), Darkness);
	}
	// 天氣狀態機
	WeatherT -= Dt;
	if (WeatherT <= 0.f)
	{
		const FString& Th = Settings.Map.Theme;
		const float R = RS::Rand01();
		int32 NW = 0;
		if (Th == TEXT("snow")) NW = R < 0.45f ? 3 : (R < 0.6f ? 2 : 0);
		else if (Th == TEXT("desert")) NW = R < 0.3f ? 2 : 0;
		else NW = R < 0.3f ? 1 : (R < 0.45f ? 2 : 0);
		if (NW == Weather) NW = 0;
		Weather = NW;
		WeatherT = NW == 0 ? RS::Rand(90.f, 180.f) : RS::Rand(60.f, 120.f);
		static const TCHAR* Msg[] = { TEXT("氣象：天氣轉晴"), TEXT("氣象：開始下雨，視野降低"), TEXT("氣象：濃霧來襲，視野大幅降低"), TEXT("氣象：開始下雪，視野降低") };
		FString M = Msg[Weather];
		if (Weather == 2 && Th == TEXT("desert")) M = TEXT("氣象：沙塵暴來襲，視野大幅降低");
		Message(M);
	}
	if (UExponentialHeightFogComponent* F = FogC.Get())
	{
		const float Target = FogBase * (Weather == 2 ? 7.f : (Weather == 0 ? 1.f : 2.4f)) * (1.f + Darkness * 0.6f);
		F->SetFogDensity(FMath::FInterpTo(F->FogDensity, Target, FMath::Max(VDt, 0.016f), 0.6f));
		if (Weather == 2 && Settings.Map.Theme == TEXT("desert")) F->SetFogInscatteringColor(FLinearColor(0.75f, 0.6f, 0.4f));
	}
	// 夜間建築燈光（每 0.5 秒更新）
	NightLightT -= FMath::Max(VDt, 0.016f);
	if (NightLightT <= 0.f)
	{
		NightLightT = 0.5f;
		for (ARSBuilding* B : Buildings)
			if (B && !B->bDead && B->NightLight)
				B->NightLight->SetIntensity(B->bShown ? Darkness * 14.f : 0.f);
	}
	// 降水粒子
	if (PrecipISM)
	{
		const bool bPrecip = (Weather == 1 || Weather == 3) && Cam;
		const int32 N = Weather == 3 ? 450 : 600;
		if (!bPrecip)
		{
			if (PrecipISM->GetInstanceCount() > 0) PrecipISM->ClearInstances();
			PrecipP.Reset();
		}
		else
		{
			const FVector C = Cam->GetFocusWorld();
			const float Half = 1700.f;
			if (PrecipP.Num() != N)
			{
				PrecipISM->ClearInstances();
				PrecipP.SetNum(N);
				TArray<FTransform> Init;
				for (int32 K = 0; K < N; ++K)
				{
					PrecipP[K] = C + FVector(RS::Rand(-Half, Half), RS::Rand(-Half, Half), RS::Rand(0.f, 1000.f));
					Init.Add(FTransform(PrecipP[K]));
				}
				PrecipISM->AddInstances(Init, false, true);
			}
			const bool bSnow = Weather == 3;
			const FVector V = bSnow ? FVector(40.f, 25.f, -160.f) : FVector(120.f, 60.f, -1500.f);
			const FVector Sc = bSnow ? FVector(0.045f) : FVector(0.012f, 0.012f, 0.32f);
			const FRotator Rot = bSnow ? FRotator::ZeroRotator : FRotator(-8.f, 30.f, 0.f);
			TArray<FTransform> Xf;
			Xf.SetNum(N);
			const float Step = FMath::Max(VDt, 0.f);
			for (int32 K = 0; K < N; ++K)
			{
				FVector& P = PrecipP[K];
				P += V * Step;
				if (bSnow) P.X += FMath::Sin(EnvT * 1.3f + K) * 20.f * Step;
				if (P.Z < 0.f || FMath::Abs(P.X - C.X) > Half || FMath::Abs(P.Y - C.Y) > Half)
					P = C + FVector(RS::Rand(-Half, Half), RS::Rand(-Half, Half), RS::Rand(800.f, 1100.f));
				Xf[K] = FTransform(Rot, P, Sc);
			}
			EnvT += Step;
			PrecipISM->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
		}
	}
}

// ───────────────────────── 掩體 ─────────────────────────
ARSEntity* ARSGame::FireBlocker(const FVector2D& From, const FVector2D& To, const ARSEntity* Att, const ARSEntity* T) const
{
	if (!Att || !Att->Player) return nullptr;
	const float D = FVector2D::Distance(From, To);
	if (D < 1.2f) return nullptr;
	const FVector2D Dir = (To - From) / D;
	for (float S = 0.7f; S < D - 0.45f; S += 0.25f)
	{
		const FVector2D P = From + Dir * S;
		ARSBuilding* B = BldAt(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y));
		if (!B || B->bDead || B == T || B == Att) continue;
		if (B->Player == Att->Player) continue;
		const bool bNeutral = B->Player && B->Player->bNeutral;
		if (bNeutral || Att->Player->IsEnemyOf(B->Player)) return B;
	}
	return nullptr;
}

// ───────────────────────── 戰損：車輛殘骸 ─────────────────────────
void ARSGame::MakeWreck(ARSUnit* U)
{
	if (!U) return;
	// 殘骸上限：太多時移除最舊的
	while (Wrecks.Num() >= 40)
	{
		if (ARSUnit* Old = Wrecks[0].U.Get()) Old->Destroy();
		Wrecks.RemoveAt(0);
	}
	TArray<UStaticMeshComponent*> Comps;
	U->GetComponents<UStaticMeshComponent>(Comps);
	for (UStaticMeshComponent* C : Comps)
	{
		if (!C || !C->IsVisible()) continue;
		const float V = RS::Rand(0.035f, 0.075f);
		RSV::SetColor(C, FLinearColor(V * 1.1f, V, V * 0.9f));
	}
	if (U->Body) U->Body->AddLocalRotation(FRotator(RS::Rand(-7.f, 7.f), RS::Rand(-12.f, 12.f), RS::Rand(-9.f, 9.f)));
	// 一半機率砲塔被炸飛、歪倒在一旁
	if (U->TurretPivot && RS::Rand01() < 0.5f)
	{
		U->TurretPivot->AddLocalOffset(FVector(RS::Rand(-14.f, 14.f), RS::Rand(-14.f, 14.f), -3.f));
		U->TurretPivot->AddLocalRotation(FRotator(RS::Rand(-25.f, 25.f), RS::Rand(-90.f, 90.f), RS::Rand(-20.f, 20.f)));
	}
	if (U->Spinner) U->Spinner->SetVisibility(false, true);
	U->AddActorWorldOffset(FVector(0, 0, -3.f));
	U->bSelected = false;
	FRSWreck Wk;
	Wk.U = U;
	Wrecks.Add(Wk);
}

void ARSGame::UpdateWrecks(float Dt)
{
	for (int32 I = Wrecks.Num() - 1; I >= 0; --I)
	{
		FRSWreck& Wk = Wrecks[I];
		ARSUnit* U = Wk.U.Get();
		if (!U)
		{
			Wrecks.RemoveAt(I);
			continue;
		}
		Wk.T += Dt;
		Wk.SmokeT -= Dt;
		if (Wk.T < 20.f && Wk.SmokeT <= 0.f && U->bShown)
		{
			Wk.SmokeT = Wk.T < 6.f ? 0.22f : 0.5f;
			Fx->Smoke(U->Pos, 14.f, Wk.T < 6.f);
		}
		if (Wk.T > 45.f) U->AddActorWorldOffset(FVector(0, 0, -6.f * Dt));   // 慢慢沉入地面後移除
		if (Wk.T > 50.f)
		{
			U->Destroy();
			Wrecks.RemoveAt(I);
		}
	}
}

// ───────────────────────── 電影鏡頭 ─────────────────────────
void ARSGame::AddHot(const FVector2D& P, float W, const FString& Label)
{
	// 合併附近的事件，避免陣列暴增
	for (FRSHot& H2 : Hot)
		if (H2.T < 2.f && FVector2D::DistSquared(H2.P, P) < 2.25f)
		{
			H2.W += W;
			H2.T = 0.f;
			if (!Label.IsEmpty()) H2.Label = Label;
			return;
		}
	if (Hot.Num() > 200) Hot.RemoveAt(0);
	FRSHot N;
	N.P = P;
	N.W = W;
	N.Label = Label;
	Hot.Add(N);
}

void ARSGame::ToggleCinema()
{
	if (!Cam) return;
	bCinema = !bCinema;
	if (bCinema)
	{
		CineSaveZoom = Cam->Zoom;
		const FVector F = Cam->GetFocusWorld();
		CineFocus = CineTarget = FVector2D(F.X / RS_CELL, F.Y / RS_CELL);
		CineShotT = 0.f;
		CineLabel.Empty();
	}
	else
	{
		Cam->Yaw = 225.f;
		Cam->Pitch = -52.f;
		Cam->SetZoom(CineSaveZoom);
	}
}

void ARSGame::UpdateCinema(float RealDt)
{
	if (!Cam) return;
	for (FRSHot& H2 : Hot) H2.T += RealDt;
	Hot.RemoveAll([](const FRSHot& H2) { return H2.T > 12.f; });
	CineShotT -= RealDt;
	if (CineShotT <= 0.f)
	{
		// 導播：挑最近 8 秒內、4 格範圍內權重最高的戰鬥
		float Best = 0.f;
		FVector2D BestP = CineTarget;
		FString BestL;
		for (const FRSHot& A : Hot)
		{
			float Sc = 0.f;
			FString L;
			for (const FRSHot& B : Hot)
				if (FVector2D::DistSquared(A.P, B.P) < 16.f)
				{
					const float Fresh = FMath::Clamp(1.f - B.T / 8.f, 0.f, 1.f);
					Sc += B.W * Fresh;
					if (!B.Label.IsEmpty() && B.T < 6.f) L = B.Label;
				}
			// 偏好切到新的地點，畫面才有變化
			if (FVector2D::Distance(A.P, CineTarget) < 3.f) Sc *= 0.75f;
			if (Sc > Best) { Best = Sc; BestP = A.P; BestL = L; }
		}
		if (Best < 1.5f)
		{
			// 沒有戰鬥：隨機看一支自己的部隊或基地
			TArray<FVector2D> Cand;
			for (ARSUnit* U : Units) if (U && U->Player == Human && U->UKind != TEXT("air")) Cand.Add(U->Pos);
			for (ARSBuilding* B : Buildings) if (B && B->Player == Human) Cand.Add(B->Pos);
			if (Cand.Num() > 0) BestP = Cand[FMath::RandRange(0, Cand.Num() - 1)];
			BestL.Empty();
			CineShotT = 7.f;
		}
		else
		{
			CineShotT = 6.f;
		}
		CineTarget = BestP;
		CineLabel = BestL;
	}
	// 平滑移動、緩慢環繞、低角度拉近
	const float A = 1.f - FMath::Exp(-1.4f * RealDt);
	CineFocus = FMath::Lerp(CineFocus, CineTarget, A);
	Cam->SetFocusWorld(FVector(CineFocus.X * RS_CELL, CineFocus.Y * RS_CELL, 0.f));
	Cam->Yaw = FMath::Fmod(Cam->Yaw + 7.f * RealDt, 360.f);
	Cam->Pitch = FMath::Lerp(Cam->Pitch, -27.f, A);
	Cam->Zoom = FMath::Lerp(Cam->Zoom, 1.75f, A);
	Cam->ApplyCamera();
}
