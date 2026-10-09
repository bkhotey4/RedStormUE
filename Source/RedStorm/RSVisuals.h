// 3D 外觀：以引擎內建幾何體程序化組出單位 / 建築；可在「專案設定 → Red Storm 視覺」改用自訂模型與材質
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RSVisuals.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ARSEntity;
class AActor;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Red Storm 視覺"))
class REDSTORM_API URSVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	URSVisualSettings();
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// 所有程序化幾何體使用的材質（需有向量參數 ColorParam）
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> BaseMaterial;

	// 發光特效（光束、爆炸）使用的材質；留空則用 BaseMaterial
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	FName ColorParam = TEXT("Color");

	// 以單位 / 建築 id（例如 s_tank、a_power）指定自訂模型，取代程序化外觀
	UPROPERTY(Config, EditAnywhere, Category = "Models")
	TMap<FString, TSoftObjectPtr<UStaticMesh>> MeshOverrides;

	// 自訂模型縮放（預設 1）
	UPROPERTY(Config, EditAnywhere, Category = "Models")
	TMap<FString, float> MeshOverrideScale;

	// 自訂模型偏航修正（度），模型正面不是 +X 時使用
	UPROPERTY(Config, EditAnywhere, Category = "Models")
	TMap<FString, float> MeshOverrideYaw;

	// 自訂模型是否套用隊伍顏色（材質需有 ColorParam）
	UPROPERTY(Config, EditAnywhere, Category = "Models")
	bool bTintOverrides = false;
};

namespace RSV
{
	UStaticMesh* Cube();
	UStaticMesh* Sphere();
	UStaticMesh* Cylinder();
	UStaticMesh* Cone();
	UStaticMesh* Plane();
	UMaterialInterface* BaseMat();
	UMaterialInterface* GlowMat();
	UMaterialInterface* Mat(const FLinearColor& C, bool bGlow = false);

	// Size 以 uu 為單位（內建幾何體為 100uu）
	REDSTORM_API UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Loc, const FVector& Size,
		const FLinearColor& Col, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = true);
	void SetColor(UStaticMeshComponent* C, const FLinearColor& Col, bool bGlow = false);

	// 依 TypeId 建立整個實體的外觀（Body / TurretPivot / Spinner / TeamParts）
	void BuildEntity(ARSEntity* E);

	// 地圖主題（決定單位迷彩），由遊戲開局設定
	REDSTORM_API void SetTheme(const FString& Theme);
}
