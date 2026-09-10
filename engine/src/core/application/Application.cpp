#include "core/application/Application.h"

//Own includes
#include "GLFW/glfw3.h"
#include "core/event/WindowEvents.h"
#include "core/event/InputEvents.h"
#include "core/event/ApplicationStateEvents.h"

#include "core/utility/CommonUtility.h"
#include "core/utility/DebugUtility.h"
#include "core/utility/Timer.h"
#include "core/utility/Assert.h"
#include "core/utility/Performance.h"

#include "stb_image.h"
#include "imgui.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <limits>
#include <optional>

namespace CoreEngine
{
    //Constructor
    Application::Application(ApplicationConfig config) noexcept : m_original_config(config), m_vsync_is_on(config.m_enable_vsync)
    {
        s_application_instance_ptr = this;

        /// window icons
        if (config.m_window_icon_path.has_value())
        {
            if (std::filesystem::is_regular_file(config.m_window_icon_path.value()))
            {
                int width, height, channels;
                unsigned char* pixels = stbi_load(config.m_window_icon_path.value().c_str(), &width, &height, &channels, 4);

                if (pixels)
                {
                    GLFWimage icon;
                    icon.width  = width;
                    icon.height = height;
                    icon.pixels = pixels;
                    m_active_window_icon = icon;
                }
                else 
                {
                    ENGINE_ERROR_PRINT("Could not load icon from path: " << config.m_window_icon_path.value() << " stb error: " << stbi_failure_reason());
                }
            }
            else 
            {
                ENGINE_ERROR_PRINT("Could not load icon from path: " << config.m_window_icon_path.value());
            }
        }
    }

    Application::~Application()
    {
        m_window_layer_stacks.clear();
        s_application_instance_ptr = nullptr;
        glfwTerminate();
        if (m_active_window_icon.has_value())
        {
            stbi_image_free(m_active_window_icon->pixels);
        }
    }

