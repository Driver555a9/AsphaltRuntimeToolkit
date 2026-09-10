#pragma once

#include <cstdint>
#include <iomanip>
#include <fstream>
#include <string>
#include <unordered_map>
#include <string_view>
#include <iostream>
#include <optional>
#include <vector>
#include <mutex>
#include <sstream>
#include <format>
#include <source_location>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#include "BulletTypes.h"

namespace AsphaltDLL
{
    namespace Utility
    {
        namespace ColorCodes
        {
            constexpr std::string_view  RESET   = "\033[0m";
            constexpr std::string_view  RED     = "\033[31m";
            constexpr std::string_view  GREEN   = "\033[32m";
            constexpr std::string_view  YELLOW  = "\033[33m";
            constexpr std::string_view  BLUE    = "\033[34m";
            constexpr std::string_view  WHITE   = "\033[37m";
        }

        void InitConsole() noexcept;
        void ShutdownConsole() noexcept;
        void ClearConsole() noexcept;

        std::ofstream& DONOTCALL_GetDebugLogInternal() noexcept;
        bool InitDebugLog(const std::filesystem::path& path) noexcept;
        void ShutdownDebugLog() noexcept;

        template <typename T>
        void LogToFile(const T& val) noexcept 
        {
            DONOTCALL_GetDebugLogInternal() << val << std::endl;
        }

        template <typename... Args>
        void LogToFileMulti(Args&&... args) noexcept
        {
            auto& log = DONOTCALL_GetDebugLogInternal();
            (log << ... << std::forward<Args>(args));
            log << std::endl;
        }

        inline std::string_view GetFileName(std::source_location location = std::source_location::current()) noexcept
        {
            std::string_view path = location.file_name();

            const auto pos = path.find_last_of("/\\");

            if (pos == std::string_view::npos)
                return path;

            return path.substr(pos + 1);
        }
        
        inline void LogErrorToFile(const std::string& message, std::source_location location = std::source_location::current())
        {
            LogToFile(std::format("[ERROR]: File: {} Line: {}: {}", GetFileName(location), location.line(), message));
        }

        inline void LogInfoToFile(const std::string& message, std::source_location location = std::source_location::current())
        {
            LogToFile(std::format("[INFO]: File: {} Line: {}: {}", GetFileName(location), location.line(), message));
        }

        inline void PrintError(const std::string& message, std::source_location location = std::source_location::current())
        {
            std::cout << ColorCodes::RED << "\n[ERROR] File: " << GetFileName(location) << ColorCodes::GREEN << " Line: " << location.line() << ": "
                << ColorCodes::RESET << message << std::endl;
        }

        inline void PrintInfo(const std::string& message, std::source_location location = std::source_location::current())
        {
            std::cout << ColorCodes::YELLOW << "\n[INFO] File: " << GetFileName(location) << ColorCodes::GREEN << " Line: "
                << location.line() << ": " << ColorCodes::RESET << message << std::endl;
        }

        [[nodiscard]] float RandomFloat(float min, float max) noexcept;
        [[nodiscard]] int RandomInt(int min, int max) noexcept;
        [[nodiscard]] uint64_t GetMonotonicMicrosecondCount() noexcept;
        
        [[nodiscard]] uintptr_t SafeResolvePointerChain(uintptr_t module_base, const std::vector<uintptr_t>& offsets) noexcept;

        template <typename T>
        std::optional<T*> SafeReadPointer(T* const* ptr) noexcept
        {
            T* result = nullptr;
            __try
            {
                if (ptr) result = *ptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) 
            {
                return std::nullopt;
            }
            return result;
        }

        template <typename T>
        std::optional<T*> SafeDereference(T* ptr) noexcept
        {
            __try
            {
                if (!ptr) return std::nullopt;

                volatile char dummy = *reinterpret_cast<volatile char*>(ptr);
                (void)dummy;
                return ptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return std::nullopt;
            }
        }

        template <typename T, typename... Args>
        requires (std::same_as<std::remove_cvref_t<T>, std::remove_cvref_t<Args>> && ...)
        constexpr bool EqualsAny(T&& val, Args&&... args)
        {
            return ((val == args) || ...);
        }

        std::string LPCWSTRToString(LPCWSTR lpcwstr) noexcept;

        [[nodiscard]] BulletTypes::Quaternion RotationFromTransform(const BulletTypes::Transform& mat) noexcept;
        [[nodiscard]] BulletTypes::Vector3 RotateVectorByQuaternion(const BulletTypes::Quaternion& q, const BulletTypes::Vector3& v) noexcept;
                
