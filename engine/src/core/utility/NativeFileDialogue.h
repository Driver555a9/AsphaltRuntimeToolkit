#pragma once

#include <cassert>
#include <utility>
#include <winerror.h>
#ifndef _WIN32 
    #error "Native File Dialogue requires Windows API"
#endif

#include <functional>
#include <filesystem>
#include <string>

namespace CoreEngine
{
    namespace NativeFileDialogue
    {
        struct OpenDialogueConfig
        {
            void* m_main_window_handle   = nullptr;
            std::wstring m_dialogue_name = L"";
            std::wstring m_filter_name   = L"";
            std::wstring m_filter_spec   = L"*.*";
            std::filesystem::path m_initial_directory = std::filesystem::current_path();
        };

        struct CreateDialogueConfig
        {
            void* m_main_window_handle    = nullptr;
            std::wstring m_dialogue_name  = L"";
            std::wstring m_extension      = L"";
            std::wstring m_filter_name    = L"";
            std::wstring m_filter_spec    = L"*.*";
            std::filesystem::path m_initial_directory = std::filesystem::current_path();
        };

        class DialogueResult
        {
        public:
            enum class Type { Success, Cancelled, Failed, Busy };
            DialogueResult() noexcept = default;
            DialogueResult(Type type, std::filesystem::path path) noexcept : m_type(type), m_path(std::move(path)) {}
            [[nodiscard]] Type GetType() const noexcept { return m_type; };
            [[nodiscard]] bool IsSuccess() const noexcept { return GetType() == Type::Success; }
            [[nodiscard]] std::filesystem::path GetPath() const noexcept { assert(IsSuccess() && "Mustn't access path unless successful."); return m_path; }
            void SetType(Type type) noexcept { m_type = type; }
            void SetPath(std::filesystem::path path) noexcept { m_path = std::move(path); }
        private:
            Type m_type                  = Type::Failed;
            std::filesystem::path m_path = "";
        };

        void OpenFile(OpenDialogueConfig config, std::function<void(DialogueResult)> callback) noexcept;
        void CreateNewFile(CreateDialogueConfig config, std::function<void(DialogueResult)> callback) noexcept;
    };
 
}
