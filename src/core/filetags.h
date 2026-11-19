// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/i18n.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <string>
#include <sys/xattr.h>

namespace fm::filetags {

inline constexpr std::array names{cutefish::i18n::trNoop("Red"), cutefish::i18n::trNoop("Orange"),
    cutefish::i18n::trNoop("Yellow"), cutefish::i18n::trNoop("Green"), cutefish::i18n::trNoop("Blue"),
    cutefish::i18n::trNoop("Purple"), cutefish::i18n::trNoop("Gray")};
inline constexpr const char* key = "user.cutefish.tags";

inline uint8_t read(const std::string& path)
{
    uint8_t value = 0;
    return !path.empty() && lgetxattr(path.c_str(), key, &value, sizeof(value)) == sizeof(value) ? value & 0x7f : 0;
}

inline bool set(const std::string& path, int index, bool enabled)
{
    if (path.empty() || index < 0 || size_t(index) >= names.size())
        return false;
    const uint8_t bit = 1u << index;
    const uint8_t value = enabled ? read(path) | bit : read(path) & ~bit;
    if (value)
        return lsetxattr(path.c_str(), key, &value, sizeof(value), 0) == 0;
    return lremovexattr(path.c_str(), key) == 0 || errno == ENODATA;
}

} // namespace fm::filetags
