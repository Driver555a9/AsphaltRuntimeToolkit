#include "DetourFunctions.h"
#include "AsphaltDLL.h"
#include "AsphaltDLLUtility.h"
#include "BulletTypes.h"
#include "Communication.h"
#include "Tests.h"
#include "BulletDebugDrawStream.h"
#include "BulletSerializer.h"
#include "Timer.h"

#include <atomic>
#include <basetsd.h>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <XInput.h>
#include <cstdlib>
#include <cstring>
#include <excpt.h>
#include <array>
#include <ios>
#include <minwinbase.h>
#include <optional>
#include <ostream>
#include <random>
#include <synchapi.h>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <vector>
#include <winuser.h>

#include "MinHook.h"

#define DETOUR_FUNCTION_DEF __fastcall

namespace ComSharedMem = Communication::SharedMemory;
namespace ComDllIn = Communication::DllIn;
namespace ComDllOut = Communication::DllOut;

uintptr_t GetMainModule() noexcept
{
    static uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"Asphalt9_Steam_x64_rtl.exe"));
    return module;
}

namespace AsphaltDLL
{
    namespace DetourFunctions 
    {
        ////////////////////////////////////////////////////////////
        // Implementation for generic memory R/W utilities
        ////////////////////////////////////////////////////////////
        namespace _Implementation
        {
            /// Detour_func               : called in place of actual func by hook
            /// Out_real_function_address : writes address of the real function in there. Used to enable or disable hook
            /// Out_trampoline            : writes address for calling original function
            bool SetupHook(LPCWSTR module_name, LPCSTR function_name, LPVOID detour_func, LPVOID* out_real_function_address, LPVOID* out_trampoline, std::atomic<HookState>& state) noexcept
            {
                if (state.load(std::memory_order::acquire) != HookState::NotInPlace)
                    return true;

                MH_STATUS status = MH_OK;

                status = MH_CreateHookApiEx(module_name, function_name, detour_func, out_trampoline, out_real_function_address);

                if (status != MH_OK)
                {
                    DLL_ERROR_LOG_FILE("Failed to create Hook: Status: " << MH_StatusToString(status) << " Target: " 
                    << function_name << "() in module: " << Utility::LPCWSTRToString(module_name));
                    return false;
                }

                DLL_INFO_LOG_FILE("Successfully installed Hook for: " << function_name << "() in module: " << Utility::LPCWSTRToString(module_name));
                state.store(HookState::InPlaceDisabled, std::memory_order::release);
                return true;
            }

            bool SetupHook(LPCWSTR module_name, ULONG_PTR offset, LPVOID detour_func, LPVOID* out_real_function_address, LPVOID* out_trampoline, std::atomic<HookState>& state) noexcept
            {
                if (state.load(std::memory_order::acquire) != HookState::NotInPlace) return true;

                HMODULE module_hmod = GetModuleHandleW(module_name);
                if (!module_hmod)
                {
                    DLL_ERROR_LOG_FILE("Failed to get module handle for: " << Utility::LPCWSTRToString(module_name));
                    return false;
                }

                uintptr_t base_address = reinterpret_cast<uintptr_t>(module_hmod);
                *out_real_function_address = reinterpret_cast<LPVOID>(base_address + offset);

                MH_STATUS status = MH_CreateHook(*out_real_function_address, detour_func, reinterpret_cast<void**>(out_trampoline));
                
                if (status != MH_OK)
                {
                    DLL_ERROR_LOG_FILE("MinHook Error: " << MH_StatusToString(status) << " For Module: " 
                    << Utility::LPCWSTRToString(module_name) << " at Address: " << *out_real_function_address);
                    return false;
                }

                DLL_INFO_LOG_FILE("Successfully installed Hook in Module: " << Utility::LPCWSTRToString(module_name) 
                << " at offset: 0x" << std::hex << std::uppercase << offset << std::dec);
                state.store(HookState::InPlaceDisabled, std::memory_order::release);
                
                return true;
            }

            bool RemoveHook(LPVOID real_function_address, std::atomic<HookState>& state) noexcept
            {
                if (state.load(std::memory_order::acquire) == HookState::NotInPlace) return true;

                const MH_STATUS status = MH_RemoveHook(real_function_address);
                const bool success = status == MH_OK;

                if (success)
                {
                    state.store(HookState::NotInPlace, std::memory_order::release);
                    DLL_INFO_LOG_FILE("Successfully removed hook at address: 0x" << std::hex << std::uppercase << real_function_address << std::dec);
                }
                else
                {
                    DLL_ERROR_LOG_FILE("Failed to remove Hook: " << MH_StatusToString(status) << " Address: 0x" << std::hex << std::uppercase << real_function_address << std::dec);
                }

                return success;
            }

            bool EnableHook(LPVOID real_function_address, std::atomic<HookState>& state) noexcept
            {
                if (state.load(std::memory_order::acquire) == HookState::InPlaceEnabled) return true;
                if (state.load(std::memory_order::acquire) == HookState::NotInPlace) return false;

                const MH_STATUS status = MH_EnableHook(real_function_address);
                const bool success = status == MH_OK;

                if (success)
                {
                    state.store(HookState::InPlaceEnabled, std::memory_order::release);
                    DLL_INFO_LOG_FILE("Successfully enabled hook at address: 0x" << std::hex << std::uppercase << real_function_address << std::dec);
                }
                else 
                {
                    DLL_ERROR_LOG_FILE("Failed to enable Hook: " << MH_StatusToString(status) << " Address: 0x" << std::hex << std::uppercase << real_function_address << std::dec);
                }

                return success;
            }

            bool DisableHook(LPVOID real_function_address, std::atomic<HookState>& state) noexcept
            {
                if (state.load(std::memory_order::acquire) != HookState::InPlaceEnabled) return true;

                const MH_STATUS status = MH_DisableHook(real_function_address);
                const bool success = status == MH_OK;

                if (success)
                {
                    state.store(HookState::InPlaceDisabled, std::memory_order::release);
                    DLL_INFO_LOG_FILE("Successfully disabled hook at Address: 0x" << std::hex << std::uppercase << real_function_address << std::dec);
                }
                else 
                {
                    DLL_ERROR_LOG_FILE("Failed to disable Hook: " << MH_StatusToString(status) << " Address: 0x" << std::hex << std::uppercase << real_function_address << std::dec);
                }

                return success;
            }

            void PatchMemory(LPCWSTR module_name, uintptr_t offset, const uint8_t* bytes, size_t size) noexcept
            {
                HMODULE module_hmod = GetModuleHandleW(module_name);
                if (!module_hmod)
                {
                    DLL_ERROR_LOG_FILE("Failed to get module handle for: " << Utility::LPCWSTRToString(module_name));
                    return;
                }

                void* targetAddress = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(module_hmod) + offset);
                
                DWORD oldProtect;
                if (VirtualProtect(targetAddress, size, PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    std::memcpy(targetAddress, bytes, size);
                    VirtualProtect(targetAddress, size, oldProtect, &oldProtect);
                }
                DLL_INFO_LOG_FILE("Successfully patched memory in module: " << Utility::LPCWSTRToString(module_name) << " Offset: 0x" << std::hex 
                << std::uppercase << offset << std::dec << " Size: " << size);
            }
        } 

        namespace StateManager
        {
        namespace
        {
            BulletTypes::DebugDrawStream g_debug_draw_stream {};
            std::unordered_map<uint64_t, std::vector<uint64_t>> g_mesh_id_to_submesh_ids;
            int64_t g_debug_draw_call_counter = 0;

            struct EquilibriumState 
            {
                BulletTypes::UnalignedTransform m_transform;
                float m_nitro_bar;
            };
            std::optional<EquilibriumState> g_last_equilibrium_state = std::nullopt;
            
            uint32_t g_after_restart_remaining_locked_ticks = 0;
        }

            // FWD declaration
            bool OnQueuePauseMenuCmd(ComDllIn::PausedMenuCmd cmd) noexcept;

            // No mutex lock, caller must lock
            void OnHandleGeneralInBuffer() noexcept
            {
                ComSharedMem::SharedState* shared = ComSharedMem::GetSharedState();
                auto& in_buff = shared->m_dll_in_buffer_general;
                ComDllIn::DllGeneralCommandsIn command;

                if (shared->m_non_negotiable_communication_version != Communication::CURRENT_NON_NEGOTIABLE_COMMUNICATION_VERSION)
                {
                    static bool once = false;
                    if (!once)
                    {
                        DLL_ERROR_LOG_FILE("Non negotiable communication version missmatch: Expected: " << Communication::CURRENT_NON_NEGOTIABLE_COMMUNICATION_VERSION
                                     << " Actual: " << shared->m_non_negotiable_communication_version << " Commands ignored.");
                        once = true; 
                    }
                    return;
                }

                bool request_shutdown = false;
                while (in_buff.TryPop(command))
                {
                    const ComDllIn::WriteMetaData& meta_cmd = command.m_write_meta_data;
                    const ComDllIn::WriteRacerState& racer_cmd = command.m_write_racer_state;
                    const ComDllIn::WriteCameraState& camera_cmd = command.m_write_camera_state;

                    if (meta_cmd.m_command_type == ComDllIn::CommandType::ExecuteCommand)
                    {
                        if (meta_cmd.m_request_dll_shutdown)
                        {
                            request_shutdown = true;
                        }

                        if ( GameDLLState::g_current_state.m_meta_data.m_replay_mode_status != Communication::ReplayMode::Inactive
                          && GameDLLState::g_current_state.m_meta_data.m_race_status_state  == ComDllOut::RaceStatusState::IN_RACE
                          && meta_cmd.m_replay_mode == Communication::ReplayMode::Inactive)
                        {
                            // We switched from replay to inactive - final frame
                            PhysicsContextNewFrame::QueueSkipSubsequentTicks(GameDLLState::g_current_state.m_meta_data.m_on_replay_end_skip_tick_count);
                        }
                        
                        auto& curr_meta = GameDLLState::g_current_state.m_meta_data;
                            
                        curr_meta.m_physics_interval                        = meta_cmd.m_physics_interval;
                        curr_meta.m_fixed_frame_interval_micros             = std::clamp<uint32_t>(meta_cmd.m_fixed_frame_interval_micros, 4167, 8333);
                        curr_meta.m_target_frame_interval_micros            = std::clamp<uint32_t>(meta_cmd.m_target_frame_interval_micros, 1, 1'000'000);
                        curr_meta.m_replay_mode_status                      = meta_cmd.m_replay_mode;
                        curr_meta.m_apply_physics_interval_override         = meta_cmd.m_apply_physics_interval_override; 
                        curr_meta.m_gui_is_hidden                           = meta_cmd.m_hide_gui;
                        curr_meta.m_skip_animation_flags                    = meta_cmd.m_skip_animation_flags;
                        curr_meta.m_replay_speed_factor                     = meta_cmd.m_replay_speed_factor;
                        curr_meta.m_on_replay_end_skip_tick_count           = meta_cmd.m_on_replay_end_skip_tick_count;  
                        curr_meta.m_speed_up_pre_race_cinematic             = meta_cmd.m_speed_up_pre_race_cinematic;   
                        curr_meta.m_dump_track_request_id                   = meta_cmd.m_dump_track_request_id;
                        curr_meta.m_speed_up_gui_animations                 = meta_cmd.m_speed_up_gui_animations;
                        curr_meta.m_update_debug_draw_stream                = meta_cmd.m_update_debug_draw_stream;
                        curr_meta.m_bullet_debug_draw_flags                 = meta_cmd.m_bullet_debug_draw_flags;

                        OnQueuePauseMenuCmd(meta_cmd.m_paused_menu_cmd);
                        g_debug_draw_stream.SetDebugMode(meta_cmd.m_bullet_debug_draw_flags);

                        if (meta_cmd.m_request_track_reset && curr_meta.m_race_status_state == ComDllOut::RaceStatusState::IN_RACE)
                        {
                            WorldShouldResetQuery::QueueResetWorld();
                        }

                        const auto& resolved_addresses = GameDLLState::g_current_state.m_resolved_addresses;

                        if (resolved_addresses.m_game_target_fps_interval_address != NO_VALID_RESOLVED_ADDRESS)
                        {
                            //Keep game fps setting 
                            //*reinterpret_cast<uint32_t*>(resolved_addresses.m_game_target_fps_interval_address) = curr_meta.m_target_frame_interval_micros;
                        }
                    }

                    if (racer_cmd.m_command_type == ComDllIn::CommandType::ExecuteCommand)
                    {
                        const uintptr_t racer_base_addr = GameDLLState::g_current_state.m_resolved_addresses.m_local_racer_base_address;

                        // We set the values in our current state such that RacerUpdate can force this state if continuous override is on
                        GameDLLState::g_current_state.m_racer_state.m_continuous_override_on_flags = racer_cmd.m_continuous_override_on_flags;

                        std::memcpy(GameDLLState::g_current_state.m_racer_state.m_racer_transform_mat4x4.Data(), 
                                    racer_cmd.m_racer_transform_mat4x4.Data(), sizeof(racer_cmd.m_racer_transform_mat4x4));

                        std::memcpy(GameDLLState::g_current_state.m_racer_state.m_racer_velocity_vec3.Data(), 
                                    racer_cmd.m_racer_velocity_vec3.Data(), sizeof(racer_cmd.m_racer_velocity_vec3));

                        if (racer_base_addr != NO_VALID_RESOLVED_ADDRESS)
                        {
                            std::memcpy(reinterpret_cast<void*>(racer_base_addr + ComDllIn::WriteRacerState::OFFSET_TRANSFORM), racer_cmd.m_racer_transform_mat4x4.Data(),
                                        sizeof(decltype(racer_cmd.m_racer_transform_mat4x4)));
                            
                            std::memcpy(reinterpret_cast<void*>(racer_base_addr + ComDllIn::WriteRacerState::OFFSET_VELOCITY), racer_cmd.m_racer_velocity_vec3.Data(),
                                        sizeof(decltype(racer_cmd.m_racer_velocity_vec3)));
                        } 
                        else 
                        {
                            DLL_ERROR_LOG_FILE("Could not snap Racer to given transform because Racer base address is null.");
                        }

                        if (racer_cmd.m_nitro_bar_value >= 0.0f)
                        {
                            UpdateNitroBar::WriteNitroBar(racer_cmd.m_nitro_bar_value);
                        }
                    }   

                    if (camera_cmd.m_command_type == ComDllIn::CommandType::ExecuteCommand)
                    {
                        const uintptr_t camera_base_addr = GameDLLState::g_current_state.m_resolved_addresses.m_camera_state_base_address;

                        // We set the values in our current state such that CameraUpdate can force this state if continuous override is on
                        GameDLLState::g_current_state.m_camera_state.m_continuous_override_on_flags = camera_cmd.m_continuous_override_on_flags;

                        if (camera_cmd.m_continuous_override_on_flags & ComDllIn::WriteCameraState::CONTINUOUS_OVERRIDE_FOV_RAD)
                        {
                            CameraUpdate::PatchDisableGameFovWriteInstruction();
                        }
                        else 
                        {
                            CameraUpdate::PatchEnableGameFovWriteInstruction();
                        }

                        std::memcpy(GameDLLState::g_current_state.m_camera_state.m_offset_relative_to_car.Data(), 
                                    camera_cmd.m_offset_relative_to_car.Data(), sizeof(camera_cmd.m_offset_relative_to_car));

                        std::memcpy(GameDLLState::g_current_state.m_camera_state.m_camera_position_vec3.Data(), 
                                    camera_cmd.m_camera_position_vec3.Data(), sizeof(camera_cmd.m_camera_position_vec3));

                        std::memcpy(GameDLLState::g_current_state.m_camera_state.m_camera_rotation_quat.Data(), 
                                    camera_cmd.m_camera_rotation_quat.Data(), sizeof(camera_cmd.m_camera_rotation_quat));

                        GameDLLState::g_current_state.m_camera_state.m_fov_radians = camera_cmd.m_fov_radians;
                        GameDLLState::g_current_state.m_camera_state.m_look_backwards = camera_cmd.m_look_backwards;
                        
                        if (camera_base_addr != NO_VALID_RESOLVED_ADDRESS)
                        {
                            std::memcpy(reinterpret_cast<void*>(camera_base_addr + ComDllIn::WriteCameraState::OFFSET_POSITON_VEC3), camera_cmd.m_camera_position_vec3.Data(),
                                        sizeof(decltype(camera_cmd.m_camera_position_vec3)));
                            
                            std::memcpy(reinterpret_cast<void*>(camera_base_addr + ComDllIn::WriteCameraState::OFFSET_ROTATION_QUAT), camera_cmd.m_camera_rotation_quat.Data(),
                                        sizeof(decltype(camera_cmd.m_camera_rotation_quat)));

                            *reinterpret_cast<float*>(camera_base_addr + ComDllIn::WriteCameraState::OFFSET_FOV_RADIANS) = camera_cmd.m_fov_radians;
                        }
                        else 
                        {
                            DLL_ERROR_LOG_FILE("Could not snap Camera to given transform because Camera base address is null.");
                        }
                    }
                }

                if (request_shutdown)
                {
                    RequestShutdown();
                }
            }

