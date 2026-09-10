#include "core/application/Application.h"
#include "core/utility/Assert.h"
#include "core/utility/SingleAppInstance.h"

#include "layer/MainLayer.h"

#include "layer/TrackViewerLayer.h"

#include <exception>

int main()
{
    constexpr CoreEngine::Application::ApplicationConfig application_config 
    {
        .m_window_icon_path                 = "icondark.png",
        .m_enable_vsync                     = true,
        .m_debug_launch_with_console        = true,
        .m_use_glfw_await_events            = false
    };

    CoreEngine::SingleAppInstance single_app {L"AsphaltTasSingleInstance"};
    
    if (single_app.IsFirstInstance())
    {
        try 
        {
            CoreEngine::Application app = CoreEngine::Application::Create(application_config);
            AsphaltTas::MainLayer::CreateInstance();
            app.Run();
        } 
        catch (const std::exception& e)
        {
            ENGINE_INFO_LOG(e.what());
        }
    }
}
