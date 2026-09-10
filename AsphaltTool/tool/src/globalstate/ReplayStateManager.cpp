#include "globalstate/ReplayStateManager.h"
#include "AsphaltDllManager.h"
#include "Communication.h"
#include "common/Replay.h"

#include "core/utility/Assert.h"
#include "globalstate/AsphaltDllManager.h"

#include <cstdint>
#include <vector>

#include <optional>
#include <vector>

namespace AsphaltTas::ReplayStateManager
{
    namespace 
    {
        std::vector<Replay> g_recorded_replays;
        Replay g_current_recording;
        
        std::optional<PlaybackSession> g_queued_playback;

        void FinalizeRecording() noexcept
        {
            if (g_current_recording.GetAmountFrames() > 0) 
            {
                g_recorded_replays.push_back(std::move(g_current_recording));
            }
            g_current_recording = Replay();
        }

        void EnableReplayModeActiveBlock(ComDllIn::DllGeneralCommandsIn& general_cmd) noexcept
        {
            // An active replay is to be replayed, rig dll to expect inputs & set frame interval of replay
            general_cmd.m_write_meta_data.m_replay_mode = Communication::ReplayMode::ActiveBlockThread;
        }

        void DisableReplayModeActiveBlock(ComDllIn::DllGeneralCommandsIn& general_cmd) noexcept
        {
            general_cmd.m_write_meta_data.m_replay_mode = Communication::ReplayMode::Inactive;
        }

        void PushPlaybackFramesToInputInBuffer() noexcept
        {
            ComSharedMem::SharedState* shared_state = ComSharedMem::GetSharedState();
            while (const std::optional<Replay::Frame>& frame_opt = g_queued_playback->m_replay.GetCurrentFrame()) 
            {
                if (! frame_opt.has_value() || frame_opt->m_replay_input.m_race_frame_tick > g_queued_playback->m_final_tick)
                {
                    break;
                }

                if (shared_state->m_dll_in_buffer_input_replay.TryPush(frame_opt->m_replay_input)) 
                {
                    g_queued_playback->m_replay.IncrementFrameIndex();
                } 
                else 
                {
                    break;
                }
            }
        }

        void OnInitNewReplay()
        {
            g_queued_playback->m_replay.ResetFrameIndex();
            auto general_cmd_ref = AsphaltDllManager::GetDllGeneralCommandsInRef();
            EnableReplayModeActiveBlock(*general_cmd_ref);
            general_cmd_ref->m_write_meta_data.m_fixed_frame_interval_micros    = g_queued_playback->m_replay.GetFrameIntervalMicros();
        }
    }

    void ClearInputCommandBuffer() noexcept
    {
        ComSharedMem::GetSharedState()->m_dll_in_buffer_input_replay.Reset();
    }

    bool QueueReplay(const Replay& replay, uint32_t target_tick) noexcept 
    {
        const auto copy = AsphaltDllManager::GetDllStateOutCopy();

        if (replay.GetAmountFrames() == 0)
        {
            return false;
        }

        // We are in a race, with a queued replay that has not yet finished playing; disallow changing replay
        if (copy->m_meta_data.m_race_status_state == ComDllOut::RaceStatusState::IN_RACE 
            && g_queued_playback.has_value() && g_queued_playback->m_final_tick >= copy->m_replay_inputs.m_race_frame_tick)
        {
            return false;
        }

        ClearInputCommandBuffer();

        g_queued_playback.emplace(PlaybackSession{replay, std::min<uint32_t>(target_tick, replay.GetAmountFrames() - 1)});
        g_queued_playback->m_replay.ResetFrameIndex();

        // We are not in a race (before next race) therefore we rig the dll to expect frames right away
        if (copy->m_meta_data.m_race_status_state != ComDllOut::RaceStatusState::IN_RACE)
        {
            OnInitNewReplay();
        }

        return true;
    }