            // No mutex lock, caller must lock
            void OnTryResolveNitroRCXArgPointer() noexcept
            {
                const auto status = GameDLLState::g_current_state.m_meta_data.m_race_status_state;

                if (GameDLLState::g_current_state.m_resolved_addresses.m_nitro_func_spoofed_rcx_arg != NO_VALID_RESOLVED_ADDRESS
                || ! (status == ComDllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC || status == ComDllOut::RaceStatusState::IN_RACE))
                {
                    return;
                }

                /////////////////////////////////////////////////
                // Use the pointer chains to try and resolve nitro argument pointer
                // If the chains converge, we assume this is the correct value
                /////////////////////////////////////////////////
                static const std::vector<uintptr_t> pointerchain_1 = {0x6EE7C08, 0x170, 0x68, 0x30, 0x0, 0x30, 0x28,0x38, 0x30 };
                static const std::vector<uintptr_t> pointerchain_2 = {0x6EE7C48, 0x170, 0x68, 0x38, 0x0, 0x58, 0x8, 0x30, 0x28, 0x88  };
                static const std::vector<uintptr_t> pointerchain_3 = {0x6EE7C48, 0x2C8, 0x48, 0x68, 0x30, 0x0, 0x30, 0x28, 0x38, 0x30 };
                const uintptr_t rcx_chain_1 = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_1);
                const uintptr_t rcx_chain_2 = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_2);
                const uintptr_t rcx_chain_3 = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_3);

