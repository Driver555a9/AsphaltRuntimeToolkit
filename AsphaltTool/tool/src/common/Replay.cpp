#include "common/Replay.h"

#include "core/utility/Assert.h"
#include "core/utility/CommonUtility.h"

#include "simdjson/simdjson.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <exception>

namespace AsphaltTas
{
    const std::vector<Replay::Frame>& Replay::GetFrameVectorConstReference() const noexcept
    {
        return m_frames;
    }

    std::vector<Replay::Frame>& Replay::GetFrameVectorReference() noexcept
    {
        return m_frames;
    }

    void Replay::IncrementFrameIndex(size_t count) noexcept
    {
        m_current_frame_index = std::min(m_current_frame_index + count, m_frames.size() - 1);
    }

    void Replay::GoToFrameAtTick(std::uint32_t tick) noexcept
    {
        if (m_frames.empty()) return;

        // Go back if we're too far ahead
        while (m_current_frame_index > 0 && m_frames[m_current_frame_index].m_replay_input.m_race_frame_tick > tick)
        {
            m_current_frame_index--;
        }

        // Go forward if we're too far behind
        while (m_current_frame_index < m_frames.size() - 1 && m_frames[m_current_frame_index].m_replay_input.m_race_frame_tick < tick)
        {
            m_current_frame_index++;
        }
    }

    void Replay::ResetFrameIndex() noexcept
    {
        m_current_frame_index = 0;
    }

    std::optional<Replay::Frame> Replay::GetCurrentFrame() const noexcept
    {
        if (m_frames.empty()) return std::nullopt;
        return m_frames[m_current_frame_index];
    }

    std::optional<Replay::Frame> Replay::GetLastFrame() const noexcept
    {
        if (m_frames.empty()) return std::nullopt;
        return m_frames.back();
    }

    size_t Replay::GetAmountFrames() const noexcept
    {
        return m_frames.size();
    }

    size_t Replay::GetCurrentIndex() const noexcept
    {
        return m_current_frame_index;
    }

    bool Replay::IsValid() const noexcept
    {
        for (size_t i{}; i < m_frames.size(); i++)
        {
            if (m_frames[i].m_replay_input.m_race_frame_tick != i)
            {
                return false;
            }
        }
        return true;
    }

    void Replay::ClearAllFrameData() noexcept
    {
        m_frames.clear();
        m_current_frame_index = 0;
    }

    void Replay::SetName(const std::string& name) noexcept
    {
        m_name = name;
    }

    std::string Replay::GetName() const noexcept
    {
        return m_name;
    }

    uint32_t Replay::GetFrameIntervalMicros() const noexcept
    {
        return m_frame_interval_micros;
    }

    void Replay::SetFrameIntervalMicros(uint32_t interval) noexcept
    {
        m_frame_interval_micros = interval;   
    }

    enum class ReplayVersionMajor : uint32_t
    {
        VERSION_MAJOR_1 = 1, // Core features that are mandatory
        VERSION_MAJOR_2 = 2, // Added: bool RespawnButton for respawn button press inputs
        VERSION_MAJOR_3 = 3, // Added: RACE_PROGRESS, CAR_RPM, CP, GEAR - datapoints for RacerStates
        VERSION_MAJOR_4 = 4, // REMOVED BARREL STATE VALUES - Fundamentally incompatible with modern versions of tool if version < 4
        NEWEST          = VERSION_MAJOR_4
    };

    constexpr std::strong_ordering operator<=>(ReplayVersionMajor lhs, ReplayVersionMajor rhs)
    {
        return static_cast<uint32_t>(lhs) <=> static_cast<uint32_t>(rhs);
    }

    namespace SerializeKeys
    {
        namespace Meta
        {
            constexpr char ROOT []           = "Meta";
            constexpr char VERSION[]         = "ManifestVersion";
            constexpr char TITLE[]           = "Title";
            constexpr char TIMESTAMP[]       = "Timestamp";
            constexpr char FRAME_INTERVAL[]  = "FrameInterval";
        }

        namespace ReplayInputs
        {
            constexpr char ROOT[]                      = "ReplayInputs";
            constexpr char TICK[]                      = "Tick";
            constexpr char STEER_BITS[]                = "Steer";
            constexpr char BRAKE_BITS[]                = "Brake";
            constexpr char ACCEL_BITS[]                = "Accel";
            constexpr char NITRO_ACTIVATIONS[]         = "NitroActivations";
            constexpr char RESPAWN_BUTTON[]            = "RespawnButton";
        }

        namespace RacerStates
        {
            constexpr char ROOT[]          = "RacerStates";
            constexpr char TICK[]          = "Tick";
            constexpr char TRANSFORM[]     = "Transform";
            constexpr char VELOCITY[]      = "Velocity";
            constexpr char NITRO_BAR[]     = "Nitro%";
            constexpr char RACE_PROGRESS[] = "Progress%";
            constexpr char CAR_RPM[]       = "Rpm";
            constexpr char CP[]            = "Cp";
            constexpr char GEAR[]          = "Gear";
        }
    }

