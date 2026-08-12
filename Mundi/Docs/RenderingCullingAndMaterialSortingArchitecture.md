# 🎮 GameTechLabEngine 렌더링 최적화 구조

> 기준: 2026-08-12 구현
>
> 범위: Frustum Culling, GPU HZB Occlusion Culling, Material Sorting, StaticMesh Draw Cache

---

## 🚀 1. 한눈에 보는 구조

현재 StaticMesh 렌더링 경로는 불필요한 작업을 다음 순서로 줄인다.

```text
BVH 공간 검색
  → Frustum Culling
  → 이전 GPU Occlusion 결과 적용
  → MeshBatch 생성 또는 정적 캐시 참조
  → Material/State Sorting
  → 각 StaticMesh를 개별 DrawIndexed
  → 현재 SceneDepth로 다음 GPU Occlusion 결과 생성
```

| 최적화 | 제거하는 비용 |
|---|---|
| 👁️ Frustum Culling | 카메라 범위 밖 메시의 후보 처리와 Draw |
| 🧱 GPU HZB Occlusion | 다른 물체에 가려진 메시의 Draw와 GPU Geometry 처리 |
| 🎨 Material Sorting | Draw 사이의 Shader, Material, Buffer 재바인딩 |
| 💾 StaticMesh Draw Cache | 반복적인 MeshBatch 생성과 전체 정렬 |

> ⚠️ **과제 제약**
>
> StaticMesh에는 Instanced Rendering을 사용하지 않는다. 컬링을 통과한 StaticMesh는 각각 `DrawIndexed`로 제출된다. 따라서 Culling은 Draw 수를 줄이지만, Material Sorting과 StaticMesh Cache는 Draw 준비 및 상태 변경 비용만 줄인다.

### 주요 객체

| 객체 | 책임 |
|---|---|
| `UWorldPartitionManager` / `FBVHierarchy` | 공간 등록, World AABB 보관, Frustum 후보 검색 |
| `FSceneRenderer` | 후보 수집, 컬링 결과 적용, 정렬, Draw 제출 |
| `FGPUOcclusionCuller` | HZB 생성, GPU 가시성 검사, 비동기 Readback |
| `FRenderer` | World/View별 GPU Occlusion Context와 StaticMesh Cache 소유 |

---

## 👁️ 2. Frustum Culling

### 원리

카메라의 가시 영역을 Left, Right, Top, Bottom, Near, Far의 6개 평면으로 표현한다. 메시의 World AABB가 평면 하나의 바깥에 완전히 존재하면 화면에 나타날 수 없으므로 렌더링 후보에서 제외한다.

AABB는 중심과 반 크기를 이용해 평면 방향의 투영 반경을 계산한다. `평면까지의 중심 거리 + 투영 반경 < 0`이면 박스 전체가 해당 평면 밖에 있다.

### 프로젝트 적용 방식

1. 렌더링 가능한 MeshComponent를 `UWorldPartitionManager`의 BVH에 등록한다.
2. `FSceneRenderer::GatherVisibleProxies()`에서 `FrustumQuery()`를 호출한다.
3. BVH 노드 AABB가 Frustum 밖이면 Subtree 전체를 건너뛴다.
4. 노드 전체가 Frustum 안이면 자식 재검사 없이 노드의 컴포넌트 범위를 결과에 추가한다.
5. Frustum 경계와 교차하는 노드만 Leaf까지 내려가 개별 AABB를 검사한다.
6. 라이트, 포그, 스카이 등은 별도의 `NonSpatialComponents` 목록에서 수집한다.

### 특징과 한계

- 4면이 아니라 Near/Far를 포함한 완전한 6면 검사다.
- 메시 전수 검사보다 BVH Subtree 단위 Early-out에 의미가 있다.
- 카메라 밖 오브젝트가 많을수록 효과가 크다.
- 모든 메시가 Frustum 안에 있으면 제거량은 작다.
- 화면 안에서 다른 물체에 가려진 메시는 제거하지 못한다.

> 📸 **코드 캡처 1 — 6면 AABB 판정**
> - 위치: `Engine/Collision/Frustum.cpp` → `IsAABBVisible()`
> - 범위/강조점: Center/Extents 계산과 6개 Plane 검사. Near/Far까지 포함된 부분을 표시한다.

