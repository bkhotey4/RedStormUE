#include "RSAudio.h"
#include "RSCommon.h"
#include "RSGlobals.h"
#include "RSGame.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"

namespace
{
	const TMap<FString, FString>& SfxMap()
	{
		static const TMap<FString, FString> M = {
			{TEXT("mg"), TEXT("sfx/mg_burst")}, {TEXT("cannon"), TEXT("sfx/cannon_fire")}, {TEXT("rocket"), TEXT("sfx/rocket_launch")},
			{TEXT("explode"), TEXT("sfx/explosion")}, {TEXT("build"), TEXT("sfx/build_place")}, {TEXT("click"), TEXT("sfx/ui_click")},
			{TEXT("radio"), TEXT("sfx/radio_squelch")}, {TEXT("zap"), TEXT("sfx/zap")}, {TEXT("laser"), TEXT("sfx/laser")},
			{TEXT("flak"), TEXT("sfx/flak")}, {TEXT("pistol"), TEXT("sfx/pistol")}, {TEXT("bomb"), TEXT("sfx/bomb")},
			{TEXT("big_explode"), TEXT("sfx/big_explode")}, {TEXT("sell"), TEXT("sfx/sell")}, {TEXT("cash"), TEXT("sfx/cash")},
			{TEXT("deploy"), TEXT("sfx/deploy")}, {TEXT("power_down"), TEXT("sfx/power_down")}, {TEXT("alarm"), TEXT("sfx/alarm")},
			{TEXT("missile"), TEXT("sfx/missile")}, {TEXT("thunder"), TEXT("sfx/thunder")}, {TEXT("select"), TEXT("sfx/select")},
			{TEXT("tick"), TEXT("sfx/tick")}, {TEXT("inf_die"), TEXT("sfx/inf_die")}, {TEXT("place"), TEXT("sfx/place")}, {TEXT("jet"), TEXT("sfx/jet")},
		};
		return M;
	}
	float DbToLin(float Db) { return FMath::Pow(10.f, Db / 20.f); }
	double Now() { return FPlatformTime::Seconds(); }
}

void URSAudio::Init(UWorld* InWorld)
{
	World = InWorld;
}

bool URSAudio::ParseWav(const TArray<uint8>& B, FRSClip& Out)
{
	if (B.Num() < 44 || B[0] != 'R' || B[1] != 'I' || B[2] != 'F' || B[3] != 'F') return false;
	auto U16 = [&B](int32 P) { return (int32)B[P] | ((int32)B[P + 1] << 8); };
	auto U32 = [&B](int32 P) { return (uint32)B[P] | ((uint32)B[P + 1] << 8) | ((uint32)B[P + 2] << 16) | ((uint32)B[P + 3] << 24); };
	int32 Pos = 12;
	int32 Channels = 1, Rate = 22050, Bits = 16;
	int32 DataStart = -1, DataSize = 0;
	while (Pos + 8 <= B.Num())
	{
		const bool bFmt = B[Pos] == 'f' && B[Pos + 1] == 'm' && B[Pos + 2] == 't' && B[Pos + 3] == ' ';
		const bool bData = B[Pos] == 'd' && B[Pos + 1] == 'a' && B[Pos + 2] == 't' && B[Pos + 3] == 'a';
		const int32 Sz = (int32)U32(Pos + 4);
		const int32 Body = Pos + 8;
		if (bFmt && Body + 16 <= B.Num())
		{
			Channels = U16(Body + 2);
			Rate = (int32)U32(Body + 4);
			Bits = U16(Body + 14);
		}
		else if (bData)
		{
			DataStart = Body;
			DataSize = FMath::Min(Sz, B.Num() - Body);
		}
		Pos = Body + Sz + (Sz & 1);
		if (Sz < 0) break;
	}
	if (DataStart < 0 || DataSize <= 0) return false;
	Out.PCM = MakeShared<TArray<uint8>>();
	if (Bits == 8)
	{
		Out.PCM->SetNumUninitialized(DataSize * 2);
		int16* Dst = (int16*)Out.PCM->GetData();
		for (int32 I = 0; I < DataSize; ++I) Dst[I] = (int16)(((int32)B[DataStart + I] - 128) << 8);
	}
	else if (Bits == 16)
	{
		Out.PCM->Append(B.GetData() + DataStart, DataSize & ~1);
	}
	else
	{
		return false;
	}
	Out.Rate = Rate;
	Out.Channels = FMath::Max(1, Channels);
	Out.Duration = (float)(Out.PCM->Num() / 2 / Out.Channels) / (float)FMath::Max(1, Rate);
	return true;
}

const FRSClip* URSAudio::Load(const FString& Rel)
{
	if (const FRSClip* C = Clips.Find(Rel)) return C;
	if (Missing.Contains(Rel)) return nullptr;
	TArray<uint8> Bytes;
	const FString Path = RS::ContentDir() / TEXT("Audio") / Rel + TEXT(".wav");
	FRSClip Clip;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !ParseWav(Bytes, Clip))
	{
		Missing.Add(Rel);
		return nullptr;
	}
	return &Clips.Add(Rel, Clip);
}

