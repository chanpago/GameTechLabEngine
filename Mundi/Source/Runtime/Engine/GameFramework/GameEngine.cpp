#include "pch.h"
#include "GameEngine.h"
#include "USlateManager.h"
#include "SelectionManager.h"
#include "FViewport.h"
#include "PlayerCameraManager.h"
#include <ObjManager.h>
#include "FAudioDevice.h"
#include <sol/sol.hpp>
#include "GameModeBase.h"
#include "InputManager.h"
#include "Source/Editor/FBX/FbxLoader.h"
#include "Source/Runtime/Network/NetworkManager.h"
#include "StaticMeshActor.h"
#include "StaticMeshComponent.h"
#include "CameraActor.h"
#include "AmbientLightActor.h"
#include "AmbientLightComponent.h"
#include "DirectionalLightActor.h"
#include "DirectionalLightComponent.h"
#include <shellapi.h>

#pragma comment(lib, "shell32")

#include "BlueprintGraph/BlueprintActionDatabase.h"

float UGameEngine::ClientWidth = 1024.0f;
float UGameEngine::ClientHeight = 1024.0f;

static void LoadIniFile()
{
    std::ifstream infile("editor.ini");
    if (!infile.is_open()) return;

    std::string line;
    while (std::getline(infile, line))
    {
        if (line.empty() || line[0] == ';') continue;
        size_t delimiterPos = line.find('=');
        if (delimiterPos != FString::npos)
        {
            FString key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            EditorINI[key] = value;
        }
    }
}

static void SaveIniFile()
{
    std::ofstream outfile("editor.ini");
    for (const auto& pair : EditorINI)
        outfile << pair.first << " = " << pair.second << std::endl;
}

UGameEngine::UGameEngine()
{

}

UGameEngine::~UGameEngine()
{
    // Cleanup is now handled in Shutdown()
    // Do not call FObjManager::Clear() here due to static destruction order
}

void UGameEngine::ConfigureFromCommandLine(const wchar_t* CommandLine)
{
    if (!CommandLine) return;
    int ArgumentCount = 0;
    LPWSTR* Arguments = CommandLineToArgvW(CommandLine, &ArgumentCount);
    if (!Arguments) return;

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const FWideString Argument = Arguments[Index];
        if (Argument == L"-net")
        {
            bNetworkMode = true;
            StartupSceneName = "NetworkSample";
        }
        else if (Argument == L"-udp-movement" || Argument == L"-udp-movement=on")
        {
            bUseUdpMovement = true;
        }
        else if (Argument == L"-tcp-movement" || Argument == L"-udp-movement=off")
        {
            bUseUdpMovement = false;
        }
        else if (Argument == L"-reconciliation" || Argument == L"-reconciliation=on")
        {
            bUseServerReconciliation = true;
        }
        else if (Argument == L"-no-reconciliation" || Argument == L"-reconciliation=off")
        {
            bUseServerReconciliation = false;
        }
        else if (Argument.rfind(L"-scene=", 0) == 0)
        {
            StartupSceneName = WideToUTF8(Argument.substr(7));
        }
        else if (Argument == L"-scene" && Index + 1 < ArgumentCount)
        {
            StartupSceneName = WideToUTF8(Arguments[++Index]);
        }
        else if (Argument.rfind(L"-server=", 0) == 0)
        {
            NetworkServerAddress = WideToUTF8(Argument.substr(8));
        }
        else if (Argument == L"-server" && Index + 1 < ArgumentCount)
        {
            NetworkServerAddress = WideToUTF8(Arguments[++Index]);
        }
        else if (Argument.rfind(L"-port=", 0) == 0)
        {
            try
            {
                const int ParsedPort = std::stoi(Argument.substr(6));
                if (ParsedPort >= 1 && ParsedPort <= 65535) NetworkServerPort = static_cast<uint16>(ParsedPort);
            }
            catch (...) {}
        }
        else if (Argument == L"-port" && Index + 1 < ArgumentCount)
        {
            try
            {
                const int ParsedPort = std::stoi(Arguments[++Index]);
                if (ParsedPort >= 1 && ParsedPort <= 65535) NetworkServerPort = static_cast<uint16>(ParsedPort);
            }
            catch (...) {}
        }
    }
    LocalFree(Arguments);
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void UGameEngine::GetViewportSize(HWND hWnd)
{
    RECT clientRect{};
    GetClientRect(hWnd, &clientRect);

    ClientWidth = static_cast<float>(clientRect.right - clientRect.left);
    ClientHeight = static_cast<float>(clientRect.bottom - clientRect.top);

    if (ClientWidth <= 0) ClientWidth = 1;
    if (ClientHeight <= 0) ClientHeight = 1;

    //레거시
    extern float CLIENTWIDTH;
    extern float CLIENTHEIGHT;

    CLIENTWIDTH = ClientWidth;
    CLIENTHEIGHT = ClientHeight;
}

