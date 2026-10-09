#include "RSGameMode.h"
#include "RSCommon.h"
#include "RSDefs.h"
#include "RSGame.h"
#include "RSMap.h"
#include "RSAudio.h"
#include "RSCameraPawn.h"
#include "RSPlayerController.h"
#include "RSHUD.h"
#include "RSSaveLoad.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "MediaSoundComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "RSPlayer.h"
#include "RSUnit.h"
#include "RSBuilding.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"

ARSGameMode::ARSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = ARSCameraPawn::StaticClass();
	PlayerControllerClass = ARSPlayerController::StaticClass();
	HUDClass = ARSHUD::StaticClass();
}

void ARSGameMode::BeginPlay()
{
	Super::BeginPlay();
	FRSDefs::Get();
	FRSUserSettings::Get();
	FRSRecords::Get();
	Audio = NewObject<URSAudio>(this);
	Audio->Init(GetWorld());
	bAutoTest = FParse::Param(FCommandLine::Get(), TEXT("RSAutoTest"));
	if (bAutoTest) { UE_LOG(LogRS, Display, TEXT("AUTOTEST begin")); PlayIntro(); return; }
	bGallery = FParse::Param(FCommandLine::Get(), TEXT("RSGallery"));
	if (bGallery) { ToMenu(); return; }
	if (FRSUserSettings::Get().bIntroSeen) ToMenu();
	else PlayIntro();
}

ARSCameraPawn* ARSGameMode::GetCam() const
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? Cast<ARSCameraPawn>(PC->GetPawn()) : nullptr;
}

void ARSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StateT += DeltaSeconds;
	if (Audio) Audio->Tick(DeltaSeconds);
	if (bAutoTest) AutoTestTick(DeltaSeconds);
	if (bGallery) GalleryTick(DeltaSeconds);
	if (State == ERSState::Intro)
	{
		// 影片播完或超時就結束片頭
		const bool bDone = bVideoOk ? (StateT > 2.f && !VideoPlaying()) : StateT > 62.f;
		if (bDone || StateT > 75.f) FinishIntro();
	}
}

FRSGameSettings ARSGameMode::DefaultSettings() const
{
	FRSGameSettings S;
	S.Faction = FRSUserSettings::Get().Faction;
	S.Country = TEXT("random");
	S.EnemyCountry = TEXT("random");
	S.EnemyFaction = TEXT("random");
	S.Map = FRSDefs::Get().Presets.Num() > 0 ? FRSDefs::Get().Presets[0] : FRSMapPreset();
	S.Seed = FMath::Rand();
	return S;
}

bool ARSGameMode::PlayVideo(const FString& File, bool bLoop, bool bSound)
{
	StopVideo();
	const FString Path = RS::ContentDir() / TEXT("Video") / File;
	if (!FPaths::FileExists(Path)) return false;
	MediaPlayer = NewObject<UMediaPlayer>(this);
	MediaPlayer->PlayOnOpen = true;
	MediaPlayer->SetLooping(bLoop);
	MediaTexture = NewObject<UMediaTexture>(this);
	MediaTexture->AutoClear = true;
	MediaTexture->ClearColor = FLinearColor::Black;
	MediaTexture->SetMediaPlayer(MediaPlayer);
	MediaTexture->UpdateResource();
	MediaSource = NewObject<UFileMediaSource>(this);
	MediaSource->SetFilePath(Path);
	if (bSound)
	{
		MediaSound = NewObject<UMediaSoundComponent>(this);
		MediaSound->SetMediaPlayer(MediaPlayer);
		MediaSound->RegisterComponent();
		MediaSound->Start();
	}
	VideoName = File;
	VideoStartT = StateT;
	bVideoOk = MediaPlayer->OpenSource(MediaSource);
	return bVideoOk;
}

void ARSGameMode::StopVideo()
{
	if (MediaPlayer) MediaPlayer->Close();
	if (MediaSound)
	{
		MediaSound->Stop();
		MediaSound->DestroyComponent();
	}
	MediaSound = nullptr;
	MediaPlayer = nullptr;
	MediaTexture = nullptr;
	MediaSource = nullptr;
	bVideoOk = false;
	VideoName.Empty();
}

bool ARSGameMode::VideoPlaying() const
{
	if (!MediaPlayer || !bVideoOk) return false;
	return MediaPlayer->IsPlaying() || MediaPlayer->IsPreparing() || MediaPlayer->IsBuffering();
}

void ARSGameMode::PlayIntro()
{
	DestroyGame();
	if (Audio)
	{
		Audio->StopAll();
		Audio->StopMusic();
	}
	State = ERSState::Intro;
	StateT = 0.f;
	if (!PlayVideo(TEXT("redstorm_intro_cinematic.mp4"), false, true))
	{
		if (Audio) Audio->Music(TEXT("intro"));
	}
}