> 📸 **코드 캡처 2 — BVH Subtree Early-out**
> - 위치: `Engine/Spatial/BVHierarchy.cpp` → `FBVHierarchy::QueryFrustum()`
> - 범위/강조점: Frustum 밖 노드의 `continue`와 완전 포함 시 `SubtreeCount`를 한 번에 추가하는 부분.

> 📸 **코드 캡처 3 — 렌더 경로 연결부**
> - 위치: `Renderer/SceneRenderer.cpp` → `FSceneRenderer::GatherVisibleProxies()`
> - 범위/강조점: Flag 확인, `FrustumQuery()`, Proxy 수집 부분. MeshBatch 생성 전 적용됨을 표시한다.

---

## 🧱 3. GPU HZB Occlusion Culling

### 원리

Frustum 안에 있지만 다른 물체의 실제 픽셀에 완전히 가려진 메시를 찾는다. 가림 정보는 CPU가 추정한 박스가 아니라 현재 Base Pass에서 생성된 실제 `SceneDepth`를 사용한다.

Depth Test는 Draw 제출과 Vertex 처리 이후 픽셀을 버린다. Occlusion Culling은 가려진 메시의 다음 Draw 자체를 생략해 CPU 제출과 GPU Geometry 비용을 더 앞에서 제거한다.

### HZB 구성

`SceneDepth`를 계속 축소해 Mip Chain을 만든다. 현재 엔진은 Normal-Z이므로 각 축소 영역에서 가장 먼 값인 `MAX Depth`를 저장한다.

```text
SceneDepth → HZB Mip 0 → Mip 1 → Mip 2 → ... → 1×1
```

큰 오브젝트는 낮은 해상도의 Mip에서 적은 비교만으로 넓은 화면 영역의 가림 여부를 확인할 수 있다.

### 오브젝트 판정

1. StaticMesh의 축소하지 않은 전체 World AABB를 후보로 사용한다.
2. AABB의 8개 꼭짓점을 View-Projection으로 화면에 투영한다.
3. 투영된 Screen Rectangle 크기에 맞는 HZB Mip을 선택한다.
4. 너무 거친 판정을 피하기 위해 계산값보다 한 단계 정밀한 Mip을 사용한다.
5. Candidate의 가장 가까운 Depth와 해당 영역의 HZB Depth를 비교한다.
6. Texel 하나라도 보일 가능성이 있으면 Visible을 유지한다.
7. 검사 영역 전체에서 완전히 가려질 때만 Occluded로 판정한다.

가리는 물체는 실제 SceneDepth 픽셀이고, 검사 대상만 AABB다. AABB 일부가 보일 가능성이 있으면 메시를 살리므로 보수적이지만, 실제 Mesh보다 AABB가 큰 경우 컬링률은 낮아질 수 있다.

### 프레임 흐름

| 시점 | 처리 |
|---|---|
| Frame N 시작 | 완료된 이전 GPU 결과를 확인하고 현재 후보에서 Occluded Mesh 제거 |
| Frame N Base Pass | 남은 메시를 렌더링해 현재 SceneDepth 생성 |
| Frame N Base Pass 이후 | HZB 생성, AABB GPU 검사, 결과를 Staging Buffer로 복사 |
| Frame N+1 이후 | Readback 완료 시 결과를 적용하고, 미완료 시 기다리지 않음 |

같은 프레임에 GPU 결과를 기다리지 않는 이유는 CPU와 GPU의 강제 동기화를 피하기 위해서다.

### Temporal 안정화

| 장치 | 목적 |
|---|---|
| 3-slot Staging Ring + `DO_NOT_WAIT` | GPU Readback 때문에 CPU가 멈추는 상황 방지 |
| 결과 최대 수명 3 Frame | 지나치게 오래된 판정 폐기 |
| 2회 연속 Occluded | 한 번의 불안정한 결과로 메시가 사라지는 현상 방지 |
| Camera Cut 무효화 | 약 5 Unit 이동 또는 약 12도 회전 시 과거 결과 폐기 |
| 이전/현재 투영 기반 Screen Padding | 이동 중 새로 드러나는 영역을 보수적으로 포함 |
| Motion Depth Bias | 이동량에 따라 잘못된 가림 판정 완화 |
| Near Plane Fail-open | 정확한 투영이 어려우면 Cull하지 않고 Visible 처리 |

### 특징과 한계