LRESULT CALLBACK UGameEngine::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Input first
    INPUT.ProcessMessage(hWnd, message, wParam, lParam);

    switch (message)
    {
    case WM_SIZE:
    {
        WPARAM SizeType = wParam;
        if (SizeType != SIZE_MINIMIZED)
        {
            GetViewportSize(hWnd);

            UINT NewWidth = static_cast<UINT>(ClientWidth);
            UINT NewHeight = static_cast<UINT>(ClientHeight);
            GEngine.GetRHIDevice()->OnResize(NewWidth, NewHeight);
#ifdef _GAME
            if (GEngine.GameViewport.get())
            {
                GEngine.GameViewport.get()->Resize(0, 0, NewWidth, NewHeight);
            }
#endif
            // Save CLIENT AREA size (will be converted back to window size on load)
            EditorINI["WindowWidth"] = std::to_string(NewWidth);
            EditorINI["WindowHeight"] = std::to_string(NewHeight);
        }
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

UWorld* UGameEngine::GetDefaultWorld()
{
    if (!WorldContexts.IsEmpty() && WorldContexts[0].World)
    {
        return WorldContexts[0].World;
    }
    return nullptr;
}

bool UGameEngine::CreateMainWindow(HINSTANCE hInstance)
{
    // 윈도우 생성
    WCHAR WindowClass[] = L"JungleWindowClass";
    const WCHAR* Title = bNetworkMode ? L"GameTechLabEngine - Network Client" : L"Future Engine";
    HICON hIcon = (HICON)LoadImageW(NULL, L"Data\\Icon\\Future.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, hIcon, 0, 0, 0, WindowClass };
    RegisterClassW(&wndclass);

    // 전체화면 모드 설정
    // 주 모니터의 해상도 가져오기
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Network sample은 여러 client를 동시에 띄울 수 있도록 windowed로 실행한다.
    const DWORD windowStyle = bNetworkMode ? (WS_OVERLAPPEDWINDOW | WS_VISIBLE) : (WS_POPUP | WS_VISIBLE);
    const int WindowWidth = bNetworkMode ? 1024 : screenWidth;
    const int WindowHeight = bNetworkMode ? 720 : screenHeight;
    HWnd = CreateWindowExW(0, WindowClass, Title, windowStyle,
        bNetworkMode ? CW_USEDEFAULT : 0, bNetworkMode ? CW_USEDEFAULT : 0, WindowWidth, WindowHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!HWnd)
        return false;

    //종횡비 계산
    GetViewportSize(HWnd);
    return true;
}

bool UGameEngine::Startup(HINSTANCE hInstance)
{
    LoadIniFile();

    if (!CreateMainWindow(hInstance))
        return false;

    // 디바이스 리소스 및 렌더러 생성
    RHIDevice.Initialize(HWnd, bNetworkMode);
    Renderer = std::make_unique<URenderer>(&RHIDevice);

    // Initialize audio device for game runtime
    FAudioDevice::Initialize();

    // 뷰포트 생성
    GameViewport = std::make_unique<FViewport>();
    if (!GameViewport->Initialize(0, 0, ClientWidth, ClientHeight, GetRHIDevice()->GetDevice()))
    {
        UE_LOG("Failed to initialize GameViewport!");
        return false;
    }

    // 매니저 초기화
    UI.Initialize(HWnd, RHIDevice.GetDevice(), RHIDevice.GetDeviceContext());
    INPUT.Initialize(HWnd);

    FObjManager::Preload();
    FAudioDevice::Preload();
    UFbxLoader::PreLoad();
    RESOURCE.PreloadParticles();
    RESOURCE.PreloadPhysicsAssets();

    ///////////////////////////////////
    WorldContexts.Add(FWorldContext(NewObject<UWorld>(), EWorldType::Game));
    GWorld = WorldContexts[0].World;
    GWorld->Initialize();
    GWorld->bPie = true;
    GWorld->InitializePhysScene();  // PhysX 물리 씬 초기화
    ///////////////////////////////////

    // 실행 인자로 기존 게임과 network sample scene을 분리한다.
    const FString StartupScenePath = GDataDir + "/Scenes/" + StartupSceneName + ".scene";
    if (!GWorld->LoadLevelFromFile(UTF8ToWide(StartupScenePath)))
    {
        UE_LOG("Failed to load startup scene: %s", StartupScenePath.c_str());
        return false;
    }

    if (bNetworkMode)
    {
        SetupNetworkSampleWorld();
        if (!GWorld->EnableNetworkClient(NetworkServerAddress, NetworkServerPort,
            bUseUdpMovement, bUseServerReconciliation))
        {
            UE_LOG("[Network] Initial connection failed: %s:%u", NetworkServerAddress.c_str(), NetworkServerPort);
        }
        INPUT.SetCursorVisible(true);
        INPUT.ReleaseCursor();
    }
    // 기존 게임 mode는 network sample에서 생성하지 않는다.
    else
    {
        if (GWorld->GetGameMode() == nullptr)
        {
            AGameModeBase* GM = GWorld->SpawnActor<AGameModeBase>(FTransform());
            GWorld->SetGameMode(GM);
        }
        GWorld->GetGameMode()->StartPlay();
    }

    // 마우스는 게임 상태에 따라 GameState에서 제어
    // PressAnyKey/MainMenu 상태에서는 마우스가 보이고, Fighting 상태에서는 숨겨짐

    GPU_PROFILER.Initialize(&RHIDevice);

    bPlayActive = true;
    bRunning = true;
    return true;
}

void UGameEngine::Tick(float DeltaSeconds)
{
    //@TODO UV 스크롤 입력 처리 로직 이동
    HandleUVInput(DeltaSeconds);

    for (auto& WorldContext : WorldContexts)
    {
        WorldContext.World->Tick(DeltaSeconds);
    }

    UI.Update(DeltaSeconds);
    INPUT.Update();

    FAudioDevice::Update();
}

void UGameEngine::Render()
{
    Renderer->BeginFrame();

    if (GWorld)
    {
        APlayerCameraManager* PlayerCameraManager = GWorld->GetPlayerCameraManager();
        if (PlayerCameraManager)
        {
            PlayerCameraManager->CacheViewport(GameViewport.get());
            Renderer->SetCurrentViewportSize(GameViewport->GetSizeX(), GameViewport->GetSizeY());

            FMinimalViewInfo* MinimalViewInfo = PlayerCameraManager->GetCurrentViewInfo();
            TArray<FPostProcessModifier> Modifiers = PlayerCameraManager->GetModifiers();

            FSceneView CurrentViewInfo(MinimalViewInfo, &GWorld->GetRenderSettings());
            CurrentViewInfo.Modifiers = Modifiers;

            Renderer->RenderSceneForView(GWorld, &CurrentViewInfo, GameViewport.get());
        }
    }

    // ImGui 렌더링 (게임 UI - 체력바 등)
    UI.Render();
    if (GWorld && GWorld->GetNetworkManager())
    {
        GWorld->GetNetworkManager()->DrawDebugHUD();
    }
    UI.EndFrame();

    Renderer->EndFrame();
}

void UGameEngine::SetupNetworkSampleWorld()
{
    if (!GWorld) return;

    // Scene 파일은 network mode 선택 단위이고, runtime 전용 actor는 code에서 구성한다.
    // 이 actor들은 editor 저장 데이터나 기존 FINALgameScene을 변경하지 않는다.
    AStaticMeshActor* Ground = GWorld->SpawnActor<AStaticMeshActor>(
        FTransform(FVector(0.0f, 0.0f, -0.5f), FQuat::Identity(), FVector(15.0f, 15.0f, 0.5f)));
    if (Ground)
    {
        Ground->ObjectName = "NetworkSample_Ground";
        if (UStaticMeshComponent* Mesh = Ground->GetStaticMeshComponent())
        {
            if (Mesh->GetMaterial(0)) Mesh->SetMaterialColorByUser(0, "DiffuseColor", FLinearColor(0.18f, 0.22f, 0.28f, 1.0f));
        }
    }

    ADirectionalLightActor* Directional = GWorld->SpawnActor<ADirectionalLightActor>();
    if (Directional)
    {
        Directional->SetActorRotation(FQuat::FromAxisAngle(FVector(0.5f, -1.0f, 0.2f).GetSafeNormal(), DegreesToRadians(50.0f)));
        Directional->GetLightComponent()->SetIntensity(2.0f);
    }
    AAmbientLightActor* Ambient = GWorld->SpawnActor<AAmbientLightActor>();
    if (Ambient) Ambient->GetLightComponent()->SetIntensity(0.35f);

    ACameraActor* Camera = GWorld->SpawnActor<ACameraActor>();
    if (Camera)
    {
        const FVector CameraLocation(-14.0f, -14.0f, 11.0f);
        Camera->SetActorLocation(CameraLocation);
        Camera->SetForward((FVector(0.0f, 0.0f, 1.0f) - CameraLocation).GetSafeNormal());
    }
}

void UGameEngine::HandleUVInput(float DeltaSeconds)
{
    UInputManager& InputMgr = UInputManager::GetInstance();
    if (InputMgr.IsKeyPressed('T'))
    {
        bUVScrollPaused = !bUVScrollPaused;
        if (bUVScrollPaused)
        {
            UVScrollTime = 0.0f;
            if (Renderer) Renderer->GetRHIDevice()->UpdateUVScrollConstantBuffers(UVScrollSpeed, UVScrollTime);
        }
    }
    if (!bUVScrollPaused)
    {
        UVScrollTime += DeltaSeconds;
        if (Renderer) Renderer->GetRHIDevice()->UpdateUVScrollConstantBuffers(UVScrollSpeed, UVScrollTime);
    }

}

void UGameEngine::MainLoop()
{
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);

    LARGE_INTEGER PrevTime, CurrTime;
    QueryPerformanceCounter(&PrevTime);

    MSG msg;

    while (bRunning)
    {
        QueryPerformanceCounter(&CurrTime);
        float DeltaSeconds = static_cast<float>((CurrTime.QuadPart - PrevTime.QuadPart) / double(Frequency.QuadPart));
        PrevTime = CurrTime;

        // DeltaTime 제한 제거 (프레임 제한 없음)
        // DeltaSeconds = FMath::Min(DeltaSeconds, 1.0f / 30.0f); // 이 줄 삭제됨

        // 처리할 메시지가 더 이상 없을때 까지 수행
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                bRunning = false;
                break;
            }
        }

        if (!bRunning) break;

        Tick(DeltaSeconds);
        Render();

        // Shader Hot Reloading - Call AFTER render to avoid mid-frame resource conflicts
        // This ensures all GPU commands are submitted before we check for shader updates
        UResourceManager::GetInstance().CheckAndReloadShaders(DeltaSeconds);
    }
}

void UGameEngine::Shutdown()
{
    // AudioDevice 종료 (반드시 ObjectFactory::DeleteAll 이전에 호출)
    // 컴포넌트들이 아직 Tick 중일 수 있으므로 먼저 오디오 시스템을 정지시켜야 함
    FAudioDevice::Shutdown();

    // 월드부터 삭제해야 DeleteAll 때 문제가 없음
    for (FWorldContext WorldContext : WorldContexts)
    {
        ObjectFactory::DeleteObject(WorldContext.World);
    }
    WorldContexts.clear();

    // Delete all UObjects (Components, Actors, Resources)
    // Resource destructors will properly release D3D resources
    ObjectFactory::DeleteAll(true);

    // Clear FObjManager's static map BEFORE static destruction 
    // This must be done in Shutdown() (before main() exits) rather than ~UGameEngine()
    // because ObjStaticMeshMap is a static member variable that may be destroyed
    // before the global GEngine variable's destructor runs
    FObjManager::Clear();

    // IMPORTANT: Explicitly release Renderer before RHIDevice destructor runs
    // Renderer may hold references to D3D resources
    Renderer.reset();

    // Explicitly release D3D11RHI resources before global destruction
    RHIDevice.Release();

    SaveIniFile();
}
