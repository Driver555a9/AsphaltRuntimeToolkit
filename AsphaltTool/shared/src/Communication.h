#pragma once

#ifndef _WIN32 
    #error "Windows API required for Communication header"
#endif

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cassert>
#include <atomic>
#include <type_traits>

#include "BulletDebugDrawStream.h"

namespace Communication
{
    /////////////////////////////////////////////
    // All 3D data is to be interpreted using Gameloft XZY right-handed convention
    // Quaternions as XZYW
    // Matrices column major
    /////////////////////////////////////////////

    //On "Inactive" the dll will not Read or consider the buffer of DllInReplayInputData
    //On "ActiveBlockThread" the dll will replay the inputs given
    //Enum class shared between DllOut and DllIn
    enum class ReplayMode : std::uint32_t
    {
        Inactive, ActiveBlockThread
    };

    // Skips the animations corresponding to the flags
    enum class SkipAnimationFlags : std::uint32_t
    {
        SKIP_NONE            = 0,
        SKIP_RACE_INTRO      = 1 << 0,
        SKIP_RACE_COUNT_DOWN = 1 << 1
    };

    constexpr char DLL_DUMPED_TRACK_FILE_NAME[] = "objects.TRACK";
    constexpr char REPLAY_FILE_TYPE[]           = ".NEOREPLAY";
    constexpr char TRACK_FILE_TYPE[]            = ".TRACK";
    constexpr wchar_t REPLAY_FILE_TYPE_WIDE[]   = L".NEOREPLAY";
    constexpr wchar_t TRACK_FILE_TYPE_WIDE[]    = L".TRACK";
    constexpr wchar_t TRACK_FILE_TYPE_NO_DOT_WIDE[]     = L"TRACK";
    constexpr wchar_t REPLAY_FILE_TYPE_NO_DOT_WIDE[]    = L"NEOREPLAY";
    constexpr wchar_t REPLAY_FILE_TYPE_WITH_STAR_WIDE[] = L"*.NEOREPLAY";
    constexpr wchar_t TRACK_FILE_TYPE_WITH_STAR_WIDE[]  = L"*.TRACK";

    constexpr uint32_t CURRENT_NON_NEGOTIABLE_COMMUNICATION_VERSION = 6; // Detect dll ABI missmatches

    namespace DllOut
    {
        struct RecordedReplayInputData 
        {
            //// Frame tick
            std::uint32_t m_race_frame_tick{};

            //// Steer
            float m_steer_value {};

            //// Brake
            float m_brake_value {};

            //// Nitro
            std::uint32_t m_nitro_activation_count {};

            //// Accelerator
            float m_accelerator_value = 1;

            // Respawn button press
            bool m_respawn_button_press = false;

            uint8_t __ignore__padding__[7];
        };
        static_assert(sizeof(RecordedReplayInputData) == 3 * sizeof(float) + 2 * sizeof(std::uint32_t) + 8 * sizeof(uint8_t), "No packing should occur");

        struct RecordedRacerState
        {
            BulletTypes::UnalignedTransform m_racer_transform_mat4x4 {};
            BulletTypes::UnalignedVector3 m_racer_velocity_vec3 {};
            float m_nitro_bar_value {};
            float m_race_progress_percentage {};
            float m_rpm {};
            std::uint32_t m_checkpoint {};
            std::uint32_t m_gear {};
            std::uint32_t m_continuous_override_on_flags = 0;
            uint32_t m_path {};
            uint32_t m_segment {};
            float    m_mu {};
            uint8_t  m_pad[4];

            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_TRANSFORM = 1 << 0;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_VELOCITY  = 1 << 1;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_NITRO_BAR = 1 << 2;
            
            // Utility: memory offsets from resolved address
            constexpr static uintptr_t OFFSET_TRANSFORM = 0x20;
            constexpr static uintptr_t OFFSET_VELOCITY  = 0x160; 
        };
        static_assert(sizeof(RecordedRacerState) == 23 * sizeof(float) + 5 * sizeof(uint32_t) + 4 * sizeof(uint8_t), "No packing should occur");

