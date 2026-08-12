#pragma once
#include "pch.h"

class URenderer;
class D3D11RHI;
class UWorld;
class FViewport;

class UGameEngine final
{
public:
    UGameEngine();
    ~UGameEngine();

    bool Startup(HINSTANCE hInstance);
    void MainLoop();
    void Shutdown();
    void ConfigureFromCommandLine(const wchar_t* CommandLine);

    bool IsPlayActive() const { return bPlayActive; }
    bool IsPIEActive() const { return bPlayActive; }  // Game 모드에서는 PlayActive와 동일

    HWND GetHWND() const { return HWnd; }

    URenderer* GetRenderer() const { return Renderer.get(); }
    D3D11RHI* GetRHIDevice() { return &RHIDevice; }
    UWorld* GetDefaultWorld();
    const TArray<FWorldContext>& GetWorldContexts() { return WorldContexts; }

    void AddWorldContext(const FWorldContext& InWorldContext)
    {
        WorldContexts.push_back(InWorldContext);
    }

private:
    bool CreateMainWindow(HINSTANCE hInstance);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void GetViewportSize(HWND hWnd);

    void Tick(float DeltaSeconds);
    void Render();

    void HandleUVInput(float DeltaSeconds);
    void SetupNetworkSampleWorld();
public:
    //게임의 메인 뷰포트
    std::unique_ptr<FViewport> GameViewport;
private:
    //윈도우 핸들
    HWND HWnd = nullptr;

    //디바이스 리소스 및 렌더러
    D3D11RHI RHIDevice;
    std::unique_ptr<URenderer> Renderer;

  

    //월드 핸들
    TArray<FWorldContext> WorldContexts;

    //틱 상태
    bool bRunning = false;
    bool bUVScrollPaused = true;
    bool bPlayActive = false;
    float UVScrollTime = 0.0f;
    FVector2D UVScrollSpeed = FVector2D(0.5f, 0.5f);

    // Standalone launch options
    bool bNetworkMode = false;
    bool bUseUdpMovement = true;
    bool bUseServerReconciliation = true;
    FString StartupSceneName = "FINALgameScene";
    FString NetworkServerAddress = "127.0.0.1";
    uint16 NetworkServerPort = 7777;

    // 클라이언트 사이즈
    static float ClientWidth;
    static float ClientHeight;
};
