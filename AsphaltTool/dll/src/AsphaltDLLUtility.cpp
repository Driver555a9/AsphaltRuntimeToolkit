#include "AsphaltDLLUtility.h"
#include "BulletTypes.h"
#include "Communication.h"
#include "Timer.h"
#include "Units.h"
#include <cstdint>
#include <cstdio>
#include <random>

namespace AsphaltDLL::Utility
{
    namespace
    {
        std::ofstream g_debug_log;
    } 

    std::ofstream& DONOTCALL_GetDebugLogInternal() noexcept
    {
        return g_debug_log;
    }

    void InitConsole() noexcept 
    {
        AllocConsole();
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);

        HANDLE handles[2] = { GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE) };

        for (HANDLE h : handles)
        {
            DWORD mode = 0;
            if (h && GetConsoleMode(h, &mode))
            {
                mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(h, mode);
            }
        }
    }

    void ShutdownConsole() noexcept
    {
        fclose(stdout);
        fclose(stderr);
        FreeConsole();
    }

    void ClearConsole() noexcept
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD coord = {0, 0};
        DWORD count;
        CONSOLE_SCREEN_BUFFER_INFO csbi;

        GetConsoleScreenBufferInfo(hConsole, &csbi);
        DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;

        FillConsoleOutputCharacter(hConsole, ' ', cellCount, coord, &count);
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, coord, &count);
        SetConsoleCursorPosition(hConsole, coord);
    }

    bool InitDebugLog(const std::filesystem::path& path) noexcept
    {
        g_debug_log.open(path.c_str(), std::ios::out | std::ios::trunc);
        if (!g_debug_log.is_open()) return false;
        g_debug_log<< "=== Debug log started ===" << std::endl;
        return true;
    }

    void ShutdownDebugLog() noexcept
    {
        if (!g_debug_log.is_open())
            return;

        g_debug_log << "=== Debug log stopped ===" << std::endl;
        g_debug_log.flush();
        g_debug_log.close();
    }

    float RandomFloat(float min, float max) noexcept
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    }
        
    int RandomInt(int min, int max) noexcept
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(min, max);
        return dis(gen);
    }

    uint64_t GetMonotonicMicrosecondCount() noexcept
    {
        return Timer::GetMonotonicTime<Units::MicroSecond>().Get();
    }

    uintptr_t SafeResolvePointerChain(uintptr_t module_base, const std::vector<uintptr_t>& offsets) noexcept
    {
        __try 
        {
            uintptr_t out = module_base;
            for (const auto it : offsets)
            {   
                out = *reinterpret_cast<uintptr_t*>(out + it);
            }
            return out;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) 
        {
            return NO_VALID_RESOLVED_ADDRESS;
        }
    }

    std::string LPCWSTRToString(LPCWSTR lpcwstr) noexcept
    {
        if (!lpcwstr) return "";

        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, lpcwstr, -1, NULL, 0, NULL, NULL);
        std::string str(sizeNeeded, 0);
        WideCharToMultiByte(CP_UTF8, 0, lpcwstr, -1, &str[0], sizeNeeded, NULL, NULL);

        return str;
    }

    BulletTypes::Quaternion RotationFromTransform(const BulletTypes::Transform& mat) noexcept 
    {
        float r00 = mat.At(0);  // Right.x
        float r10 = mat.At(2);  // Right.y
        float r20 = mat.At(1);  // Right.z

        float r02 = mat.At(4);  // Forward.x
        float r12 = mat.At(6);  // Forward.y
        float r22 = mat.At(5);  // Forward.z

        float r01 = mat.At(8);   // Up.x
        float r11 = mat.At(10);  // Up.y
        float r21 = mat.At(9);   // Up.z

        float qx, qy, qz, qw;
        float trace = r00 + r11 + r22; // Right.x + Up.y + Forward.z

        if (trace > 0.0f) 
        {
            float s = 0.5f / std::sqrt(trace + 1.0f);
            qw = 0.25f / s;
            qx = (r21 - r12) * s;
            qy = (r02 - r20) * s;
            qz = (r10 - r01) * s;
        } 
        else 
        {
            if (r00 > r11 && r00 > r22) 
            {
                float s = 2.0f * std::sqrt(1.0f + r00 - r11 - r22);
                qw = (r21 - r12) / s;
                qx = 0.25f * s;
                qy = (r01 + r10) / s;
                qz = (r02 + r20) / s;
            } 
            else if (r11 > r22) 
            {
                float s = 2.0f * std::sqrt(1.0f + r11 - r00 - r22);
                qw = (r02 - r20) / s;
                qx = (r01 + r10) / s;
                qy = 0.25f * s;
                qz = (r12 + r21) / s;
            } 
            else 
            {
                float s = 2.0f * std::sqrt(1.0f + r22 - r00 - r11);
                qw = (r10 - r01) / s;
                qx = (r02 + r20) / s;
                qy = (r12 + r21) / s;
                qz = 0.25f * s;
            }
        }

        //TODO: Fix this, why do we return wrong order?
        return BulletTypes::Quaternion{ qx, qz, qy, qw };
    }

    BulletTypes::Vector3 RotateVectorByQuaternion(const BulletTypes::Quaternion& q, const BulletTypes::Vector3& v) noexcept
    {
        const float qx = q.x;
        const float qy = q.y;
        const float qz = q.z; 
        const float qw = q.w;

        const float vx = v.x;
        const float vy = v.y;
        const float vz = v.z;

        const float tx = 2.0f * (qy * vz - qz * vy);
        const float ty = 2.0f * (qz * vx - qx * vz);
        const float tz = 2.0f * (qx * vy - qy * vx);

        BulletTypes::Vector3 rotated;
        rotated.x = vx + qw * tx + (qy * tz - qz * ty);
        rotated.y = vy + qw * ty + (qz * tx - qx * tz);
        rotated.z = vz + qw * tz + (qx * ty - qy * tx);

        return rotated;
    }
}