        struct RecordedCameraState 
        {
            BulletTypes::UnalignedVector3    m_camera_position_vec3 {};
            BulletTypes::UnalignedQuaternion m_camera_rotation_quat {};
            float m_fov_radians {};
            float m_aspect_ratio {};
            BulletTypes::UnalignedVector3 m_offset_relative_to_car {}; // Requiures continuous override flag CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR or ignored - RIGHT, FORWARD, UP
            std::uint32_t m_continuous_override_on_flags = 0;
            bool m_look_backwards = false; // Looks backwars if in CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR
            std::uint8_t __ignore__padding__[3];

            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_POSITION           = 1 << 0;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_ROTATION           = 1 << 1;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_FOV_RAD            = 1 << 2;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR    = 1 << 3;
            
            // Fixed incorrect base; Base now is the vtable - therefore all offsets moved up 0x38
            constexpr static const inline uintptr_t OFFSET_POSITON_VEC3  = 0x0  + 0x38;
            constexpr static const inline uintptr_t OFFSET_ROTATION_QUAT = 0xC  + 0x38;
            constexpr static const inline uintptr_t OFFSET_FOV_RADIANS   = 0xF0 + 0x38;
            constexpr static const inline uintptr_t OFFSET_ASPECT_RATIO  = 0xF8 + 0x38;
        };
        static_assert(sizeof(RecordedCameraState) == 12 * sizeof(float) + sizeof(std::uint32_t) + 4 * sizeof(uint8_t), "No packing should occur");

        #define NO_VALID_RESOLVED_ADDRESS 0
        struct ResolvedAddresses
        {
            uintptr_t m_local_racer_base_address                  = NO_VALID_RESOLVED_ADDRESS; // points to BulletTypes::RigidBody!
            uintptr_t m_camera_state_base_address                 = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_nitro_bar_encrypted_address               = NO_VALID_RESOLVED_ADDRESS; 
            uintptr_t m_steering_struct_base_address              = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_game_target_fps_interval_address          = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_discrete_dynamics_world_instance_address  = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_physics_world_wrapper_address             = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_physics_context_address                   = NO_VALID_RESOLVED_ADDRESS;

            uintptr_t m_nitro_func_spoofed_rcx_arg         = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_brake_func_spoofed_rcx_arg         = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_steer_func_spoofed_rcx_arg         = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_respawn_func_spoofed_rcx_arg       = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_bvh_root_node_static_objects       = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_bvh_root_node_dynamic_objects      = NO_VALID_RESOLVED_ADDRESS;
            uintptr_t m_is_paused_func_spoofed_rcx_arg     = NO_VALID_RESOLVED_ADDRESS;

            void ResetAll() noexcept
            {
                m_local_racer_base_address                  = NO_VALID_RESOLVED_ADDRESS;
                m_camera_state_base_address                 = NO_VALID_RESOLVED_ADDRESS;
                m_nitro_bar_encrypted_address               = NO_VALID_RESOLVED_ADDRESS;
                m_steering_struct_base_address              = NO_VALID_RESOLVED_ADDRESS;
                m_game_target_fps_interval_address          = NO_VALID_RESOLVED_ADDRESS;
                m_discrete_dynamics_world_instance_address  = NO_VALID_RESOLVED_ADDRESS;
                m_physics_world_wrapper_address             = NO_VALID_RESOLVED_ADDRESS;
                m_physics_context_address                   = NO_VALID_RESOLVED_ADDRESS;

                m_nitro_func_spoofed_rcx_arg         = NO_VALID_RESOLVED_ADDRESS;
                m_brake_func_spoofed_rcx_arg         = NO_VALID_RESOLVED_ADDRESS;
                m_steer_func_spoofed_rcx_arg         = NO_VALID_RESOLVED_ADDRESS;
                m_respawn_func_spoofed_rcx_arg       = NO_VALID_RESOLVED_ADDRESS;
                m_bvh_root_node_static_objects       = NO_VALID_RESOLVED_ADDRESS;
                m_bvh_root_node_dynamic_objects      = NO_VALID_RESOLVED_ADDRESS;
                m_is_paused_func_spoofed_rcx_arg     = NO_VALID_RESOLVED_ADDRESS;
            }
        };
        static_assert(sizeof(ResolvedAddresses) == 15 * sizeof(uintptr_t), "No packing should occur");

