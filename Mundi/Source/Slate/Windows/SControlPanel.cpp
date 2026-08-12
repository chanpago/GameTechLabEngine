#include "pch.h"
#include "SControlPanel.h"
#include "Windows/ControlPanelWindow.h"
#include "Windows/SceneWindow.h"

namespace
{
	// 50,000개 정적 액터 벤치마크에서 아웃라이너 UI 비용을 분리하기 위한 임시 스위치.
	constexpr bool bEnableSceneOutliner = false;
}

SControlPanel::SControlPanel()
{
	ControlPanelWidget=new UControlPanelWindow();
    SceneWindow = new USceneWindow();
}

SControlPanel::~SControlPanel()
{
    delete ControlPanelWidget;
    delete SceneWindow;
}

void SControlPanel::OnRender()
{
    ImGui::SetNextWindowPos(ImVec2(Rect.Min.X, Rect.Min.Y));
    ImGui::SetNextWindowSize(ImVec2(Rect.GetWidth(), Rect.GetHeight()));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("아웃라이너", nullptr, flags))
    {
        if (bEnableSceneOutliner && SceneWindow) {
            SceneWindow->RenderWidget();
        }
        if (ControlPanelWidget)
            ControlPanelWidget->RenderWidget();
    }
    ImGui::End();

}

void SControlPanel::OnUpdate(float deltaSecond)
{
	if (bEnableSceneOutliner && SceneWindow)
	{
		SceneWindow->Update();
	}
    ControlPanelWidget->Update();
}
