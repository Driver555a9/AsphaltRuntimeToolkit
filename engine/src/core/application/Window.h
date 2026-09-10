#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

//std
#include <string>
#include <cstdint>

//ImGUI
#include "imgui/imgui.h"

namespace CoreEngine
{
    class Window
    {
    public:
        struct WindowCreationConfig 
        {
            enum CallbackDisableFlags : int
            {
                NONE                        = 0,
                KeyCallback                 = 1 << 0,
                MouseButtonCallback         = 1 << 1,
                MouseMovedCallback          = 1 << 2,
                MouseScrollCallback         = 1 << 3,
                FramebufferResizeCallback   = 1 << 4,
                WindowCloseCallback         = 1 << 5
            };

            const char*             m_title;
            std::pair<float, float> m_relative_size {1.0f, 1.0f};
            CallbackDisableFlags    m_callback_disable_flags {CallbackDisableFlags::NONE};
            ImGuiWindowFlags        m_imgui_flags{};
            std::uint8_t            m_MSAA_sample_count {0};
            bool                    m_is_decorated                = true;
            bool                    m_has_transparent_framebuffer = false;
            bool                    m_is_clickthrough             = false;
        };

        struct Handle 
        {
            constexpr static uint32_t INVALID = 0;

            [[nodiscard]] operator uint32_t() const noexcept { return m_value; }
            [[nodiscard]] bool operator==(const Handle& other) const noexcept { return m_value == other.m_value; }
            [[nodiscard]] bool operator!=(const Handle& other) const noexcept { return m_value != other.m_value; }
            void Invalidate() noexcept { m_value = INVALID; }
        private:
            friend class Window;
            explicit Handle() noexcept : m_value(s_handle_counter++) {}
            static inline uint32_t s_handle_counter {1};
            uint32_t m_value = INVALID;
        };

        explicit Window(WindowCreationConfig config) noexcept;
        ~Window() noexcept;
        explicit Window(Window&& other) noexcept;
        Window& operator=(Window&& other) noexcept;

    ///////////////////////////////
    // Methods
    ///////////////////////////////
        [[nodiscard]] GLFWwindow* GetGLFWwindow() noexcept;
        [[nodiscard]] ImGuiContext* GetImGuiContext() noexcept;
        [[nodiscard]] void* GetHWND() noexcept;

        void BeginFrame() noexcept;
        void FinishFrame() noexcept;

        void BeginImGuiFrame() noexcept;
        void FinishImGuiFrame() noexcept;

        [[nodiscard]] Handle GetHandle() const noexcept;

        [[nodiscard]] std::pair<int, int> GetFramebufferSize() const noexcept;
        [[nodiscard]] float GetAspectRatio() const noexcept;
        [[nodiscard]] const char* GetTitle() const noexcept;
        [[nodiscard]] bool IsVisible() const noexcept;


    ///////////////////////////////
    // Copying forbidden
    ///////////////////////////////
        Window(const Window&)            = delete;
        Window& operator=(const Window&) = delete;

    private:
        void DestroyContexts() noexcept;

        //Hack since ImGui_ImplOpenGL3_Shutdown is a bitch
        static inline std::uint32_t s_window_counter = 0;
        
        Handle          m_handle {};
        GLFWwindow*     m_window_ptr      = nullptr;
        ImGuiContext*   m_imgui_context   = nullptr;
        void*           m_hwnd            = nullptr;
    };
}