        struct XInputState 
        {
            uint32_t m_packet_id {};
            uint16_t m_buttons {};
            uint8_t  m_left_trigger {};
            uint8_t  m_right_trigger {};
            int16_t  m_thumb_lx {};
            int16_t  m_thumb_ly {};
            int16_t  m_thumb_rx {}; 
            int16_t  m_thumb_ry {};
        };
        static_assert(sizeof(XInputState) == sizeof(uint32_t) + sizeof(uint16_t) + 2 * sizeof(uint8_t) + 4 * sizeof(int16_t), "No packing should occur");

        enum class RaceStatusState : uint8_t 
        {
            IN_MENU, IN_RACE, IN_LOADING_SCREEN, IN_PRE_RACE_CINEMATIC
        };

        struct DllStateMetaData 
        {
            ReplayMode m_replay_mode_status                 = ReplayMode::Inactive;
            std::uint32_t m_fixed_frame_interval_micros     = 8333;
            float m_physics_interval                        = 1/60.0f;
            std::uint32_t m_target_frame_interval_micros    = 8333;
            SkipAnimationFlags m_skip_animation_flags       = SkipAnimationFlags::SKIP_NONE; // Deprecated, prefer m_speed_up_pre_race_cinematic
            std::uint32_t m_on_replay_end_skip_tick_count   = 0;
            std::uint32_t m_replay_speed_factor             = 1;
            std::uint32_t m_dump_track_request_id           = 0; // While dump request > last_completed_dump, objects.TRACK will be dumped
            std::uint32_t m_last_completed_dump_request_id  = 0;
            int m_bullet_debug_draw_flags                   = BulletTypes::DebugDrawStream::DBG_MAX_DEBUG_DRAW_MODE;
            RaceStatusState m_race_status_state             = RaceStatusState::IN_MENU;
            bool m_apply_physics_interval_override          = false;
            bool m_gui_is_hidden                            = false;
            bool m_speed_up_pre_race_cinematic              = false;
            bool m_speed_up_gui_animations                  = false;
            bool m_is_currently_paused                      = false;
            bool m_update_debug_draw_stream                 = false;
            uint8_t __ignore_padding__[1];  
        };
        static_assert(sizeof(DllStateMetaData) == sizeof(ReplayMode) + 6 * sizeof(std::uint32_t) + sizeof(int) + sizeof(float) + sizeof(SkipAnimationFlags) + 
                      sizeof(RaceStatusState) + 7 * sizeof(bool), "No packing should occur");
                      

        struct DllStateOut
        {   
        private:
            std::uint64_t m_monotonic_packet_id {0};
            static inline std::atomic<std::uint64_t> s_monotonic_packet_counter = {};

        public:
            ResolvedAddresses m_resolved_addresses {};
            RecordedReplayInputData m_replay_inputs {};
            RecordedRacerState m_racer_state {};
            RecordedCameraState m_camera_state {};
            XInputState m_xinput_state {};
            DllStateMetaData m_meta_data {};

            constexpr size_t GetActiveByteSize() const noexcept { return sizeof(DllStateOut); }
            [[nodiscard]] inline std::uint64_t GetMonotonicPacketID() noexcept { return m_monotonic_packet_id; }
            inline void IncreasePacketIDToHighest() noexcept { m_monotonic_packet_id = DllStateOut::s_monotonic_packet_counter.fetch_add(1, std::memory_order_acq_rel); }
            inline DllStateOut() noexcept : m_monotonic_packet_id(DllStateOut::s_monotonic_packet_counter.fetch_add(1, std::memory_order_acq_rel)) {}
        };
        static_assert(sizeof(DllStateOut) == sizeof(std::uint64_t) + sizeof(ResolvedAddresses) + sizeof(RecordedReplayInputData) + sizeof(RecordedRacerState) 
                                           + sizeof(RecordedCameraState) + sizeof(XInputState) + sizeof(DllStateMetaData), "No packing should occur");
    }

