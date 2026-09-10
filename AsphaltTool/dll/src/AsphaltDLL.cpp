#include "AsphaltDLL.h"

#include "AsphaltDLLUtility.h"
#include "Communication.h"
#include "DetourFunctions.h"

#include "MinHook.h"

#include <Xinput.h>
#include <chrono>
#include <atomic>
#include <thread>

namespace
{
    HANDLE g_thread_handle      = nullptr;
    HMODULE g_hmodule           = nullptr;
    std::atomic<bool> g_running = false;

    struct Hook
    {
        bool (*Setup)();
        bool (*Remove)();
        bool (*Enable)();
        bool (*Disable)();
    };
    std::vector<Hook> g_hooks;
}

#define EXPAND_HOOK(name) { name::SetupHook, name::RemoveHook, name::EnableHook, name::DisableHook }

extern "C" __declspec(dllexport) void RequestShutdown() noexcept
{
    AsphaltDLL::Shutdown();
}

namespace AsphaltDLL 
{
    DWORD WINAPI Loop(LPVOID) noexcept
    {
        //Utility::InitConsole();
        const auto* shared = ComSharedMem::GetSharedState();

        bool use_fallback_log_loc = true;
        if (shared->m_directory_external_tool_size > 0 && shared->m_directory_external_tool_size < Communication::SharedMemory::SharedState::EXTERNAL_TOOL_PATH_SIZE)
        {
            const std::wstring directory(shared->m_directory_external_tool, shared->m_directory_external_tool_size);
            use_fallback_log_loc = ! Utility::InitDebugLog(directory + L"\\dll_debug_log.txt");
        }
        if (use_fallback_log_loc)
        {
            Utility::InitDebugLog("dll_debug_log.txt");
        }

        if (MH_Initialize() == MH_OK)
        {
            DLL_INFO_LOG_FILE("Successfully initialized MinHook.");
            SetupHooks();
        }
        else
        {
            DLL_ERROR_LOG_FILE("Failed to initialize MinHook.");
        }

        while (g_running.load(std::memory_order::acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            
            {
                LOCK_CURRENT_STATE_MUTEX();
                DetourFunctions::StateManager::OnHandleGeneralInBuffer();
            }
        }

        RemoveHooks();

        MH_Uninitialize();
        Communication::SharedMemory::ShutdownSharedMemory();

        //Utility::ShutdownConsole();
        Utility::ShutdownDebugLog();

        FreeLibraryAndExitThread(g_hmodule, 0);
    }

    void SetupHooks() noexcept
    {
        DetourFunctions::CameraUpdate::PatchEnableGameFovWriteInstruction();
        DetourFunctions::AllowWreck::SetWreckIsAllowed(true);

        g_hooks.clear();

        g_hooks = {
            EXPAND_HOOK(DetourFunctions::FloatXorObfuscationSetter),
            EXPAND_HOOK(DetourFunctions::FloatXorObfuscationGetter),
            EXPAND_HOOK(DetourFunctions::PhysicsContextNewFrame),
            EXPAND_HOOK(DetourFunctions::PhysicsWorldWrapperNewTick),
            EXPAND_HOOK(DetourFunctions::InternalSingleStepSimulation),
            EXPAND_HOOK(DetourFunctions::DiscreteDynamicsWorldDestructor),
            EXPAND_HOOK(DetourFunctions::ProcessCollisionPolicyGetShouldCollide),
            EXPAND_HOOK(DetourFunctions::BarrelRandomLerp),
            EXPAND_HOOK(DetourFunctions::BarrelRandomBool),
            EXPAND_HOOK(DetourFunctions::BrakeValue),
            EXPAND_HOOK(DetourFunctions::SteeringValue),
            EXPAND_HOOK(DetourFunctions::AcceleratorValue),
            EXPAND_HOOK(DetourFunctions::EnableNitro),
            EXPAND_HOOK(DetourFunctions::DecreaseNitroBarFunc),
            EXPAND_HOOK(DetourFunctions::IncreaseNitroBarFunc),
            EXPAND_HOOK(DetourFunctions::LocalRacerAccessPoint),
            EXPAND_HOOK(DetourFunctions::GetLocalRacerStruct),
            EXPAND_HOOK(DetourFunctions::CameraUpdate),
            EXPAND_HOOK(DetourFunctions::NativeQueueRacerRespawn),
            //EXPAND_HOOK(DetourFunctions::BarrelRollStabilization), // Deprecated
            //EXPAND_HOOK(DetourFunctions::BarrelYawStabilization),  // Deprecated
            EXPAND_HOOK(DetourFunctions::FinalRacerTransformWriter),
            EXPAND_HOOK(DetourFunctions::OnWreckDeployBreakables),
            EXPAND_HOOK(DetourFunctions::OnRespawnButtonPressed),
            EXPAND_HOOK(DetourFunctions::GetPhysicsInterval),
            EXPAND_HOOK(DetourFunctions::OnBeginRaceFunction),
            EXPAND_HOOK(DetourFunctions::OnClickPlayFunction),
            EXPAND_HOOK(DetourFunctions::OnEndRaceFunction),
            EXPAND_HOOK(DetourFunctions::OnUpdateRaceProgress),
            EXPAND_HOOK(DetourFunctions::OnUpdateCheckpoint),
            EXPAND_HOOK(DetourFunctions::SegmentResolve),
            EXPAND_HOOK(DetourFunctions::RenderGUIToggle),
            EXPAND_HOOK(DetourFunctions::XInput_GetState),
            EXPAND_HOOK(DetourFunctions::NewLogicTickDispatcher),
            EXPAND_HOOK(DetourFunctions::AnimationProgressFunction),
            EXPAND_HOOK(DetourFunctions::UpdateCursorVisibility),
            EXPAND_HOOK(DetourFunctions::SpeedUpUIAnimations),
            EXPAND_HOOK(DetourFunctions::UcrtBaseRand),
            EXPAND_HOOK(DetourFunctions::BVHBroadphaseTraversal),
            EXPAND_HOOK(DetourFunctions::PauseMenuLogic),
            EXPAND_HOOK(DetourFunctions::WorldShouldResetQuery),
            EXPAND_HOOK(DetourFunctions::ProcessLevelResetFadePhase)
            ///////////////////////// Experimental /////////////////////////
            // EXPAND_HOOK(DetourFunctions::Experimental::InitiateNewFrame),
            // EXPAND_HOOK(DetourFunctions::Experimental::NewFrameSubscriberList),
             //EXPAND_HOOK(DetourFunctions::Experimental::MainFpsLimiter),
            // EXPAND_HOOK(DetourFunctions::Experimental::QueryPerformanceCounterHook),
            // EXPAND_HOOK(DetourFunctions::Experimental::OnRaycastVehicleUpdate),
            // EXPAND_HOOK(DetourFunctions::Experimental::PhysicsWorldRaycast)
        };

        for (auto& hook : g_hooks)
            hook.Setup();

        for (auto& hook : g_hooks)
            hook.Enable();
    }

    void RemoveHooks() noexcept
    {
        DetourFunctions::CameraUpdate::PatchEnableGameFovWriteInstruction();
        DetourFunctions::AllowWreck::SetWreckIsAllowed(true);
        
        for (auto it = g_hooks.rbegin(); it != g_hooks.rend(); ++it)
            it->Disable();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        for (auto it = g_hooks.rbegin(); it != g_hooks.rend(); ++it)
            it->Remove();

        g_hooks.clear();
    }

    void Initialize(HMODULE hmodule) noexcept
    {
        g_hmodule = hmodule;
        g_running.store(true, std::memory_order::release);
        g_thread_handle = CreateThread(nullptr, 0, &AsphaltDLL::Loop, nullptr, 0, nullptr);
    }

    void Shutdown() noexcept
    {
        DLL_INFO_LOG_FILE("Shutdown initiated...");
        g_running.store(false, std::memory_order::release);
    }
}