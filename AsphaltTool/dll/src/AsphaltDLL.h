#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#include "Communication.h"

#include <mutex>
#include <optional>

#define LOCK_CURRENT_STATE_MUTEX() ::std::scoped_lock<std::mutex> __lock__game__dll__state__(::AsphaltDLL::GameDLLState::g_modify_current_state_mutex);

extern "C" __declspec(dllexport) void RequestShutdown() noexcept;

namespace AsphaltDLL 
{
    DWORD WINAPI Loop(void) noexcept;
    void SetupHooks() noexcept;
    void RemoveHooks() noexcept;

    void Initialize(HMODULE hmodule) noexcept;
    void Shutdown() noexcept;

    namespace GameDLLState
    {
        inline std::mutex g_modify_current_state_mutex;
        // Any detour function will write it's data in here. This is pushed to shared mem after each "NewPhysicsFrame" function call
        inline Communication::DllOut::DllStateOut g_current_state {};

        // "OnNewTick()" will check current replay input data, and should it be avaiable, add input data for the frame
        inline std::optional<Communication::DllIn::DllReplayInputIn> g_replay_current_frame_inputs = std::nullopt;
    }
}