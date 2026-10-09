// 音效 / 語音 / 音樂管理：執行期直接讀取 WAV（Content/RedStorm/Audio），不需要匯入資產
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RSAudio.generated.h"

class UAudioComponent;
class ARSGame;

struct FRSClip
{
	TSharedPtr<TArray<uint8>> PCM;   // 16-bit little endian
	int32 Rate = 22050;
	int32 Channels = 1;
	float Duration = 0.f;
};

UCLASS()
class REDSTORM_API URSAudio : public UObject
{
	GENERATED_BODY()
public:
	void Init(UWorld* InWorld);
	void Tick(float Dt);
	void Play(const FString& Name, float VolDb = 0.f, const FVector2D* WorldPos = nullptr, float PitchVar = 0.07f);
	void Say(const FString& Name, float Cooldown = 4.f);
	void Ack(const FString& Name);
	void Music(const FString& Name);
	void StopAll();
	void StopMusic();
	void UpdateMusicVolume();
	UAudioComponent* PlayRaw(const FRSClip& Clip, float Volume, float Pitch);

	TWeakObjectPtr<ARSGame> Game;

private:
	const FRSClip* Load(const FString& Rel);
	static bool ParseWav(const TArray<uint8>& Bytes, FRSClip& Out);
	TMap<FString, FRSClip> Clips;
	TSet<FString> Missing;
	TWeakObjectPtr<UWorld> World;
	UPROPERTY() TArray<TObjectPtr<UAudioComponent>> Active;
	UPROPERTY() TObjectPtr<UAudioComponent> VoiceComp = nullptr;
	UPROPERTY() TObjectPtr<UAudioComponent> AckComp = nullptr;
	UPROPERTY() TObjectPtr<UAudioComponent> MusicComp = nullptr;
	FString MusicName;
	TArray<FString> VoiceQueue;
	TMap<FString, double> LastPlay;
	TMap<FString, double> LastVoice;
	double AckT = -100.0;
};
