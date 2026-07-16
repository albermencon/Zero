#include "pch.h"
#include "Engine/Layers/ImGuiLayer.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "Engine/Application.h"
#include "Engine/Window.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/core/FrameData.h"
#include <GLFW/glfw3.h>
#include "Engine/ConfigSystem.h"

namespace Zero
{
    ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}
    ImGuiLayer::~ImGuiLayer() {}

    void ImGuiLayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef IMGUI_HAS_DOCK
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
#endif


        ImGui::StyleColorsDark();

        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

        uint32_t backend = ConfigSystem::Get().GetUInt("Window", "Backend", 0);
        if (backend == 0) // Vulkan
        {
            ImGui_ImplGlfw_InitForVulkan(window, true);
        }
        else // OpenGL
        {
            ImGui_ImplGlfw_InitForOpenGL(window, true);
        }

        Renderer::Get().InitImGui();
    }

    void ImGuiLayer::OnDetach()
    {
        Renderer::Get().ShutdownImGui();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::Begin()
    {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::End(FrameData* currentFrame)
    {
        ImGui::Render();
        currentFrame->imguiFrame.CopyFrom(ImGui::GetDrawData());
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        event.Handled |= event.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
        event.Handled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
    }
}
