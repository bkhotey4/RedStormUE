// 3D 地形：高度場網格（含坡道與懸崖面）、水面、樹木、岩石、礦石、橋樑、戰爭迷霧
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RSTerrain.generated.h"

class ARSGame;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;

struct FRSPropInst
{
	UInstancedStaticMeshComponent* ISM = nullptr;
	int32 Index = 0;
	FTransform Xf;
};

UCLASS()
class REDSTORM_API ARSTerrain : public AActor
{
	GENERATED_BODY()
public:
	ARSTerrain();
	void Build(ARSGame* G);
	void UpdateTerrain(float Dt);
	void RevealCells(const TArray<int32>& Cells);
	void ResetFog(const TArray<uint8>& Explored);
	void RebuildOre();
	void HideCellsForBuilding(const TArray<int32>& Cells);
	void AddRubble(FIntPoint TL, FIntPoint Size, bool bWall, bool bShown);   // 建築被摧毀後留下的瓦礫堆   // 建築蓋上去後隱藏該處的草叢碎石

	static constexpr float WATER_Z = -14.f;

private:
	UPROPERTY() TObjectPtr<ARSGame> Game = nullptr;
	UPROPERTY() TObjectPtr<USceneComponent> RootScene = nullptr;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Ground = nullptr;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> TerrainMID = nullptr;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> WaterTexMID = nullptr;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Static;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> OreISM = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GemISM = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FogISM = nullptr;
	TArray<int32> FogIndex;     // 格子 → 迷霧實例索引
	TArray<FRSPropInst> Props;   // 樹木 / 岩石實例（未探索時隱藏）
	TMultiMap<int32, int32> CellProps;
	TSet<UInstancedStaticMeshComponent*> DirtyISM;
	TSet<int32> BuiltCells;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> RubbleISM;
	void BuildClutter();
	FTransform FogXf(int32 X, int32 Y) const;
	void AddProp(UInstancedStaticMeshComponent* ISM, const FTransform& Xf, const FIntPoint& Cell);
	void SetPropsShown(int32 CellIdx, bool bShown);
	float OreT = 0.f;

	UInstancedStaticMeshComponent* MakeISM(UStaticMesh* Mesh, const FLinearColor& C, bool bShadow = true);
	void BuildGround();
	bool BuildGroundTextured();
	void BuildProps();
	void BuildFog();
};
