# Shadow Rendering Crash Fix

## 문제 원인

1. **`Proxies.Meshes`는 카메라 Frustum 컬링을 통과한 메시만 저장** (SceneRenderer.cpp:761)
2. 그림자 렌더링에서 `Proxies.Meshes`를 사용하면 **카메라에서 안 보이지만 그림자를 드리워야 하는 오브젝트가 누락됨**
3. 직접 Actor 순회로 바꾸면 **유효하지 않은 버퍼**를 가진 `FMeshBatchElement`가 수집될 수 있음
4. `RenderShadowDepthPass`에서 **버퍼 유효성 검사 없이** `IASetVertexBuffers`를 호출해서 크래시 발생

## 크래시 콜스택

```
Exception Code: 0xc0000005 (Access Violation)
D3DKMTOpenResource - (Unknown File) (0)
FSceneRenderer::RenderShadowDepthPass - SceneRenderer.cpp (555)
```

555번 줄: `IASetPrimitiveTopology`에서 유효하지 않은 Batch 데이터 접근 시 크래시

## 해결 방법

### 방법 1: `RenderShadowDepthPass`에 유효성 검사 추가 (가장 빠른 해결책)

SceneRenderer.cpp:528 이후에 유효성 검사 추가:

```cpp
for (const FMeshBatchElement& Batch : InShadowBatches)
{
    // 버퍼 유효성 검사 - 크래시 방지
    if (!Batch.VertexBuffer || !Batch.IndexBuffer || Batch.IndexCount == 0)
    {
        continue;
    }

    // ... 기존 코드 ...
}
```

### 방법 2: 그림자용 메시 수집 부분 수정 (SceneRenderer.cpp:259)

그림자는 라이트 Frustum 기준으로 컬링해야 하므로, 별도로 수집하되 유효성 검사를 추가:

```cpp
// 2. 그림자 캐스터(Caster) 메시 수집
TArray<FMeshBatchElement> ShadowMeshBatches;
for (AActor* Actor : World->GetActors())
{
    if (!Actor || !Actor->IsActorVisible() || !Actor->IsActorActive())
    {
        continue;
    }

    for (USceneComponent* Component : Actor->GetSceneComponents())
    {
        if (!Component)
        {
            continue;
        }

        UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component);
        if (!MeshComponent || !MeshComponent->IsCastShadows() || !MeshComponent->IsVisible())
        {
            continue;
        }

        // 메시 리소스가 유효한지 확인 (HasValidMeshData 함수가 없다면 생략 가능)
        // if (!MeshComponent->HasValidMeshData())
        // {
        //     continue;
        // }

        MeshComponent->CollectMeshBatches(ShadowMeshBatches, View);
    }
}
```

### 방법 3: Frustum Culling과 유효성 검사 모두 적용

```cpp
TArray<FMeshBatchElement> ShadowMeshBatches;
for (AActor* Actor : World->GetActors())
{
    if (!Actor || !Actor->IsActorVisible() || !Actor->IsActorActive())
    {
        continue;
    }

    for (USceneComponent* Component : Actor->GetSceneComponents())
    {
        if (!Component)
        {
            continue;
        }

        UMeshComponent* MeshComponent = Cast<UMeshComponent>(Component);
        if (!MeshComponent || !MeshComponent->IsCastShadows() || !MeshComponent->IsVisible())
        {
            continue;
        }

        // Light Frustum Culling (선택적)
        // const FBoxSphereBounds& Bounds = MeshComponent->GetBounds();
        // if (!LightFrustum.Intersects(Bounds))
        // {
        //     continue;
        // }

        MeshComponent->CollectMeshBatches(ShadowMeshBatches, View);
    }
}
```

## 권장 사항

1. **즉시 적용**: 방법 1 (RenderShadowDepthPass에 유효성 검사)
2. **추후 최적화**: 라이트별 Frustum Culling 구현으로 불필요한 그림자 캐스터 제외