- 큰 벽이나 가까운 대형 오브젝트 뒤에 메시가 많을 때 효과가 크다.
- 모든 메시가 화면에 노출된 장면에서는 Cull 이득 없이 Compute/Readback 비용만 남을 수 있다.
- 실제 픽셀 삼각형을 Candidate로 검사하는 방식보다 가볍고 안전하지만 AABB가 큰 메시에는 보수적이다.
- 결과가 비동기이므로 정지 상태와 이동 상태의 비용 및 컬링률을 따로 측정해야 한다.

> 📸 **코드 캡처 4 — HZB MAX Reduction**
> - 위치: `Shaders/Occlusion/OcclusionHZBReduce_CS.hlsl` → `mainCS()`
> - 범위/강조점: Source 영역의 `max()` Reduction과 `FarthestDepth` 출력. Normal-Z임을 표시한다.

> 📸 **코드 캡처 5 — AABB 투영과 HZB 판정**
> - 위치: `Shaders/Occlusion/OcclusionCull_CS.hlsl` → `mainCS()`
> - 범위/강조점: 8 Corner 투영·Mip 선택과 HZB 비교를 두 장으로 나눈다. 불확실하면 Visible인 분기를 표시한다.

> 📸 **코드 캡처 6 — 이전 GPU 결과 적용**
> - 위치: `Renderer/GPUOcclusionCuller.cpp` → `PrepareCandidatesAndCull()`
> - 범위/강조점: Readback/Temporal 검증과 `LastVisibleState` 기반 후보 제거. MeshBatch 생성 전 단계임을 표시한다.

> 📸 **코드 캡처 7 — 비동기 Readback**
> - 위치: `Renderer/GPUOcclusionCuller.cpp` → `PollReadbacks()`, `ConsumeReadback()`
> - 범위/강조점: 1 Frame Latency, `DO_NOT_WAIT`, 2회 Hysteresis. Stall과 Popping 방지 구조를 표시한다.

---

## 🎨 4. Material Sorting과 StaticMesh Draw Cache

### Material Sorting 원리

Draw Call 수를 합치는 대신 같은 GPU 상태를 사용하는 MeshBatch를 연속 배치한다. Draw 루프는 직전에 바인딩한 상태를 기억하고 다음 Batch가 같으면 중복 API 호출을 생략한다.

정렬 우선순위는 다음과 같다.

1. 지정된 경우 `SortPriority`
2. Vertex Shader
3. Pixel Shader
4. Material
5. Vertex Buffer
6. Index Buffer
7. Vertex Stride
8. Primitive Topology

Draw 시에는 Shader, Material, Texture SRV, Vertex/Index Buffer, Stride, Topology 등을 직전 상태와 비교한다. 같은 상태면 Bind를 생략하지만 오브젝트별 상수 데이터 갱신과 개별 `DrawIndexed`는 유지한다.

### 관측 결과

Frustum과 Occlusion을 끄고 Sorting만 비교했을 때 다음 결과가 관측됐다.

```text
Material Bind: 약 45,002회 → 2회
Frame Time: 약 4ms 감소
```

이는 정상적인 결과다. Material 재바인딩은 거의 제거됐지만 약 45,000개의 Draw 제출, 오브젝트 상수 갱신, Vertex/Raster 비용은 남아 있기 때문이다.

Occlusion이 이미 대부분의 메시를 제거한 상태에서는 정렬할 Batch 자체가 적어져 Sorting ON/OFF 차이가 작게 나타난다.

### StaticMesh Draw Cache

정적 메시가 변하지 않는데도 매 프레임 `CollectMeshBatches()`와 전체 정렬을 반복하는 비용을 줄이기 위한 경로다.

캐시는 다음을 보관한다.

- 정적 `FMeshBatchElement` 배열
- 미리 정렬된 Batch Index 배열
- World Draw Cache Revision과 Shader Reload Revision

캐시는 World와 View Shader Key 조합별로 관리하며 Actor/Component, Mesh, Material, Shader 구성이 바뀌면 재구축한다.

중요한 점은 가시성과 Transform을 고정 저장하지 않는다는 것이다. 현재 Frustum/Occlusion 결과에 포함된 StaticMesh만 Draw하고, World Matrix는 Draw 시 `SourceStaticMeshComponent`에서 다시 가져온다.