                if (rcx_chain_1 == rcx_chain_2 && rcx_chain_2 == rcx_chain_3)
                {
                    GameDLLState::g_current_state.m_resolved_addresses.m_nitro_func_spoofed_rcx_arg = rcx_chain_1;
                }
            }

            void OnTryResolveRespawnButtonRCXArgPointer() noexcept
            {
                const auto status = GameDLLState::g_current_state.m_meta_data.m_race_status_state;
                
                if (GameDLLState::g_current_state.m_resolved_addresses.m_respawn_func_spoofed_rcx_arg != NO_VALID_RESOLVED_ADDRESS
                || ! (status == ComDllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC || status == ComDllOut::RaceStatusState::IN_RACE))
                {
                    return;
                }

                static const std::vector<uintptr_t> pointerchain_1 = {0x6EE7C48, 0x378, 0x20, 0x8,  0x30, 0x40 };
                static const std::vector<uintptr_t> pointerchain_2 = {0x6EE7C08, 0x18,  0x8,  0x0,  0x8,  0x58, 0x30 };
                static const std::vector<uintptr_t> pointerchain_3 = {0x6EE7C08, 0x378, 0x10, 0x30, 0x40 };
                const uintptr_t rcx_chain_1 = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_1) + 0x38;
                const uintptr_t rcx_chain_2 = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_2) + 0x1E0;
                const uintptr_t rcx_chain_3 = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_3) + 0x38;

                if (rcx_chain_1 == rcx_chain_2 && rcx_chain_2 == rcx_chain_3)
                {
                    GameDLLState::g_current_state.m_resolved_addresses.m_respawn_func_spoofed_rcx_arg = rcx_chain_1;
                }
            }

            // No mutex lock, caller must lock
            void OnTryResolveFpsTargetIntervalPointer() noexcept
            {
                auto& fps_addr = GameDLLState::g_current_state.m_resolved_addresses.m_game_target_fps_interval_address;
                if (fps_addr != NO_VALID_RESOLVED_ADDRESS)
                {
                    return;
                }

                static const std::vector<uintptr_t> pointerchain_1 = {0x06EE14E0, 0xFC8};
                fps_addr = Utility::SafeResolvePointerChain(GetMainModule(), pointerchain_1) + 0xC9C;

                if (! Utility::SafeDereference(reinterpret_cast<uint32_t*>(fps_addr)))
                {
                    fps_addr = NO_VALID_RESOLVED_ADDRESS;
                }

                GameDLLState::g_current_state.m_resolved_addresses.m_game_target_fps_interval_address = fps_addr; 
                //Detached our logic interval from game fps entirely
                /*if (fps_addr != NO_VALID_RESOLVED_ADDRESS)
                {
                    //Detached our logic interval from game fps
                    //GameDLLState::g_current_state.m_meta_data.m_target_frame_interval_micros = *reinterpret_cast<uint32_t*>(GameDLLState::g_current_state.m_resolved_addresses.m_game_target_fps_interval_address);
                }*/
            }

            // No mutex lock, caller must lock
            void OnNewTick() noexcept
            {
                GameDLLState::g_replay_current_frame_inputs = std::nullopt;

                OnTryResolveNitroRCXArgPointer();
                OnTryResolveFpsTargetIntervalPointer();
                OnTryResolveRespawnButtonRCXArgPointer();
                OnHandleGeneralInBuffer();

                if (GameDLLState::g_current_state.m_meta_data.m_race_status_state != ComDllOut::RaceStatusState::IN_RACE)
                {
                    return;
                }

                const uint32_t current_tick = GameDLLState::g_current_state.m_replay_inputs.m_race_frame_tick;
                const auto replay_mode= GameDLLState::g_current_state.m_meta_data.m_replay_mode_status;
                ComSharedMem::SharedState* shared = ComSharedMem::GetSharedState();
                auto& replay_buffer = shared->m_dll_in_buffer_input_replay;

                if (shared->m_non_negotiable_communication_version != Communication::CURRENT_NON_NEGOTIABLE_COMMUNICATION_VERSION)
                {
                    static bool once = false;
                    if (!once)
                    {
                        DLL_ERROR_LOG_FILE("Non negotiable communication version missmatch: Expected: " << Communication::CURRENT_NON_NEGOTIABLE_COMMUNICATION_VERSION
                                     << " Actual: " << shared->m_non_negotiable_communication_version << " Replay Inputs ignored.");
                        once = true; 
                    }
                    return;
                }

                if (replay_mode == Communication::ReplayMode::ActiveBlockThread)
                {
                    while (true)
                    {
                        OnHandleGeneralInBuffer();
                        if (GameDLLState::g_current_state.m_meta_data.m_replay_mode_status != Communication::ReplayMode::ActiveBlockThread) break;

                        ComDllIn::DllReplayInputIn pkt;
                        if (replay_buffer.TryPeek(pkt))
                        {
                            if (pkt.m_race_frame_tick == current_tick)
                            {
                                (void)replay_buffer.TryPop(pkt);
                                GameDLLState::g_replay_current_frame_inputs = pkt;
                                break;
                            }
                            else if (pkt.m_race_frame_tick < current_tick)
                            { 
                                (void)replay_buffer.TryPop(pkt); 
                                continue; 
                            }
                            else break;
                        }
                    }
                }

                if (g_after_restart_remaining_locked_ticks > 0)
                {
                    GameDLLState::g_replay_current_frame_inputs = std::nullopt;
                }
            }

            // No mutex lock, caller must lock
            void OnEndTick() noexcept 
            {
                const auto PushData = []()
                {
                    GameDLLState::g_current_state.IncreasePacketIDToHighest();
                    ComSharedMem::GetSharedState()->m_dll_out_buffer.PushOverwrite(GameDLLState::g_current_state);
                    ComSharedMem::GetSharedState()->m_dll_out_secondary_buffer.PushOverwrite(GameDLLState::g_current_state);
                };

                PushData();

                // Reset tick specific input state that is not updated per tick on its own
                GameDLLState::g_current_state.m_replay_inputs.m_respawn_button_press              = false;
                GameDLLState::g_current_state.m_replay_inputs.m_nitro_activation_count = 0;

                if (GameDLLState::g_current_state.m_meta_data.m_race_status_state == ComDllOut::RaceStatusState::IN_RACE)
                {
                    GameDLLState::g_current_state.m_replay_inputs.m_race_frame_tick++;
                }
            }

            // No mutex lock, caller must lock
            void OnEndRace() noexcept
            {
                GameDLLState::g_current_state.m_meta_data.m_race_status_state   = ComDllOut::RaceStatusState::IN_MENU;
                GameDLLState::g_current_state.m_replay_inputs.m_race_frame_tick = 0;
                GameDLLState::g_current_state.m_resolved_addresses.ResetAll();

                StateManager::g_last_equilibrium_state = std::nullopt;
            }

            // No mutex lock, caller must lock
            void OnBeginRace() noexcept
            {
                GameDLLState::g_current_state.m_meta_data.m_race_status_state   = ComDllOut::RaceStatusState::IN_RACE;
                GameDLLState::g_current_state.m_resolved_addresses.ResetAll();
            }

            // No mutex lock, caller must lock
            void OnDestroyPhysicsWorld() noexcept
            {
                StateManager::g_mesh_id_to_submesh_ids.clear();
                StateManager::g_debug_draw_call_counter = 0;
            }

            // Does not lock, caller must lock
            bool OnExportTrack() noexcept
            {
                BulletTypes::DiscreteDynamicsWorld* world = reinterpret_cast<BulletTypes::DiscreteDynamicsWorld*>(
                                                             GameDLLState::g_current_state.m_resolved_addresses.m_discrete_dynamics_world_instance_address);

                if (! world)
                {
                    DLL_INFO_LOG_FILE("Could not export track because Dynamics World is unresolved");
                    return false;
                }

                BulletTypes::AlignedObjectArray<BulletTypes::RigidBody*>& rigid_bodies    = world->m_rigid_bodies;
                BulletTypes::AlignedObjectArray<BulletTypes::GhostObject*>& ghost_objects = world->m_ghost_objects;
                BulletTypes::AlignedObjectArray<BulletTypes::RigidBody*>& non_static_rigid_bodies = world->m_non_static_rigid_bodies;

                std::vector<BulletTypes::CollisionObject*> objects;
                objects.reserve(ghost_objects.m_size + rigid_bodies.m_size);

                DLL_INFO_LOG_FILE("Exporting - RigidBodies: " << rigid_bodies.m_size << " - GhostObjects: " << ghost_objects.m_size 
                    << " - Non Static RigidBodies: " << non_static_rigid_bodies.m_size);

                for (size_t i {}; i < rigid_bodies.m_size; ++i)
                {
                    objects.push_back(rigid_bodies.m_data[i]);
                }
                for (size_t i {}; i < ghost_objects.m_size; ++i)
                {
                    objects.push_back(ghost_objects.m_data[i]);
                }
                for (size_t i {}; i < non_static_rigid_bodies.m_size; ++i)
                {
                    objects.push_back(non_static_rigid_bodies.m_data[i]);
                }

                BulletTypes::Serializer::SerializeObjectsToFile(objects, Communication::DLL_DUMPED_TRACK_FILE_NAME);

                return true;
            }

            // Does not lock, caller must lock
            bool OnExecuteDebugDrawToStream() noexcept
            {
                g_debug_draw_stream.ClearLines();

                /*if (! Utility::EqualsAny(GameDLLState::g_current_state.m_meta_data.m_race_status_state, Communication::DllOut::RaceStatusState::IN_RACE, 
                                                                                                        Communication::DllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC))
                {
                    return false;
                }*/

                auto* world = std::bit_cast<BulletTypes::DiscreteDynamicsWorld*>(GameDLLState::g_current_state.m_resolved_addresses.m_discrete_dynamics_world_instance_address);
                if (!world) return false;

                if (g_debug_draw_call_counter++ == 1)
                {
                    StateManager::g_debug_draw_stream.PushResetAllDataDrawCmd();
                    ComSharedMem::GetSharedState()->m_dll_out_debug_draw_stream.PushOverwrite(g_debug_draw_stream.GetStagingFrameData());
                    g_debug_draw_stream.ClearLines();
                    return true;
                }
                if (g_debug_draw_call_counter < 10) // We give external tool time to reset state before sending first packets of new track
                {
                    return true;
                }

                g_debug_draw_stream.SetDebugMode(GameDLLState::g_current_state.m_meta_data.m_bullet_debug_draw_flags);
                world->SetDebugDrawer(&g_debug_draw_stream);

                auto* shared = ComSharedMem::GetSharedState();

                const auto IsWithin = [](BulletTypes::UnalignedVector3 a, BulletTypes::UnalignedVector3 b, BulletTypes::UnalignedVector3 margin)
                {
                    return std::abs(a.x - b.x) < margin.x && std::abs(a.y - b.y) < margin.y && std::abs(a.z - b.z) < margin.z;
                };

                const auto DebugDrawObjectNormal = [&world](BulletTypes::CollisionObject* obj)
                {
                    const auto flags = obj->m_collision_flags;
                    obj->m_collision_flags &= ~BulletTypes::CF_DISABLE_VISUALIZE_OBJECT;
                    world->DebugDrawObject(obj->m_transform_matrix, obj->m_collision_shape_ptr, {0, 0, 1});
                    obj->m_collision_flags = flags;
                };

                const auto camera_pos = GameDLLState::g_current_state.m_camera_state.m_camera_position_vec3;

                for (int i = 0; i < world->m_rigid_bodies.m_size; ++i)
                {
                    auto* body = world->m_rigid_bodies.m_data[i];
                    const uint64_t mesh_id = reinterpret_cast<uint64_t>(body);

                    const BulletTypes::MultimaterialTriangleMeshShape* multimat = nullptr;
                    BulletTypes::Vector3 scale {1, 1, 1};

                    if (body->m_collision_shape_ptr->Is<BulletTypes::MultimaterialTriangleMeshShape>())
                    {
                        multimat = body->m_collision_shape_ptr->As<BulletTypes::MultimaterialTriangleMeshShape*>();
                    }
                    else if (body->m_collision_shape_ptr->Is<BulletTypes::ScaledBvhTriangleMeshShape>())
                    {
                        auto* scaled = body->m_collision_shape_ptr->As<BulletTypes::ScaledBvhTriangleMeshShape*>();
                        multimat = scaled->m_bvh_tri_mesh_shape->As<BulletTypes::MultimaterialTriangleMeshShape*>();
                        scale = scaled->m_local_scaling;
                    }

                    if (multimat)
                    {
                        auto [it, inserted] = g_mesh_id_to_submesh_ids.try_emplace(mesh_id);
                        if (inserted)
                        {
                            if (multimat->m_mesh_interface->IsInternalTriangleVertexMaterialArray())
                            {
                                g_debug_draw_stream.SetCaptureMode(BulletTypes::DebugDrawStream::CaptureMode::CachedMeshDefine);
                                it->second = g_debug_draw_stream.CustomDrawStaticMultiMaterialTriangleMesh(
                                    multimat, reinterpret_cast<BulletTypes::TriangleIndexVertexMaterialArray*>(multimat->m_mesh_interface), mesh_id
                                );
                                g_debug_draw_stream.SetCaptureMode(BulletTypes::DebugDrawStream::CaptureMode::Live);

                                for (const auto& chunk : g_debug_draw_stream.DrainPendingMeshChunks())
                                {
                                    shared->m_dll_out_debug_draw_static_meshes.PushOverwrite(chunk);
                                }
                            }
                            else
                            {
                                DLL_ERROR_LOG_FILE("Unexpected multimaterial triangle mesh shape without TriangleIndexVertexMaterialArray!");
                            }
                        }

                        for (uint64_t sub_id : g_mesh_id_to_submesh_ids[mesh_id])
                        {
                            g_debug_draw_stream.DrawCachedMeshInstance(sub_id, body->m_transform_matrix, scale);
                        }
                    }
                    else
                    {
                        DebugDrawObjectNormal(body);
                    }
                }

                for (int i = 0; i < world->m_non_static_rigid_bodies.m_size; i++)
                {
                    auto* body = world->m_non_static_rigid_bodies.m_data[i];
                    DebugDrawObjectNormal(body);
                }
                for (int i = 0; i < world->m_ghost_objects.m_size; i++)
                {
                    auto* ghost = world->m_ghost_objects.m_data[i];
                    if (IsWithin(camera_pos, {ghost->m_transform_matrix.m_origin.x, ghost->m_transform_matrix.m_origin.y, ghost->m_transform_matrix.m_origin.z}, {1.5f, 1.5f, 1.0f}))
                    {
                        continue; // hide annoying camera ghost sphere
                    }
                    DebugDrawObjectNormal(ghost);
                }

                /*int flags_without_objects = GameDLLState::g_current_state.m_meta_data.m_bullet_debug_draw_flags;
                flags_without_objects &= ~BulletTypes::DebugDrawStream::DebugDrawModes::DBG_DrawWireframe;
                flags_without_objects &= ~BulletTypes::DebugDrawStream::DebugDrawModes::DBG_DrawAabb;
                g_debug_draw_stream.SetDebugMode(flags_without_objects);
                world->DebugDrawWorld();
                g_debug_draw_stream.SetDebugMode(GameDLLState::g_current_state.m_meta_data.m_bullet_debug_draw_flags); */

                shared->m_dll_out_debug_draw_stream.PushOverwrite(g_debug_draw_stream.GetStagingFrameData());

                g_debug_draw_stream.ClearLines();
                world->SetDebugDrawer(nullptr);
                return true;
            }

            // Does not lock, caller must lock
            bool OnQueueQuickRestart() noexcept
            {
                /*if (! g_last_equilibrium_state.has_value())
                {
                    DLL_INFO_LOG_FILE("Could not restore equilibrium state - none avaiable.");
                    return false;
                }

                const auto base = GameDLLState::g_current_state.m_resolved_addresses.m_local_racer_base_address;

                if (base == NO_VALID_RESOLVED_ADDRESS)
                {
                    DLL_INFO_LOG_FILE("Could not restore equilibrium state - racer base is null.");
                    return false;
                }

                std::memcpy(reinterpret_cast<void*>(base + ComDllIn::WriteRacerState::OFFSET_TRANSFORM), g_last_equilibrium_state->m_transform.Data(), sizeof(BulletTypes::UnalignedTransform));
                BulletTypes::UnalignedVector3 zeros {0.0f, 0.0f, 0.0f};
                std::memcpy(reinterpret_cast<void*>(base + ComDllIn::WriteRacerState::OFFSET_VELOCITY), zeros.Data(), sizeof(zeros));
                UpdateNitroBar::WriteNitroBar(g_last_equilibrium_state->m_nitro_bar); */

                WorldShouldResetQuery::QueueResetWorld();

                // g_after_restart_remaining_locked_ticks = 100;

                return true;
            }

            // Does not lock, caller must lock
            bool OnQueuePauseMenuCmd(ComDllIn::PausedMenuCmd cmd) noexcept
            {
                const auto SendEscapeKeyInput = []()
                {
                    INPUT inputs[2] = {};

                    inputs[0].type       = INPUT_KEYBOARD;
                    inputs[0].ki.wVk     = VK_ESCAPE;
                    inputs[0].ki.dwFlags = 0;
                    inputs[1].type       = INPUT_KEYBOARD;
                    inputs[1].ki.wVk     = VK_ESCAPE;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

                    SendInput(2, inputs, sizeof(INPUT));
                };
                
                const auto state = GameDLLState::g_current_state.m_meta_data.m_race_status_state;
                if (! Utility::EqualsAny(state, ComDllOut::RaceStatusState::IN_RACE, ComDllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC) || IsPaused::GetIsPaused())
                {
                    return false;
                }

                switch (cmd)
                {
                    case ComDllIn::PausedMenuCmd::NONE: 
                    {
                        //PauseMenuLogic::QueueNothing();
                        break;
                    }
                    case ComDllIn::PausedMenuCmd::QUIT:
                    {
                        SendEscapeKeyInput();
                        PauseMenuLogic::QueueQuit();
                        break;
                    }
                    case ComDllIn::PausedMenuCmd::RESTART:
                    {
                        SendEscapeKeyInput();
                        PauseMenuLogic::QueueRestart();
                        break;
                    }
                }
                return true;
            }
        }

        namespace NewLogicTickDispatcher
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (*NewLogicTickDispatcher_t)(uintptr_t rcx, uintptr_t rdx);
                NewLogicTickDispatcher_t RealNewLogicTickDispatcherCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_NewLogicTickDispatcher(uintptr_t rcx, uintptr_t rdx)
                {
            //////////////////////////////// Experimental
                    /*Tests::LoadCustomTrack("test.TRACK");
                    static bool once = false;
                    if (! once)
                    {
                        AllowWreck::SetWreckIsAllowed(false);
                        NativeQueueRacerRespawn::SetRespawnIsAllowed(false);
                        once = true;
                    } */
            ////////////////////////////////

                    std::uint32_t iterations = 0;
                    
                    enum class FrameExecutionMode 
                    {
                        StandardFallback,
                        IntroCinematicSkip,
                        AccumulatorNormalRace,
                        AccumulatorReplayFastForward,
                        IsPausedOnlyOneTick
                    };
                    
                    FrameExecutionMode current_mode = FrameExecutionMode::StandardFallback;

                    {
                        LOCK_CURRENT_STATE_MUTEX();

                        GameDLLState::g_current_state.m_resolved_addresses.m_is_paused_func_spoofed_rcx_arg = reinterpret_cast<uintptr_t*>(rcx)[26];
                        GameDLLState::g_current_state.m_meta_data.m_is_currently_paused = IsPaused::GetIsPaused();

                        if (GameDLLState::g_current_state.m_meta_data.m_dump_track_request_id > GameDLLState::g_current_state.m_meta_data.m_last_completed_dump_request_id
                        && (GameDLLState::g_current_state.m_meta_data.m_race_status_state == Communication::DllOut::RaceStatusState::IN_RACE 
                         || GameDLLState::g_current_state.m_meta_data.m_race_status_state == Communication::DllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC))
                        {
                            if (StateManager::OnExportTrack())
                            {
                                GameDLLState::g_current_state.m_meta_data.m_last_completed_dump_request_id = GameDLLState::g_current_state.m_meta_data.m_dump_track_request_id; 
                            }
                            else 
                            {
                                DLL_ERROR_LOG_FILE("Failed to export track, despite request.");
                            }
                        }
                        
                        const auto status = GameDLLState::g_current_state.m_meta_data.m_race_status_state;
                        const bool speed_up_cin = GameDLLState::g_current_state.m_meta_data.m_speed_up_pre_race_cinematic;
                        const bool is_replay = (GameDLLState::g_current_state.m_meta_data.m_replay_mode_status != Communication::ReplayMode::Inactive);
                        const uint32_t replay_speed = GameDLLState::g_current_state.m_meta_data.m_replay_speed_factor;
                        const uint32_t speed_factor = is_replay ? replay_speed : 1;

                        if (IsPaused::GetIsPaused())
                        {
                            iterations   = 1;
                            current_mode = FrameExecutionMode::IsPausedOnlyOneTick;
                        }
                        else if (status == ComDllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC && speed_up_cin)
                        {
                            iterations = 10'000;
                            current_mode = FrameExecutionMode::IntroCinematicSkip;
                        }
                        else
                        {
                            current_mode = is_replay ? FrameExecutionMode::AccumulatorReplayFastForward : FrameExecutionMode::AccumulatorNormalRace;

                            static uint64_t last_time = Utility::GetMonotonicMicrosecondCount();
                            const  uint64_t time_now  = Utility::GetMonotonicMicrosecondCount();

                            const uint64_t elapsed = std::clamp<uint64_t>(time_now - last_time, 0, 250'000);
                            last_time = time_now; 

                            static int64_t accumulator = 0;
                            accumulator += (elapsed * speed_factor);
                            
                            const int64_t target_interval = GameDLLState::g_current_state.m_meta_data.m_target_frame_interval_micros;

                            if (target_interval > 0)
                            {
                                const int64_t ticks = accumulator / target_interval;

                                iterations += static_cast<int>(ticks);
                                accumulator -= ticks * target_interval;
                            }
                            else
                            {
                                iterations = 1;
                            }
                        }
                    }

                    for (std::uint32_t i = 0; i < iterations; ++i)
                    {
                        RealNewLogicTickDispatcherCall(rcx, rdx);

                        if (current_mode == FrameExecutionMode::AccumulatorNormalRace || current_mode == FrameExecutionMode::StandardFallback)
                        {
                            continue; 
                        }
                        {
                            LOCK_CURRENT_STATE_MUTEX();
                            if (current_mode == FrameExecutionMode::IntroCinematicSkip)
                            {
                                const auto status = GameDLLState::g_current_state.m_meta_data.m_race_status_state;
                                if (status != ComDllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC)
                                {
                                    break;
                                }
                            }
                            else if (current_mode == FrameExecutionMode::AccumulatorReplayFastForward)
                            {
                                if (!GameDLLState::g_replay_current_frame_inputs.has_value())
                                {
                                    break; 
                                }
                            }
                        }
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x4CF180; 

                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_NewLogicTickDispatcher), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealNewLogicTickDispatcherCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealNewLogicTickDispatcherCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace PhysicsContextNewFrame
        {
            namespace
            {
                inline std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                inline LPVOID g_real_function_address = nullptr;

                inline uint32_t g_ticks_left_to_skip = 0;

                using PhysicsContextNewFrame_t = uintptr_t* (DETOUR_FUNCTION_DEF*)(uintptr_t p_this, uintptr_t* p_out_delta_micros, uintptr_t* p_in_delta_micros);
                inline PhysicsContextNewFrame_t RealNewPhysicsFrameCall = nullptr;

                uintptr_t* Detour_PhysicsContextNewFrame(uintptr_t p_this, uintptr_t* p_out_delta_micros, uintptr_t* p_in_delta_micros)
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_resolved_addresses.m_physics_context_address = p_this;

                        // We skip this frames logic entirely if requested
                        if (g_ticks_left_to_skip > 0)
                        {
                            g_ticks_left_to_skip--;
                            *p_in_delta_micros  = 0; 
                            *p_out_delta_micros = 0;
                            return p_out_delta_micros;
                        }

                        if (GameDLLState::g_current_state.m_replay_inputs.m_race_frame_tick == 0)
                        {
                            // RESET MERSENNE TWISTER PRNG
                            BarrelPRNG::Reset();
                        }

                        *p_in_delta_micros = GameDLLState::g_current_state.m_meta_data.m_fixed_frame_interval_micros;

                        GameDLLState::g_current_state.m_racer_state.m_nitro_bar_value = UpdateNitroBar::ReadNitroBar();

                        if (GameDLLState::g_replay_current_frame_inputs.has_value())
                        {
                            if (! (GameDLLState::g_replay_current_frame_inputs->m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::NITRO_ACTIVATION))
                            {
                                EnableNitro::SpoofCallToEnableNitroFunction(GameDLLState::g_replay_current_frame_inputs->m_nitro_activation_count);
                            }

                            if ( ! (GameDLLState::g_replay_current_frame_inputs->m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::RESPAWN_BUTTON)
                                &&  GameDLLState::g_replay_current_frame_inputs->m_respawn_button_press)
                            {
                                OnRespawnButtonPressed::SpoofCallToRespawnInputFunc();
                            }
                        }
                    }

                    (void)RealNewPhysicsFrameCall(p_this, p_out_delta_micros, p_in_delta_micros);

                    {
                        LOCK_CURRENT_STATE_MUTEX();

                        if (GameDLLState::g_current_state.m_resolved_addresses.m_steering_struct_base_address != NO_VALID_RESOLVED_ADDRESS)
                        {
                            const auto gear_addr = GameDLLState::g_current_state.m_resolved_addresses.m_steering_struct_base_address;
                            constexpr uintptr_t OFFSET_TO_GEAR = 0xC0;
                            GameDLLState::g_current_state.m_racer_state.m_gear = *reinterpret_cast<uint32_t*>(gear_addr + OFFSET_TO_GEAR);
                            constexpr uintptr_t OFFSET_TO_RPM = 0x1D8;
                            GameDLLState::g_current_state.m_racer_state.m_rpm = *reinterpret_cast<float*>(gear_addr + OFFSET_TO_RPM);
                        }
                        StateManager::OnEndTick();
                    }

                    return p_out_delta_micros;
                }
            }

            void QueueSkipSubsequentTicks(uint32_t amount) noexcept
            {
                g_ticks_left_to_skip = amount;
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x4869C30;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_PhysicsContextNewFrame), 
                                                  &g_real_function_address, reinterpret_cast<LPVOID*>(&RealNewPhysicsFrameCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealNewPhysicsFrameCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace PhysicsWorldWrapperNewTick
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                LPVOID g_real_function_address = nullptr;

                typedef void* (DETOUR_FUNCTION_DEF* PhysicsWorldWrapperNewTickFunction_t)(uintptr_t p_this, float* p_delta_time, void* p_passthrough);
                PhysicsWorldWrapperNewTickFunction_t RealPhysicsWorldWrapperNewTickCall = nullptr;

                void* DETOUR_FUNCTION_DEF Detour_PhysicsWorldWrapperNewTick(uintptr_t p_this, float* p_delta_time, void* p_passthrough) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_resolved_addresses.m_physics_world_wrapper_address = p_this;
                    }
                    return RealPhysicsWorldWrapperNewTickCall(p_this, p_delta_time, p_passthrough);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x555D850;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_PhysicsWorldWrapperNewTick), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealPhysicsWorldWrapperNewTickCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealPhysicsWorldWrapperNewTickCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state);}
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace InternalSingleStepSimulation
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* InternalSingleStepSimulation_t)(uintptr_t p_this, float delta_time);
                InternalSingleStepSimulation_t RealInternalSingleStepSimulationCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_InternalSingleStepSimulation(uintptr_t p_this, float delta_time) noexcept
                {
                    bool debug_draw = false;
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_resolved_addresses.m_discrete_dynamics_world_instance_address = p_this;
                        debug_draw = GameDLLState::g_current_state.m_meta_data.m_update_debug_draw_stream;
                    }

                    RealInternalSingleStepSimulationCall(p_this, delta_time);

                    if (debug_draw)
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        StateManager::OnExecuteDebugDrawToStream();
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x634C7CC;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_InternalSingleStepSimulation), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealInternalSingleStepSimulationCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealInternalSingleStepSimulationCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state);}
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace DiscreteDynamicsWorldDestructor
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* DiscreteDynamicsWorldDestructor_t)(BulletTypes::DiscreteDynamicsWorld* p_this, char a2);
                DiscreteDynamicsWorldDestructor_t RealDiscreteDynamicsWorldDestructorCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_DiscreteDynamicsWorldDestructor(BulletTypes::DiscreteDynamicsWorld* p_this, char a2) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        StateManager::OnDestroyPhysicsWorld();
                    }
                    RealDiscreteDynamicsWorldDestructorCall(p_this, a2);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x555CAA0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_DiscreteDynamicsWorldDestructor), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealDiscreteDynamicsWorldDestructorCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealDiscreteDynamicsWorldDestructorCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state);}
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace ProcessCollisionPolicyGetShouldCollide
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                LPVOID g_real_function_address = nullptr;

                using ProcessCollisionPolicyGetShouldCollide_t = bool (DETOUR_FUNCTION_DEF*)(uintptr_t a1, BulletTypes::BroadphaseProxy*, BulletTypes::BroadphaseProxy*);
                ProcessCollisionPolicyGetShouldCollide_t RealProcessCollisionPolicyGetShouldCollideCall = nullptr;

                bool DETOUR_FUNCTION_DEF Detour_ProcessCollisionPolicyGetShouldCollide(uintptr_t a1, BulletTypes::BroadphaseProxy* proxy_0, BulletTypes::BroadphaseProxy* proxy_1) noexcept
                {
                    BulletTypes::CollisionObject* obj_0 = proxy_0->m_client_object;
                    BulletTypes::CollisionObject* obj_1 = proxy_1->m_client_object;

                    if (obj_0 && obj_1) 
                    {
                        if (obj_0->IsCustomHackedObject() || obj_1->IsCustomHackedObject())
                        {
                            return true;
                        }
                    }

                    return RealProcessCollisionPolicyGetShouldCollideCall(a1, proxy_0, proxy_1);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x555BB10;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_ProcessCollisionPolicyGetShouldCollide), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealProcessCollisionPolicyGetShouldCollideCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealProcessCollisionPolicyGetShouldCollideCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state);}
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        // Makes barrels random
        // NEVER CHANGE ANY OF THIS
        namespace BarrelPRNG
        {
            namespace
            {
                std::mutex g_mutex;
                std::mt19937 g_engine;
                bool g_seeded = false;
                constexpr uint32_t FIXED_SEED = 0; // NEVER CHANGE THIS
                constexpr float F_CONSTANT = 4294967296.0f;
                constexpr float D_CONSTANT = 4294967296.0;
                // Anti change protection
                static_assert(F_CONSTANT == 4294967296.0f);  
                static_assert(D_CONSTANT == 4294967296.0);
                static_assert(FIXED_SEED == 0);
            }

            void Reset() noexcept
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_engine.seed(FIXED_SEED);
                g_seeded = true;
            }

            float NextFloat01() noexcept
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (!g_seeded) { g_engine.seed(FIXED_SEED); g_seeded = true; }
                uint32_t word = g_engine();
                return static_cast<float>(word) / F_CONSTANT;
            }

            double NextDouble01() noexcept
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (!g_seeded) { g_engine.seed(FIXED_SEED); g_seeded = true; }
                uint32_t w0 = g_engine();
                uint32_t w1 = g_engine();
                double v6 = static_cast<double>(static_cast<int>(w0)) + static_cast<double>(static_cast<int>(w1)) * D_CONSTANT;
                return v6 / (D_CONSTANT * D_CONSTANT);
            }
        }

        namespace BarrelRandomLerp
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef float (DETOUR_FUNCTION_DEF* BarrelRandomLerp_t)(uintptr_t a1, float* a2);
                BarrelRandomLerp_t RealBarrelRandomLerpCall = nullptr;

                float DETOUR_FUNCTION_DEF Detour_BarrelRandomLerp(uintptr_t a1, float* a2) noexcept
                {
                    const float lo = a2[0];
                    const float hi = a2[1];
                    return BarrelPRNG::NextFloat01() * (hi - lo) + lo;
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x486A1D0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_BarrelRandomLerp), &g_real_function_address,
                    reinterpret_cast<LPVOID*>(&RealBarrelRandomLerpCall), g_hook_state);
            }
            bool RemoveHook() noexcept  { RealBarrelRandomLerpCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace BarrelRandomBool
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef bool (DETOUR_FUNCTION_DEF* BarrelRandomBool_t)(uintptr_t a1);
                BarrelRandomBool_t RealBarrelRandomBoolCall = nullptr;

                bool DETOUR_FUNCTION_DEF Detour_BarrelRandomBool(uintptr_t a1) noexcept
                {
                    return BarrelPRNG::NextDouble01() < 0.5;
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x486A120;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_BarrelRandomBool), &g_real_function_address,
                    reinterpret_cast<LPVOID*>(&RealBarrelRandomBoolCall), g_hook_state);
            }
            bool RemoveHook() noexcept { RealBarrelRandomBoolCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace SteeringValue 
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* SteeringValue_t)(void* p_this, float* p_value_ptr);
                SteeringValue_t RealSteeringValueCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_SteeringValue(void* p_this, float* p_value_ptr) noexcept
                {
                    {  
                        LOCK_CURRENT_STATE_MUTEX();

                        GameDLLState::g_current_state.m_resolved_addresses.m_steer_func_spoofed_rcx_arg = reinterpret_cast<uintptr_t>(p_this);
                        if (GameDLLState::g_replay_current_frame_inputs.has_value() 
                        && ! (GameDLLState::g_replay_current_frame_inputs.value().m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::STEER))
                        {
                            *p_value_ptr = GameDLLState::g_replay_current_frame_inputs->m_steer_value;
                        }
       
                        GameDLLState::g_current_state.m_replay_inputs.m_steer_value = *p_value_ptr;
                    }
                    RealSteeringValueCall(p_this, p_value_ptr);
                }
            }

            // This must not lock mutex such that we avoid a deadlock
            bool SpoofCallToSteerValueFunction(float steer) noexcept
            {
                if (GameDLLState::g_current_state.m_resolved_addresses.m_steer_func_spoofed_rcx_arg == NO_VALID_RESOLVED_ADDRESS)
                {
                    Utility::LogToFile("Could not spoof call to steer value function because rcx arg is not resolved yet");
                    return false;
                }

                if (!RealSteeringValueCall)
                {
                    Utility::LogToFile("Could not spoof call to steer value function because hook is not in place");
                    return false;
                }

                __try 
                {
                    RealSteeringValueCall(reinterpret_cast<void*>(GameDLLState::g_current_state.m_resolved_addresses.m_steer_func_spoofed_rcx_arg), &steer);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Utility::LogToFile("Could not spoof call to steer value function because of OS exception");
                    return false;
                }
                return true;
            }

            bool SetupHook() noexcept 
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x494C9D0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_SteeringValue), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealSteeringValueCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealSteeringValueCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace BrakeValue
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* BrakeStatus_t)(uintptr_t p_this, float* p_value_ptr);
                BrakeStatus_t RealBrakeValueCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_BrakeValue(uintptr_t p_this, float* p_value_ptr) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_resolved_addresses.m_brake_func_spoofed_rcx_arg = p_this;
                        // See offsets in steering non static dword CT
                        GameDLLState::g_current_state.m_resolved_addresses.m_steering_struct_base_address = p_this;

                        ///////////// First called hooked func that only runs for logic begins tick
                        StateManager::OnNewTick();

                        if (GameDLLState::g_replay_current_frame_inputs.has_value()
                        && ! (GameDLLState::g_replay_current_frame_inputs.value().m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::BRAKE))
                        {
                            *p_value_ptr = GameDLLState::g_replay_current_frame_inputs->m_brake_value;
                        }

                        GameDLLState::g_current_state.m_replay_inputs.m_brake_value = *p_value_ptr;
                    }
                    RealBrakeValueCall(p_this, p_value_ptr);
                }
            }

            // This must not lock mutex such that we avoid a deadlock
            bool SpoofCallToBrakeValueFunction(float brake) noexcept
            {
                if (GameDLLState::g_current_state.m_resolved_addresses.m_brake_func_spoofed_rcx_arg == NO_VALID_RESOLVED_ADDRESS)
                {
                    DLL_ERROR_LOG_FILE("Could not spoof call to brake value function because rcx arg is not resolved yet");
                    return false;
                }

                if (!RealBrakeValueCall)
                {
                    DLL_ERROR_LOG_FILE("Could not spoof call to brake value function because hook is not in place");
                    return false;
                }

                RealBrakeValueCall(GameDLLState::g_current_state.m_resolved_addresses.m_brake_func_spoofed_rcx_arg, &brake);
                return true;
            }

            bool SetupHook() noexcept 
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x494C9C0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_BrakeValue), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealBrakeValueCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealBrakeValueCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace AcceleratorValue
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* AcceleratorValue_t)(void* p_this, float* p_pass_through);
                AcceleratorValue_t RealAcceleratorValueCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_AcceleratorValue(void* p_this, float* p_pass_through) noexcept
                {
                    // Function modifies our value, so we must run it prior to modifying the value afterwards
                    RealAcceleratorValueCall(p_this, p_pass_through);   
                    float* accelerator_value = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(p_this) + 0x1D0);

                    {
                        LOCK_CURRENT_STATE_MUTEX();

                        if (GameDLLState::g_replay_current_frame_inputs.has_value()
                        && ! (GameDLLState::g_replay_current_frame_inputs.value().m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::ACCELERATOR))
                        {
                            *accelerator_value = GameDLLState::g_replay_current_frame_inputs->m_accelerator_value;
                        }

                        GameDLLState::g_current_state.m_replay_inputs.m_accelerator_value = *accelerator_value;
                    }  
                }
            }

            bool SetupHook() noexcept 
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x49514C0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_AcceleratorValue), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealAcceleratorValueCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealAcceleratorValueCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace EnableNitro 
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* NitroEnableFunction_t)(uintptr_t nitro_state_rcx);
                NitroEnableFunction_t RealNitroEnableCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_EnableNitro(uintptr_t nitro_state_rcx) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        if (GameDLLState::g_replay_current_frame_inputs.has_value())
                        {
                            // We prevent player from clicking nitro if we're in a replay frame
                            return;
                        }

                        GameDLLState::g_current_state.m_replay_inputs.m_nitro_activation_count++;
                    }
                    RealNitroEnableCall(nitro_state_rcx);
                }

            }

            // This must not lock mutex such that we avoid a deadlock
            void SpoofCallToEnableNitroFunction(uint32_t count) noexcept
            {
                if (RealNitroEnableCall == nullptr)
                {
                    Utility::LogToFile("Could not spoof call to enable nitro: hook is not in place.");
                    return;
                }
                if (GameDLLState::g_current_state.m_resolved_addresses.m_nitro_func_spoofed_rcx_arg == NO_VALID_RESOLVED_ADDRESS)
                {
                    Utility::LogToFile("Could not spoof call to enable nitro: verify global rcx arg pointer is not null.");
                    return;
                }
                while (count-- > 0)
                {
                    __try 
                    {
                        RealNitroEnableCall(GameDLLState::g_current_state.m_resolved_addresses.m_nitro_func_spoofed_rcx_arg);
                        GameDLLState::g_current_state.m_replay_inputs.m_nitro_activation_count++;
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        return;
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x4939870;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_EnableNitro), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealNitroEnableCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealNitroEnableCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace DecreaseNitroBarFunc
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* DecreaseNitroBar_t)(uintptr_t rcx, uintptr_t rdx_passthrough);
                DecreaseNitroBar_t RealDecreaseNitroBarCall = nullptr;

                void Detour_DecreaseNitroBar(uintptr_t rcx, uintptr_t rdx_passthrough) noexcept 
                {
                    float nitro_before {};
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_resolved_addresses.m_nitro_bar_encrypted_address = rcx + 0x18C;
                        nitro_before = GameDLLState::g_current_state.m_racer_state.m_nitro_bar_value;
                    }

                    RealDecreaseNitroBarCall(rcx, rdx_passthrough);

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        if (GameDLLState::g_current_state.m_racer_state.m_continuous_override_on_flags & ComDllIn::WriteRacerState::CONTINUOUS_OVERRIDE_NITRO_BAR)
                        {
                            UpdateNitroBar::WriteNitroBar(nitro_before);
                        }
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x49A1100;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, 
                    reinterpret_cast<LPVOID>(&Detour_DecreaseNitroBar), &g_real_function_address, reinterpret_cast<LPVOID*>(&RealDecreaseNitroBarCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealDecreaseNitroBarCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

    namespace IncreaseNitroBarFunc
    {
        namespace 
        {
            std::atomic<HookState> g_hook_state = HookState::NotInPlace;
            LPVOID g_real_function_address = nullptr;

            typedef void (DETOUR_FUNCTION_DEF* IncreaseNitroBar_t)(uintptr_t rcx, uintptr_t rdx, int r8d, float amount);
            IncreaseNitroBar_t RealIncreaseNitroBarCall = nullptr;

            void Detour_IncreaseNitroBar(uintptr_t rcx, uintptr_t rdx, int r8d, float amount) noexcept 
            {
                float nitro_before {};
                
                {
                    LOCK_CURRENT_STATE_MUTEX();
                    GameDLLState::g_current_state.m_resolved_addresses.m_nitro_bar_encrypted_address = rcx + 0x18C;
                    nitro_before = GameDLLState::g_current_state.m_racer_state.m_nitro_bar_value;
                }

                RealIncreaseNitroBarCall(rcx, rdx, r8d, amount);

                {
                    LOCK_CURRENT_STATE_MUTEX();
                    if (GameDLLState::g_current_state.m_racer_state.m_continuous_override_on_flags & ComDllIn::WriteRacerState::CONTINUOUS_OVERRIDE_NITRO_BAR)
                    {
                        UpdateNitroBar::WriteNitroBar(nitro_before);
                    }
                }
            }
        }

        bool SetupHook() noexcept
        {
            constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x49A0A10; 
            
            return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_IncreaseNitroBar), 
                &g_real_function_address, reinterpret_cast<LPVOID*>(&RealIncreaseNitroBarCall), g_hook_state);
        }

        bool RemoveHook() noexcept { RealIncreaseNitroBarCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
        bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
        bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
        HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
    }

        namespace UpdateNitroBar 
        {
            // This must not lock mutex such that we avoid a deadlock
            bool WriteNitroBar(float value) noexcept
            {
                const uintptr_t target_addr = GameDLLState::g_current_state.m_resolved_addresses.m_nitro_bar_encrypted_address;
                
                if (target_addr == NO_VALID_RESOLVED_ADDRESS)
                {
                    return false;
                }

                FloatXorObfuscationSetter::EncryptToAddress(reinterpret_cast<void*>(target_addr), value);
                
                return true;
            }

            // This must not lock mutex such that we avoid a deadlock
            float ReadNitroBar() noexcept
            {
                const uintptr_t target_addr = GameDLLState::g_current_state.m_resolved_addresses.m_nitro_bar_encrypted_address;

                if (target_addr == NO_VALID_RESOLVED_ADDRESS)
                {
                    return 0.0f;
                }

                return FloatXorObfuscationGetter::DecryptFromAddress(reinterpret_cast<void*>(target_addr));
            }
        }

        namespace LocalRacerAccessPoint
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* UpdateTransform_t)(uintptr_t transform_rcx, uintptr_t rdx, uintptr_t r8, uintptr_t r9);
                UpdateTransform_t RealUpdateTransformCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_LocalRacerAccessPoint(uintptr_t transform_rcx, uintptr_t rdx, uintptr_t r8, uintptr_t r9) noexcept
                {
                    RealUpdateTransformCall(transform_rcx, rdx, r8, r9);

                    {
                        LOCK_CURRENT_STATE_MUTEX();

                        const uintptr_t base = *reinterpret_cast<uintptr_t*>(transform_rcx);

                        if (GameDLLState::g_current_state.m_resolved_addresses.m_local_racer_base_address == NO_VALID_RESOLVED_ADDRESS 
                           || GameDLLState::g_current_state.m_resolved_addresses.m_local_racer_base_address != base)
                        {
                            return; // We're not in the local racer dynamics object
                        }
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x636FAA4; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_LocalRacerAccessPoint), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealUpdateTransformCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealUpdateTransformCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace GetLocalRacerStruct
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* RealGetLocalRacerStruct_t)(uintptr_t rcx);
                RealGetLocalRacerStruct_t RealGetLocalRacerStructCall = nullptr;

                void Detour_GetLocalRacerStruct(uintptr_t rcx) noexcept 
                {
                    RealGetLocalRacerStructCall(rcx);

                    uintptr_t rbx_container = *reinterpret_cast<uintptr_t*>(rcx + 0x1B8);

                    uintptr_t local_player_ptr = *reinterpret_cast<uintptr_t*>(rbx_container + 0x08);

                    constexpr uintptr_t offset_local_racer_rigidbody = 0x90; //Assumed constant; Original CT deduces this dynamically

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_resolved_addresses.m_local_racer_base_address = *reinterpret_cast<uintptr_t*>(local_player_ptr + offset_local_racer_rigidbody);
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x532CD0; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_GetLocalRacerStruct), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealGetLocalRacerStructCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealGetLocalRacerStructCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace CameraUpdate
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address      = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* CameraUpdate_t)(uintptr_t rcx);
                CameraUpdate_t RealCameraUpdateCall = nullptr;

                // movss [rcx+00000128],xmm0
                constexpr uint8_t g_update_fov_instruction_original_bytes[8] = {0xF3, 0x0F, 0x11, 0x81, 0x28, 0x01, 0x00, 0x00};
                constexpr uint8_t g_nops_8[8] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

                std::atomic<bool> g_memory_is_patched = false;

                void SetCameraRelativeToLocalRacerWithOffset(BulletTypes::UnalignedVector3 offsets, bool look_backwards = false)
                {
                    const float right   = offsets.x;
                    const float forward = offsets.y;
                    const float up      = offsets.z;

                    if (GameDLLState::g_current_state.m_resolved_addresses.m_steering_struct_base_address != NO_VALID_RESOLVED_ADDRESS)
                    {
                        // See offsets in steering CT
                        const uintptr_t steer_base = GameDLLState::g_current_state.m_resolved_addresses.m_steering_struct_base_address;

                        const uintptr_t pos_x                = steer_base + 0x1D38; // 0x7F8 from misslabeled "base"
                        const uintptr_t pos_y                = steer_base + 0x1D3C; // 0x7FC ^^
                        const uintptr_t terrain_height       = steer_base + 0x1D40; // 0x800 ^^
                        const uintptr_t height_above_terrain = steer_base + 0x1D58; // 0x818 ^^

                        const auto racer_trans = std::bit_cast<BulletTypes::Transform>(GameDLLState::g_current_state.m_racer_state.m_racer_transform_mat4x4);
                        BulletTypes::Quaternion rotation = Utility::RotationFromTransform(racer_trans);
                        
                        if (look_backwards)
                        {
                            const auto orig = rotation;
                            rotation.x = -orig.z;
                            rotation.y =  orig.w;
                            rotation.z =  orig.x;
                            rotation.w = -orig.y;
                        }

                        //TODO: Fix this - why must we inverse right?
                        BulletTypes::Vector3 world_offset = Utility::RotateVectorByQuaternion(rotation, { -1.0f * right, forward, up });

                        const float pos_x_val = *reinterpret_cast<float*>(pos_x);
                        const float pos_y_val = *reinterpret_cast<float*>(pos_y);
                        const float pos_z_val = *reinterpret_cast<float*>(terrain_height) + *reinterpret_cast<float*>(height_above_terrain);

                        GameDLLState::g_current_state.m_camera_state.m_camera_position_vec3 = 
                        {
                            pos_x_val + world_offset.x,
                            pos_y_val + world_offset.y,
                            pos_z_val + world_offset.z
                        };

                        GameDLLState::g_current_state.m_camera_state.m_camera_rotation_quat = 
                        {
                            rotation.x, 
                            rotation.y, 
                            rotation.z, 
                            rotation.w
                        };
                    }
                }

                void DETOUR_FUNCTION_DEF Detour_CameraUpdate(uintptr_t rcx) noexcept
                {
                    RealCameraUpdateCall(rcx);

                    const uintptr_t cam_actual_base   = *reinterpret_cast<uintptr_t*>(rcx + 0x28);
                    const uintptr_t cam_position_addr = cam_actual_base;

                    {
                        LOCK_CURRENT_STATE_MUTEX();

                        GameDLLState::g_current_state.m_resolved_addresses.m_camera_state_base_address = cam_position_addr;

                        ComDllOut::RecordedCameraState& cam = GameDLLState::g_current_state.m_camera_state;
                        const auto& override_flags = cam.m_continuous_override_on_flags;

                        if (override_flags & ComDllIn::WriteCameraState::CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR)
                        {
                            SetCameraRelativeToLocalRacerWithOffset(cam.m_offset_relative_to_car, cam.m_look_backwards);
                            std::memcpy(reinterpret_cast<uint8_t*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_POSITON_VEC3), 
                                cam.m_camera_position_vec3.Data(), sizeof(cam.m_camera_position_vec3));
                            std::memcpy(reinterpret_cast<uint8_t*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_ROTATION_QUAT), 
                                cam.m_camera_rotation_quat.Data(), sizeof(cam.m_camera_rotation_quat));
                        }

                        if (override_flags & ComDllIn::WriteCameraState::CONTINUOUS_OVERRIDE_POSITION)
                        {
                            std::memcpy(reinterpret_cast<uint8_t*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_POSITON_VEC3), 
                                cam.m_camera_position_vec3.Data(), sizeof(cam.m_camera_position_vec3));
                        }
                        else
                        {
                            std::memcpy(cam.m_camera_position_vec3.Data(), 
                                reinterpret_cast<uint8_t*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_POSITON_VEC3), sizeof(cam.m_camera_position_vec3));
                        }

                        if (override_flags & ComDllIn::WriteCameraState::CONTINUOUS_OVERRIDE_ROTATION)
                        {
                            std::memcpy(reinterpret_cast<uint8_t*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_ROTATION_QUAT), 
                                cam.m_camera_rotation_quat.Data(), sizeof(cam.m_camera_rotation_quat));
                        }
                        else
                        {
                            std::memcpy(cam.m_camera_rotation_quat.Data(), 
                                reinterpret_cast<uint8_t*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_ROTATION_QUAT), sizeof(cam.m_camera_rotation_quat));
                        }

                        if (override_flags & ComDllIn::WriteCameraState::CONTINUOUS_OVERRIDE_FOV_RAD)
                        {
                            *reinterpret_cast<float*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_FOV_RADIANS) = cam.m_fov_radians;
                        }
                        else
                        {
                            cam.m_fov_radians = *reinterpret_cast<float*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_FOV_RADIANS);
                        } 

                        cam.m_aspect_ratio = *reinterpret_cast<float*>(cam_position_addr + ComDllIn::WriteCameraState::OFFSET_ASPECT_RATIO);
                    }

                }
            }

            void PatchDisableGameFovWriteInstruction() noexcept
            {
                if (! g_memory_is_patched.load(std::memory_order::acquire))
                {
                    _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", 0x5DB61C, g_nops_8, sizeof(g_nops_8));
                    g_memory_is_patched.store(true, std::memory_order::release);
                }
            }

            void PatchEnableGameFovWriteInstruction() noexcept
            {
                if (g_memory_is_patched.load(std::memory_order::acquire))
                {
                    _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", 0x5DB61C, g_update_fov_instruction_original_bytes, sizeof(g_update_fov_instruction_original_bytes));
                    g_memory_is_patched.store(false, std::memory_order::release);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x5DB5C0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_CameraUpdate),
                                                  &g_real_function_address, reinterpret_cast<LPVOID*>(&RealCameraUpdateCall), g_hook_state);
            }

            bool RemoveHook()  noexcept { RealCameraUpdateCall = nullptr; PatchEnableGameFovWriteInstruction(); return _Implementation::RemoveHook (g_real_function_address, g_hook_state); }
            bool EnableHook()  noexcept { return _Implementation::EnableHook (g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            [[nodiscard]] HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace NativeQueueRacerRespawn
        {
        namespace
        {
            std::atomic<HookState> g_hook_state = HookState::NotInPlace;
            LPVOID g_real_function_address      = nullptr;

            using NativeQueueRacerRespawn_t = void (DETOUR_FUNCTION_DEF*)(uintptr_t a1, int a2, uintptr_t* a3, int* a4, uintptr_t* a5, uintptr_t* a6, uintptr_t* a7);
            NativeQueueRacerRespawn_t RealNativeQueueRacerRespawnCall = nullptr;

            std::atomic<bool> g_respawn_allowed = true;

            void DETOUR_FUNCTION_DEF Detour_NativeQueueRacerRespawn(uintptr_t a1, int a2, uintptr_t* a3, int* a4, uintptr_t* a5, uintptr_t* a6, uintptr_t* a7) 
            {
                if (GetRespawnIsAllowed())
                {
                    RealNativeQueueRacerRespawnCall(a1, a2, a3, a4, a5, a6, a7);
                }
            }
        }
            [[nodiscard]] bool GetRespawnIsAllowed() noexcept
            {
                return g_respawn_allowed.load(std::memory_order::relaxed);
            }

            void SetRespawnIsAllowed(bool on) noexcept
            {
                g_respawn_allowed.store(on, std::memory_order::relaxed);
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x493A290;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_NativeQueueRacerRespawn),
                                                  &g_real_function_address, reinterpret_cast<LPVOID*>(&RealNativeQueueRacerRespawnCall), g_hook_state);
            }

            bool RemoveHook()  noexcept { RealNativeQueueRacerRespawnCall = nullptr; return _Implementation::RemoveHook (g_real_function_address, g_hook_state); }
            bool EnableHook()  noexcept { return _Implementation::EnableHook (g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            [[nodiscard]] HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace AllowWreck
        {
        namespace
        {
            std::atomic<bool> g_wreck_allowed = true;
        }
            [[nodiscard]] bool GetWreckIsAllowed() noexcept
            {
                return g_wreck_allowed.load(std::memory_order::relaxed);
            }

            void SetWreckIsAllowed(bool on) noexcept
            {
                g_wreck_allowed.store(on, std::memory_order::relaxed);
                constexpr std::array<uint8_t, 2> orig_2  = {0x74, 0x6F}; //Asphalt9_Steam_x64_rtl.exe+4954749 - 74 6F - je Asphalt9_Steam_x64_rtl.exe+49547BA
                constexpr std::array<uint8_t, 2> patch_2 = {0xEB, 0x6F}; //Asphalt9_Steam_x64_rtl.exe+4954749 - EB 6F - jmp Asphalt9_Steam_x64_rtl.exe+49547BA
                if (on)
                {
                    _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", 0x4954749, orig_2.data(), orig_2.size());
                }
                else 
                {
                    _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", 0x4954749, patch_2.data(), patch_2.size());
                }
            }
        }

        namespace BarrelRollStabilization
        {
            namespace
            {
                inline std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                inline LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* BarrelRollStabilization_t)(uintptr_t rcx, float *rdx);
                inline BarrelRollStabilization_t RealBarrelRollStabilizationCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_BarrelRollStabilization(uintptr_t rcx, float *rdx) noexcept
                {
                    RealBarrelRollStabilizationCall(rcx, rdx);

                    // Deprecated: NO FORCE VALUES
                    /*float* rbx_plus_2228 = reinterpret_cast<float*>(rcx + 0x2228);
                    float* rbx_plus_222C = reinterpret_cast<float*>(rcx + 0x222C);

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        if (GameDLLState::g_replay_current_frame_inputs.has_value() &&
                         ! (GameDLLState::g_replay_current_frame_inputs.value().m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::BARREL_RBX))
                        {
                            if (*rbx_plus_2228 != GameDLLState::g_replay_current_frame_inputs->m_value_rbx_2228 || 
                                *rbx_plus_222C != GameDLLState::g_replay_current_frame_inputs->m_value_rbx_222C)
                            {
                                *rbx_plus_2228 = GameDLLState::g_replay_current_frame_inputs->m_value_rbx_2228;
                                *rbx_plus_222C = GameDLLState::g_replay_current_frame_inputs->m_value_rbx_222C;
                            }
                        }

                        GameDLLState::g_current_state.m_replay_inputs.m_value_rbx_2228 = *rbx_plus_2228;
                        GameDLLState::g_current_state.m_replay_inputs.m_value_rbx_222C = *rbx_plus_222C;
                    } */
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x49639D0; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_BarrelRollStabilization), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealBarrelRollStabilizationCall), g_hook_state
                );
            }

            bool EnableHook() noexcept { RealBarrelRollStabilizationCall = nullptr; return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            bool RemoveHook() noexcept { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace BarrelYawStabilization
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* BarrelYawStabilization_t)(uintptr_t rcx, uintptr_t rdx);
                BarrelYawStabilization_t RealBarrelYawStabilizationCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_BarrelYawStabilization(uintptr_t rcx, uintptr_t rdx) noexcept
                {
                    RealBarrelYawStabilizationCall(rcx, rdx);
                    
                    // Deprecated: NO FORCE VALUES
                    /*
                    const uintptr_t physics_obj_wrapper   = *reinterpret_cast<uintptr_t*>(rcx + 0x18);
                    const uintptr_t rigid_body = *reinterpret_cast<uintptr_t*>(physics_obj_wrapper + 0x90);
                    BulletTypes::Vector3* angular_velocity = reinterpret_cast<BulletTypes::Vector3*>(rigid_body + 0x170);
                    {
                       LOCK_CURRENT_STATE_MUTEX();
                        if (GameDLLState::g_replay_current_frame_inputs.has_value() &&
                         ! (GameDLLState::g_replay_current_frame_inputs.value().m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::BARREL_ANGULAR))
                        {
                            std::memcpy(angular_velocity, GameDLLState::g_replay_current_frame_inputs->m_barrel_angular_velocities_vec3.Data(), 
                                    sizeof(decltype(GameDLLState::g_replay_current_frame_inputs->m_barrel_angular_velocities_vec3)));

                        }
                        std::memcpy(GameDLLState::g_current_state.m_replay_inputs.m_barrel_angular_velocities_vec3.Data(), angular_velocity, 
                                    sizeof(decltype(GameDLLState::g_current_state.m_replay_inputs.m_barrel_angular_velocities_vec3))); 
                    }*/
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x49662B0; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_BarrelYawStabilization), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealBarrelYawStabilizationCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealBarrelYawStabilizationCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace FinalRacerTransformWriter
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using FinalRacerTransformWriter = char(*)(__m128** a1, int64_t* a2, int a3);
                FinalRacerTransformWriter RealFinalRacerTransformWriterCall = nullptr;


                char DETOUR_FUNCTION_DEF Detour_FinalRacerTransformWriter(__m128** a1, int64_t* a2, int a3)
                {
                    const char ret = RealFinalRacerTransformWriterCall(a1, a2, a3);

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        const auto base = GameDLLState::g_current_state.m_resolved_addresses.m_local_racer_base_address;

                        if (base == NO_VALID_RESOLVED_ADDRESS) 
                        {
                            return ret;
                        }

                        BulletTypes::UnalignedTransform* curr_trans = reinterpret_cast<BulletTypes::UnalignedTransform*>(base + ComDllIn::WriteRacerState::OFFSET_TRANSFORM);
                        BulletTypes::UnalignedVector3* curr_velo    = reinterpret_cast<BulletTypes::UnalignedVector3*>(base + ComDllIn::WriteRacerState::OFFSET_VELOCITY);

                        if (GameDLLState::g_replay_current_frame_inputs.has_value() 
                        && ! (GameDLLState::g_replay_current_frame_inputs->m_skip_override_flags & ComDllIn::DllReplayInputIn::SkipOverride::TRANSFORM_FORCED))
                        {
                            const auto inputs   = GameDLLState::g_replay_current_frame_inputs.value();

                            if (*curr_trans != inputs.m_racer_transform_mat4x4 || *curr_velo != inputs.m_racer_velocity_vec3)
                            {
                                std::memcpy(curr_trans->Data(), inputs.m_racer_transform_mat4x4.Data(), sizeof(inputs.m_racer_transform_mat4x4));
                                std::memcpy(curr_velo->Data(), inputs.m_racer_velocity_vec3.Data(), sizeof(inputs.m_racer_velocity_vec3));
                            }
                        }

                        // If continuous override, write the value from current state, else record the new value by game
                        // Transform
                        if (GameDLLState::g_current_state.m_racer_state.m_continuous_override_on_flags & ComDllIn::WriteRacerState::CONTINUOUS_OVERRIDE_TRANSFORM)
                        {
                            std::memcpy(curr_trans->Data(), GameDLLState::g_current_state.m_racer_state.m_racer_transform_mat4x4.Data(),
                                        sizeof(decltype(GameDLLState::g_current_state.m_racer_state.m_racer_transform_mat4x4)));
                        }
                        else
                        {
                            std::memcpy(GameDLLState::g_current_state.m_racer_state.m_racer_transform_mat4x4.Data(), curr_trans->Data(), 
                                        sizeof(decltype(GameDLLState::g_current_state.m_racer_state.m_racer_transform_mat4x4)));
                        }

                        // Velocity
                        if (GameDLLState::g_current_state.m_racer_state.m_continuous_override_on_flags & ComDllIn::WriteRacerState::CONTINUOUS_OVERRIDE_VELOCITY)
                        {
                            std::memcpy(curr_velo->Data(), GameDLLState::g_current_state.m_racer_state.m_racer_velocity_vec3.Data(),
                                        sizeof(decltype(GameDLLState::g_current_state.m_racer_state.m_racer_velocity_vec3)));
                        }
                        else 
                        {
                            std::memcpy(GameDLLState::g_current_state.m_racer_state.m_racer_velocity_vec3.Data(), curr_velo->Data(), 
                                    sizeof(decltype(GameDLLState::g_current_state.m_racer_state.m_racer_velocity_vec3)));
                        }
                    }

                    return ret;
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x636FDE0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_FinalRacerTransformWriter),
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealFinalRacerTransformWriterCall), g_hook_state);
            }
            bool RemoveHook() noexcept  { RealFinalRacerTransformWriterCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnWreckDeployBreakables
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                uint64_t g_wreck_call_index = 0;

                typedef void (DETOUR_FUNCTION_DEF* OnWreckDeployBreakables_t)(uintptr_t rcx);
                OnWreckDeployBreakables_t RealOnWreckDeployBreakablesCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_OnWreckDeployBreakables(uintptr_t rcx) noexcept
                {
                    RealOnWreckDeployBreakablesCall(rcx);
                    g_wreck_call_index++;

                    /*
                    ////////////////  DEBUG
                    if (!rcx) return;

                    float probability_threshold_2 = *reinterpret_cast<float*>(rcx + 0x54);
                    float breakage_threshold_1    = *reinterpret_cast<float*>(rcx + 0x58);

                    uintptr_t parts_start = *reinterpret_cast<uintptr_t*>(rcx + 0x38);
                    uintptr_t parts_end   = *reinterpret_cast<uintptr_t*>(rcx + 0x40);


                    if (parts_start == 0 || parts_end == 0 || parts_start >= parts_end) 
                    {
                        return;
                    }

                    size_t part_counter = 0;
                    std::cout << "========= [Wreck session: " << (g_wreck_call_index-1) << "] =========\n";
                    std::cout << "Will Break Threshhold: " << breakage_threshold_1 << "\nWill Detach Treshhold: " << probability_threshold_2 << "\n";
                    uint32_t broken_mask {};
                    uint32_t detach_mask {};
                    for (uintptr_t current_part = parts_start; current_part < parts_end; current_part += 0x60, part_counter++)
                    {
                        uint8_t byte_0x44    = *reinterpret_cast<uint8_t*>(current_part + 0x44); // IS DETACHED FLAG
                        uint8_t byte_0x45    = *reinterpret_cast<uint8_t*>(current_part + 0x45); // IS BROKEN FLAG
                        uint8_t byte_0x46    = *reinterpret_cast<uint8_t*>(current_part + 0x46); 
                        uintptr_t rigid_body = *reinterpret_cast<uintptr_t*>(current_part + 0x08);

                        std::cout << "PART: " << part_counter << " IS_BROKEN : " << static_cast<int>(byte_0x45) << " IS_DETACHED : " << static_cast<int>(byte_0x44) << std::endl;
                        broken_mask |= (byte_0x45 << part_counter);
                        detach_mask |= (byte_0x44 << part_counter); 
                    }
                    std::cout << "Broken Mask: " << std::hex << std::uppercase << broken_mask << "\nDetach Mask: " << detach_mask << "\n==========================" << std::dec << std::endl;

                    //////////// 
                    */
                }
            }

            std::uint64_t GetMonotonicWreckSessionCount() noexcept
            {
                return g_wreck_call_index;
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x2AC2B0; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnWreckDeployBreakables), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnWreckDeployBreakablesCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealOnWreckDeployBreakablesCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnRespawnButtonPressed
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using OnRespawnButtonPressed_t = uintptr_t(__fastcall*)(uint8_t* a1, uint64_t a2);
                OnRespawnButtonPressed_t RealOnRespawnButtonPressedCall = nullptr;  

                uintptr_t DETOUR_FUNCTION_DEF Detour_OnRespawnButtonPressed(uint8_t* a1, uint64_t a2)
                {
                    unsigned int v8 = *reinterpret_cast<int*>(a2 + 28);
                    if (v8 == 1 && !*reinterpret_cast<int*>(a2 + 32))
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_replay_inputs.m_respawn_button_press = true;
                    }
                    return RealOnRespawnButtonPressedCall(a1, a2);
                }
            }

            void SpoofCallToRespawnInputFunc() noexcept
            {
                if (RealOnRespawnButtonPressedCall == nullptr)
                {
                    Utility::LogToFile("Could not spoof call to respawn button: hook not in place.");
                    return;
                }
                if (GameDLLState::g_current_state.m_resolved_addresses.m_respawn_func_spoofed_rcx_arg == NO_VALID_RESOLVED_ADDRESS)
                {
                    Utility::LogToFile("Could not spoof call to respawn button: verify global rcx arg pointer is not null.");
                    return;
                }
                
                __try 
                {
                    constexpr uintptr_t OFFSET_EVENT_TRIGGER_FUNC = 0x250390;
                    using FnTriggerEvent = void(*)(uintptr_t);
                    FnTriggerEvent func  = reinterpret_cast<FnTriggerEvent>(GetMainModule() + OFFSET_EVENT_TRIGGER_FUNC);
                    func(GameDLLState::g_current_state.m_resolved_addresses.m_respawn_func_spoofed_rcx_arg);
                    GameDLLState::g_current_state.m_replay_inputs.m_respawn_button_press = true;
                } 
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Utility::LogToFile("Could not spoof call to respawn button: Event threw OS Exception");
                    return;
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x5B5BA0; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnRespawnButtonPressed), 
                                                &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnRespawnButtonPressedCall), g_hook_state);
            }
            bool RemoveHook() noexcept { RealOnRespawnButtonPressedCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept {  return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace GetPhysicsInterval
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef float* (DETOUR_FUNCTION_DEF* GetPhysicsInterval_t)(void* p_this, float* p_out);
                GetPhysicsInterval_t RealGetPhysicsIntervalCall = nullptr;

                float* DETOUR_FUNCTION_DEF Detour_GetPhysicsInterval(void* p_this, float* p_out) noexcept
                {
                    RealGetPhysicsIntervalCall(p_this, p_out);

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        
                        if (GameDLLState::g_current_state.m_meta_data.m_apply_physics_interval_override)
                        {
                            *p_out = GameDLLState::g_current_state.m_meta_data.m_physics_interval;
                        }
                        
                        GameDLLState::g_current_state.m_meta_data.m_physics_interval = *p_out;
                    }
                    return p_out;
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x494F240; 

                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_GetPhysicsInterval), 
                                                  &g_real_function_address, reinterpret_cast<LPVOID*>(&RealGetPhysicsIntervalCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealGetPhysicsIntervalCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnBeginRaceFunction
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* BeginRaceFunction_t)(void* p_this);
                BeginRaceFunction_t RealOnBeginRaceFunctionCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_OnBeginRaceFunction(void* p_this) noexcept
                {
                    RealOnBeginRaceFunctionCall(p_this);

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        StateManager::OnBeginRace();
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x855940;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnBeginRaceFunction), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnBeginRaceFunctionCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealOnBeginRaceFunctionCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnClickPlayFunction
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* OnClickPlayFunction_t)(uintptr_t rcx);
                OnClickPlayFunction_t RealOnClickPlayFunctionCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_OnClickPlayFunction(uintptr_t rcx) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_meta_data.m_race_status_state = ComDllOut::RaceStatusState::IN_LOADING_SCREEN;
                    }
                    RealOnClickPlayFunctionCall(rcx);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x6080A0;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnClickPlayFunction), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnClickPlayFunctionCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealOnClickPlayFunctionCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnEndRaceFunction
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef uintptr_t (DETOUR_FUNCTION_DEF* OnEndFunction_t)(void* p_this);
                OnEndFunction_t RealOnEndRaceFunctionCall = nullptr;

                uintptr_t DETOUR_FUNCTION_DEF Detour_OnEndRaceFunction(void* p_this) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        StateManager::OnEndRace();
                    }
                    return RealOnEndRaceFunctionCall(p_this);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x8A3580; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnEndRaceFunction), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnEndRaceFunctionCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealOnEndRaceFunctionCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnUpdateRaceProgress
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (DETOUR_FUNCTION_DEF* OnUpdateRaceProgress_t)(uintptr_t a1, float *a2, int64_t a3);
                OnUpdateRaceProgress_t RealOnUpdateRaceProgressCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_OnUpdateRaceProgress(uintptr_t a1, float *a2, int64_t a3) noexcept
                {
                    RealOnUpdateRaceProgressCall(a1, a2, a3);
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_racer_state.m_race_progress_percentage = *reinterpret_cast<float*>(a1 + 0x1D8) * 100.0f;
                    }
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x1259900; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnUpdateRaceProgress), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnUpdateRaceProgressCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealOnUpdateRaceProgressCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace OnUpdateCheckpoint
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef uintptr_t (DETOUR_FUNCTION_DEF* OnUpdateCheckpoint_t)(uintptr_t a1);
                OnUpdateCheckpoint_t RealOnUpdateCheckpointCall = nullptr;

                uintptr_t DETOUR_FUNCTION_DEF Detour_OnUpdateCheckpoint(uintptr_t a1) noexcept
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_racer_state.m_checkpoint = *reinterpret_cast<uint32_t*>(a1 + 0x24C);
                    }
                    return RealOnUpdateCheckpointCall(a1);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x4995730; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnUpdateCheckpoint), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnUpdateCheckpointCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealOnUpdateCheckpointCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace SegmentResolve
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef uintptr_t (DETOUR_FUNCTION_DEF* SegmentResolve_t)(uintptr_t a1, uintptr_t a2, float* a3, uintptr_t a4, uintptr_t a5, uintptr_t a6);
                SegmentResolve_t RealSegmentResolveCall = nullptr;

                uintptr_t DETOUR_FUNCTION_DEF Detour_SegmentResolve(uintptr_t a1, uintptr_t a2, float* a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) noexcept
                {
                    const uintptr_t ret = RealSegmentResolveCall(a1, a2, a3, a4, a5, a6);

                    struct ResolvedOut { uint64_t m_path; uint64_t m_segment; float m_mu; };
                    static_assert(offsetof(ResolvedOut, m_path) == 0);
                    static_assert(offsetof(ResolvedOut, m_segment) == 8);

                    const auto* out = reinterpret_cast<const ResolvedOut*>(a2);

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        auto& racer = GameDLLState::g_current_state.m_racer_state;
                        racer.m_path    = out->m_path;
                        racer.m_segment = out->m_segment;
                        racer.m_mu      = out->m_mu;
                    }

                    return ret;
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x5EAE170;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_SegmentResolve),
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealSegmentResolveCall), g_hook_state);
            }

            bool RemoveHook() noexcept  { RealSegmentResolveCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace FloatXorObfuscationSetter
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void* (DETOUR_FUNCTION_DEF* FloatXORSetter_t)(void* p_destination, float* p_source_value);
                FloatXORSetter_t RealFloatXORSetterCall = nullptr;

                void* DETOUR_FUNCTION_DEF Detour_FloatXORSetter(void* p_destination, float* p_source_value) noexcept
                {
                    return RealFloatXORSetterCall(p_destination, p_source_value);
                }
            } 

            // This must not lock mutex such that we avoid a deadlock
            void EncryptToAddress(void* p_dest, float value) noexcept 
            {
                RealFloatXORSetterCall(p_dest, &value); 
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x292420; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_FloatXORSetter), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealFloatXORSetterCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealFloatXORSetterCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace FloatXorObfuscationGetter
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef float (*FloatXORGetter_t)(void* p_destination);
                FloatXORGetter_t RealFloatXORGetterCall = nullptr;

                float DETOUR_FUNCTION_DEF Detour_FloarXORGetter(void* p_destination)
                {
                    return RealFloatXORGetterCall(p_destination);
                }
            } 

            // This must not lock mutex such that we avoid a deadlock
            float DecryptFromAddress(void* original_addr) noexcept 
            {
                return RealFloatXORGetterCall(original_addr);
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x2A3EF0; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_FloarXORGetter), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealFloatXORGetterCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealFloatXORGetterCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace PauseMenuLogic
        {
            namespace 
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using PauseMenuLogic_t = void (*)(uintptr_t a1);
                PauseMenuLogic_t RealPauseMenuLogicCall = nullptr;

                enum class CmdMode
                {
                    NONE, QUIT, RESTART
                };

                std::atomic<CmdMode> g_menu_forced_cmd = CmdMode::NONE;

                void DETOUR_FUNCTION_DEF Detour_PauseMenuLogic(uintptr_t a1)
                {
                    const CmdMode cmd = g_menu_forced_cmd.exchange(CmdMode::NONE, std::memory_order::acq_rel);
                    if (cmd == CmdMode::NONE)
                    {
                        RealPauseMenuLogicCall(a1);
                    }
                    else 
                    {
                        __try 
                        {
                            using TransitionFn = uintptr_t(__fastcall*)(uintptr_t, int*);
                            constexpr uintptr_t OFFSET_STATE_TRANSITION_FUNC = 0xA2F110;
                            
                            TransitionFn StateTransition = reinterpret_cast<TransitionFn>(GetMainModule() + OFFSET_STATE_TRANSITION_FUNC);

                            constexpr int RESTART_VAL = 4;
                            constexpr int QUIT_VAL    = 2;
                            
                            if (cmd == CmdMode::RESTART)
                            {
                                int arg = RESTART_VAL;
                                StateTransition(a1, &arg);
                            }
                            else if (cmd == CmdMode::QUIT)
                            {
                                int arg = QUIT_VAL;
                                StateTransition(a1, &arg);
                            }
                            else 
                            {
                                Utility::LogToFile("Pause Menu Logic - Unkown command mode: This shouldn't happen");
                            }
                        } 
                        __except(EXCEPTION_EXECUTE_HANDLER)
                        {
                            Utility::LogToFile("Pause Menu Logic - Could not restart race due to exception");
                        }
                    }
                }
            }

            void QueueRestart() noexcept
            {
                g_menu_forced_cmd.store(CmdMode::RESTART, std::memory_order::release);
            }

            void QueueQuit() noexcept
            {
                g_menu_forced_cmd.store(CmdMode::QUIT, std::memory_order::release);
            }

            void QueueNothing() noexcept
            {       
                g_menu_forced_cmd.store(CmdMode::NONE, std::memory_order::release);
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0xA27180;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_PauseMenuLogic),
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealPauseMenuLogicCall), g_hook_state);
            }

            bool RemoveHook() noexcept  { RealPauseMenuLogicCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace WorldShouldResetQuery
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using WorldShouldResetQuery_t = uintptr_t (*)(uintptr_t a1, uintptr_t* a2);
                WorldShouldResetQuery_t RealWorldShouldResetQueryCall = nullptr;

                std::atomic<bool> g_should_reset_flag = false;

                uintptr_t DETOUR_FUNCTION_DEF Detour_WorldShouldResetQuery(uintptr_t a1, uintptr_t* a2)
                {
                    bool is_in_race = false;
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        is_in_race = GameDLLState::g_current_state.m_meta_data.m_race_status_state == Communication::DllOut::RaceStatusState::IN_RACE;
                    }

                    bool patched_disable_camera_reset = false;

                    constexpr std::array<uint8_t, 3> orig_bytes = {0xFF, 0x50, 0x28}; //Same for both callsites
                    constexpr static uintptr_t STATIC_OFFSET_CAR_CONTROLLER    = 0x487C3C6;
                    constexpr static uintptr_t STATIC_OFFSET_CAMERA_CONTROLLER = 0x487D0C9;

                    constexpr std::array<uint8_t, 3> nop_3 = {0x90, 0x90, 0x90};

                    if (g_should_reset_flag.exchange(false, std::memory_order::acq_rel) && is_in_race)
                    {
                        uintptr_t v8 = *a2;

                        _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_CAR_CONTROLLER, nop_3.data(), sizeof(nop_3));
                        _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_CAMERA_CONTROLLER, nop_3.data(), sizeof(nop_3));

                        ProcessLevelResetFadePhase::SpoofCallToProcessLevelResetFadePhase(a1, &v8);
            
                        _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_CAR_CONTROLLER, orig_bytes.data(), sizeof(orig_bytes));
                        patched_disable_camera_reset = true;
                    }

                    const auto ret = RealWorldShouldResetQueryCall(a1, a2);

                    if (patched_disable_camera_reset)
                    {
                         // Different part of function uses this in RealWorldShouldResetQueryCall, therefore always reset patch after real func call
                        _Implementation::PatchMemory(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_CAMERA_CONTROLLER, orig_bytes.data(), sizeof(orig_bytes));
                    }

                    return ret;
                }
            }

            void QueueResetWorld() noexcept { g_should_reset_flag.store(true, std::memory_order::release); }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x487B800;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_WorldShouldResetQuery),
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealWorldShouldResetQueryCall), g_hook_state);
            }

            bool RemoveHook() noexcept  { RealWorldShouldResetQueryCall = nullptr; g_should_reset_flag.store(false); return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace ProcessLevelResetFadePhase
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using ProcessLevelResetFadePhase_t = uintptr_t (*)(uintptr_t a1, uintptr_t* a2);
                ProcessLevelResetFadePhase_t RealProcessLevelResetFadePhaseCall = nullptr;

                uintptr_t DETOUR_FUNCTION_DEF Detour_ProcessLevelResetFadePhase(uintptr_t a1, uintptr_t* a2)
                {
                    return RealProcessLevelResetFadePhaseCall(a1, a2);
                }
            }

            void SpoofCallToProcessLevelResetFadePhase(uintptr_t a1, uintptr_t* a2) noexcept
            {
                if (!a2 || !RealProcessLevelResetFadePhaseCall) 
                {
                    Utility::LogToFile("Can not spoof call to level reset without func pointer or invalid a2*");
                    return;
                }
                __try 
                {
                    RealProcessLevelResetFadePhaseCall(a1, a2);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    Utility::LogToFile("Exception at reset level phade phase");
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x64F780;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_ProcessLevelResetFadePhase),
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealProcessLevelResetFadePhaseCall), g_hook_state);
            }

            bool RemoveHook() noexcept  { RealProcessLevelResetFadePhaseCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace RenderGUIToggle
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef uint32_t (*RenderGUIToggle_t)();
                RenderGUIToggle_t RealRenderGUIToggleCall = nullptr;

                uint32_t DETOUR_FUNCTION_DEF Detour_RenderGUIToggle()
                {
                    LOCK_CURRENT_STATE_MUTEX();
                    return GameDLLState::g_current_state.m_meta_data.m_gui_is_hidden ? 0 : 1;
                }
            } 

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x4B16D20; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_RenderGUIToggle), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealRenderGUIToggleCall), g_hook_state
                );
            }

            bool RemoveHook() noexcept { RealRenderGUIToggleCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace AnimationProgressFunction
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (*AnimationProgressFunction_t)(uintptr_t rcx, int64_t* rdx);
                AnimationProgressFunction_t RealAnimationProgressFunctionCall = nullptr;

                constexpr std::array<std::pair<Communication::SkipAnimationFlags, int64_t>, 5> banned_durations {
                    std::pair<Communication::SkipAnimationFlags, int64_t>{Communication::SkipAnimationFlags::SKIP_RACE_INTRO,        8217000},
                    std::pair<Communication::SkipAnimationFlags, int64_t>{Communication::SkipAnimationFlags::SKIP_RACE_INTRO,        1945000},
                    std::pair<Communication::SkipAnimationFlags, int64_t>{Communication::SkipAnimationFlags::SKIP_RACE_INTRO,        5500000},
                    std::pair<Communication::SkipAnimationFlags, int64_t>{Communication::SkipAnimationFlags::SKIP_RACE_INTRO,        3366666},
                    std::pair<Communication::SkipAnimationFlags, int64_t>{Communication::SkipAnimationFlags::SKIP_RACE_COUNT_DOWN,   2000000}
                };

                void DETOUR_FUNCTION_DEF Detour_AnimationProgressFunction(uintptr_t rcx, int64_t* rdx)
                {
                    const uintptr_t metadata_ptr  = *reinterpret_cast<uintptr_t*>(rcx + 8);
                    const int32_t internal_offset = *reinterpret_cast<int32_t*>(metadata_ptr + 4);
                    
                    const uintptr_t adjusted_rcx = rcx + internal_offset + 8;

                    const uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(adjusted_rcx);

                    using GetDuration_t = void(__fastcall*)(uintptr_t, int64_t*);
                    GetDuration_t GetDuration = reinterpret_cast<GetDuration_t>(vtable[12]);

                    int64_t duration = 0;
                    GetDuration(adjusted_rcx, &duration);

                    bool is_in_race = false;
                    uint32_t flags  = 0;

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        is_in_race = GameDLLState::g_current_state.m_meta_data.m_race_status_state == ComDllOut::RaceStatusState::IN_RACE;
                        flags = std::to_underlying(GameDLLState::g_current_state.m_meta_data.m_skip_animation_flags);

                        // If we previously were in loading screen and now match an animation duration for in race or countdown, we assume we're in pre race cinematic
                        if (GameDLLState::g_current_state.m_meta_data.m_race_status_state == ComDllOut::RaceStatusState::IN_LOADING_SCREEN)
                        {
                            for (const auto& it : banned_durations)
                            {
                                if ( (std::to_underlying(it.first) & std::to_underlying(Communication::SkipAnimationFlags::SKIP_RACE_INTRO) || 
                                      std::to_underlying(it.first) & std::to_underlying(Communication::SkipAnimationFlags::SKIP_RACE_COUNT_DOWN)) &&
                                      duration == it.second)
                                {
                                    GameDLLState::g_current_state.m_meta_data.m_race_status_state = ComDllOut::RaceStatusState::IN_PRE_RACE_CINEMATIC;
                                }
                            }
                        }
                    }

                    //////////////////// Change this when there is animation skips inisde of race
                    if (! is_in_race)
                    {
                        for (const auto& it : banned_durations)
                        {
                            if ((flags & std::to_underlying(it.first)) && duration == it.second)
                            {
                                *rdx = duration;
                                break; 
                            }
                        }
                    }

                    RealAnimationProgressFunctionCall(rcx, rdx);
                }
            } 

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x2F37B40; 
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_AnimationProgressFunction), 
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealAnimationProgressFunctionCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealAnimationProgressFunctionCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace SpeedUpUIAnimations
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using SpeedUpUIAnimations_t = uintptr_t (*)(uintptr_t a1, uint64_t ui_dt, uint64_t a3);
                SpeedUpUIAnimations_t RealSpeedUpUIAnimationsCall = nullptr;

                uintptr_t DETOUR_FUNCTION_DEF Detour_SpeedUpUIAnimations(uintptr_t a1, uint64_t ui_dt, uint64_t a3)
                {
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        if (GameDLLState::g_current_state.m_meta_data.m_speed_up_gui_animations)
                        {
                            if (GameDLLState::g_current_state.m_meta_data.m_race_status_state == Communication::DllOut::RaceStatusState::IN_RACE)
                            {
                                if (IsPaused::GetIsPaused())
                                {
                                    ui_dt *= 8;
                                }
                            }
                            else 
                            {
                                ui_dt *= 8;
                            }
                        }
                    }
                    return RealSpeedUpUIAnimationsCall(a1, ui_dt, a3);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x255290;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_SpeedUpUIAnimations),
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealSpeedUpUIAnimationsCall), g_hook_state);
            }

            bool RemoveHook() noexcept  { RealSpeedUpUIAnimationsCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace UpdateCursorVisibility
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                using UpdateCursorVisibility_t = void (*)(uint64_t a1, bool a2);
                UpdateCursorVisibility_t RealUpdateCursorVisibilityCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_UpdateCursorVisibility(uint64_t a1, bool a2)
                {
                    a2 = true;
                    RealUpdateCursorVisibilityCall(a1, a2);
                }
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x5E45780;
                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_UpdateCursorVisibility),
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealUpdateCursorVisibilityCall), g_hook_state);
            }

            bool RemoveHook() noexcept  { RealUpdateCursorVisibilityCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace IsPaused
        {
            bool GetIsPaused() noexcept
            {
                if (GameDLLState::g_current_state.m_resolved_addresses.m_is_paused_func_spoofed_rcx_arg == NO_VALID_RESOLVED_ADDRESS)
                {
                    return false;
                }
                using Fn = uint64_t(*)(uintptr_t rcx);
                Fn func = reinterpret_cast<Fn>(GetMainModule() + 0x4BACA0);

                return static_cast<bool>(func(GameDLLState::g_current_state.m_resolved_addresses.m_is_paused_func_spoofed_rcx_arg));
            }
        }

        namespace XInput_GetState
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                LPVOID g_real_function_address = nullptr;

                typedef DWORD (DETOUR_FUNCTION_DEF* XInputGetState_t)(DWORD user_index, XINPUT_STATE* state);
                XInputGetState_t RealXInputGetStateCall = nullptr;

                DWORD DETOUR_FUNCTION_DEF Detour_XInputGetState(DWORD user_index, XINPUT_STATE* state) noexcept
                {
                    if (!RealXInputGetStateCall) return ERROR_ASSERTION_FAILURE;

                    DWORD result = RealXInputGetStateCall(user_index, state);

                    if (result == ERROR_SUCCESS && state)
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        GameDLLState::g_current_state.m_xinput_state = ComDllOut::XInputState 
                        {
                            .m_packet_id     = state->dwPacketNumber,
                            .m_buttons       = state->Gamepad.wButtons,
                            .m_left_trigger  = state->Gamepad.bLeftTrigger,
                            .m_right_trigger = state->Gamepad.bRightTrigger,
                            .m_thumb_lx      = state->Gamepad.sThumbLX,
                            .m_thumb_ly      = state->Gamepad.sThumbLY,
                            .m_thumb_rx      = state->Gamepad.sThumbRX,
                            .m_thumb_ry      = state->Gamepad.sThumbRY
                        };
                    }
                    return result;
                }
            }

            bool SetupHook() noexcept
            {
                return _Implementation::SetupHook(L"xinput1_4.dll", "XInputGetState", reinterpret_cast<LPVOID>(&Detour_XInputGetState), &g_real_function_address, 
                    reinterpret_cast<LPVOID*>(&RealXInputGetStateCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealXInputGetStateCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state);}
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire);}
        }

        namespace UcrtBaseRand
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;

                LPVOID g_real_function_address = nullptr;

                typedef int (DETOUR_FUNCTION_DEF* UcrtBaseRand_t)();
                UcrtBaseRand_t RealUcrtBaseRandCall = nullptr;

                int DETOUR_FUNCTION_DEF Detour_UcrtBaseRand() noexcept
                {
                    ////////// TLS reset makes rand deterministic per race for important logic
                    const int natural_rand = RealUcrtBaseRandCall();
                    return natural_rand; 
                    //////////
                }
            }

            bool SetupHook() noexcept
            {
                return _Implementation::SetupHook(L"ucrtbase.dll", "rand", reinterpret_cast<LPVOID>(&Detour_UcrtBaseRand), &g_real_function_address, 
                    reinterpret_cast<LPVOID*>(&RealUcrtBaseRandCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealUcrtBaseRandCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state);}
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire);}
        }

        namespace BVHBroadphaseTraversal
        {
            namespace
            {
                std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                LPVOID g_real_function_address = nullptr;

                typedef void (__fastcall *BVHTraverse_t)(uintptr_t ctx_bvh, uintptr_t root_node, float* ray_origin, uintptr_t unused_a4, float* inv_ray_dir,
                                                        int* axis_signs, float max_distance, float* offset_min, float* offset_max, uintptr_t* callback_ctx );

                BVHTraverse_t RealBVHTraverseCall = nullptr;

                void DETOUR_FUNCTION_DEF Detour_BVHTraverse(uintptr_t ctx_bvh, uintptr_t root_node, float* ray_origin, uintptr_t unused_a4, float* inv_ray_dir, 
                                                            int* axis_signs, float max_distance, float* offset_min, float* offset_max, uintptr_t* callback_ctx) noexcept
                {
                    const uintptr_t ret_address = std::bit_cast<uintptr_t>(_ReturnAddress());
                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        if (GetMainModule() + 0x635EBF0 == ret_address)
                        {
                            GameDLLState::g_current_state.m_resolved_addresses.m_bvh_root_node_static_objects = root_node;
                        }
                        else if (GetMainModule() + 0x635EBAE == ret_address) 
                        {
                            GameDLLState::g_current_state.m_resolved_addresses.m_bvh_root_node_dynamic_objects = root_node;
                        }
                    }
                    RealBVHTraverseCall(ctx_bvh, root_node, ray_origin, unused_a4, inv_ray_dir, axis_signs, max_distance, offset_min, offset_max, callback_ctx);
                }

                std::vector<DumpedNode> _DumpAllLeaves(void* root_node, bool from_normal_tree) noexcept
                {
                    if (!root_node) return {};
                    std::vector<void*> stack;
                    std::vector<DumpedNode> out;
                    stack.push_back(root_node);

                    while (!stack.empty())
                    {
                        auto* node = reinterpret_cast<uint8_t*>(stack.back());
                        stack.pop_back();

                        float* min = reinterpret_cast<float*>(node + 0x00);
                        float* max = reinterpret_cast<float*>(node + 0x10);
                        int64_t child0 = *reinterpret_cast<int64_t*>(node + 0x28);
                        int64_t child1 = *reinterpret_cast<int64_t*>(node + 0x30);

                        if (child1 != 0)
                        {
                            stack.push_back(reinterpret_cast<void*>(child0));
                            stack.push_back(reinterpret_cast<void*>(child1));
                        }
                        else
                        {
                            out.push_back({ reinterpret_cast<decltype(DumpedNode::m_broadphase_proxy)>(child0),
                                            {min[0],min[1],min[2]},
                                            {max[0],max[1],max[2]}, from_normal_tree});

                        }
                    }
                    return out;
                }
            }

            // Does not lock - caller to lock
            std::vector<DumpedNode> DumpAllLeaves() noexcept 
            {
                void* root = nullptr;
                constexpr bool FROM_STATIC_TREE = true;
                {
                    root = std::bit_cast<void*>(GameDLLState::g_current_state.m_resolved_addresses.m_bvh_root_node_static_objects);
                }
                if (root == nullptr) return {};
                std::vector<DumpedNode> v1 = _DumpAllLeaves(root, FROM_STATIC_TREE);
                {
                    root = std::bit_cast<void*>(GameDLLState::g_current_state.m_resolved_addresses.m_bvh_root_node_dynamic_objects);
                }
                if (root == nullptr) return v1;
                std::vector<DumpedNode> v2 = _DumpAllLeaves(root, !FROM_STATIC_TREE);
                v1.insert(v1.end(), v2.begin(), v2.end());
                return v1;
            }

            bool SetupHook() noexcept
            {
                constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x635DC18; 

                return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_BVHTraverse),
                    &g_real_function_address, reinterpret_cast<LPVOID*>(&RealBVHTraverseCall), g_hook_state);
            }

            bool RemoveHook() noexcept { RealBVHTraverseCall = nullptr; return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
            bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
            bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
            HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
        }

        namespace Experimental 
        {
            namespace PacingState
            {
                inline std::atomic<bool> g_should_render = true;
                inline int64_t g_last_time_micros = 0;
                inline int64_t g_accumulator_micros = 0;

                inline void SetShouldRender(bool render) noexcept
                {
                    g_should_render.store(render, std::memory_order_relaxed);
                }
                inline bool GetShouldRender() noexcept
                {
                    return g_should_render.load(std::memory_order_relaxed);
                }
            }

            namespace QueryPerformanceCounterHook
            {
                namespace
                {
                    std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                    LPVOID g_real_function_address = nullptr;

                    typedef BOOL(WINAPI* QueryPerformanceCounter_t)(LARGE_INTEGER* value);
                    QueryPerformanceCounter_t RealQueryPerformanceCounterCall = nullptr;

                    LARGE_INTEGER g_virtual_qpc{ 0 };
                    LARGE_INTEGER g_qpc_frequency{ 0 };
                    bool g_initialized = false;
                    std::mutex g_time_mutex;

                    BOOL WINAPI Detour_QueryPerformanceCounter(LARGE_INTEGER* value) noexcept
                    {
                        if (!value) return FALSE;

                        std::lock_guard<std::mutex> lock(g_time_mutex);

                        if (!g_initialized)
                        {
                            return RealQueryPerformanceCounterCall(value);
                        }

                        *value = g_virtual_qpc;
                        return TRUE;
                    }
                }

                void Initialize() noexcept
                {
                    if (!g_initialized && RealQueryPerformanceCounterCall)
                    {
                        QueryPerformanceFrequency(&g_qpc_frequency);
                        RealQueryPerformanceCounterCall(&g_virtual_qpc);
                        g_initialized = true;
                    }
                }

                void AdvanceVirtualTime(int64_t micros) noexcept
                {
                    std::lock_guard<std::mutex> lock(g_time_mutex);
                    if (!g_initialized) return;
                    LONGLONG ticks_to_add = (micros * g_qpc_frequency.QuadPart) / 1000000LL;
                    g_virtual_qpc.QuadPart += ticks_to_add;
                }

                void SyncToRealTime() noexcept
                {
                    std::lock_guard<std::mutex> lock(g_time_mutex);
                    if (g_initialized && RealQueryPerformanceCounterCall)
                    {
                        RealQueryPerformanceCounterCall(&g_virtual_qpc);
                    }
                }

                int64_t GetRealTimeMicros() noexcept
                {
                    if (!g_initialized || !RealQueryPerformanceCounterCall) return 0;
                    
                    LARGE_INTEGER counter;
                    RealQueryPerformanceCounterCall(&counter);
                    return 1000000LL * (counter.QuadPart / g_qpc_frequency.QuadPart) +
                        1000000LL * (counter.QuadPart % g_qpc_frequency.QuadPart) / g_qpc_frequency.QuadPart;
                }

                bool SetupHook() noexcept
                {
                    return _Implementation::SetupHook(L"KernelBase.dll", "QueryPerformanceCounter", reinterpret_cast<LPVOID>(&Detour_QueryPerformanceCounter), &g_real_function_address,
                        reinterpret_cast<LPVOID*>(&RealQueryPerformanceCounterCall), g_hook_state);
                }
                bool RemoveHook() noexcept { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
                bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
                bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
                HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
            }

            namespace InitiateNewFrame
            {
                namespace
                {
                    std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                    LPVOID g_real_function_address = nullptr;

                    using InitiateNewFrame_t = uintptr_t (*)(uintptr_t a1, uint64_t a2, uint64_t a3);
                    InitiateNewFrame_t RealInitiateNewFrameCall = nullptr;

                    uintptr_t DETOUR_FUNCTION_DEF Detour_InitiateNewFrame(uintptr_t a1, uint64_t a2, uint64_t a3)
                    {
                        uint8_t* const p_skip_rendering = reinterpret_cast<uint8_t*>(a1 + 0x1D0);
                        
                        bool should_render = PacingState::GetShouldRender();

                        *p_skip_rendering = !should_render;

                        return RealInitiateNewFrameCall(a1, a2, a3);
                    }
                }

                bool SetupHook() noexcept
                {
                    constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x238E50;
                    return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_InitiateNewFrame),
                          &g_real_function_address, reinterpret_cast<LPVOID*>(&RealInitiateNewFrameCall), g_hook_state);
                }

                bool RemoveHook() noexcept  { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
                bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
                bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
                HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
            }

            namespace NewFrameSubscriberList
            {
                namespace 
                {
                    std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                    LPVOID g_real_function_address = nullptr;

                    typedef uintptr_t (DETOUR_FUNCTION_DEF* NewFrameSubscriberList_t)(uintptr_t a1);
                    NewFrameSubscriberList_t RealNewFrameSubscriberListCall = nullptr;

                    uintptr_t DETOUR_FUNCTION_DEF Detour_NewFrameSubscriberList(uintptr_t a1) noexcept
                    {
                        const uintptr_t expected_ret = GetMainModule() + 0x5E43308;
                        const uintptr_t ret = std::bit_cast<uintptr_t>(_ReturnAddress());

                        if (ret != expected_ret)
                        {
                            return RealNewFrameSubscriberListCall(a1);
                        }

                        //QueryPerformanceCounterHook::Initialize();
                        //QueryPerformanceCounterHook::AdvanceVirtualTime(16333);

                        //PacingState::SetShouldRender(true);
                        return RealNewFrameSubscriberListCall(a1);
                    }
                }

                bool SetupHook() noexcept
                {
                    constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x250390; 
                    return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_NewFrameSubscriberList), 
                          &g_real_function_address, reinterpret_cast<LPVOID*>(&RealNewFrameSubscriberListCall), g_hook_state);
                }

                bool RemoveHook() noexcept  { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
                bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
                bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
                HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
            }

            namespace MainFpsLimiter
            {
                namespace 
                {
                    std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                    LPVOID g_real_function_address = nullptr;

                    typedef void (DETOUR_FUNCTION_DEF* MainFpsLimiter_t)(uintptr_t a1);
                    MainFpsLimiter_t RealMainFpsLimiterCall = nullptr;

                    void DETOUR_FUNCTION_DEF Detour_MainFpsLimiter(uintptr_t a1) noexcept
                    {
                        RealMainFpsLimiterCall(a1);
                    }
                }

                bool SetupHook() noexcept
                {
                    constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x5E44F90; 
                    return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_MainFpsLimiter), 
                          &g_real_function_address, reinterpret_cast<LPVOID*>(&RealMainFpsLimiterCall), g_hook_state);
                }

                bool RemoveHook() noexcept  { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
                bool EnableHook() noexcept  { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
                bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
                HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
            }

            namespace OnRaycastVehicleUpdate
            {
                namespace
                {
                    std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                    LPVOID g_real_function_address = nullptr;

                    typedef uint64_t* (*OnRaycastVehicleUpdate_t)(uint64_t a1, float *a2, uint64_t a3);
                    OnRaycastVehicleUpdate_t RealOnRaycastVehicleUpdateCall = nullptr;

                    uint64_t* DETOUR_FUNCTION_DEF Detour_OnRaycastVehicleUpdate(uint64_t a1, float *a2, uint64_t a3) noexcept
                    {
                        const uint64_t addr_ptr_to_array = a1 + 0x228;
                        const uint64_t* wheel_array_4 = *reinterpret_cast<uint64_t**>(addr_ptr_to_array);

                        constexpr uint64_t offset_direction_vector = 0x14;

                        //*reinterpret_cast<float*>(static_cast<uintptr_t>(wheel_array_4[0]) + offset_direction_vector + 2*sizeof(float)) = -0.05f;
                        //*reinterpret_cast<float*>(static_cast<uintptr_t>(wheel_array_4[1]) + offset_direction_vector + 2*sizeof(float)) = -0.05f;
                        //*reinterpret_cast<float*>(static_cast<uintptr_t>(wheel_array_4[2]) + offset_direction_vector + 2*sizeof(float)) = -0.3f;
                        //*reinterpret_cast<float*>(static_cast<uintptr_t>(wheel_array_4[3]) + offset_direction_vector + 2*sizeof(float)) = -0.3f;

                        //std::cout << "Wheels: " << std::hex << std::uppercase << wheel_array_4[0] << ", " << wheel_array_4[1] << ", " 
                        //<< wheel_array_4[2] << ", " << wheel_array_4[3] << std::dec << std::endl;
                        
                        return RealOnRaycastVehicleUpdateCall(a1, a2, a3);
                    }
                } 

                bool SetupHook() noexcept
                {
                    constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x49543D0; 
                    return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_OnRaycastVehicleUpdate), 
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealOnRaycastVehicleUpdateCall), g_hook_state
                    );
                }

                bool RemoveHook() noexcept { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
                bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
                bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
                HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
            }

            namespace PhysicsWorldRaycast
            {
                namespace 
                {
                    std::atomic<HookState> g_hook_state = HookState::NotInPlace;
                    LPVOID g_real_function_address = nullptr;

                    using PhysicsWorldRaycast_t = uint64_t(__fastcall*)(void* world, BulletTypes::RaycastOutput* output, float* start, 
                                                                        float* end, int16_t layer_mask, int16_t query_flags, int64_t* entity_filter);

                    PhysicsWorldRaycast_t RealPhysicsWorldRaycastCall = nullptr;

                    uint64_t DETOUR_FUNCTION_DEF Detour_PhysicsWorldRaycast(void* world, BulletTypes::RaycastOutput* output, float* start, 
                                                                        float* end, int16_t layer_mask, int16_t query_flags, int64_t* entity_filter) noexcept
                    {
                        return RealPhysicsWorldRaycastCall(world, output, start, end, layer_mask, query_flags, entity_filter);
                    }
                }

                BulletTypes::RaycastOutput SpoofCallToCastRay(BulletTypes::Vector3 start, BulletTypes::Vector3 end, uint16_t layer_mask, uint16_t query_flags) noexcept
                {
                    BulletTypes::RaycastOutput result{};

                    void* world = nullptr;

                    {
                        LOCK_CURRENT_STATE_MUTEX();
                        world = std::bit_cast<void*>(GameDLLState::g_current_state.m_resolved_addresses.m_physics_world_wrapper_address);
                    }

                    if (!world || !RealPhysicsWorldRaycastCall) return result;

                    int64_t no_filter_dummy = 0;

                    static_assert(sizeof(start) == 16 && sizeof(end) == 16, "Must never not be 16-aligned");

                    RealPhysicsWorldRaycastCall(world, &result, start.Data(), end.Data(), static_cast<int16_t>(layer_mask), static_cast<int16_t>(query_flags), &no_filter_dummy);

                    return result;
                }

                bool SetupHook() noexcept
                {
                    constexpr uintptr_t STATIC_OFFSET_ABI_47_1_0 = 0x555DBB0; 
                    return _Implementation::SetupHook(L"Asphalt9_Steam_x64_rtl.exe", STATIC_OFFSET_ABI_47_1_0, reinterpret_cast<LPVOID>(&Detour_PhysicsWorldRaycast), 
                        &g_real_function_address, reinterpret_cast<LPVOID*>(&RealPhysicsWorldRaycastCall), g_hook_state
                    );
                }

                bool RemoveHook() noexcept { return _Implementation::RemoveHook(g_real_function_address, g_hook_state); }
                bool EnableHook() noexcept { return _Implementation::EnableHook(g_real_function_address, g_hook_state); }
                bool DisableHook() noexcept { return _Implementation::DisableHook(g_real_function_address, g_hook_state); }
                HookState GetHookState() noexcept { return g_hook_state.load(std::memory_order::acquire); }
            }
        }
    }
}