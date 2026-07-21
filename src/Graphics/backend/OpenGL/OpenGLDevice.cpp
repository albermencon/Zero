#include "pch.h"
#include "OpenGLDevice.h"
#include "Debug/OpenGLDebug.h"
#include "Buffer/OpenGLBuffer.h"
#include "Graphics/backend/OpenGL/Translator/OpenGLTranslator.h"
#include "Graphics/core/FrameData.h"
#include <Engine/Window.h>
#include <Engine/Log.h>
#include <Engine/Graphics/RenderResources.h>
#include <Engine/Graphics/ImGuiFrame.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

namespace Zero
{
    OpenGLDevice::OpenGLDevice(Window* window)
        : m_window(window)
    {
        init();
    }

    OpenGLDevice::~OpenGLDevice()
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
        glfwMakeContextCurrent(glfwWindow);

        if (m_shaderProgram != 0)
        {
            glDeleteProgram(m_shaderProgram);
        }
        if (m_vao != 0)
        {
            glDeleteVertexArrays(1, &m_vao);
        }

        glfwMakeContextCurrent(nullptr);
    }

    void OpenGLDevice::init()
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
        glfwMakeContextCurrent(glfwWindow);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        if (!status)
        {
            ZERO_CORE_CRITICAL("Failed to initialize Glad!");
            return;
        }

#ifdef ZERO_DEBUG
        int flags;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); // Forces callback to be synchronous (useful for breakpoints)
            glDebugMessageCallback(OpenGLDebugCallback, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }
#endif

        const char* vertexShaderSource = R"(#version 460 core
        layout(location = 0) out vec3 fragColor;
        void main() {
            vec2 positions[3] = vec2[](
                vec2(0.0, 0.5),
                vec2(-0.5, -0.5),
                vec2(0.5, -0.5)
            );
            vec3 colors[3] = vec3[](
                vec3(1.0, 0.0, 0.0),
                vec3(0.0, 1.0, 0.0),
                vec3(0.0, 0.0, 1.0)
            );
            gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
            fragColor = colors[gl_VertexID];
        })";

        const char* fragmentShaderSource = R"(#version 460 core
        layout(location = 0) in vec3 fragColor;
        out vec4 outColor;
        void main() {
            outColor = vec4(fragColor, 1.0);
        })";

        unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vertexShaderSource, nullptr);
        glCompileShader(vs);

        unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fs);

        m_shaderProgram = glCreateProgram();
        glAttachShader(m_shaderProgram, vs);
        glAttachShader(m_shaderProgram, fs);
        glLinkProgram(m_shaderProgram);

        glDeleteShader(vs);
        glDeleteShader(fs);

        glGenVertexArrays(1, &m_vao);

        glfwMakeContextCurrent(nullptr);
    }

    void OpenGLDevice::SwapBuffers()
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
        PresentModePolicy requested = m_window->GetPresentModePolicy();
        if (!m_policyInitialized || m_currentPolicy != requested)
        {
            switch (requested)
            {
                case PresentModePolicy::VSync:
                    glfwSwapInterval(1);
                    break;
                default:
                    glfwSwapInterval(0);
                    break;
            }
            m_currentPolicy = requested;
            m_policyInitialized = true;
        }
        glfwSwapBuffers(glfwWindow);
    }

    bool OpenGLDevice::BeginFrame()
    {
        if (!m_contextMadeCurrentOnRenderThread)
        {
            GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
            glfwMakeContextCurrent(glfwWindow);
            m_contextMadeCurrentOnRenderThread = true;
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        return true;
    }

    void OpenGLDevice::InitImGui()
    {
        if (!m_contextMadeCurrentOnRenderThread)
        {
            GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
            glfwMakeContextCurrent(glfwWindow);
            m_contextMadeCurrentOnRenderThread = true;
        }

        ImGui_ImplOpenGL3_Init("#version 460 core");
        ImGui_ImplOpenGL3_CreateDeviceObjects();
    }

    void OpenGLDevice::ShutdownImGui()
    {
        ImGui_ImplOpenGL3_Shutdown();
    }

    void OpenGLDevice::RenderFrame(FrameData* frame)
    {
        GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
        int w, h;
        glfwGetFramebufferSize(glfwWindow, &w, &h);
        glViewport(0, 0, w, h);

        glUseProgram(m_shaderProgram);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);

        ImDrawData* drawData = frame->imguiFrame.GetNativeDrawData();
        if (drawData)
        {
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        }
    }

    void OpenGLDevice::OnFinished()
    {
        glFinish();
    }

    void OpenGLDevice::OnRenderSurfaceResize()
    {
        m_SwapChainDirty = true;
    }

    Buffer* OpenGLDevice::CreateBuffer(const BufferDesc& desc)
    {
        GLuint id = 0;
        glGenBuffers(1, &id);
        if (id == 0)
        {
            return nullptr;
        }

        GLbitfield flags = OpenGL::toGLBufferStorageFlags(desc.memory);

        // Allocate immutable storage without binding
        glNamedBufferStorage(id, desc.size, nullptr, flags);

        // Upload initial data without binding
        if (desc.initialData && desc.initialDataSize > 0)
        {
            glNamedBufferSubData(id, 0, desc.initialDataSize, desc.initialData);
        }

#ifdef ZERO_DEBUG
        if (desc.debugName)
        {
            glObjectLabel(GL_BUFFER, id, -1, desc.debugName);
        }
#endif

        return new OpenGLBuffer(id, desc.size, desc.usage, desc.memory);
    }

    void OpenGLDevice::DestroyBuffer(Buffer* buffer)
    {
        if (buffer)
        {
            delete buffer;
        }
    }

    void OpenGLDevice::CopyBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t sizeBytes)
    {
        if (!src || !dst) return;
        
        OpenGLBuffer* glSrc = static_cast<OpenGLBuffer*>(src);
        OpenGLBuffer* glDst = static_cast<OpenGLBuffer*>(dst);

        glCopyNamedBufferSubData(glSrc->GetHandle(), glDst->GetHandle(), srcOffset, dstOffset, sizeBytes);
    }

    void OpenGLDevice::FlushTransfers()
    {
        // Guarantee that all preceding buffer copies and mapped memory flushes 
        // are complete and visible to the graphics/compute pipelines.
        glMemoryBarrier(
            GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
            GL_ELEMENT_ARRAY_BARRIER_BIT |
            GL_UNIFORM_BARRIER_BIT |
            GL_SHADER_STORAGE_BARRIER_BIT |
            GL_COMMAND_BARRIER_BIT
        );
    }
}
