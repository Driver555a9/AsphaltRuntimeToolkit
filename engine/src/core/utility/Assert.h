#pragma once

#include <cassert>
#include <iostream>

#include "core/utility/ColorCodes.h"

#define ENGINE_ASSERT(expr) assert(expr)

#if defined(_MSC_VER)
    consteval const char* GetFileName(const char* path) 
    {
        const char* file = path;
        for (const char* p = path; *p; ++p) 
        {
            if (*p == '/' || *p == '\\') 
            {
                file = p + 1;
            }
        }
        return file;
    }

    #define __FILENAME__HELPER__ GetFileName(__FILE__)

    #define ENGINE_ERROR_PRINT(expr) std::cout << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RED) \
    << "\n[ERROR] File: " << __FILENAME__HELPER__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::GREEN) \
    << " Line " << __LINE__ << ": " << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RESET) << expr << std::endl

    #define ENGINE_ERROR_PRINT_NO_TEXT() std::cout << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RED) \
    << "\n[ERROR] File: " << __FILENAME__HELPER__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::GREEN) \
    << " Line " << __LINE__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RESET) << std::endl

    #define ENGINE_INFO_LOG(expr) std::cout << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::YELLOW) \
    << "\n[INFO] File: " << __FILENAME__HELPER__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::GREEN) \
    << " Line " << __LINE__ << ": " << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RESET) << expr << std::endl
#elif defined(__GNUC__)

    #define ENGINE_ERROR_PRINT(expr) std::cout << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RED) \
    << "\n[ERROR] File: " << __FILE_NAME__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::GREEN) \
    << " Line " << __LINE__ << ": " << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RESET) << expr << std::endl

    #define ENGINE_ERROR_PRINT_NO_TEXT() std::cout << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RED) \
    << "\n[ERROR] File: " << __FILE_NAME__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::GREEN) \
    << " Line " << __LINE__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RESET) << std::endl

    #define ENGINE_INFO_LOG(expr) std::cout << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::YELLOW) \
    << "\n[INFO] File: " << __FILE_NAME__ << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::GREEN) \
    << " Line " << __LINE__ << ": " << ::CoreEngine::ColorCodes::GetColor(::CoreEngine::ColorCodes::RESET) << expr << std::endl
#endif 
