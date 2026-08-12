# 🎮 GameTechLabEngine 렌더링 아키텍처

> 기준: 2026-08-12 현재 구현
>
> Direct3D 11 기반의 **Forward Renderer**이며, View 단위로 가시성 판정부터 최종 BackBuffer 합성까지 수행한다.

---

## 🚀 1. 전체 구조

```text
Engine Frame
  → URenderer::BeginFrame
  → FViewportClient가 FSceneView 생성
  → URenderer::RenderSceneForView
  → View마다 임시 FSceneRenderer 생성
  → Visibility / Shadow / Lighting / Base / Post Process / Editor Pass
  → SceneColor를 BackBuffer에 Composite
  → URenderer::EndFrame → Present
```

| 계층 | 책임 | 수명 |
|---|---|---|
| `D3D11RHI` | Device, Context, SwapChain, Render Target과 Pipeline State 관리 | 엔진 수명 |
| `URenderer` | 프레임 시작/종료, View 렌더 진입, 장기 GPU Context와 Cache 소유 | 엔진 수명 |
| `FSceneView` | 카메라 행렬, Frustum, ViewRect, ViewMode, Shader Macro | Viewport Draw 단위 |
| `FSceneRenderer` | 한 World·View의 렌더 패스 조율과 임시 Proxy/Batch 관리 | View 렌더 1회 |
| `UWorld` | RenderSettings, LightManager, WorldPartition과 SceneComponent 소유 | World 수명 |

`FSceneRenderer`는 매 View마다 새로 생성되는 Transient Orchestrator다. 반면 비용이 큰 `FTileLightCuller`, `FGPUOcclusionCuller`, StaticMesh Draw Cache는 `URenderer`가 보관해 View와 Frame 사이에서 재사용한다.

> 📸 **코드 캡처 1 — 렌더 진입과 수명**
> - 위치: `Renderer/FViewportClient.cpp::Draw()`, `Renderer/Renderer.cpp::RenderSceneForView()`
> - 강조점: Viewport가 `FSceneView`를 만들고, View마다 임시 `FSceneRenderer`를 생성하는 흐름

---

## 🧭 2. View와 렌더 설정

`FSceneView`는 하나의 관점에서 렌더링하는 데 필요한 데이터를 묶는다.

- View / Projection Matrix와 카메라 위치·회전
- 6면 View Frustum
- Viewport 내부의 `ViewRect`
- Perspective / Orthographic 투영 정보
- ViewMode별 Shader Macro
- Post Process Modifier 목록
- World의 `URenderSettings` 참조

지원 ViewMode는 Lit Phong/Gouraud/Lambert, Unlit, World Normal, PBR Mask, Wireframe, Scene Depth다. 기능별 Show Flag는 Mesh, Lighting, Shadow, Particle, Post Process와 각종 Culling/Sorting 경로를 독립적으로 제어한다.

에디터의 다중 Viewport와 PIE 카메라는 동일한 `FSceneView → RenderSceneForView` 진입점을 공유한다.

---

## 🔄 3. View 렌더 패스 순서

Lit View의 주요 순서는 다음과 같다.

```text
PrepareView
  → GatherVisibleProxies
  → Shadow Maps
  → Light Buffer Update
  → Camera Depth Prepass          [Forward+ 요청 시]
  → Tile Light Culling            [Forward+ 요청 시]
  → Sky
  → Opaque Base Pass
  → GPU Occlusion Submit
  → Decal
  → Particle
  → Post Process Chain
  → Tile Debug                    [선택]
  → Editor / Debug Primitives     [에디터]
  → FXAA                          [선택]
  → Composite to BackBuffer
  → Final Overlay Lines
```

Wireframe과 SceneDepth는 별도 경로를 사용하지만, 동일한 가시성 수집과 Opaque Batch 제출 구조를 재사용한다.

| 단계 | 핵심 역할 |
|---|---|
| Visibility | BVH Frustum과 이전 GPU HZB 결과로 렌더 후보 축소 |
| Shadow | Light별 Render Request를 Shadow Atlas에 렌더링 |
| Lighting | Light Buffer 갱신 및 선택적 Forward+ Tile List 생성 |
| Base | Sky, Opaque Mesh, ID, Depth 기록 |
| Secondary | Decal과 Particle 합성 |
| Post | Fog, Fade, Vignette, Bloom, Gamma, DOF, FXAA |
| Editor | Grid, Bounds, Icon, Gizmo와 Always-on-top Line |

