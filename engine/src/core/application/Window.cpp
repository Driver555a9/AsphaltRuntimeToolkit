#include "core/application/Window.h"
#include "core/application/Application.h"

#include "core/utility/Assert.h"
#include "core/application/Application.h"

#include "default_fonts/JetBrainsMono.h"
#include "imgui.h"

//std
#include <iostream>
#include <algorithm>
#include <optional>
#include <filesystem>

#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3native.h>
    #include <dwmapi.h>
#endif

//glad
#include "glad/gl.h"

#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui_internal.h"

namespace CoreEngine
{
    Window::Window(WindowCreationConfig config) noexcept
    {
    ///////////////////////////////
    // GLFW
    ///////////////////////////////
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, std::max<int>(8, config.m_MSAA_sample_count));
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, config.m_has_transparent_framebuffer);
        glfwWindowHint(GLFW_DECORATED, config.m_is_decorated);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();

        int wx, wy, ww, wh;
        glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);

        const int avoid_fullscreen = config.m_has_transparent_framebuffer ? 1 : 0;
        const int window_width  = static_cast<int>(ww * config.m_relative_size.first)  + avoid_fullscreen;
        const int window_height = static_cast<int>(wh * config.m_relative_size.second) + avoid_fullscreen;

        m_window_ptr = glfwCreateWindow(window_width, window_height, config.m_title, nullptr, nullptr);

        glfwSetWindowPos(m_window_ptr, wx + (ww - window_width) / 2, wy + (wh - window_height) / 2);

        ENGINE_ASSERT (m_window_ptr && "Failed to create window");

    #ifdef _WIN32
        m_hwnd = glfwGetWin32Window(m_window_ptr);
        if (! config.m_is_decorated)
        {
            HWND hwnd = reinterpret_cast<HWND>(m_hwnd);
            LONG style_ws = GetWindowLongPtr(hwnd, GWL_STYLE);
            style_ws &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            SetWindowLongPtr(hwnd, GWL_STYLE, style_ws);

            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }

    #endif

        if ( (config.m_callback_disable_flags & WindowCreationConfig::CallbackDisableFlags::KeyCallback) == WindowCreationConfig::CallbackDisableFlags::NONE)
        {
            glfwSetKeyCallback (m_window_ptr, &Application::KeyCallback);
        }
        if ( (config.m_callback_disable_flags & WindowCreationConfig::CallbackDisableFlags::FramebufferResizeCallback) == WindowCreationConfig::CallbackDisableFlags::NONE )
        {
            glfwSetFramebufferSizeCallback (m_window_ptr, &Application::FramebufferResizeCallback);
        }
        if ( (config.m_callback_disable_flags & WindowCreationConfig::CallbackDisableFlags::MouseButtonCallback) == WindowCreationConfig::CallbackDisableFlags::NONE )
        {
            glfwSetMouseButtonCallback (m_window_ptr, &Application::MouseButtonCallback);
        }
        if ( (config.m_callback_disable_flags & WindowCreationConfig::CallbackDisableFlags::MouseMovedCallback) == WindowCreationConfig::CallbackDisableFlags::NONE )
        {
            glfwSetCursorPosCallback (m_window_ptr, &Application::MouseMovedCallback);
        }
        if ( (config.m_callback_disable_flags & WindowCreationConfig::CallbackDisableFlags::MouseScrollCallback) == WindowCreationConfig::CallbackDisableFlags::NONE )
        {
            glfwSetScrollCallback (m_window_ptr, &Application::MouseScrollCallback);
        }
        if ( (config.m_callback_disable_flags & WindowCreationConfig::CallbackDisableFlags::WindowCloseCallback) == WindowCreationConfig::CallbackDisableFlags::NONE )
        {
            glfwSetWindowCloseCallback (m_window_ptr, &Application::WindowCloseCallback);
        }

        if (Application::Get()->GetGlobalWindowIcon().has_value())
        {
            glfwSetWindowIcon(m_window_ptr, 1, &Application::Get()->GetGlobalWindowIcon().value());
        }
    ///////////////////////////////
    // OpenGL
    ///////////////////////////////
        glfwMakeContextCurrent(m_window_ptr);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        if (config.m_has_transparent_framebuffer)
        {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        }
        
        glEnable(GL_DEPTH_TEST);

        glFrontFace(GL_CCW); 
        glCullFace(GL_BACK);
        glEnable(GL_CULL_FACE);

        if (config.m_MSAA_sample_count > 0) 
        {
            glEnable(GL_MULTISAMPLE);
            glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        }

    ///////////////////////////////
    // ImGui
    ///////////////////////////////
        m_imgui_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_imgui_context);

        ImGui_ImplGlfw_InitForOpenGL(m_window_ptr, true);
        ImGui_ImplOpenGL3_Init("#version 460");

        ImGuiIO& io = ImGui::GetIO();

        const std::optional<std::string>& font_path = Application::Get()->GetGlobalImGuiFontPath();
        bool added_custom = false;
        if (font_path.has_value())
        {
            const std::string& path = font_path.value();

            std::error_code ec;
            if (! path.empty() && std::filesystem::is_regular_file(path, ec) && !ec)
            {
                ImFont* font = io.Fonts->AddFontFromFileTTF(font_path->c_str(), 18.0f);
                if (font)
                {
                    io.FontDefault = font;
                    added_custom = true;
                }
            }
        }
        
        if (! added_custom)
        {
            Application::Get()->SetImGuiFontGlobal(std::nullopt);
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            ImFont* font = io.Fonts->AddFontFromMemoryTTF(JET_BRAINS_MONO_BOLD, JET_BRAINS_MONO_BOLD_length, 18.0f, &cfg);
            if (font)
            {
                io.FontDefault = font;
            }
        }

        ImGui::StyleColorsDark();
        io.ConfigFlags |= config.m_imgui_flags;

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        const float scale = std::min<float>(ww / 1920.0f, wh / 1080.0f);

        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale);
        io.FontGlobalScale = scale;

        s_window_counter++;
    }

    Window::~Window() noexcept
    {
        DestroyContexts();
        m_handle.Invalidate();
        s_window_counter--;
    }

    ///////////////////////////////
    // Move
    ///////////////////////////////
    Window::Window(Window&& other) noexcept : m_handle(other.m_handle), m_window_ptr(other.m_window_ptr), m_imgui_context(other.m_imgui_context)
    {
        other.m_window_ptr    = nullptr;
        other.m_imgui_context = nullptr;
        other.m_handle.Invalidate();
    }

    Window& Window::operator=(Window&& other) noexcept
    {
        if (this != &other)
        {
            DestroyContexts();
            m_window_ptr          = other.m_window_ptr;
            m_imgui_context       = other.m_imgui_context;
            m_handle              = other.m_handle;

            other.m_window_ptr    = nullptr;
            other.m_imgui_context = nullptr;
            other.m_handle.Invalidate();
        }
        return *this;
    }

    ///////////////////////////////
    // Methods
    ///////////////////////////////
    GLFWwindow* Window::GetGLFWwindow() noexcept
    {
        return m_window_ptr;
    }
    
    ImGuiContext* Window::GetImGuiContext() noexcept
    {
        return m_imgui_context;
    }

    [[nodiscard]] void* Window::GetHWND() noexcept
    {
        return m_hwnd;
    }

    void Window::BeginFrame() noexcept
    {
        glfwMakeContextCurrent(m_window_ptr);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void Window::FinishFrame() noexcept
    {
        glfwMakeContextCurrent(m_window_ptr);
        glfwSwapBuffers(m_window_ptr);
    }

    void Window::BeginImGuiFrame() noexcept
    {
        glfwMakeContextCurrent(m_window_ptr);
        ImGui::SetCurrentContext(m_imgui_context);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void Window::FinishImGuiFrame() noexcept
    {
        glfwMakeContextCurrent(m_window_ptr);
        ImGui::SetCurrentContext(m_imgui_context);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }
    }  
    
    Window::Handle Window::GetHandle() const noexcept
    {
        return m_handle;
    }

    std::pair<int, int> Window::GetFramebufferSize() const noexcept
    {
        std::pair<int, int> size;
        glfwGetFramebufferSize(m_window_ptr, &size.first, &size.second);
        return size;
    }

    float Window::GetAspectRatio() const noexcept
    {
        auto [width, height] = GetFramebufferSize();
        return static_cast<float>(width) / height;
    }

    const char* Window::GetTitle() const noexcept
    {
        return glfwGetWindowTitle(m_window_ptr);
    }

    bool Window::IsVisible() const noexcept
    {
        if (!m_window_ptr) 
        {
            return false;
        }

        if (glfwGetWindowAttrib(m_window_ptr, GLFW_VISIBLE) == GLFW_FALSE) 
        {
            return false;
        }

        if (glfwGetWindowAttrib(m_window_ptr, GLFW_ICONIFIED) == GLFW_TRUE) 
        {
            return false;
        }

        return true;
    }

    
    ///////////////////////////////
    // Private
    ///////////////////////////////

    void Window::DestroyContexts() noexcept
    {
        glfwMakeContextCurrent(m_window_ptr);

        ImGui::SetCurrentContext(m_imgui_context);

        if (s_window_counter > 1)
        {
            //ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            //ImGui::DestroyContext();
        }

        m_imgui_context = nullptr;

        glfwDestroyWindow(m_window_ptr);
        m_window_ptr = nullptr;
    }

}
