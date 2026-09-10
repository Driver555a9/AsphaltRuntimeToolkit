#pragma once

#include <cassert>
#include <cstdint>

/*TODO: Implement getter for every single offset that varies between versions */

class Offsets
{
public:
    enum class Version : uint32_t
    {
        V47_1_0     = 0,
        MIN_VERSION = V47_1_0,
        MAX_VERSION = V47_1_0,
    };

    explicit Offsets(Version version) noexcept { SetVersion(version); }

    [[nodiscard]] Version GetVersion() const noexcept
    {
        return m_version;
    }

    void SetVersion(Version v) noexcept
    {
        m_version = v;
        m_index   = IndexOf(v);
    }

private:
    constexpr static size_t IndexOf(Version v) noexcept { return static_cast<size_t>(v); }
    size_t  m_index   = 0;
    Version m_version = Version::MIN_VERSION;
};