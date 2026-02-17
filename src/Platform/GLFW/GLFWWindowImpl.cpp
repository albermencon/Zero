#include "pch.h"
#include "GLFWWindowImpl.h"
#include "Platform/InputInternal.h"
#include <Engine/Input/KeyCode.h>
#include <Engine/Input/MouseButton.h>

#include <Engine/Log.h>

#include <Engine/ApplicationEvent.h>
#include <Engine/KeyEvent.h>
#include <Engine/MouseEvent.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace VoxelEngine
{
    static bool s_GLFWInitialized = false;

    GLFWWindowImpl::GLFWWindowImpl(const WindowProps &props)
    {
        Init(props);
    }

    GLFWWindowImpl::~GLFWWindowImpl()
    {
        Shutdown();
    }

    void GLFWWindowImpl::Init(const WindowProps &props)
    {
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;
        m_Data.VSync = props.VSync;
        m_Data.Fullscreen = props.Fullscreen;
        m_Data.Visible = props.Visible;

        if (!s_GLFWInitialized)
        {
            int success = glfwInit();
            if (success == GLFW_FALSE)
            {
                ENGINE_CORE_CRITICAL("Could not initialize GLFW!");
                return;
            }
            s_GLFWInitialized = true;
        }

        // Set GLFW window hints
        glfwWindowHint(GLFW_RESIZABLE, props.Resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, props.Visible ? GLFW_TRUE : GLFW_FALSE);

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        // Get primary monitor for fullscreen
        GLFWmonitor *monitor = props.Fullscreen ? glfwGetPrimaryMonitor() : nullptr;

        // Create the GLFW window
        m_Window = glfwCreateWindow(
            static_cast<int>(props.Width),
            static_cast<int>(props.Height),
            props.Title.c_str(),
            monitor,
            nullptr);

        if (!m_Window)
        {
            ENGINE_CORE_CRITICAL("Failed to create GLFW window!");
            return;
        }

        // Make context current BEFORE any GL call
        glfwMakeContextCurrent(m_Window);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            ENGINE_CORE_ERROR("Failed to initialize GLAD");
            return;
        }

        // Verify a context is actually current
        if (!glfwGetCurrentContext())
            ENGINE_CORE_ERROR("OpenGL context is not current");

        // Load OpenGL function pointers
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            ENGINE_CORE_ERROR("Failed to initialize GLAD");

        // Hard validation checks
        const GLubyte *version = glGetString(GL_VERSION);
        const GLubyte *vendor = glGetString(GL_VENDOR);
        const GLubyte *renderer = glGetString(GL_RENDERER);

        const char *versionStr = reinterpret_cast<const char *>(version);
        const char *vendorStr = reinterpret_cast<const char *>(vendor);
        const char *rendererStr = reinterpret_cast<const char *>(renderer);

        if (!version || !vendor || !renderer)
            ENGINE_CORE_ERROR("glGetString returned null — invalid OpenGL context");

        ENGINE_CORE_INFO("OpenGL Version: {}", versionStr);
        ENGINE_CORE_INFO("Vendor: {}", vendorStr);
        ENGINE_CORE_INFO("Renderer: {}", rendererStr);

        // Check for AMD memory extension
        if (GLAD_GL_ATI_meminfo)
        {
            GLint memInfo[4] = {0};

            // Returns free texture memory in KB
            glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, memInfo);

            // memInfo layout (in KB):
            // memInfo[0] = total free texture memory
            // memInfo[1] = largest available free block
            // memInfo[2] = total auxiliary memory free
            // memInfo[3] = largest auxiliary free block

            ENGINE_CORE_INFO("AMD GL_ATI_meminfo detected");

            ENGINE_CORE_INFO("Free Texture Memory: {} MB", memInfo[0] / 1024);
            ENGINE_CORE_INFO("Largest Free Texture Block: {} MB", memInfo[1] / 1024);
            ENGINE_CORE_INFO("Free Auxiliary Memory: {} MB", memInfo[2] / 1024);
            ENGINE_CORE_INFO("Largest Auxiliary Free Block: {} MB", memInfo[3] / 1024);
        }
        else
        {
            ENGINE_CORE_WARN("GL_ATI_meminfo not supported on this driver");
        }

        int major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

        if (major == 0 && minor == 0)
            ENGINE_CORE_ERROR("Failed to query OpenGL version");

        ENGINE_CORE_INFO("Parsed Version: {0}.{1}", major, minor);

        if (major < 4)
            ENGINE_CORE_ERROR("OpenGL 4.x required");

        // Final sanity check
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
            ENGINE_CORE_ERROR("OpenGL error detected immediately after initialization");

        ENGINE_CORE_INFO("OpenGL context successfully initialized.");

        // Set the user pointer to our WindowData struct for callback access
        glfwSetWindowUserPointer(m_Window, &m_Data);
        // SetVSync(props.VSync);

        // Setup callbacks
        SetupCallBacks();
    }

    void GLFWWindowImpl::SetupCallBacks()
    {
        // Window resize callback
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow *window, int width, int height)
        {
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;

            WindowResizeEvent event(width, height);
            data.EventCallback(event); 
        });

        // Window close callback
        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow *window)
        {
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.EventCallback(event); 
        });

        // Key callback
        glfwSetKeyCallback(m_Window, [](GLFWwindow *window, int key, int scancode, int action, int mods)
        {
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);

            if (action == GLFW_PRESS)
                Input::SetKeyState(static_cast<KeyCode>(key), true);
            else if (action == GLFW_RELEASE)
                Input::SetKeyState(static_cast<KeyCode>(key), false);

            switch (action)
            {
                case GLFW_PRESS:
                {
                    KeyPressedEvent event(key, 0);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    KeyReleasedEvent event(key);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_REPEAT:
                {
                    KeyPressedEvent event(key, 1);
                    data.EventCallback(event);
                    break;
                }
            } 
        });

        glfwSetCharCallback(m_Window, [](GLFWwindow *window, unsigned int character)
        {
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);
            KeyTypedEvent event(character);
            data.EventCallback(event);
        });

        // Mouse button callback
        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow *window, int button, int action, int mods)
        {
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);

            if (action == GLFW_PRESS)
                Input::SetMouseButtonState(static_cast<MouseButton>(button), true);
            else if (action == GLFW_RELEASE)
                Input::SetMouseButtonState(static_cast<MouseButton>(button), false);

            switch (action)
            {
                case GLFW_PRESS:
                {
                    MouseButtonPressedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
                case GLFW_RELEASE:
                {
                    MouseButtonReleasedEvent event(button);
                    data.EventCallback(event);
                    break;
                }
            } 
        });

        // Scroll callback
        glfwSetScrollCallback(m_Window, [](GLFWwindow *window, double xOffset, double yOffset)
        {
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);

            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.EventCallback(event); 
        });

        // Cursor position callback
        glfwSetCursorPosCallback(m_Window, [](GLFWwindow *window, double xPos, double yPos)
        { 
            WindowData &data = *(WindowData *)glfwGetWindowUserPointer(window);

            MouseMovedEvent event((float)xPos, (float)yPos);
            data.EventCallback(event);
        });
    }

    void GLFWWindowImpl::Shutdown()
    {
        if (m_Window)
        {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }
    }

    void GLFWWindowImpl::OnUpdate()
    {
        glfwPollEvents();
        glfwSwapBuffers(m_Window);
    }

    void GLFWWindowImpl::SetVSync(bool enabled)
    {
        m_Data.VSync = enabled;
        glfwSwapInterval(enabled ? 1 : 0);
    }

    bool GLFWWindowImpl::IsVSync() const
    {
        return m_Data.VSync;
    }

    uint32_t GLFWWindowImpl::GetWidth() const
    {
        return m_Data.Width;
    }

    uint32_t GLFWWindowImpl::GetHeight() const
    {
        return m_Data.Height;
    }

    void GLFWWindowImpl::SetEventCallback(const Window::EventCallbackFn &callback)
    {
        m_Data.EventCallback = callback;
    }

    void GLFWWindowImpl::SetTitle(const std::string &title)
    {
        m_Data.Title = title;
        glfwSetWindowTitle(m_Window, title.c_str());
    }

    const std::string &GLFWWindowImpl::GetTitle() const
    {
        return m_Data.Title;
    }

    void GLFWWindowImpl::SetSize(uint32_t width, uint32_t height)
    {
        m_Data.Width = width;
        m_Data.Height = height;
        glfwSetWindowSize(m_Window, static_cast<int>(width), static_cast<int>(height));
    }

    void GLFWWindowImpl::SetWidth(uint32_t width)
    {
        SetSize(width, m_Data.Height);
    }

    void GLFWWindowImpl::SetHeight(uint32_t height)
    {
        SetSize(m_Data.Width, height);
    }

    void GLFWWindowImpl::SetFullscreen(bool fullscreen)
    {
        if (m_Data.Fullscreen == fullscreen)
            return;

        m_Data.Fullscreen = fullscreen;

        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);

        if (fullscreen)
        {
            glfwSetWindowMonitor(m_Window, monitor, 0, 0,
                                 mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            glfwSetWindowMonitor(m_Window, nullptr,
                                 (mode->width - m_Data.Width) / 2,
                                 (mode->height - m_Data.Height) / 2,
                                 m_Data.Width, m_Data.Height, 0);
        }
    }

    bool GLFWWindowImpl::IsFullscreen() const
    {
        return m_Data.Fullscreen;
    }

    void GLFWWindowImpl::SetVisible(bool visible)
    {
        m_Data.Visible = visible;
        if (visible)
        {
            glfwShowWindow(m_Window);
        }
        else
        {
            glfwHideWindow(m_Window);
        }
    }

    bool GLFWWindowImpl::IsVisible() const
    {
        return m_Data.Visible;
    }
    void *GLFWWindowImpl::GetNativeWindow() const
    {
        return m_Window;
    }
}
