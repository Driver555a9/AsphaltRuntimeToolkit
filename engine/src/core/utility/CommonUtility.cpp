#include "core/utility/CommonUtility.h"

//std
#include <filesystem>
#include <fstream>
#include <format>

//glm
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/gtx/vector_angle.hpp"

namespace CoreEngine::CommonUtility
{
    std::string ReadFileToString(const std::filesystem::path& filepath) 
    {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("At CommonUtility::ReadFileToString(): Failed to open file");
        }

        const auto size = file.tellg();
        if (size < 0) {
            throw std::runtime_error("At CommonUtility::ReadFileToString(): Failed to determine file size");
        }

        std::string content(static_cast<std::string::size_type>(size), '\0');
        file.seekg(0);
        file.read(content.data(), content.size());

        if (!file) {
            throw std::runtime_error("At CommonUtility::ReadFileToString(): Failed to read file");
        }

        return content;
    }

    void WriteStringToFile(const std::string& str, const std::filesystem::path& filepath)
    {
        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("At CommonUtility::WriteStringToFile(): Failed to open file for writing");
        }

        file.write(str.data(), static_cast<std::streamsize>(str.size()));

        if (!file) {
            throw std::runtime_error("At CommonUtility::WriteStringToFile(): Failed to write to file");
        }
    }

    std::pair<double, double> GetMousePosition(GLFWwindow* window) noexcept
    {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        return {x, y};
    }

    std::pair<int, int> GetFramebufferSize(GLFWwindow* window) noexcept
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        return {width, height};
    }

    std::string GlmVec3ToString(const glm::vec3& vec) noexcept
    {
        return std::format("[x: {:.1f}, y: {:.1f}, z: {:.1f}]", vec.x, vec.y, vec.z);
    }

    std::string GlmQuatToString(const glm::quat& q) noexcept
    {
        return std::format("[w: {:.1f}, x: {:.1f}, y: {:.1f}, z: {:.1f}]", q.w, q.x, q.y, q.z);
    }
}