> 📸 **코드 캡처 8 — Material 정렬 기준**
> - 위치: `Renderer/MeshBatchElement.h` → `FMeshBatchElement::operator<()`
> - 범위/강조점: 전체 비교 함수. Material뿐 아니라 Shader와 IA State까지 정렬함을 표시한다.

> 📸 **코드 캡처 9 — 상태 캐시와 개별 Draw**
> - 위치: `Renderer/SceneRenderer.cpp` → `FSceneRenderer::DrawMeshBatches()`
> - 범위/강조점: 상태 비교/Bind와 `DrawIndexed()`를 두 장으로 나눈다. 중복 Bind 제거와 개별 Draw 유지를 함께 표시한다.

> 📸 **코드 캡처 10 — 영구 StaticMesh Cache**
> - 위치: `Renderer/SceneRenderer.cpp` → `GetOrBuildStaticMeshDrawCache()`, `RenderOpaquePass()`
> - 범위/강조점: Revision/정렬 Index와 Visible Set 필터. Metadata만 캐시하고 현재 가시성은 재검사함을 표시한다.

---

## 🔗 5. 최적화 간 상호작용

| 장면 | 가장 잘 드러나는 최적화 | 해석 |
|---|---|---|
| 카메라 밖에 메시가 많음 | Frustum | BVH가 화면 밖 Subtree를 대량 제거 |
| 큰 물체 뒤에 메시가 겹침 | Occlusion | 실제 Depth로 가려진 Draw 제거 |
| 5만 개가 모두 화면에 노출 | Material Sorting | Culling 이득은 작고 상태 변경 절감이 드러남 |
| 정적 메시 구성이 안정적 | StaticMesh Cache | Batch 생성과 반복 정렬 CPU 비용 절감 |

세 기능은 대체 관계가 아니다.

- Frustum은 **화면 밖 Draw**를 제거한다.
- Occlusion은 **화면 안이지만 가려진 Draw**를 제거한다.
- Material Sorting은 **남은 Draw 사이의 상태 변경**을 줄인다.
- StaticMesh Cache는 **남은 Draw를 준비하는 반복 CPU 작업**을 줄인다.

따라서 한 기능이 매우 잘 동작하면 다른 기능의 ON/OFF 차이가 작아질 수 있다.

---

## 📊 6. 측정 기준

한 번에 하나의 Flag만 바꾸고 같은 카메라 위치에서 Warm-up 이후 평균 Frame Time을 비교한다.

| 검증 대상 | 고정/비활성 조건 | 확인할 값 |
|---|---|---|
| Frustum | Occlusion OFF | Frustum Visible/Culled, Draw 수, CPU Time |
| Occlusion | Frustum과 Sorting 조건 고정 | Candidate/Tested/Culled, Latency, Draw 수, GPU Time |
| Material Sorting | Frustum/Occlusion OFF | Draw 수 동일 여부, Material/Shader/Buffer Bind, Frame Time |
| StaticMesh Cache | 나머지 Flag 고정 | Rebuild 횟수/시간, Cached Batch 수, Render CPU Time |

Material Sorting의 정상적인 결과는 **Draw 수는 같고 Bind 수와 Frame Time이 감소하는 것**이다. Occlusion의 정상적인 결과는 **가림이 많은 장면에서 Culled 수와 Draw 수가 함께 감소하는 것**이다.

---

## ✅ 7. 결론

현재 구현은 다음 구조로 정리할 수 있다.

1. BVH와 6면 AABB 검사로 카메라 밖 메시를 제거한다.
2. 실제 SceneDepth 기반 GPU HZB로 가려진 메시를 제거한다.
3. GPU 결과는 비동기 Readback과 Temporal 보정으로 다음 프레임 이후 안전하게 적용한다.
4. 살아남은 Batch는 Shader, Material, Buffer 기준으로 정렬해 중복 Bind를 제거한다.
5. 정적 Batch Metadata와 정렬 순서는 캐시하지만 현재 가시성과 Transform은 매 프레임 반영한다.
6. 모든 최적화 이후에도 각 StaticMesh는 개별 `DrawIndexed`를 사용한다.

즉, 이 프로젝트는 Draw를 강제로 합치는 방식이 아니라 **그릴 필요가 없는 Draw를 먼저 제거하고, 남은 Draw의 준비 및 상태 변경 비용을 줄이는 방식**으로 최적화되어 있다.