        template <typename TVal>
        class DebugValCompare
        {
        public:
            explicit DebugValCompare(const std::string& log_file_path) noexcept : m_log_file_path(log_file_path)
            {
                m_log_file.open(m_log_file_path, std::ios::out | std::ios::trunc);
                if (m_log_file.is_open())
                {
                    m_log_file << "--- DebugValCompare Session Started ---\n";
                    m_log_file.flush();
                }
            }

            ~DebugValCompare()
            {
                if (m_log_file.is_open())
                {
                    m_log_file << "--- DebugValCompare Session Ended ---\n";
                    m_log_file.close();
                }
            }

            DebugValCompare(const DebugValCompare&) = delete;
            DebugValCompare& operator=(const DebugValCompare&) = delete;

            void AddValue(uint32_t tick, TVal val) noexcept
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                auto it = m_value_at_tick.find(tick);
                if (it == m_value_at_tick.end())
                {
                    m_log_file << "[ADDED]    Tick " << tick << " | Rec: " << val << "\n";
                    m_value_at_tick[tick] = val;
                }
                else
                {
                    const TVal& recorded_val = it->second;

                    if (m_log_file.is_open())
                    {
                        if constexpr (std::is_floating_point_v<TVal>)
                        {
                            m_log_file << std::setprecision(8) << std::fixed;
                        }

                        if (recorded_val == val)
                        {
                            m_log_file << "[MATCH]    Tick " << tick << " | Rec: " << recorded_val << " == Cur: " << val << "\n";
                        }
                        else
                        {
                            m_log_file << "[MISMATCH] Tick " << tick << " | Rec: " << recorded_val << " != Cur: " << val << "\n";
                        }
                        m_log_file.flush();
                    }
                }
            }

            void ClearRecordedData() noexcept
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_value_at_tick.clear();
                if (m_log_file.is_open())
                {
                    m_log_file << "--- Data Cleared ---\n";
                    m_log_file.flush();
                }
            }

        private:
            std::string m_log_file_path;
            std::ofstream m_log_file;
            std::unordered_map<uint32_t, TVal> m_value_at_tick;
            std::mutex m_mutex;
        };

        template <typename TVal>
        class DebugValForce
        {
        public:
            explicit DebugValForce() noexcept = default;
            ~DebugValForce() = default;

            DebugValForce(const DebugValForce&) = delete;
            DebugValForce& operator=(const DebugValForce&) = delete;

            void ForceValue(uint32_t tick, TVal& val) noexcept
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                auto it = m_value_at_tick.find(tick);
                if (it == m_value_at_tick.end())
                {
                    m_value_at_tick[tick] = val;
                }
                else
                {
                    val = it->second;
                }
            }

            void ClearRecordedData() noexcept
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_value_at_tick.clear();
            }

        private:
            std::unordered_map<uint32_t, TVal> m_value_at_tick;
            std::mutex m_mutex;
        };
    }
}

#define DLL_ERROR_PRINT(expr) \
    do { \
        std::ostringstream _dll_log_stream; \
        _dll_log_stream << expr; \
        ::AsphaltDLL::Utility::PrintError(_dll_log_stream.str()); \
    } while (false)

#define DLL_INFO_PRINT(expr) \
    do { \
        std::ostringstream _dll_log_stream; \
        _dll_log_stream << expr; \
        ::AsphaltDLL::Utility::PrintInfo(_dll_log_stream.str()); \
    } while (false)

#define DLL_ERROR_LOG_FILE(expr) \
    do { \
        std::ostringstream _dll_log_stream; \
        _dll_log_stream << expr; \
        ::AsphaltDLL::Utility::LogErrorToFile(_dll_log_stream.str()); \
    } while (false)

#define DLL_INFO_LOG_FILE(expr) \
    do { \
        std::ostringstream _dll_log_stream; \
        _dll_log_stream << expr; \
        ::AsphaltDLL::Utility::LogInfoToFile(_dll_log_stream.str()); \
    } while (false)

#define DLL_ERROR_LOG_FILE_FORMATED(fmt, ...) \
    ::AsphaltDLL::Utility::LogErrorToFile(std::format((fmt), ##__VA_ARGS__))

#define DLL_INFO_LOG_FILE_FORMATED(fmt, ...) \
    ::AsphaltDLL::Utility::LogInfoToFile(std::format((fmt), ##__VA_ARGS__))