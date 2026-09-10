#pragma once

//std imports
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <mutex>
#include <expected>

//personal imports
#include "core/layer/Layer.h"

#include "core/event/Event.h"

#include "core/application/Window.h"

#include "core/utility/Assert.h"
#include "core/utility/Units.h"

namespace CoreEngine
{
    class Application final
    {
    public:
        //////////////////////////////////////////////// 
        //--------- Setup config
        //////////////////////////////////////////////// 
        struct ApplicationConfig
        {
            std::optional<std::string> m_window_icon_path = std::nullopt;
            bool                       m_enable_vsync                     {false};
            bool                       m_debug_launch_with_console        {true};
            bool                       m_use_glfw_await_events            {false};
        };

        /////////////////////////////////////////////// 
        //--------- Application Instance Creation - Once instance goes out of scope, OnCleanup() is called in ~Application() - then Create() may be called again
        //////////////////////////////////////////////// 
        [[nodiscard]] static Application Create(const ApplicationConfig& config);

        [[nodiscard]] static Application* Get() noexcept;

        //////////////////////////////////////////////// 
        //--------- Public Methods
        //////////////////////////////////////////////// 
        ~Application() noexcept;
        void Run() noexcept;
        void Stop() noexcept;

        void QueueDeleteWindowLayerStack(Window::Handle group_handle) noexcept;

        /// Throws runtime error if window ptr not found
        [[nodiscard]] Window* GetWindowPtr(Window::Handle group_handle);

        [[nodiscard]] size_t GetAmountWindows() const noexcept;

        void SetVsync(bool on) noexcept;
        [[nodiscard]] bool GetVsyncIsOn() const noexcept;

        [[nodiscard]] Units::MicroSecond GetLastFrameTime() const noexcept;

        template <typename TLayer, typename... Args>
        requires std::is_constructible_v<TLayer, Args...>
        void AddLayerToExistingWindow(Window::Handle group_handle, Args&&... args)
        {
            size_t index = FindWindowLayerStackIndexFromWindowHandle(group_handle);
            m_window_layer_stacks[index]->m_layer_stack.emplace_back(std::make_unique<TLayer>(std::forward<Args>(args)...));
        }

        using LayerFactory = std::function<std::unique_ptr<Basic_Layer>(Window::Handle)>;
        
        template <typename TLayer, typename... Args>
        requires std::is_constructible_v<TLayer, Window::Handle, Args...>
        inline void QueueCreateWindowAndPushLayer(const Window::WindowCreationConfig& config, Args&&... args)
        {
            LayerFactory factory = [args = std::make_tuple(std::forward<Args>(args)...)] (Window::Handle handle) -> std::unique_ptr<Basic_Layer> 
            {
                return std::apply([&handle](auto&&... unpacked_args) {
                    return std::make_unique<TLayer>(handle, std::forward<decltype(unpacked_args)>(unpacked_args)...);
                }, 
                std::move(args));
            };
            m_window_creations_to_add_next_frame.emplace_back(config, std::vector<LayerFactory>{ std::move(factory) });
        }

        template <typename TEvent>
        requires std::is_base_of_v<Basic_Event, TEvent>
        inline void RaiseEvent(Window::Handle handle, TEvent&& event) noexcept
        {
            const size_t index = FindWindowLayerStackIndexFromWindowHandle(handle);

            for (auto it = m_window_layer_stacks[index]->m_layer_stack.rbegin(); it != m_window_layer_stacks[index]->m_layer_stack.rend(); ++it)
            {
                if (event.GetIsHandled())
                {
                    break;
                }
                (**it).OnEvent(event);
            }
        }
        
        struct BasicTask
        {
            virtual ~BasicTask() noexcept {};
            virtual void Execute() noexcept = 0;
        };
        void ScheduleTask(std::unique_ptr<BasicTask> task) noexcept 
        { 
            ENGINE_ASSERT(task && "You must not provide a nullptr to ScheduleTask");
            std::scoped_lock<std::mutex> lock(m_schedule_tasks_mutex);
            m_scheduled_tasks.push_back(std::move(task)); 
        }

        void SetImGuiFontGlobal(const std::optional<std::string>& path) noexcept;
        const std::optional<std::string>& GetGlobalImGuiFontPath() noexcept;
        const std::optional<GLFWimage>& GetGlobalWindowIcon() noexcept;

        //////////////////////////////////////////////// 
        //--------- Glfw callbacks
        //////////////////////////////////////////////// 
        static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void MouseMovedCallback(GLFWwindow* window, double x_pos, double y_pos);
        static void MouseScrollCallback(GLFWwindow* window, double x_offset, double y_offset);
        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void WindowCloseCallback(GLFWwindow* window);

    private:
        // Throws runtime err if not found
        [[nodiscard]] std::optional<Window::Handle> FindWindowHandleFromGlfwWindow(GLFWwindow* window) const;
        // Throws runtime err if not found
        [[nodiscard]] size_t FindWindowLayerStackIndexFromWindowHandle(Window::Handle handle) const;
        //////////////////////////////////////////////// 
        //--------- Member variables
        //////////////////////////////////////////////// 
        struct WindowLayerStack
        {
            std::unique_ptr<Window>                   m_window_ptr;
            std::vector<std::unique_ptr<Basic_Layer>> m_layer_stack;
        };

        std::vector<std::unique_ptr<WindowLayerStack>> m_window_layer_stacks;

        std::vector<Window::Handle> m_window_layer_stacks_to_delete_next_frame;
        std::vector<std::pair<Window::WindowCreationConfig, std::vector<LayerFactory>>> m_window_creations_to_add_next_frame;

        std::mutex m_schedule_tasks_mutex;
        std::vector<std::unique_ptr<BasicTask>> m_scheduled_tasks;

        std::optional<std::string> m_active_global_font_path = std::nullopt;
        std::optional<GLFWimage>   m_active_window_icon      = std::nullopt;           
        Units::MicroSecond         m_frame_delta_time {0};
        ApplicationConfig          m_original_config  {};
        bool                       m_stop_flag        {false};
        bool                       m_vsync_is_on      {false};
        
        //////////////////////////////////////////////// 
        //--------- Instance
        //////////////////////////////////////////////// 
        static inline Application* s_application_instance_ptr = nullptr;

        //////////////////////////////////////////////// 
        //--------- Constructor to be called by Create()
        //////////////////////////////////////////////// 
        explicit Application(ApplicationConfig config) noexcept;

        //////////////////////////////////////////////// 
        //--------- Copy / Move policy 
        //////////////////////////////////////////////// 
        Application()                              = delete;
        Application(const Application&)            = delete;
        Application& operator=(const Application&) = delete;
        Application& operator=(Application&&)      = delete;
        Application(Application&&)                 = delete;
    };
}