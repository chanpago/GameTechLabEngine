#pragma once
#include "Object.h"
#include "Vector.h"

class UPrimitiveComponent;
class USceneComponent;
class AStaticMeshActor;
class UStaticMeshComponent;

class FOctree;
class FBVHierarchy;

struct FRay;
struct FAABB;
struct FFrustum;

class UWorldPartitionManager : public UObject
{
public:
	DECLARE_CLASS(UWorldPartitionManager, UObject)

	UWorldPartitionManager();
	~UWorldPartitionManager();

	void Clear();

	// 신규 등록 API
	void Register(USceneComponent* Component);
	void RegisterActorComponents(AActor* Actor);
	void BeginBulkRegistration();
	void BulkRegister(const TArray<AActor*>& Actors); // 여러 액터 한 번에 추가 (+즉시 리빌드)
	
	void Unregister(USceneComponent* Component);

	// 업데이트 큐 등록 API
	void MarkDirty(AActor* Actor);
	void MarkDirty(UPrimitiveComponent* Smc);

	void Update(float DeltaTime, const uint32 BudgetCount = 256);

    //void RayQueryOrdered(FRay InRay, OUT TArray<std::pair<AActor*, float>>& Candidates);
    void RayQueryClosest(FRay InRay, OUT AActor*& OutActor, OUT float& OutBestT);
	void FrustumQuery(const FFrustum& InFrustum, OUT TArray<UPrimitiveComponent*>& OutVisibleComponents) const;
	const TArray<USceneComponent*>& GetNonSpatialComponents() const { return NonSpatialComponents; }

	/** 옥트리 게터 */
	FOctree* GetSceneOctree() const { return SceneOctree; }
	/** BVH 게터 */
	FBVHierarchy* GetBVH() const { return BVH; }
	uint64 GetSpatialRevision() const { return SpatialRevision; }

private:

	// 싱글톤 
	UWorldPartitionManager(const UWorldPartitionManager&) = delete;
	UWorldPartitionManager& operator=(const UWorldPartitionManager&) = delete;

	//재시작시 필요 
	void ClearSceneOctree();
	void ClearBVHierarchy();
	
	TQueue<UPrimitiveComponent*> ComponentDirtyQueue; // 추가 혹은 갱신이 필요한 요소의 대기 큐
	TSet<UPrimitiveComponent*> ComponentDirtySet;     // 더티 큐 중복 추가를 막기 위한 Set
	TArray<USceneComponent*> NonSpatialComponents;    // BVH 대상이 아닌 렌더 컴포넌트
	bool bDeferringBulkRegistration = false;
	uint64 SpatialRevision = 1;
	FOctree* SceneOctree = nullptr;
	FBVHierarchy* BVH = nullptr;
};