UAudioComponent* URSAudio::PlayRaw(const FRSClip& Clip, float Volume, float Pitch)
{
	UWorld* W = World.Get();
	if (!W || !Clip.PCM.IsValid()) return nullptr;
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(Clip.Rate);
	Wave->NumChannels = Clip.Channels;
	Wave->Duration = Clip.Duration;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(Clip.PCM->GetData(), Clip.PCM->Num());
	UAudioComponent* AC = UGameplayStatics::SpawnSound2D(W, Wave, Volume, Pitch, 0.f, nullptr, false, true);
	return AC;
}

void URSAudio::Tick(float Dt)
{
	Active.RemoveAll([](const TObjectPtr<UAudioComponent>& C) { return !IsValid(C) || !C->IsPlaying(); });
	if (VoiceQueue.Num() > 0 && (!IsValid(VoiceComp) || !VoiceComp->IsPlaying()))
	{
		const FString N = VoiceQueue[0];
		VoiceQueue.RemoveAt(0);
		if (const FRSClip* C = Load(TEXT("voice/") + N))
		{
			VoiceComp = PlayRaw(*C, FMath::Max(0.001f, FRSUserSettings::Get().VoiceVol), 1.f);
		}
	}
	// 音樂循環
	if (!MusicName.IsEmpty() && (!IsValid(MusicComp) || !MusicComp->IsPlaying()))
	{
		if (const FRSClip* C = Load(TEXT("music/") + MusicName))
		{
			MusicComp = PlayRaw(*C, FMath::Max(0.001f, FRSUserSettings::Get().MusicVol) * DbToLin(-6.f), 1.f);
		}
		else
		{
			MusicName.Empty();
		}
	}
}

void URSAudio::Play(const FString& Name, float VolDb, const FVector2D* WorldPos, float PitchVar)
{
	const FString* Rel = SfxMap().Find(Name);
	const FRSClip* C = Load(Rel ? *Rel : Name);
	if (!C) return;
	const double T = Now();
	if (const double* L = LastPlay.Find(Name))
	{
		if (T - *L < 0.06) return;
	}
	LastPlay.Add(Name, T);
	if (WorldPos && Game.IsValid())
	{
		const float Att = Game->SoundAttenuation(*WorldPos);
		if (Att <= -40.f) return;
		VolDb += Att;
	}
	if (Active.Num() >= 20) return;
	const float Vol = DbToLin(VolDb) * FMath::Max(0.001f, FRSUserSettings::Get().SfxVol);
	if (UAudioComponent* AC = PlayRaw(*C, Vol, 1.f + FMath::FRandRange(-PitchVar, PitchVar)))
	{
		Active.Add(AC);
	}
}

void URSAudio::Say(const FString& Name, float Cooldown)
{
	const double T = Now();
	if (const double* L = LastVoice.Find(Name))
	{
		if (T - *L < Cooldown) return;
	}
	LastVoice.Add(Name, T);
	const FRSClip* C = Load(TEXT("voice/") + Name);
	if (!C) return;
	if (IsValid(VoiceComp) && VoiceComp->IsPlaying())
	{
		if (VoiceQueue.Num() < 2 && !VoiceQueue.Contains(Name)) VoiceQueue.Add(Name);
		return;
	}
	VoiceComp = PlayRaw(*C, FMath::Max(0.001f, FRSUserSettings::Get().VoiceVol), 1.f);
}

void URSAudio::Ack(const FString& Name)
{
	const double T = Now();
	if ((IsValid(AckComp) && AckComp->IsPlaying()) || T - AckT < 4.0) return;
	const FRSClip* C = Load(TEXT("voice/") + Name);
	if (!C) return;
	AckT = T;
	AckComp = PlayRaw(*C, FMath::Max(0.001f, FRSUserSettings::Get().VoiceVol) * DbToLin(-3.f), 1.f);
}

void URSAudio::Music(const FString& Name)
{
	if (MusicName == Name && IsValid(MusicComp) && MusicComp->IsPlaying()) return;
	StopMusic();
	MusicName = Name;
	Tick(0.f);
}

void URSAudio::StopMusic()
{
	MusicName.Empty();
	if (IsValid(MusicComp)) MusicComp->Stop();
	MusicComp = nullptr;
}

void URSAudio::StopAll()
{
	for (UAudioComponent* C : Active)
		if (IsValid(C)) C->Stop();
	Active.Reset();
	if (IsValid(VoiceComp)) VoiceComp->Stop();
	if (IsValid(AckComp)) AckComp->Stop();
	VoiceQueue.Reset();
}

void URSAudio::UpdateMusicVolume()
{
	if (IsValid(MusicComp)) MusicComp->SetVolumeMultiplier(FMath::Max(0.001f, FRSUserSettings::Get().MusicVol) * DbToLin(-6.f));
}