void ARSGameMode::FinishIntro()
{
	if (State != ERSState::Intro) return;
	StopVideo();
	FRSUserSettings& US = FRSUserSettings::Get();
	US.bIntroSeen = true;
	US.Save();
	ToMenu();
}

void ARSGameMode::DestroyGame()
{
	if (Game)
	{
		Game->Destroy();
		Game = nullptr;
	}
}

void ARSGameMode::ToMenu()
{
	DestroyGame();
	if (Audio)
	{
		Audio->StopAll();
		Audio->Music(TEXT("menu"));
	}
	State = ERSState::Menu;
	StateT = 0.f;
	PlayVideo(TEXT("redstorm_bg_rotation.mp4"), true, false);
	if (ARSHUD* H = Cast<ARSHUD>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetHUD() : nullptr)) H->OnEnterMenu();
}

void ARSGameMode::StartGame(const FRSGameSettings& S)
{
	LastSettings = S;
	if (!S.Mission.IsEmpty() && !S.bFromSave && !S.bBriefShown)
	{
		const FRSMissionDef* M = FRSDefs::Get().FindMission(S.Mission);
		if (M && !M->Headline.IsEmpty())
		{
			DestroyGame();
			StopVideo();
			if (Audio)
			{
				Audio->StopAll();
				Audio->Music(TEXT("intro"));
				Audio->Play(TEXT("radio"), -6.f);
			}
			PendingSettings = S;
			PendingSettings.bBriefShown = true;
			BriefMission = M;
			State = ERSState::Briefing;
			StateT = 0.f;
			return;
		}
	}
	SpawnGame(S);
}

void ARSGameMode::FinishBriefing()
{
	if (State != ERSState::Briefing) return;
	SpawnGame(PendingSettings);
}

void ARSGameMode::SpawnGame(const FRSGameSettings& S)
{
	DestroyGame();
	StopVideo();
	if (Audio)
	{
		Audio->StopAll();
		Audio->StopMusic();
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Game = GetWorld()->SpawnActor<ARSGame>(ARSGame::StaticClass(), FTransform::Identity, SP);
	State = ERSState::Game;
	StateT = 0.f;
	Game->Start(S, Audio, GetCam());
	if (ARSHUD* H = Cast<ARSHUD>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetHUD() : nullptr)) H->OnEnterGame();
}

void ARSGameMode::RestartGame()
{
	FRSGameSettings S = LastSettings;
	S.bFromSave = false;
	StartGame(S);
}

bool ARSGameMode::LoadGame(int32 Slot)
{
	TSharedPtr<FJsonObject> D = RSSave::LoadData(Slot);
	if (!D.IsValid())
	{
		if (Game) Game->Message(TEXT("沒有存檔"));
		return false;
	}
	const TSharedPtr<FJsonObject>* SO = nullptr;
	if (!D->TryGetObjectField(TEXT("settings"), SO)) return false;
	FRSGameSettings S = FRSGameSettings::FromJson(*SO);
	S.bFromSave = true;
	S.bBriefShown = true;
	LastSettings = S;
	SpawnGame(S);
	RSSave::Apply(Game, D);
	return true;
}

void ARSGameMode::QuitGame()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
}

