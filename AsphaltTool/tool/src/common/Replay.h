#pragma once

#include "Communication.h"

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace AsphaltTas
{
    class Replay
    {
    public:
        struct Frame
        {
            // -- Stores frame tick input / For Tas -- 
            Communication::DllIn::DllReplayInputIn m_replay_input;
            // -- Stores frame transform / For Ghosts
            Communication::DllOut::RecordedRacerState m_recorded_racer_state;
            // -- Stores camera transform
            Communication::DllOut::RecordedCameraState m_recorded_camera_state;
        };

        template <typename... Args>
        requires std::is_constructible_v<Frame, Args...>
        void EmplaceBackFrame(Args&&... args) noexcept
        {
            m_frames.emplace_back(std::forward<Args>(args)...);
        }
        
        const std::vector<Frame>& GetFrameVectorConstReference() const noexcept;
        std::vector<Frame>& GetFrameVectorReference() noexcept;

        void IncrementFrameIndex(size_t count = 1) noexcept;
        void GoToFrameAtTick(std::uint32_t tick) noexcept;
        void ResetFrameIndex() noexcept;
        [[nodiscard]] std::optional<Frame> GetCurrentFrame() const noexcept;
        [[nodiscard]] std::optional<Frame> GetLastFrame() const noexcept;
        [[nodiscard]] size_t GetAmountFrames() const noexcept;
        [[nodiscard]] size_t GetCurrentIndex() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;

        void ClearAllFrameData() noexcept;

        void SetName(const std::string& name) noexcept;
        [[nodiscard]] std::string GetName() const noexcept;

        uint32_t GetFrameIntervalMicros() const noexcept;
        void SetFrameIntervalMicros(uint32_t interval) noexcept;

        static bool SerializeReplayToFile(const Replay& replay, const std::filesystem::path& path) noexcept;
        [[nodiscard]] static Replay DeserializeReplayFromFile(const std::filesystem::path& file_path) noexcept;

        [[nodiscard]] static std::string SerializeReplayToString(const Replay& replay);
        [[nodiscard]] static Replay DeserializeReplayFromString(const std::string& replay_json);

    private:
        std::vector<Frame> m_frames;
        std::string m_name                        = "Unnamed";
        uint32_t m_frame_interval_micros          = 8333;
        size_t m_current_frame_index              = 0;
    };
}