    namespace DllIn
    {
        //On "IgnoreCommand" no action on behalf of the dll will be taken
        //On "ExecuteCommand" the dll will, should it be appropriate, modify the game as instructed right away
        enum class CommandType : std::uint32_t
        {
            IgnoreCommand, ExecuteCommand
        };

        struct DllReplayInputIn
        {
            //// Frame tick
            std::uint32_t m_race_frame_tick{};

            //// Steer
            float m_steer_value {};

            //// Brake
            float m_brake_value {};

            //// Nitro
            std::uint32_t m_nitro_activation_count {};

            //// Accelerator
            float m_accelerator_value {};

            // Skips
            enum SkipOverride : std::uint32_t
            {
                NONE             = 0,
                STEER            = 1 << 0, 
                BRAKE            = 1 << 1, 
                NITRO_ACTIVATION = 1 << 2, 
                ACCELERATOR      = 1 << 3, 
                RESPAWN_BUTTON   = 1 << 4,
                TRANSFORM_FORCED = 1 << 5
            };
            std::uint32_t m_skip_override_flags = SkipOverride::NONE;

            // Respawn button press
            bool m_respawn_button_press = false;

            // Pad
            uint8_t __ignore__padding__[7];

            // Patch for rare inexplicable fuckups
            BulletTypes::UnalignedTransform m_racer_transform_mat4x4 {};
            BulletTypes::UnalignedVector3 m_racer_velocity_vec3 {};

            constexpr size_t GetActiveByteSize() const noexcept { return sizeof(DllReplayInputIn); }
        };
        static_assert(sizeof(DllReplayInputIn) == 3 * sizeof(float) + 3 * sizeof(std::uint32_t) + 8 * sizeof(uint8_t) 
                    + sizeof(BulletTypes::UnalignedTransform) + sizeof(BulletTypes::UnalignedVector3), "No packing should occur");

        struct WriteRacerState
        {
            CommandType m_command_type = CommandType::IgnoreCommand;
            BulletTypes::UnalignedTransform m_racer_transform_mat4x4 {};
            BulletTypes::UnalignedVector3 m_racer_velocity_vec3 {};
            float m_nitro_bar_value { -1.0f }; // negative = ignore
            std::uint32_t m_continuous_override_on_flags = 0;

            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_TRANSFORM = DllOut::RecordedRacerState::CONTINUOUS_OVERRIDE_TRANSFORM;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_VELOCITY  = DllOut::RecordedRacerState::CONTINUOUS_OVERRIDE_VELOCITY;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_NITRO_BAR = DllOut::RecordedRacerState::CONTINUOUS_OVERRIDE_NITRO_BAR;

            // Utility: memory offsets from resolved address
            constexpr static uintptr_t OFFSET_TRANSFORM = DllOut::RecordedRacerState::OFFSET_TRANSFORM;
            constexpr static uintptr_t OFFSET_VELOCITY  = DllOut::RecordedRacerState::OFFSET_VELOCITY; 
        };
        static_assert(sizeof(WriteRacerState) == sizeof(CommandType) + sizeof(std::uint32_t) + 20 * sizeof(float), "No packing should occur");

        struct WriteCameraState 
        {
            CommandType m_command_type = CommandType::IgnoreCommand;
            BulletTypes::UnalignedVector3 m_camera_position_vec3 {};
            BulletTypes::UnalignedQuaternion m_camera_rotation_quat {}; // XZYW
            float m_fov_radians {};
            BulletTypes::UnalignedVector3 m_offset_relative_to_car {}; // Requiures continuous override flag CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR or ignored - RIGHT, FORWARD, UP
            std::uint32_t m_continuous_override_on_flags = 0;
            bool m_look_backwards = false; // Looks backwars if in CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR
            uint8_t __ignore__padding[7];

            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_POSITION           = DllOut::RecordedCameraState::CONTINUOUS_OVERRIDE_POSITION;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_ROTATION           = DllOut::RecordedCameraState::CONTINUOUS_OVERRIDE_ROTATION;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_FOV_RAD            = DllOut::RecordedCameraState::CONTINUOUS_OVERRIDE_FOV_RAD;
            constexpr static std::uint32_t CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR    = DllOut::RecordedCameraState::CONTINUOUS_OVERRIDE_RELATIVE_TO_CAR;

