// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "core/common.h"
#include "base/signal.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <string>

namespace fm::folderappearance {

struct Appearance {
    std::optional<uint32_t> color;
    std::string symbol;
    bool operator==(const Appearance&) const = default;
};

struct Symbol {
    const char* group;
    const char* id;
    const char* name;
    const char* glyph;
    const char* emoji;
};

const std::vector<Symbol>& symbols();
std::string_view glyph(std::string_view id);
Appearance read(const std::string& path);
base::Signal<const std::string&>& changed();

// Keep the inode open: a rename or replacement while the picker is open
// must never redirect a write to another directory or a symlink target.
class Editor {
public:
    explicit Editor(std::string path);
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    bool valid() const { return m_fd >= 0; }
    Appearance appearance() const;
    bool setColor(std::optional<uint32_t> color);
    bool setSymbol(std::string_view id);

private:
    bool set(const char* key, std::string_view value);
    std::string m_path;
    int m_fd;
};

} // namespace fm::folderappearance
