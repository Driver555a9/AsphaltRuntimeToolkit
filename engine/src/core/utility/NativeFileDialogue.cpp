#include "core/utility/NativeFileDialogue.h"
#include "core/application/Application.h"

#include <atomic>
#include <winerror.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace CoreEngine
{
namespace 
{
    std::jthread g_dialogue_thread;
    std::atomic<bool> g_running = false;

    NativeFileDialogue::DialogueResult OpenFileDialog(const NativeFileDialogue::OpenDialogueConfig& config) noexcept
    {
        NativeFileDialogue::DialogueResult result;
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;

        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));

        if (FAILED(hr)) 
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        COMDLG_FILTERSPEC filter { .pszName = config.m_filter_name.c_str(), .pszSpec = config.m_filter_spec.c_str() };
        COMDLG_FILTERSPEC filters[] = {filter};

        hr = dialog->SetFileTypes(ARRAYSIZE(filters), filters);
        if (FAILED(hr))
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        dialog->SetTitle(config.m_dialogue_name.c_str());

        Microsoft::WRL::ComPtr<IShellItem> folder;
        hr = SHCreateItemFromParsingName(config.m_initial_directory.c_str(), nullptr, IID_PPV_ARGS(&folder));

        if (SUCCEEDED(hr))
        {
            dialog->SetFolder(folder.Get());
        }

        hr = dialog->Show(static_cast<HWND>(config.m_main_window_handle));

        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) 
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Cancelled);
            return result;
        }

        if (FAILED(hr)) 
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        Microsoft::WRL::ComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);

        if (FAILED(hr)) 
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        PWSTR path = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);

        if (FAILED(hr)) 
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        std::wstring wstr_path(path);
        CoTaskMemFree(path);

        result.SetPath(wstr_path);
        result.SetType(NativeFileDialogue::DialogueResult::Type::Success);
        return result;
    }

    NativeFileDialogue::DialogueResult CreateNewFileDialogue(const NativeFileDialogue::CreateDialogueConfig& config) noexcept
    {
        NativeFileDialogue::DialogueResult result;
        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;

        HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));

        if (FAILED(hr))
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        if (! config.m_filter_spec.empty())
        {
            COMDLG_FILTERSPEC filter{ .pszName = config.m_filter_name.c_str(), .pszSpec = config.m_filter_spec.c_str()};
            dialog->SetFileTypes(1, &filter);
        }

        if (! config.m_extension.empty())
        {
            if (SUCCEEDED(dialog->SetDefaultExtension(config.m_extension.c_str())))
            {
                FILEOPENDIALOGOPTIONS options;
                if (SUCCEEDED(dialog->GetOptions(&options)))
                {
                    dialog->SetOptions(options | FOS_STRICTFILETYPES);
                }
            }
        }

        Microsoft::WRL::ComPtr<IShellItem> folder;
        hr = SHCreateItemFromParsingName(config.m_initial_directory.c_str(), nullptr, IID_PPV_ARGS(&folder));

        if (SUCCEEDED(hr))
        {
            dialog->SetFolder(folder.Get());
        }

        dialog->SetTitle(config.m_dialogue_name.c_str());

        hr = dialog->Show(static_cast<HWND>(config.m_main_window_handle));

        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Cancelled);
            return result;
        }

        if (FAILED(hr))
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        Microsoft::WRL::ComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);

        if (FAILED(hr))
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        PWSTR path = nullptr;

        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);

        if (FAILED(hr))
        {
            result.SetType(NativeFileDialogue::DialogueResult::Type::Failed);
            return result;
        }

        result.SetPath(std::wstring(path));
        result.SetType(NativeFileDialogue::DialogueResult::Type::Success);

        CoTaskMemFree(path);

        return result;
    }

    class ExecuteCallbackTask : public Application::BasicTask
    {
    public:
        ExecuteCallbackTask(std::function<void(NativeFileDialogue::DialogueResult)> callback,NativeFileDialogue:: DialogueResult result) noexcept 
        : m_callback(std::move(callback)), m_result(std::move(result)) {}
        virtual ~ExecuteCallbackTask() noexcept override = default;
        virtual void Execute() noexcept override 
        {
            m_callback(m_result);
        }
    private:
        std::function<void(NativeFileDialogue::DialogueResult)> m_callback;
        NativeFileDialogue::DialogueResult m_result;
    };
}

    void NativeFileDialogue::OpenFile(OpenDialogueConfig config, std::function<void(DialogueResult)> callback) noexcept 
    { 
        if (g_running.exchange(true))
        {
            Application::Get()->ScheduleTask(std::make_unique<ExecuteCallbackTask>(std::move(callback), DialogueResult(DialogueResult::Type::Busy, "")));
            return;
        }
        
        g_dialogue_thread = std::jthread([config = std::move(config), callback = std::move(callback)]() -> void 
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr))
            {
                Application::Get()->ScheduleTask(std::make_unique<ExecuteCallbackTask>(std::move(callback), DialogueResult(DialogueResult::Type::Failed, "")));
                g_running.store(false);
                return;
            }
            NativeFileDialogue::DialogueResult result = OpenFileDialog(config);
            CoUninitialize();
            Application::Get()->ScheduleTask(std::make_unique<ExecuteCallbackTask>(std::move(callback), std::move(result)));
            g_running.store(false);
        });
    }

    void NativeFileDialogue::CreateNewFile(CreateDialogueConfig config, std::function<void(DialogueResult)> callback) noexcept
    {
        if (g_running.exchange(true))
        {
            Application::Get()->ScheduleTask(std::make_unique<ExecuteCallbackTask>(std::move(callback), DialogueResult(DialogueResult::Type::Busy, "")));
            return;
        }
        
        g_dialogue_thread = std::jthread([config = std::move(config), callback = std::move(callback)]() -> void 
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr))
            {
                Application::Get()->ScheduleTask(std::make_unique<ExecuteCallbackTask>(std::move(callback), DialogueResult(DialogueResult::Type::Failed, "")));
                g_running.store(false);
                return;
            }

            NativeFileDialogue::DialogueResult result = CreateNewFileDialogue(config);
            CoUninitialize();
            Application::Get()->ScheduleTask(std::make_unique<ExecuteCallbackTask>(std::move(callback), std::move(result)));
            g_running.store(false);
        });
    }

}