            constexpr static uintptr_t OFFSET_POSITON_VEC3  = DllOut::RecordedCameraState::OFFSET_POSITON_VEC3;
            constexpr static uintptr_t OFFSET_ROTATION_QUAT = DllOut::RecordedCameraState::OFFSET_ROTATION_QUAT;
            constexpr static uintptr_t OFFSET_FOV_RADIANS   = DllOut::RecordedCameraState::OFFSET_FOV_RADIANS;
            constexpr static uintptr_t OFFSET_ASPECT_RATIO  = DllOut::RecordedCameraState::OFFSET_ASPECT_RATIO;
        };
        static_assert(sizeof(WriteCameraState) == sizeof(CommandType) + 11 * sizeof(float) + sizeof(std::uint32_t) + 8 * sizeof(uint8_t), "No packing should occur");

        enum class PausedMenuCmd : uint8_t
        {
            NONE    = 0,
            QUIT    = 1 << 0,
            RESTART = 1 << 1
        };

        struct WriteMetaData
        {
            CommandType m_command_type                      = CommandType::IgnoreCommand;
            ReplayMode  m_replay_mode                       = ReplayMode::Inactive;
            std::uint32_t m_fixed_frame_interval_micros     = 8333;    // 120fps
            float m_physics_interval                        = 1/60.0f; // 60pf
            std::uint32_t m_target_frame_interval_micros    = 8333;
            SkipAnimationFlags m_skip_animation_flags       = SkipAnimationFlags::SKIP_NONE;
            std::uint32_t m_on_replay_end_skip_tick_count   = 0; 
            std::uint32_t m_dump_track_request_id           = 0;       // This must be greater than out states index for dump to happen
            std::uint32_t m_replay_speed_factor             = 1;
            int m_bullet_debug_draw_flags                   = BulletTypes::DebugDrawStream::DBG_MAX_DEBUG_DRAW_MODE;
            bool m_apply_physics_interval_override          = false;   // true changes game behaviour
            bool m_request_dll_shutdown                     = false;   // Alternative to extern C func RequestShutdown call
            bool m_hide_gui                                 = false;
            bool m_speed_up_pre_race_cinematic              = false;
            bool m_speed_up_gui_animations                  = false;
            bool m_request_track_reset                      = false;
            bool m_update_debug_draw_stream                 = false;
            PausedMenuCmd m_paused_menu_cmd                 = PausedMenuCmd::NONE;
            uint8_t __ignore__padd__[3];
        };
        static_assert(sizeof(WriteMetaData) == sizeof(CommandType) + sizeof(ReplayMode) + 5 * sizeof(std::uint32_t) + sizeof(float) +
                      sizeof(SkipAnimationFlags) + sizeof(int) + 8 * sizeof(bool) + sizeof(PausedMenuCmd) + 3 * sizeof(uint8_t), "No packing should occur");

        struct DllGeneralCommandsIn
        {
        private:
            std::uint64_t m_monotonic_packet_id   = {};
            static inline std::atomic<std::uint64_t> s_monotonic_packet_counter = {};

        public:
            WriteRacerState m_write_racer_state   = {};
            WriteCameraState m_write_camera_state = {};
            WriteMetaData m_write_meta_data       = {};

            constexpr size_t GetActiveByteSize() const noexcept { return sizeof(DllGeneralCommandsIn); }
            [[nodiscard]] inline std::uint64_t GetMonotonicPacketID() noexcept { return m_monotonic_packet_id; }
            inline void IncreasePacketIDToHighest() noexcept { m_monotonic_packet_id = DllGeneralCommandsIn::s_monotonic_packet_counter.fetch_add(1, std::memory_order_acq_rel); }
            inline DllGeneralCommandsIn() noexcept : m_monotonic_packet_id(DllGeneralCommandsIn::s_monotonic_packet_counter.fetch_add(1, std::memory_order_acq_rel)) {}
        };
        static_assert(sizeof(DllGeneralCommandsIn) == sizeof(std::uint64_t) + sizeof(WriteRacerState) + sizeof(WriteCameraState) + sizeof(WriteMetaData), "No packing should occur");
    }