    void Application::Run() noexcept
    {
        m_stop_flag = false;

        Timer frame_timer {};

        while (! m_stop_flag)
        {
            ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME("Main Loop()  ");

            constexpr Units::MicroSecond min_dt (1L);
            constexpr Units::MicroSecond max_dt (100'000L);
            m_frame_delta_time = std::clamp<Units::MicroSecond>(frame_timer.GetElapsedAndRestart<Units::MicroSecond>(), min_dt, max_dt);

            {
                std::scoped_lock<std::mutex> lock(m_schedule_tasks_mutex);
                for (auto& task : m_scheduled_tasks)
                {
                    task->Execute();
                }
                m_scheduled_tasks.clear();
            }

            for (std::unique_ptr<WindowLayerStack>& wls : m_window_layer_stacks)
            {
                //////////////////////////////////////////////// 
                //--------- Updating
                //////////////////////////////////////////////// 
                glfwMakeContextCurrent(wls->m_window_ptr->GetGLFWwindow());
                {
                    ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME( wls->m_window_ptr->GetTitle() + std::string(" : OnUpdate()     ") );
                    for (std::unique_ptr<Basic_Layer>& layer : wls->m_layer_stack)
                    {
                        layer->OnUpdate(m_frame_delta_time);
                    }
                }

                if (! wls->m_window_ptr->IsVisible())
                {
                    continue;
                }

                //////////////////////////////////////////////// 
                //--------- Rendering
                //////////////////////////////////////////////// 
                wls->m_window_ptr->BeginFrame();
                {
                    ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME( wls->m_window_ptr->GetTitle() + std::string(" : OnRender()     ") );
                    for (std::unique_ptr<Basic_Layer>& layer : wls->m_layer_stack)
                    {
                        layer->OnRender();
                    }
                }
                
                //////////////////////////////////////////////// 
                //--------- Gui Rendering
                //////////////////////////////////////////////// 
                {
                    ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME( wls->m_window_ptr->GetTitle() + std::string(" : OnImGuiRender()") );
                    wls->m_window_ptr->BeginImGuiFrame();
                    for (std::unique_ptr<Basic_Layer>& layer : wls->m_layer_stack)
                    {
                        layer->OnImGuiRender();
                    }
                    wls->m_window_ptr->FinishImGuiFrame();
                }

                {
                    ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME(wls->m_window_ptr->GetTitle() + std::string(" : FinishFrame()  "));
                    wls->m_window_ptr->FinishFrame();
                }
            }

            //////////////////////////////////////////////// 
            //--------- Events
            //////////////////////////////////////////////// 
            {
                if (m_original_config.m_use_glfw_await_events)
                {
                    ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME("AwaitEvents()");
                    glfwWaitEvents();
                }
                else 
                {
                    ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME("PollEvents() ");
                    glfwPollEvents();
                }
            }

            //////////////////////////////////////////////// 
            //--------- Deleting windows
            //////////////////////////////////////////////// 
            {
                ENGINE_PERFORMANCE_MEASURE_SCOPE_TIME("Manage Window");
                while (! m_window_layer_stacks_to_delete_next_frame.empty())
                {
                    const Window::Handle handle = m_window_layer_stacks_to_delete_next_frame.back();
                    m_window_layer_stacks_to_delete_next_frame.pop_back();

                    const size_t index = FindWindowLayerStackIndexFromWindowHandle(handle);
                    if (index == std::numeric_limits<size_t>::max())
                    {
                        ENGINE_ERROR_PRINT("Failed to delete window with handle: " << handle);
                    }
                    else 
                    {
                        m_window_layer_stacks.erase(m_window_layer_stacks.begin() + index);
                    }
                }

                //////////////////////////////////////////////// 
                //--------- Adding windows
                //////////////////////////////////////////////// 
                while (! m_window_creations_to_add_next_frame.empty())
                {
                    std::pair<Window::WindowCreationConfig, std::vector<Application::LayerFactory>> request = m_window_creations_to_add_next_frame.back();
                    m_window_creations_to_add_next_frame.pop_back();

                    m_window_layer_stacks.emplace_back(std::make_unique<WindowLayerStack>(std::make_unique<Window>(request.first)));
                    Window::Handle new_handle = m_window_layer_stacks.back()->m_window_ptr->GetHandle();
                    WindowLayerStack* new_wls = m_window_layer_stacks.back().get();

                    glfwMakeContextCurrent(new_wls->m_window_ptr->GetGLFWwindow());
                    glfwSwapInterval(m_vsync_is_on);

                    while (! request.second.empty())
                    {
                        LayerFactory factory = request.second.back();
                        request.second.pop_back();
                        
                        std::unique_ptr<Basic_Layer> layer = factory(new_handle);
                        new_wls->m_layer_stack.push_back(std::move(layer));
                    }
                }
                m_window_creations_to_add_next_frame.clear();
            }
        }
    }

    void Application::Stop() noexcept
    {
        m_stop_flag = true;
        
        for (std::unique_ptr<CoreEngine::Application::WindowLayerStack>& window_layerstack : m_window_layer_stacks)
        {
            RaiseEvent(window_layerstack->m_window_ptr->GetHandle(), ApplicationShutdownEvent {});
        }
    }

    void Application::QueueDeleteWindowLayerStack(Window::Handle group_handle) noexcept
    {
        if (group_handle == Window::Handle::INVALID)
        {
            ENGINE_ERROR_PRINT("Attempted to queue invalid window deletion");
            return;
        }

        m_window_layer_stacks_to_delete_next_frame.push_back(group_handle);
    }

    Window* Application::GetWindowPtr(Window::Handle group_handle) 
    {
        const size_t result = FindWindowLayerStackIndexFromWindowHandle(group_handle);
        return result == std::numeric_limits<size_t>::max() ? nullptr : m_window_layer_stacks[result]->m_window_ptr.get();
    }

    size_t Application::GetAmountWindows() const noexcept
    {
        return m_window_layer_stacks.size();
    }

    void Application::SetVsync(bool on) noexcept
    {
        m_vsync_is_on = on;
        GLFWwindow* context = glfwGetCurrentContext();
        for (std::unique_ptr<WindowLayerStack>& wls : m_window_layer_stacks)
        {
            glfwMakeContextCurrent(wls->m_window_ptr->GetGLFWwindow());
            glfwSwapInterval(on);
        }
        glfwMakeContextCurrent(context);
    }

    bool Application::GetVsyncIsOn() const noexcept
    {
        return m_vsync_is_on;
    }  

    Units::MicroSecond Application::GetLastFrameTime() const noexcept
    {
        return m_frame_delta_time;
    }
    
    /////////////////////////////////////////////// 
    // Application creation
    /////////////////////////////////////////////// 
    Application Application::Create(const ApplicationConfig& config)
    {
        if (s_application_instance_ptr)
        {
            ENGINE_ASSERT(false && "At Application::Create() called multiple times. Only one Application instance is allowed.");
        }

        if (! glfwInit())
        {
            throw std::runtime_error("At Application::Create(): failed to initialize GLFW.");
        }
        
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        GLFWwindow* dummy = glfwCreateWindow(1, 1, "", nullptr, nullptr);
        if (!dummy) throw std::runtime_error("Failed to create dummy window");

        glfwMakeContextCurrent(dummy);

        if (!gladLoadGL(glfwGetProcAddress))
            throw std::runtime_error("gladLoadGL failed");

        glfwSwapInterval(config.m_enable_vsync);

        if (config.m_debug_launch_with_console)
        {
            CoreEngine::DebugUtility::PrintHardwareInfo();
            CoreEngine::DebugUtility::EnableDebugMessages();
            CoreEngine::DebugUtility::ForceInitConsole();
        }
        else 
        {
            CoreEngine::DebugUtility::ForceCloseConsole();
        }

        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(dummy);

        return Application {config};
    }

    Application* Application::Get() noexcept
    {
        ENGINE_ASSERT(s_application_instance_ptr && "Can not call Get() if no instance of application exists.");
        return s_application_instance_ptr;
    }

    void Application::SetImGuiFontGlobal(const std::optional<std::string>& path_opt) noexcept
    {
        if (! path_opt.has_value())
        {
            m_active_global_font_path = std::nullopt;
            return;
        }

        const std::string& path = path_opt.value();
        if (path.empty()) return;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) return;

        m_active_global_font_path = path;

        GLFWwindow* originalGLFW = glfwGetCurrentContext();
        ImGuiContext* originalImGui = ImGui::GetCurrentContext();

        for (auto& wls : m_window_layer_stacks)
        {
            if (!wls || !wls->m_window_ptr) continue;

            GLFWwindow* window = wls->m_window_ptr->GetGLFWwindow();
            if (!window) continue;

            glfwMakeContextCurrent(window);

            ImGuiContext* context = wls->m_window_ptr->GetImGuiContext();
            if (!context) continue;

            ImGui::SetCurrentContext(context);

            ImGuiIO& io = ImGui::GetIO();

            ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f);

            if (font)
            {
                io.FontDefault = font;
            }
        }