> 📸 **코드 캡처 2 — 전체 Pass Orchestration**
> - 위치: `Renderer/SceneRenderer.cpp::FSceneRenderer::Render()`
> - 강조점: Gather 이후 Shadow, ViewMode별 경로, Post Process, Editor Pass, Composite 순서

---

## 📦 4. Scene 수집과 Draw 제출

### Visibility와 Proxy 분류

`GatherVisibleProxies()`는 World의 컴포넌트를 용도별 배열로 분리한다.

| 분류 | 데이터 |
|---|---|
| Main Scene | Mesh, Billboard, Decal, Text, Particle |
| Local Lights | Point, Spot Light |
| Scene Globals | Directional/Ambient Light, Fog, Sky |
| Editor | Grid/Line/Icon, Overlay Gizmo |

Mesh는 BVH Frustum Culling과 GPU HZB Occlusion을 거친다. 전역 컴포넌트는 별도 등록 목록에서 수집해 공간 BVH 전체 순회를 피한다.

### MeshBatch

컴포넌트는 `CollectMeshBatches()`를 통해 `FMeshBatchElement`를 만든다. Batch에는 Shader, Material, Vertex/Index Buffer, Draw Parameter와 오브젝트 데이터가 들어 있다.

Opaque Pass는 다음 두 경로를 지원한다.

- 일반 경로: 현재 Visible Mesh에서 매 프레임 Batch 생성
- StaticMesh Cache 경로: 영구 Batch Metadata와 정렬 Index를 재사용하고 현재 Visible Set만 Draw

Material Sorting은 Shader → Material → Buffer 순으로 Batch를 정렬한다. Draw Loop는 현재 GPU 상태를 캐시해 중복 Bind를 건너뛰지만, 살아남은 각 StaticMesh는 과제 제약에 따라 개별 `DrawIndexed`로 제출한다.

> 📸 **코드 캡처 3 — Proxy에서 Draw까지**
> - 위치: `Renderer/SceneRenderer.cpp::GatherVisibleProxies()`, `RenderOpaquePass()`, `DrawMeshBatches()`
> - 강조점: 가시성 결과 → Batch 생성/캐시 → 정렬 → 상태 캐시 → 개별 Draw 흐름

---

## 💡 5. Lighting과 Shadow

현재 Base Pass에서 조명을 계산하는 Forward 방식이다. Material Shader는 공통 Light Buffer와 Point/Spot Structured Buffer를 읽고, `SF_PBR`에 따라 Direct-light PBR 또는 기존 조명 경로를 선택한다.

### Forward+ Tile Light Culling

로컬 라이트가 많을 때는 선택적으로 Forward+ 경로를 사용한다.

1. Camera Depth Prepass로 SceneDepth를 준비한다.
2. Compute Shader가 화면을 Tile로 나누고 Tile별 Min/Max Depth를 구한다.
3. Point/Spot Light를 Tile과 교차 검사한다.
4. Pixel Shader는 현재 Tile의 Light Index만 순회한다.
5. 준비 실패 시 전체 라이트를 순회하는 기존 Forward 경로로 Fallback한다.

### Shadow

`FLightManager`는 Light Buffer와 Shadow 리소스를 World 단위로 관리한다.

- Directional/Spot Light: 2D Shadow Atlas
- Point Light: Cube Texture Array
- Shadow AA: PCF 또는 VSM
- Light Component가 `FShadowRenderRequest`를 생성하고 `FSceneRenderer`가 Atlas 영역별 Depth Pass 실행

> 📸 **코드 캡처 4 — Lighting 경로**
> - 위치: `Renderer/SceneRenderer.cpp::Render()`, `PerformTileLightCulling()`, `RenderShadowMaps()`
> - 강조점: Depth Prepass → Tile Culling → Base Pass 연결과 Light별 Shadow Request 처리

---

## 🖼️ 6. Render Target과 Post Process

`D3D11RHI`가 주요 렌더 리소스와 상태를 소유한다.

| 리소스 | 용도 |
|---|---|
| BackBuffer | 최종 Present 대상 |
| SceneColor 2개 | 후처리용 Source/Target Ping-pong |
| SceneDepth | Depth Test, HZB Occlusion, Forward+와 Depth 기반 효과 |
| ID Buffer | 에디터 Object Picking |
| Shadow Atlas | Directional/Spot/Point Shadow |
| DOF Buffer 4개 | Near/Far Field와 수평/수직 Blur |
| Bloom Buffer 2개 | Bloom 중간 결과 |