    namespace SharedMemory 
    {
        template <typename T, std::uint32_t Size>
        requires std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> 
        && requires(const T& obj) { { obj.GetActiveByteSize() } -> std::convertible_to<std::size_t>; }
        struct alignas(64) SharedRingBuffer
        {
            static_assert(Size > 0, "Ring Buffer size must not be empty.");
            
            static constexpr std::uint32_t CAPACITY = Size;
            using value_type = T;

            [[nodiscard]] inline bool IsFull() const noexcept
            {
                const uint32_t w = m_write_idx.load(std::memory_order_relaxed);
                const uint32_t r = m_read_idx.load(std::memory_order_acquire);
                return (w - r) >= Size;
            }

            inline bool TryPush(const T& value) noexcept
            {
                const uint32_t w = m_write_idx.load(std::memory_order_relaxed);
                const uint32_t r = m_read_idx.load(std::memory_order_acquire);
                if ((w - r) >= Size) return false;

                m_data[w % Size] = value;
                m_write_idx.store(w + 1, std::memory_order_release);
                return true;
            }

            inline void PushOverwrite(const T& value) noexcept
            {
                const uint32_t w = m_write_idx.load(std::memory_order_relaxed);
                const uint32_t r = m_read_idx.load(std::memory_order_acquire);
                if ((w - r) >= Size)
                {
                    m_read_idx.store(r + 1, std::memory_order_relaxed);
                }
                size_t bytes_to_copy = sizeof(T);
                if constexpr (requires(const T& v) { { v.GetActiveByteSize() } -> std::convertible_to<std::size_t>; })
                {
                    const size_t reported = value.GetActiveByteSize();
                    bytes_to_copy = (reported < sizeof(T)) ? reported : sizeof(T);
                }
                std::memcpy(&m_data[w % Size], &value, bytes_to_copy);
                m_write_idx.store(w + 1, std::memory_order_release);
            }

            [[nodiscard]] inline bool IsEmpty() const noexcept
            {
                return m_read_idx.load(std::memory_order_acquire) == m_write_idx.load(std::memory_order_acquire);
            }

            [[nodiscard]] inline bool TryPop(T& out) noexcept
            {
                const uint32_t r = m_read_idx.load(std::memory_order_relaxed);
                const uint32_t w = m_write_idx.load(std::memory_order_acquire);
                if (r == w) return false;

                size_t bytes_to_copy = sizeof(T);
                if constexpr (requires(const T& v) { { v.GetActiveByteSize() } -> std::convertible_to<std::size_t>; })
                {
                    const size_t reported = m_data[r % Size].GetActiveByteSize();
                    bytes_to_copy = (reported < sizeof(T)) ? reported : sizeof(T);
                }
                std::memcpy(&out, &m_data[r % Size], bytes_to_copy);

                m_read_idx.store(r + 1, std::memory_order_release);
                return true;
            }

            [[nodiscard]] inline bool TryPeek(T& out) const noexcept
            {
                const uint32_t r = m_read_idx.load(std::memory_order_relaxed);
                const uint32_t w = m_write_idx.load(std::memory_order_acquire);

                if (r == w) return false;

                out = m_data[r % Size]; 
                return true;
            }

            inline void Reset() noexcept
            {
                m_write_idx.store(0, std::memory_order_release);
                m_read_idx.store(0,  std::memory_order_release);
            }

        private:
            std::atomic<std::uint32_t> m_write_idx {0};
            std::uint8_t _pad0[60] {};

            std::atomic<std::uint32_t> m_read_idx  {0};
            std::uint8_t _pad1[60] {};

            T m_data[Size] {};
        };