    bool ChangeQueuedReplayTargetTick(uint32_t target_tick) noexcept
    {
        if (! HasQueuedReplay() || g_queued_playback->m_replay.GetAmountFrames() == 0)
        {
            return false;
        }
        
        g_queued_playback->m_final_tick = std::min<uint32_t>(target_tick, g_queued_playback->m_replay.GetAmountFrames() - 1);
        return true;
    }

    bool HasQueuedReplay() noexcept
    {
        return g_queued_playback.has_value();
    }

    const std::optional<PlaybackSession>& GetQueuedPlaybackSessionConstRef() noexcept
    {
        return g_queued_playback;
    }

    void ClearQueuedReplay() noexcept 
    { 
        g_queued_playback.reset();
        DisableReplayModeActiveBlock(*AsphaltDllManager::GetDllGeneralCommandsInRef());
        ClearInputCommandBuffer();
    }

    void OnRaceStarted() noexcept 
    {   
        g_current_recording.ClearAllFrameData();
        const auto state_out = AsphaltDllManager::GetDllStateOutCopy();
        if (!state_out) return;
        g_current_recording.SetFrameIntervalMicros(state_out->m_meta_data.m_fixed_frame_interval_micros);
    }

    void OnRaceEnded() noexcept 
    {
        FinalizeRecording();

        //If we have a queued replay, we enable active block in preparation for next race
        if (HasQueuedReplay())
        {
            OnInitNewReplay();
        }

        // Clean up junk inputs before next race
        ReplayStateManager::ClearInputCommandBuffer();
    }

    void OnUpdate() noexcept 
    {
        ///////////////////////////////////
        // Record new tick
        ///////////////////////////////////
        const std::optional<Communication::DllOut::DllStateOut> dll_out_state = AsphaltDllManager::GetDllStateOutCopy();
        if (! dll_out_state.has_value())
        {
            return;
        }

        // Record current tick
        if (dll_out_state->m_meta_data.m_race_status_state == ComDllOut::RaceStatusState::IN_RACE)
        {
            Replay::Frame new_frame;
            new_frame.m_replay_input.m_race_frame_tick                   = dll_out_state->m_replay_inputs.m_race_frame_tick;
            new_frame.m_replay_input.m_steer_value                       = dll_out_state->m_replay_inputs.m_steer_value;
            new_frame.m_replay_input.m_brake_value                       = dll_out_state->m_replay_inputs.m_brake_value;
            new_frame.m_replay_input.m_nitro_activation_count = dll_out_state->m_replay_inputs.m_nitro_activation_count;
            new_frame.m_replay_input.m_accelerator_value                 = dll_out_state->m_replay_inputs.m_accelerator_value;
            new_frame.m_replay_input.m_respawn_button_press              = dll_out_state->m_replay_inputs.m_respawn_button_press;
            new_frame.m_replay_input.m_racer_transform_mat4x4            = dll_out_state->m_racer_state.m_racer_transform_mat4x4;
            new_frame.m_replay_input.m_racer_velocity_vec3               = dll_out_state->m_racer_state.m_racer_velocity_vec3;
            new_frame.m_recorded_camera_state                            = dll_out_state->m_camera_state;
            new_frame.m_recorded_racer_state                             = dll_out_state->m_racer_state;

            g_current_recording.EmplaceBackFrame(new_frame);
        }

        ///////////////////////////////////
        // Playback update
        ///////////////////////////////////

        //If we have no replay, or our replay is already fully played, we disable the dll block
        if (! HasQueuedReplay() || dll_out_state->m_replay_inputs.m_race_frame_tick >= g_queued_playback->m_final_tick)
        {
            DisableReplayModeActiveBlock(*AsphaltDllManager::GetDllGeneralCommandsInRef());
            return;
        }

        // Writing as many inputs as possible always e.g. in menu
        PushPlaybackFramesToInputInBuffer();
    }

    std::vector<Replay>& GetRecordedReplayListRef() noexcept
    {
        return g_recorded_replays;
    }
}