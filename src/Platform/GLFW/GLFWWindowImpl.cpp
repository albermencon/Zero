#include "pch.h"
#include "GLFWWindowImpl.h"
#include "Platform/InputInternal.h"
#include <Engine/Input/KeyCode.h>
#include <Engine/Input/MouseButton.h>

#include <Engine/Core.h>

#include <Engine/Log.h>

#include <Engine/ApplicationEvent.h>
#include <Engine/KeyEvent.h>
#include <Engine/MouseEvent.h>

#include <GLFW/glfw3.h>

namespace Zero
{
    static bool s_GLFWInitialized = false;

    GLFWWindowImpl::GLFWWindowImpl(const WindowProps& props)
    {
        Init(props);
    }

    GLFWWindowImpl::~GLFWWindowImpl()
    {
        Shutdown();
    }

    void GLFWWindowImpl::Init(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.WindowWidth = props.Width;
        m_Data.WindowHeight = props.Height;
        m_Data.VSync = props.VSync;
        m_Data.Fullscreen = props.Fullscreen;
        m_Data.Visible = props.Visible;
        m_backend = props.backend;

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

        switch (m_backend)
        {
        case BackendType::OpenGL:
        {
            // Use the OpenGL context
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

            // Request a modern core profile context. Change versions if needed.
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

            // On macOS, forward-compatible flag is required for core profiles >= 3.2
#if defined(PLATFORM_MACOS)
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
            break;
        }

        case BackendType::Vulkan:
        {
            // No client API: the app will use Vulkan (glfwCreateWindowSurface expects this).
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            break;
        }

        case BackendType::DirectX12:
        {
            // DirectX12 is only available on Windows. GLFW windows should be created with NO_API.
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if !defined(PLATFORM_WINDOWS)
            ENGINE_CORE_WARN("DirectX12 backend selected but current platform is not Windows. Window created with NO_API anyway.");
#endif
            break;
        }

        case BackendType::Metal:
        {
            // Metal targets macOS / iOS. GLFW should be created with NO_API for Metal rendering.
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if !defined(PLATFORM_MACOS)
            ENGINE_CORE_WARN("Metal backend selected but current platform is not macOS. Window created with NO_API anyway.");
#endif
            break;
        }

        default:
        {
            // Safe fallback: no client API. Log the occurrence.
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            ENGINE_CORE_WARN("Unknown BackendType: defaulting to NO_API window hints.");
            break;
        }
        }

        // Get primary monitor for fullscreen
        GLFWmonitor* monitor = props.Fullscreen ? glfwGetPrimaryMonitor() : nullptr;

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

        // Set the user pointer to our WindowData struct for callback access
        glfwSetWindowUserPointer(m_Window, &m_Data);
        // SetVSync(props.VSync);

        // Setup callbacks
        SetupCallBacks();
    }

    void GLFWWindowImpl::SetupCallBacks()
    {
        // Window resize callback
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);
                data.WindowWidth = width;
                data.WindowHeight = height;

                WindowResizeEvent event(width, height);
                data.EventCallback(event);
            });

        // Frame buffer callback
        glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);
                data.FramebufferWidth = width;
                data.FramebufferHeight = height;

                RenderSurfaceResize event(width, height);
                data.EventCallback(event);
            });

        // Window close callback
        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);
                WindowCloseEvent event;
                data.EventCallback(event);
            });

        // Key callback
        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);

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

        glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int character)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);
                KeyTypedEvent event(character);
                data.EventCallback(event);
            });

        // Mouse button callback
        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);

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
        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);

                MouseScrolledEvent event((float)xOffset, (float)yOffset);
                data.EventCallback(event);
            });

        // Cursor position callback
        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
            {
                WindowState& data = *(WindowState*)glfwGetWindowUserPointer(window);

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

    void GLFWWindowImpl::PollEvents()
    {
        glfwPollEvents();
    }

    void GLFWWindowImpl::SetVSync(bool enabled)
    {
        m_Data.VSync = enabled;
        // TODO: The backend is in charge of this
    }

    bool GLFWWindowImpl::IsVSync() const
    {
        return m_Data.VSync;
    }

    uint32_t GLFWWindowImpl::GetWindowWidth() const
    {
        return m_Data.WindowWidth;
    }

    uint32_t GLFWWindowImpl::GetWindowHeight() const
    {
        return m_Data.WindowHeight;
    }

    void GLFWWindowImpl::SetEventCallback(const Window::EventCallbackFn& callback)
    {
        m_Data.EventCallback = callback;
    }

    void GLFWWindowImpl::SetTitle(const std::string& title)
    {
        m_Data.Title = title;
        glfwSetWindowTitle(m_Window, title.c_str());
    }

    const std::string& GLFWWindowImpl::GetTitle() const
    {
        return m_Data.Title;
    }

    void GLFWWindowImpl::SetWindowSize(uint32_t width, uint32_t height)
    {
        m_Data.WindowWidth = width;
        m_Data.WindowHeight = height;
        glfwSetWindowSize(m_Window, static_cast<int>(width), static_cast<int>(height));
    }

    uint32_t GLFWWindowImpl::GetFrameBufferWidth() const
    {
        return m_Data.FramebufferWidth;
    }

    uint32_t GLFWWindowImpl::GetFrameBufferHeight() const
    {
        return m_Data.FramebufferHeight;
    }

    void GLFWWindowImpl::SetWindowWidth(uint32_t width)
    {
        SetWindowSize(width, m_Data.WindowHeight);
    }

    void GLFWWindowImpl::SetWindowHeight(uint32_t height)
    {
        SetWindowSize(m_Data.WindowWidth, height);
    }

    void GLFWWindowImpl::SetFullscreen(bool fullscreen)
    {
        if (m_Data.Fullscreen == fullscreen)
            return;

        m_Data.Fullscreen = fullscreen;

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);

        if (fullscreen)
        {
            glfwSetWindowMonitor(m_Window, monitor, 0, 0,
                mode->width, mode->height, mode->refreshRate);
        }
        else
        {
            glfwSetWindowMonitor(m_Window, nullptr,
                (mode->width - m_Data.WindowWidth) / 2,
                (mode->height - m_Data.WindowHeight) / 2,
                m_Data.WindowWidth, m_Data.WindowHeight, 0);
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
    void* GLFWWindowImpl::GetNativeWindow() const
    {
        return m_Window;
    }

    BackendType GLFWWindowImpl::GetBackend() const
    {
        return m_backend;
    }
}
