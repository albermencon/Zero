#include "pch.h"
#include <Engine/Window.h>
#include <Engine/Log.h>
#include <Engine/Graphics/RenderResources.h>
#include "OpenGLDevice.h"
#include "OpenGLBuffer.h"
#include "Graphics/backend/OpenGL/Translator/OpenGLTranslator.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
            ENGINE_CORE_CRITICAL("Failed to initialize Glad!");
            return;
        }

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
        glfwSwapBuffers(glfwWindow);
    }

    void OpenGLDevice::BeginFrame()
    {
        if (!m_contextMadeCurrentOnRenderThread)
        {
            GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
            glfwMakeContextCurrent(glfwWindow);
            m_contextMadeCurrentOnRenderThread = true;
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLDevice::EndFrame()
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

        glBindBuffer(GL_COPY_WRITE_BUFFER, id);
        glBufferStorage(GL_COPY_WRITE_BUFFER, desc.size, desc.initialData, flags);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

        return new OpenGLBuffer(id, desc.size, desc.usage, desc.memory);
    }

    void OpenGLDevice::DestroyBuffer(Buffer* buffer)
    {
        if (buffer)
        {
            delete buffer;
        }
    }

    void OpenGLDevice::UpdateBufferData(Buffer* buffer, const void* data, size_t offsetBytes, size_t sizeBytes)
    {
        if (!buffer || !data) return;
        OpenGLBuffer* glb = static_cast<OpenGLBuffer*>(buffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, glb->GetHandle());
        glBufferSubData(GL_COPY_WRITE_BUFFER, offsetBytes, sizeBytes, data);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
}