    bool Replay::SerializeReplayToFile(const Replay& replay, const std::filesystem::path& path) noexcept
    {   
        try 
        {
            CoreEngine::CommonUtility::WriteStringToFile(SerializeReplayToString(replay), path);
            return true;
        } 
        catch (const std::exception& err)
        {
            ENGINE_ERROR_PRINT("Could not save replay to file: " << err.what());
            return false;
        }
    }

    Replay Replay::DeserializeReplayFromFile(const std::filesystem::path& file_path) noexcept
    {
        try 
        {
            return DeserializeReplayFromString(CoreEngine::CommonUtility::ReadFileToString(file_path));
        }
        catch (const std::exception& err)
        {
            ENGINE_ERROR_PRINT("Could not load replay from file: " << err.what());
            return Replay{};
        }
    }

    // always serializes to newest version (all features)
    std::string Replay::SerializeReplayToString(const Replay& replay)
    {
        const auto FloatToDecimal = [](float f) -> std::string 
        {
            char buf[32];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), f, std::chars_format::general, 9);
            return std::string(buf, ptr);
        };

        nlohmann::ordered_json j;

        j[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::VERSION]          = ReplayVersionMajor::NEWEST;
        j[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::TITLE]            = replay.GetName();
        j[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::TIMESTAMP]        = std::time(nullptr);
        j[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::FRAME_INTERVAL]   = replay.GetFrameIntervalMicros();

        j[SerializeKeys::ReplayInputs::ROOT] = nlohmann::ordered_json::array();
        j[SerializeKeys::RacerStates::ROOT]  = nlohmann::ordered_json::array();

        const auto& frames = replay.GetFrameVectorConstReference();
        const size_t count = replay.GetAmountFrames();

        for (size_t i = 0; i < count; ++i)
        {
            const Frame& f = frames[i];

            {
                const auto& in = f.m_replay_input;
                nlohmann::ordered_json tick;

                tick[SerializeKeys::ReplayInputs::TICK] = in.m_race_frame_tick;

                tick[SerializeKeys::ReplayInputs::STEER_BITS] = FloatToDecimal(in.m_steer_value);
                tick[SerializeKeys::ReplayInputs::BRAKE_BITS] = FloatToDecimal(in.m_brake_value);
                tick[SerializeKeys::ReplayInputs::ACCEL_BITS] = FloatToDecimal(in.m_accelerator_value);
                tick[SerializeKeys::ReplayInputs::NITRO_ACTIVATIONS] = in.m_nitro_activation_count;
                tick[SerializeKeys::ReplayInputs::RESPAWN_BUTTON]  = in.m_respawn_button_press;

                j[SerializeKeys::ReplayInputs::ROOT].push_back(tick);
            }

            {
                const auto& rs = f.m_recorded_racer_state;
                nlohmann::ordered_json tick;

                tick[SerializeKeys::RacerStates::TICK] = static_cast<uint32_t>(f.m_replay_input.m_race_frame_tick);

                {
                    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
                    for (size_t i{}; i < 16; i++)
                    {
                        arr.push_back(FloatToDecimal(rs.m_racer_transform_mat4x4.At(i)));
                    }

                    tick[SerializeKeys::RacerStates::TRANSFORM] = arr;
                }

                {
                    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
                    arr.push_back(FloatToDecimal(rs.m_racer_velocity_vec3.x));
                    arr.push_back(FloatToDecimal(rs.m_racer_velocity_vec3.y));
                    arr.push_back(FloatToDecimal(rs.m_racer_velocity_vec3.z));

                    tick[SerializeKeys::RacerStates::VELOCITY] = arr;
                }

                tick[SerializeKeys::RacerStates::NITRO_BAR]     = FloatToDecimal(rs.m_nitro_bar_value);
                tick[SerializeKeys::RacerStates::RACE_PROGRESS] = FloatToDecimal(rs.m_race_progress_percentage);
                tick[SerializeKeys::RacerStates::CAR_RPM]       = FloatToDecimal(rs.m_rpm);
                tick[SerializeKeys::RacerStates::CP]            = rs.m_checkpoint;
                tick[SerializeKeys::RacerStates::GEAR]          = rs.m_gear;

                j[SerializeKeys::RacerStates::ROOT].push_back(tick);
            }
        }

        return j.dump(2);
    }

    Replay Replay::DeserializeReplayFromString(const std::string& replay_json)
    {
        const auto DecimalToFloat = [](simdjson::ondemand::value v) -> float
        {
            std::string_view sv = v.get_string().value();
            float out{};
            std::from_chars(sv.data(), sv.data() + sv.size(), out);
            return out;
        };
    
        std::vector<Communication::DllOut::RecordedRacerState> racer_state_cache;
    
        {
            simdjson::padded_string padded_json_pass1(replay_json);
            simdjson::ondemand::parser parser_pass1;
            auto doc_pass1 = parser_pass1.iterate(padded_json_pass1);

            const ReplayVersionMajor replay_version { static_cast<uint32_t>(doc_pass1[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::VERSION].get_uint32()) };

            if (replay_version < ReplayVersionMajor::VERSION_MAJOR_4) 
            {
                ENGINE_ERROR_PRINT("Replay version unsupported. Version must at least be V4 to be compatible with this tool.");
                return {};
            }
    
            for (auto rs : doc_pass1[SerializeKeys::RacerStates::ROOT].get_array())
            {
                Communication::DllOut::RecordedRacerState racer_state{};
    
                {
                    auto arr = rs[SerializeKeys::RacerStates::TRANSFORM].get_array();
                    size_t k = 0;
                    for (auto v : arr)
                    {
                        racer_state.m_racer_transform_mat4x4.At(k++) = DecimalToFloat(v.value());
                    }
                }
    
                {
                    auto arr = rs[SerializeKeys::RacerStates::VELOCITY].get_array();
                    size_t k = 0;
                    for (auto v : arr)
                    {
                        racer_state.m_racer_velocity_vec3[k++] = DecimalToFloat(v.value());
                    }
                }
    
                racer_state.m_nitro_bar_value = DecimalToFloat(rs[SerializeKeys::RacerStates::NITRO_BAR].value());

                if (replay_version >= ReplayVersionMajor::VERSION_MAJOR_3)
                {
                    racer_state.m_race_progress_percentage = DecimalToFloat(rs[SerializeKeys::RacerStates::RACE_PROGRESS].value());
                    racer_state.m_rpm        = DecimalToFloat(rs[SerializeKeys::RacerStates::CAR_RPM].value());
                    racer_state.m_checkpoint = rs[SerializeKeys::RacerStates::CP].get_uint32();
                    racer_state.m_gear       = rs[SerializeKeys::RacerStates::GEAR].get_uint32();
                }

                racer_state_cache.push_back(racer_state);
            }
        }
    
        simdjson::padded_string padded_json(replay_json);
        simdjson::ondemand::parser parser;
        auto doc = parser.iterate(padded_json);
    
        Replay replay;
        const ReplayVersionMajor replay_version { static_cast<uint32_t>(doc[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::VERSION].get_uint32()) };
        replay.SetName(std::string(doc[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::TITLE].get_string().value()));
        replay.SetFrameIntervalMicros(doc[SerializeKeys::Meta::ROOT][SerializeKeys::Meta::FRAME_INTERVAL].get_uint32());
    
        size_t index = 0;
        for (auto input : doc[SerializeKeys::ReplayInputs::ROOT].get_array())
        {
            Frame frame;
            auto& out_input = frame.m_replay_input;
    
            out_input.m_race_frame_tick = input[SerializeKeys::ReplayInputs::TICK].get_uint32();
    
            out_input.m_steer_value       = DecimalToFloat(input[SerializeKeys::ReplayInputs::STEER_BITS].value());
            out_input.m_brake_value       = DecimalToFloat(input[SerializeKeys::ReplayInputs::BRAKE_BITS].value());
            out_input.m_accelerator_value = DecimalToFloat(input[SerializeKeys::ReplayInputs::ACCEL_BITS].value());
    
            out_input.m_nitro_activation_count = input[SerializeKeys::ReplayInputs::NITRO_ACTIVATIONS].get_uint32();

            if (replay_version >= ReplayVersionMajor::VERSION_MAJOR_2)
            {
                out_input.m_respawn_button_press = input[SerializeKeys::ReplayInputs::RESPAWN_BUTTON]->get_bool();
            }
    
            if (index < racer_state_cache.size())
            {
                const Communication::DllOut::RecordedRacerState& raw       = racer_state_cache[index];
                Communication::DllOut::RecordedRacerState& out_racer_state = frame.m_recorded_racer_state;
    
                out_racer_state.m_racer_transform_mat4x4 = raw.m_racer_transform_mat4x4;
                out_racer_state.m_racer_velocity_vec3    = raw.m_racer_velocity_vec3;
                out_racer_state.m_nitro_bar_value        = raw.m_nitro_bar_value;
            }
    
            replay.EmplaceBackFrame(std::move(frame));
            ++index;
        }

        if (!replay.IsValid())
        {
            ENGINE_ERROR_PRINT("Replay could not be loaded because of invalid formatting. Some ticks are duplicated or missing.");
            return Replay{};
        }

        for (size_t i{}; i < replay.GetAmountFrames(); ++i)
        {
            auto& frame = replay.m_frames[i];
            frame.m_replay_input.m_racer_transform_mat4x4 = frame.m_recorded_racer_state.m_racer_transform_mat4x4;
            frame.m_replay_input.m_racer_velocity_vec3 = frame.m_recorded_racer_state.m_racer_velocity_vec3;
        }
    
        return replay;
    }
}