#include "pch.h"
#include "Engine/Layers/ImGuiLayer.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "Engine/Application.h"
#include "Engine/Window.h"
#include "Graphics/Renderer/Renderer.h"
#include <Engine/Graphics/FrameData.h>
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

        // Apply per-monitor DPI scaling to the UI. The GLFW backend only sets
        // io.DisplaySize / io.DisplayFramebufferScale and does NOT scale fonts,
        // so without this the UI is laid out in native pixels and appears tiny
        // on high-DPI displays
        const float contentScale = ImGui_ImplGlfw_GetContentScaleForWindow(window);
        if (contentScale > 0.0f)
            ImGui::GetStyle().FontScaleDpi = contentScale;

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
        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Per-monitor DPI scaling. GLFW reports physical pixels on a DPI-aware
        // process and the ImGui GLFW backend does not scale the UI, so:
        //  - style.FontScaleDpi scales the whole UI by the monitor content scale.
        //  - The main viewport's FramebufferScale is what ImGui uses as the font
        //    rasterizer density. It stays 1.0 on Windows (even at 175% scaling),
        //    which would rasterize glyphs at 1x and make text look soft when
        //    scaled. Override it with the content scale so glyphs are rasterized
        //    at DPI resolution (crisp on 4K). ImDrawData::FramebufferScale is
        //    taken from io.DisplayFramebufferScale, so this does not affect the
        //    render projection.
        const float contentScale = ImGui_ImplGlfw_GetContentScaleForWindow(window);
        if (contentScale > 0.0f)
        {
            ImGui::GetStyle().FontScaleDpi = contentScale;
            if (ImGuiViewport* viewport = ImGui::GetMainViewport())
                viewport->FramebufferScale = ImVec2(contentScale, contentScale);
        }
    }

    void ImGuiLayer::End(FrameData* currentFrame)
    {
        ImGui::Render();

        // The engine defers rendering to a render thread, but ImGui rebuilds its
        // platform texture list every frame. Upload any pending font-atlas texture
        // updates (e.g. glyphs rasterized at the current DPI scale) synchronously
        // on the render thread right now, while the atlas data is still valid, so
        // the deferred frame never acts on stale texture state.
        bool pendingTextureUpdates = false;
        for (const ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        {
            if (tex->Status != ImTextureStatus_OK)
            {
                pendingTextureUpdates = true;
                break;
            }
        }
        if (pendingTextureUpdates)
            Renderer::Get().UpdateImGuiTextures();

        currentFrame->imguiFrame.CopyFrom(ImGui::GetDrawData());
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        event.Handled |= event.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
        event.Handled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
    }
}