        ImGui::SetCurrentContext(originalImGui);
        glfwMakeContextCurrent(originalGLFW);
    }

    const std::optional<std::string>& Application::GetGlobalImGuiFontPath() noexcept
    {
        return m_active_global_font_path;
    }

    const std::optional<GLFWimage>& Application::GetGlobalWindowIcon() noexcept
    {
        return m_active_window_icon;
    }

    //////////////////////////////////////////////// 
    //--------- Glfw callbacks
    //////////////////////////////////////////////// 
    void Application::KeyCallback(GLFWwindow* window, int key, [[maybe_unused]] int scancode, [[maybe_unused]] int action, [[maybe_unused]] int mods)
    {
        const auto handle = Get()->FindWindowHandleFromGlfwWindow(window);
        if (handle)
        {
            if (action == GLFW_PRESS)        Get()->RaiseEvent(handle.value(), KeyPressedEvent  {key} );
            else if (action == GLFW_RELEASE) Get()->RaiseEvent(handle.value(), KeyReleasedEvent {key} );
        }
    }

    void Application::MouseButtonCallback(GLFWwindow* window, int button, int action, [[maybe_unused]] int mods)
    {
        const auto handle = Get()->FindWindowHandleFromGlfwWindow(window);
        if (handle)
        {
            if (action == GLFW_PRESS)        Get()->RaiseEvent(handle.value(), MousePressedEvent  {button, CommonUtility::GetMousePosition(window)});
            else if (action == GLFW_RELEASE) Get()->RaiseEvent(handle.value(), MouseReleasedEvent {button, CommonUtility::GetMousePosition(window)});
        }
    }

    void Application::MouseMovedCallback(GLFWwindow* window, double x_pos, double y_pos)
    {
        const auto handle = s_application_instance_ptr->FindWindowHandleFromGlfwWindow(window);
        if (handle)
        {
            s_application_instance_ptr->RaiseEvent(handle.value(), MouseMovedEvent{x_pos, y_pos});
        }
    }

    void Application::MouseScrollCallback(GLFWwindow* window, double x_offset, double y_offset)
    {
        const auto handle = Get()->FindWindowHandleFromGlfwWindow(window);
        if (handle)
        {
            Get()->RaiseEvent(handle.value(), MouseScrolledEvent {x_offset, y_offset} );
        }
    }

    void Application::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        glfwMakeContextCurrent(window);
        glViewport(0, 0, width, height);
        const auto handle = Get()->FindWindowHandleFromGlfwWindow(window);
        if (handle)
        {
            Get()->RaiseEvent(handle.value(), FramebufferResizeEvent {width, height});
        }
    }

    void Application::WindowCloseCallback(GLFWwindow* window)
    {
        Application* app = Get();
        const auto handle = app->FindWindowHandleFromGlfwWindow(window);
        if (handle)
        {
            app->RaiseEvent(handle.value(), WindowCloseEvent {});

            app->QueueDeleteWindowLayerStack(handle.value());

            if (app->m_window_layer_stacks.size() == 1)
            {
                app->Stop();
            }
        }
    }
    

    //////////////////////////////////////////////// 
    //--------- Private methods
    //////////////////////////////////////////////// 
    std::optional<Window::Handle> Application::FindWindowHandleFromGlfwWindow(GLFWwindow* window) const
    {
        for (size_t index{}; index < m_window_layer_stacks.size(); ++index)
        {
            if (m_window_layer_stacks[index]->m_window_ptr->GetGLFWwindow() == window)
                return m_window_layer_stacks[index]->m_window_ptr->GetHandle();
        }
        return std::nullopt;
    }

    size_t Application::FindWindowLayerStackIndexFromWindowHandle(Window::Handle handle) const
    {
        for (size_t index{}; index < m_window_layer_stacks.size(); ++index)
        {
            if (m_window_layer_stacks[index]->m_window_ptr->GetHandle() == handle)
                return index;
        }
        return std::numeric_limits<size_t>::max();
    }
}