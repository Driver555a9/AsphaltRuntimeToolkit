#pragma once

#include "core/layer/Layer.h"
#include "core/utility/Timer.h"

#include <string>

namespace AsphaltTas
{
    class TasInputLayer : public CoreEngine::Basic_Layer
    {
    public:
        explicit TasInputLayer(CoreEngine::Window::Handle handle) noexcept;
        virtual ~TasInputLayer() noexcept;    

        virtual void OnEvent(CoreEngine::Basic_Event& e) noexcept override;
        virtual void OnUpdate(CoreEngine::Units::MicroSecond dt) noexcept override;
        virtual void OnRender() noexcept override;
        virtual void OnImGuiRender() noexcept override;

        static void OnRaceStarted() noexcept;
        static void OnRaceEnded() noexcept;
        static void OnDLLUpdate() noexcept;

        static void CreateInstance() noexcept;
        [[nodiscard]] static bool InstanceExists() noexcept;
        static void DeleteInstance() noexcept;

    private:
        void OnLoadHotkeys(const std::string& path) noexcept;

        bool m_use_transform_override_patch = false;
        bool m_is_in_delete_all_process = false;
        CoreEngine::Timer m_delete_all_timer;

        struct Hotkey
        {
            enum class Type { NONE, RESTART_RACE, QUIT_RACE };
            Type m_type         = Type::NONE;
            int  m_gflw_key     = 0;
            int  m_win32_key    = 0;
            bool m_was_pressed = false;
        };

        std::vector<Hotkey> m_hotkeys;

        static inline TasInputLayer* s_instance = nullptr;
        constexpr static inline std::string REPLAY_FOLDER_PATH = "replays/";
        constexpr static char HOTKEY_DEF_FILE_NAME[] = "tas.HOTKEYDEF";
    };
}