Post Process Modifier는 Priority와 Weight로 정렬된다. Fog, Fade, Vignette, Bloom, Gamma, DOF가 SceneColor를 순차 처리하며, 각 Full-screen Pass는 `FSwapGuard`를 이용해 Source/Target을 교환하고 SRV 바인딩을 정리한다. 이후 FXAA를 적용하고 최종 SceneColor를 BackBuffer에 Blit한다.

Editor Primitive는 일반 Post Process 이후에 렌더링되어 게임 화면 효과의 영향을 받지 않는다. Final Overlay Line은 BackBuffer에 직접 그려 항상 최상단에 표시한다.

> 📸 **코드 캡처 5 — Ping-pong과 최종 합성**
> - 위치: `Renderer/SceneRenderer.cpp::RenderPostProcessingPasses()`, `CompositeToBackBuffer()`
> - 강조점: Modifier 정렬/실행, `FSwapGuard`, Current Source SRV를 BackBuffer로 Blit하는 흐름

---

## ⚙️ 7. RHI 경계와 리소스 소유권

`D3D11RHI`는 현재 완전한 범용 RHI 추상화라기보다 엔진의 Direct3D 11 Backend Facade다.

- Device / Immediate Context / SwapChain 생성과 해제
- RTV, DSV, SRV, UAV 및 Buffer 생성
- Rasterizer, Depth-Stencil, Blend, Sampler State 관리
- Constant/Structured Buffer 갱신
- Render Target 전환과 Present
- Compute Dispatch 및 Resource Unbind

상위 Renderer가 일부 `ID3D11DeviceContext` API를 직접 호출하므로 D3D11 의존성은 Renderer 계층까지 노출되어 있다.

| Persistent Owner | 소유 리소스 |
|---|---|
| `D3D11RHI` | SwapChain, Scene/Depth/ID/DOF/Bloom Target, 공통 State와 Buffer |
| `URenderer` | Tile Culler, Viewport별 GPU Occlusion Context, World/View별 StaticMesh Cache |
| `UWorld::FLightManager` | Light Structured Buffer와 Shadow Atlas |
| `FSceneRenderer` | 해당 View 렌더 중에만 사용하는 Proxy와 MeshBatch 배열 |

---

## 🧩 8. 확장 지점과 현재 제약

- 새 Scene Pass는 현재 `FSceneRenderer::Render()`의 명시적 순서에 추가한다.
- 새 Post Process는 Pass 클래스를 만들고 Modifier Type 및 Dispatch Switch에 등록한다.
- 새 ViewMode는 `EViewMode`와 `FSceneView::CreateViewShaderMacros()`를 함께 확장한다.
- 새 렌더 기능 토글은 `EEngineShowFlags`와 `URenderSettings`를 사용한다.
- Render Graph가 없으므로 Pass 간 Resource 상태와 실행 순서는 수동으로 관리한다.
- Immediate Context 기반이라 Command Recording/병렬 제출 구조는 아니다.
- ID Picking은 현재 Staging Buffer를 동기 Map하므로 Picking Frame에서 Stall 가능성이 있다.
- `FSceneRenderer`에 Orchestration 책임이 집중되어 있어 Pass가 늘수록 분리가 필요한 구조다.

---

## ✅ 9. 요약

이 엔진은 `FSceneView`를 입력으로 받아 `FSceneRenderer`가 모든 Pass를 순서대로 실행하는 **View 중심 Forward Rendering 구조**다.

핵심 특성은 다음과 같다.

1. World Component를 Proxy와 MeshBatch로 변환해 제출한다.
2. Frustum/GPU Occlusion으로 Draw 후보를 줄이고 Material Sorting/Static Cache로 제출 비용을 줄인다.
3. 선택적 Forward+ 경로로 Tile별 로컬 라이트 목록을 만든다.
4. Shadow Atlas, SceneColor Ping-pong, 공유 SceneDepth와 ID Buffer를 사용한다.
5. 후처리 후 BackBuffer에 합성하고 Editor Overlay를 별도로 렌더링한다.
6. 장기 GPU 리소스는 RHI/Renderer/World가 소유하고, `FSceneRenderer`는 View 단위 임시 상태만 가진다.