        struct SharedState
        {
            constexpr static uint32_t DLL_OUT_BUFF_SIZE           = 5000;
            constexpr static uint32_t DLL_OUT_SECONDARY_BUFF_SIZE = 5000;
            constexpr static uint32_t DLL_IN_REPLAY_BUFF_SIZE     = 1000;
            constexpr static uint32_t DLL_IN_GENERAL_BUFF_SIZE    = 500;
            constexpr static uint32_t DLL_OUT_DEBUG_DRAW_STREAM_BUFF_SIZE      = 3;
            constexpr static uint32_t DLL_OUT_DEBUG_DRAW_STATIC_MESH_BUFF_SIZE = 1000;
            constexpr static uint32_t EXTERNAL_TOOL_PATH_SIZE = 32767;
            
            std::atomic<uint32_t> m_non_negotiable_communication_version {0xFFFFFFFF}; // Must be set by external tool - must match DLLs value

            // WRITE: DLL - READ: Remote Tool -> This is used for all recorded data by the dll. External tools must never write here
            SharedRingBuffer<DllOut::DllStateOut, DLL_OUT_BUFF_SIZE>  m_dll_out_buffer;

            // Copy for third party tool that may want to read memory without claiming the main out data
            SharedRingBuffer<DllOut::DllStateOut, DLL_OUT_SECONDARY_BUFF_SIZE> m_dll_out_secondary_buffer;

            // WRITE: Remote Tool - READ: DLL -> This is used specifically for Replay data. 
            // This must be enabled/disabled in the general buffer meta data struct
            SharedRingBuffer<DllIn::DllReplayInputIn, DLL_IN_REPLAY_BUFF_SIZE> m_dll_in_buffer_input_replay;

            // WRITE: Remote Tool - READ: DLL -> This is used for general data
            // Frame agnostic (new frame function will clear this buffer in one go)
            SharedRingBuffer<DllIn::DllGeneralCommandsIn, DLL_IN_GENERAL_BUFF_SIZE>  m_dll_in_buffer_general;

            // WRITE: DLL - READ: Remote Tool -> This is used for live display of btDebugDraw
            SharedRingBuffer<BulletTypes::DebugDrawStream::DebugFrameData, DLL_OUT_DEBUG_DRAW_STREAM_BUFF_SIZE> m_dll_out_debug_draw_stream;
            SharedRingBuffer<BulletTypes::DebugDrawStream::CachedMeshDefinitionChunk, DLL_OUT_DEBUG_DRAW_STATIC_MESH_BUFF_SIZE> m_dll_out_debug_draw_static_meshes;

            // Path of external tool
            wchar_t m_directory_external_tool[EXTERNAL_TOOL_PATH_SIZE];
            uint32_t m_directory_external_tool_size = 0; // 0 = ignore
        };

        constexpr size_t  SHARED_MEMORY_SIZE  = sizeof(SharedState);
        constexpr LPCWSTR SHARED_MEMORY_NAME  = L"AsphaltToolCommunicationSharedMemory";

        /// Must not be used or modified directly, instead prefer getter functions below
        inline HANDLE _internal_shared_memory_handle_ = nullptr;
        inline void* _internal_shared_memory_ptr_     = nullptr;

        [[nodiscard]] inline void* GetSharedMemoryPtr() noexcept 
        {
            if (!_internal_shared_memory_handle_)
            {
                _internal_shared_memory_handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,(DWORD)SHARED_MEMORY_SIZE, SHARED_MEMORY_NAME);
            }
            if (!_internal_shared_memory_ptr_)
            {
                _internal_shared_memory_ptr_ = MapViewOfFile(_internal_shared_memory_handle_, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEMORY_SIZE);;
            }
            return _internal_shared_memory_ptr_;
        }

        [[nodiscard]] inline SharedState* GetSharedState() noexcept
        {
            return static_cast<SharedState*>(GetSharedMemoryPtr());
        }

        inline void ShutdownSharedMemory() noexcept
        {
            if (_internal_shared_memory_ptr_)
            {
                UnmapViewOfFile(_internal_shared_memory_ptr_);
                _internal_shared_memory_ptr_ = nullptr;
            }

            if (_internal_shared_memory_handle_)
            {
                CloseHandle(_internal_shared_memory_handle_);
                _internal_shared_memory_handle_ = nullptr;
            }
        }
    }
    
}

namespace ComSharedMem = Communication::SharedMemory;
namespace ComDllIn = Communication::DllIn;
namespace ComDllOut = Communication::DllOut;