void ARSGameMode::AutoShot(const FString& Name)
{
	const FString P = FPaths::ProjectSavedDir() / TEXT("AutoTest") / (Name + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(P, true, false);
	UE_LOG(LogRS, Display, TEXT("AUTOTEST shot %s state=%d"), *Name, (int32)State);
}

void ARSGameMode::AutoStats(const TCHAR* Tag)
{
	if (!Game) { UE_LOG(LogRS, Display, TEXT("AUTOTEST [%s] no game"), Tag); return; }
	UE_LOG(LogRS, Display, TEXT("AUTOTEST [%s] t=%.1f units=%d bldg=%d over=%d"), Tag, Game->Time, Game->Units.Num(), Game->Buildings.Num(), Game->bGameOver ? 1 : 0);
	for (URSPlayer* P : Game->Players)
	{
		if (!P || P->bNeutral) continue;
		int32 U = 0, B = 0;
		for (ARSUnit* X : Game->Units) if (X && X->Player == P && !X->bDead) ++U;
		for (ARSBuilding* X : Game->Buildings) if (X && X->Player == P && !X->bDead) ++B;
		UE_LOG(LogRS, Display, TEXT("AUTOTEST   %s (%s) alive=%d money=%.0f units=%d bldg=%d"), *P->Name, *P->Country, P->bAlive ? 1 : 0, P->Money, U, B);
	}
}

void ARSGameMode::AutoTestTick(float Dt)
{
	AutoT += Dt;
	auto At = [&](float T) { return AutoT >= T; };
	switch (AutoStep)
	{
	case 0: if (At(5)) { AutoShot(TEXT("01_intro")); ++AutoStep; } break;
	case 1: if (At(7)) { FinishIntro(); ++AutoStep; } break;
	case 2: if (At(11)) { AutoShot(TEXT("02_menu")); ++AutoStep; } break;
	case 3: if (At(13))
		{
			FRSGameSettings S = DefaultSettings();
			S.Speed = 4.f;
			StartGame(S);
			if (Game) Game->EnableAutopilot();
			AutoNextStat = AutoT + 4.f;
			++AutoStep;
		} break;
	case 4: if (At(18)) { AutoShot(TEXT("03_skirmish_start")); ++AutoStep; } break;
	case 5: if (At(50)) { AutoShot(TEXT("04_skirmish_50")); ++AutoStep; } break;
	case 6: if (At(85)) { AutoShot(TEXT("05_skirmish_85")); ++AutoStep; } break;
	case 7: if (At(120)) { AutoShot(TEXT("06_skirmish_120")); ++AutoStep; } break;
	case 8: if (At(124)) { UE_LOG(LogRS, Display, TEXT("AUTOTEST save=%d"), Game ? (int32)RSSave::SaveGame(Game, 2) : -1); ++AutoStep; } break;
	case 9: if (At(126)) { UE_LOG(LogRS, Display, TEXT("AUTOTEST load=%d"), (int32)LoadGame(2)); if (Game) Game->EnableAutopilot(); ++AutoStep; } break;
	case 10: if (At(131)) { AutoStats(TEXT("after load")); AutoShot(TEXT("07_loaded")); ++AutoStep; } break;
	case 11: if (At(134))
		{
			const TArray<FRSMissionDef>& Ms = FRSDefs::Get().Missions;
			if (Ms.Num() > 0) StartGame(FRSGameSettings::FromMission(Ms[0], 1));
			++AutoStep;
		} break;
	case 12: if (At(139)) { AutoShot(TEXT("08_briefing")); ++AutoStep; } break;
	case 13: if (At(141)) { FinishBriefing(); if (Game) Game->EnableAutopilot(); ++AutoStep; } break;
	case 14: if (At(152)) { AutoStats(TEXT("mission")); AutoShot(TEXT("09_mission")); ++AutoStep; AutoQ = 0; AutoQT = 0.f; } break;
	case 15:
		{
			// 巡檢：每個任務、每張地圖（3 個敵人）各跑 10 秒，確認沒有當機
			const TArray<FRSMissionDef>& Ms = FRSDefs::Get().Missions;
			const TArray<FRSMapPreset>& Ps = FRSDefs::Get().Presets;
			const int32 Total = Ms.Num() + Ps.Num();
			if (AutoQT == 0.f || AutoT >= AutoQT)
			{
				if (AutoQT > 0.f && !bAutoShotDone)
				{
					bAutoShotDone = true;
					AutoQT = AutoT + 1.5f;   // 截圖在影格結束才拍，先等一下再換下一局
					const int32 Prev = AutoQ - 1;
					const FString Nm = Prev < Ms.Num() ? (TEXT("m_") + Ms[Prev].Id) : FString::Printf(TEXT("map_%d"), Prev - Ms.Num());
					AutoStats(*Nm);
					AutoShot(FString::Printf(TEXT("q%02d_%s"), Prev, *Nm));
					break;
				}
				bAutoShotDone = false;
				if (AutoQ >= Total) { AutoStep = 16; AutoQT = AutoT + 3.f; break; }
				if (AutoQ < Ms.Num())
				{
					FRSGameSettings S = FRSGameSettings::FromMission(Ms[AutoQ], 1);
					S.bBriefShown = true;
					S.Speed = 3.f;
					StartGame(S);
				}
				else
				{
					FRSGameSettings S = DefaultSettings();
					S.Map = Ps[AutoQ - Ms.Num()];
					S.Enemies = 3;
					S.Speed = 3.f;
					StartGame(S);
				}
				if (Game) Game->EnableAutopilot();
				++AutoQ;
				AutoQT = AutoT + 10.f;
			}
		} break;
	case 16: if (AutoT >= AutoQT) { UE_LOG(LogRS, Display, TEXT("AUTOTEST done")); QuitGame(); ++AutoStep; } break;
	default: break;
	}
	if (State == ERSState::Game && AutoNextStat > 0.f && AutoT >= AutoNextStat && AutoStep <= 8)
	{
		AutoNextStat = AutoT + 10.f;
		AutoStats(TEXT("skirmish"));
	}
}

void ARSGameMode::BuildGallery()
{
	URSPlayer* P = Game->Human;
	const FIntPoint S0 = P->StartCell;
	const FRSDefs& D = FRSDefs::Get();
	TArray<FString> Bids;
	for (const auto& Kv : D.Buildings)
		if (!Kv.Value.bCiv && !Kv.Value.bNaval && Kv.Key != TEXT("oil")) Bids.Add(Kv.Key);
	Bids.Sort();
	FBox2D Area(ForceInit);
	for (const FString& Id : Bids)
	{
		bool bDone = false;
		for (int32 R = 3; R < 30 && !bDone; ++R)
			for (int32 DY = -R; DY <= R && !bDone; ++DY)
				for (int32 DX = -R; DX <= R && !bDone; ++DX)
				{
					if (FMath::Abs(DX) != R && FMath::Abs(DY) != R) continue;
					const FIntPoint TL(S0.X + DX, S0.Y + DY);
					if (Game->CanPlace(Id, TL, P, nullptr, false))
					{
						if (ARSBuilding* B = Game->PlaceBuilding(P, Id, TL, true))
						{
							Area += B->Pos;
							bDone = true;
						}
					}
				}
		if (!bDone) UE_LOG(LogRS, Warning, TEXT("GALLERY cannot place %s"), *Id);
	}
	TArray<FString> Uids;
	for (const auto& Kv : D.Units)
		if (Kv.Value.Kind != TEXT("ship")) Uids.Add(Kv.Key);
	Uids.Sort();
	// 單位排在基地下方（+Y）兩列
	const FVector2D MapC(Game->Map->W * 0.5f, Game->Map->H * 0.5f);
	const FVector2D UBase = RS::CellCenter(S0) + (MapC - RS::CellCenter(S0)).GetSafeNormal() * 17.f - FVector2D(11.f, 2.f);
	for (int32 I = 0; I < Uids.Num(); ++I)
	{
		const FIntPoint C = Game->NearestFree(RS::ToCell(UBase + FVector2D((I % 12) * 2.f, (I / 12) * 2.5f)), 6);
		if (C.X >= 0) Game->SpawnUnit(P, Uids[I], RS::CellCenter(C));
	}
	for (int32 X = -40; X <= 40; X += 8)
		for (int32 Y = -40; Y <= 40; Y += 8) Game->Reveal(RS::CellCenter(S0) + FVector2D(X, Y), 10.f);
	const FVector2D Ctr = RS::CellCenter(S0);
	GalleryShots = { Ctr + FVector2D(-7, -7), Ctr + FVector2D(7, -7), Ctr + FVector2D(-7, 7), Ctr + FVector2D(7, 7),
		Ctr + FVector2D(-12, 0), Ctr + FVector2D(12, 0), Ctr + FVector2D(0, -12), Ctr + FVector2D(0, 12),
		UBase + FVector2D(4, 1), UBase + FVector2D(16, 1), UBase + FVector2D(4, 5), UBase + FVector2D(16, 5) };
	UE_LOG(LogRS, Display, TEXT("GALLERY placed %d buildings, %d units"), Game->Buildings.Num(), Game->Units.Num());
}

void ARSGameMode::GalleryTick(float Dt)
{
	AutoT += Dt;
	if (AutoStep == 0 && AutoT > 4.f && !bAutoShotDone) { bAutoShotDone = true; AutoShot(TEXT("g_menu")); }
	if (AutoStep == 0 && AutoT > 6.f)
	{
		FRSGameSettings S = DefaultSettings();
		S.Enemies = 1;
		S.bEvents = false;
		S.bCrates = false;
		S.Money = 100000.f;
		// 熱帶地圖：同時看得到闊葉樹、針葉樹與棕櫚
		for (const FRSMapPreset& Pm : FRSDefs::Get().Presets) if (Pm.Theme == TEXT("tropical")) { S.Map = Pm; break; }
		StartGame(S);
		AutoStep = 1;
		AutoQT = AutoT + 4.f;
	}
	else if (AutoStep == 1 && AutoT > AutoQT - 2.5f && AutoQ == 0 && Game)
	{
		// 先讓開局步兵走動，拍一張迷霧＋走路的畫面
		AutoQ = 1;
		for (ARSUnit* U : Game->Units)
			if (U && U->Player == Game->Human && U->UKind == TEXT("inf")) U->CmdMove(U->Pos + FVector2D(6.f, 3.f), false);
		if (Game->Cam) Game->Cam->SetZoom(0.9f);
	}
	else if (AutoStep == 1 && AutoT > AutoQT - 0.8f && AutoQ == 1)
	{
		AutoQ = 2;
		AutoShot(TEXT("g_fog"));
	}
	else if (AutoStep == 1 && AutoT > AutoQT)
	{
		BuildGallery();
		AutoStep = 2;
		AutoQ = 0;
		AutoQT = AutoT + 4.f;
	}
	else if (AutoStep == 2 && AutoT > AutoQT)
	{
		if (Game) Game->bPaused = true;
		if (AutoQ > 0 && !bWarShot) AutoShot(FString::Printf(TEXT("g%02d"), AutoQ - 1));
		if (AutoQ >= GalleryShots.Num() && AutoStep == 2 && !bWarShot)
		{
			// 戰損＋陣型：炸掉幾輛車與建築、打傷幾輛，並讓一群單位以陣型移動
			bWarShot = true;
			if (Game)
			{
				Game->bPaused = false;
				const FVector2D C0 = GalleryShots[0];
				TArray<ARSUnit*> Veh, Grp;
				for (ARSUnit* U : Game->Units)
					if (U && U->Player == Game->Human && U->UKind != TEXT("air") && U->UKind != TEXT("ship") && FVector2D::Distance(U->Pos, C0) < 14.f)
					{
						if (U->UKind == TEXT("veh") && Veh.Num() < 6) Veh.Add(U);
						else if (Grp.Num() < 14) Grp.Add(U);
					}
				for (int32 I = 0; I < Veh.Num(); ++I)
				{
					if (I < 3) { Veh[I]->Hp = 0.f; Veh[I]->Die(); }
					else Veh[I]->Hp = Veh[I]->MaxHp * (I == 3 ? 0.15f : 0.35f);
				}
				int32 NB = 0;
				for (ARSBuilding* B : TArray<ARSBuilding*>(Game->Buildings))
					if (B && B->Player == Game->Human && NB < 2 && FVector2D::Distance(B->Pos, C0) < 12.f && B->Def->Role != TEXT("cy"))
					{
						B->Hp = 0.f;
						B->Die();
						++NB;
					}
				for (ARSBuilding* B : Game->Buildings)
					if (B && B->Player == Game->Human && NB < 4 && FVector2D::Distance(B->Pos, C0) < 12.f) { B->Hp = B->MaxHp * 0.2f; ++NB; }
				Game->CommandMove(Grp, C0 + FVector2D(5.f, -3.f));
				UE_LOG(LogRS, Display, TEXT("GALLERY war scene: wrecks=%d group=%d"), Game->Wrecks.Num(), Grp.Num());
				Game->CenterOn(C0 + FVector2D(1.f, 0.f));
				if (Game->Cam) Game->Cam->SetZoom(1.2f);
			}
			AutoQT = AutoT + 3.5f;
			return;
		}
		if (AutoQ >= GalleryShots.Num())
		{
			AutoShot(TEXT("g_war"));
			// 接著開啟電影鏡頭拍一張
			if (Game)
			{
				Game->bPaused = false;
				Game->AddHot(GalleryShots[0], 30.f, TEXT("測試：戰鬥爆發"));
				Game->ToggleCinema();
			}
			AutoStep = 7; AutoQT = AutoT + 4.5f; return;
		}
		if (Game)
		{
			Game->CenterOn(GalleryShots[AutoQ]);
			if (Game->Cam) Game->Cam->SetZoom(AutoQ >= 8 ? 1.9f : 1.3f);
		}
		++AutoQ;
		AutoQT = AutoT + 1.5f;
	}
	else if (AutoStep == 7 && AutoT > AutoQT)
	{
		AutoShot(TEXT("g_cine"));
		if (Game && Game->bCinema) Game->ToggleCinema();
		{
			// 最後一張：夜晚＋下雨
			if (Game)
			{
				Game->Tod = 0.93f;
				Game->Weather = 1;
				Game->WeatherT = 999.f;
				Game->CenterOn(GalleryShots[0] + FVector2D(7, 7));
				if (Game->Cam) Game->Cam->SetZoom(1.1f);
			}
			AutoStep = 3; AutoQT = AutoT + 3.f; return;
		}
	}
	else if (AutoStep == 3 && AutoT > AutoQT)
	{
		if (AutoQ >= 0) { AutoShot(TEXT("g_night")); AutoQ = -1; AutoQT = AutoT + 1.5f; return; }
		UE_LOG(LogRS, Display, TEXT("GALLERY done"));
		QuitGame();
		AutoStep = 4;
	}
}
