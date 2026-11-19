// SPDX-License-Identifier: BSD-3-Clause
#include "core/folderappearance.h"

#include "base/i18n.h"

#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <sys/xattr.h>
#include <unistd.h>

using cutefish::i18n::trNoop;

namespace fm::folderappearance {

static constexpr auto colorKey = "user.cutefish.folder-color";
static constexpr auto symbolKey = "user.cutefish.folder-symbol";

const std::vector<Symbol>& symbols()
{
    static const auto people = trNoop("People");
    static const auto nature = trNoop("Animals & Nature");
    static const auto objects = trNoop("Objects");
    static const auto travel = trNoop("Travel & Places");
    static const std::vector<Symbol> items {
        {people, "person", trNoop("Person"), "♟", "👤"}, {people, "smile", trNoop("Smile"), "☺", "🙂"},
        {people, "hand", trNoop("Hand"), "✋︎", "✋"}, {people, "heart", trNoop("Heart"), "♥", "❤️"},
        {people, "team", trNoop("Team"), "♟♟", "👥"}, {people, "celebrate", trNoop("Celebrate"), "✦", "🎉"},
        {nature, "flower", trNoop("Flower"), "✿", "🌼"}, {nature, "tree", trNoop("Tree"), "♣", "🌲"},
        {nature, "sun", trNoop("Sun"), "☀︎", "☀️"}, {nature, "moon", trNoop("Moon"), "☾", "🌙"},
        {nature, "cloud", trNoop("Cloud"), "☁︎", "☁️"}, {nature, "snow", trNoop("Snow"), "❄︎", "❄️"},
        {travel, "car", trNoop("Car"), "🚗︎", "🚗"}, {travel, "house", trNoop("House"), "⌂", "🏠"},
        {travel, "plane", trNoop("Plane"), "✈︎", "✈️"}, {travel, "anchor", trNoop("Anchor"), "⚓︎", "⚓️"},
        {travel, "mountain", trNoop("Mountain"), "⛰︎", "⛰️"}, {travel, "map", trNoop("Map"), "⚑", "🗺️"},
        {objects, "music", trNoop("Music"), "♫", "🎵"}, {objects, "star", trNoop("Star"), "★", "⭐"},
        {objects, "pen", trNoop("Pencil"), "✎", "✏️"}, {objects, "camera", trNoop("Camera"), "📷︎", "📷"},
        {objects, "gear", trNoop("Gear"), "⚙︎", "⚙️"}, {objects, "tool", trNoop("Wrench"), "⚒︎", "🔧"},
    };
    return items;
}

std::string_view glyph(std::string_view id)
{
    const bool emoji = id.starts_with("emoji:");
    if (emoji)
        id.remove_prefix(6);
    for (const auto& item : symbols())
        if (item.id == id)
            return emoji ? item.emoji : item.glyph;
    return {};
}

static Appearance load(const auto& attribute)
{
    Appearance result;
    auto color = attribute(colorKey);
    uint32_t value = 0;
    if (color.size() == 6) {
        const auto [end, error] = std::from_chars(color.data(), color.data() + color.size(), value, 16);
        if (error == std::errc() && end == color.data() + color.size())
            result.color = value;
    }
    auto symbol = attribute(symbolKey);
    if (!glyph(symbol).empty())
        result.symbol = std::move(symbol);
    return result;
}

Appearance read(const std::string& path)
{
    if (path.empty())
        return {};
    return load([&](const char* key) {
        char buf[48];
        const auto size = lgetxattr(path.c_str(), key, buf, sizeof(buf));
        return size < 0 ? std::string{} : std::string(buf, size_t(size));
    });
}

base::Signal<const std::string&>& changed()
{
    static base::Signal<const std::string&> signal;
    return signal;
}

Editor::Editor(std::string path)
    : m_path(std::move(path)), m_fd(open(m_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC))
{
}

Editor::~Editor()
{
    if (valid())
        close(m_fd);
}

Appearance Editor::appearance() const
{
    return load([&](const char* key) {
        char buf[48];
        const auto size = valid() ? fgetxattr(m_fd, key, buf, sizeof(buf)) : -1;
        return size < 0 ? std::string{} : std::string(buf, size_t(size));
    });
}

bool Editor::set(const char* key, std::string_view value)
{
    if (!valid())
        return false;
    if (value.empty()) {
        if (fremovexattr(m_fd, key) < 0 && errno != ENODATA)
            return false;
    } else if (fsetxattr(m_fd, key, value.data(), value.size(), 0) < 0) {
        return false;
    }
    changed().emit(m_path);
    return true;
}

bool Editor::setColor(std::optional<uint32_t> color)
{
    if (!color)
        return set(colorKey, {});
    if (*color > 0xffffff)
        return false;
    char buf[7];
    std::fill_n(buf, 6, '0');
    char converted[6];
    auto end = std::to_chars(std::begin(converted), std::end(converted), *color, 16).ptr;
    std::copy_backward(converted, end, buf + 6);
    return set(colorKey, std::string_view(buf, 6));
}

bool Editor::setSymbol(std::string_view id)
{
    return (id.empty() || !glyph(id).empty()) && set(symbolKey, id);
}

} // namespace fm::folderappearance
