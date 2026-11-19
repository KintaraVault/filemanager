// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "core/preferences.h"
#include "base/hash.h"
#include "core/config.h"

#include "base/paths.h"
#include "base/settings.h"

#include <algorithm>
#include <cmath>

namespace fm {

static constexpr size_t kMaxRecentServers = 10;

Preferences& Preferences::instance()
{
    static Preferences preferences(settingsFile());
    return preferences;
}

Preferences::Preferences(const std::string& path)
    : m_settings(std::make_unique<base::Settings>(path))
{
    base::Settings& s = *m_settings;
    m_viewMethod = std::clamp(int(s.intValue("viewMethod", m_viewMethod)), int(List), int(Gallery));
    m_groupBy = std::clamp(int(s.intValue("groupBy", m_groupBy)), int(NoGroup), int(TagsGroup));
    m_options = unsigned(s.intValue("browserOptions", m_options));
    m_toolbarDisplay = std::clamp(int(s.intValue("toolbarDisplay", m_toolbarDisplay)), 0, 2);
    m_toolbarOrder = s.stringListValue("toolbarOrder", defaultToolbar());
    m_sortMode = int(s.intValue("sortMode", m_sortMode));
    m_sortDesc = s.boolValue("sortDesc", m_sortDesc);
    m_trashSortMode = int(s.intValue("trashSortMode", m_trashSortMode));
    m_trashSortDesc = s.boolValue("trashSortDesc", m_trashSortDesc);
    m_listOriginColumnWidth = s.doubleValue("listOriginColumnWidth", m_listOriginColumnWidth);
    m_listDateColumnWidth = s.doubleValue("listDateColumnWidth", m_listDateColumnWidth);
    m_listSizeColumnWidth = s.doubleValue("listSizeColumnWidth", m_listSizeColumnWidth);
    m_listTypeColumnWidth = s.doubleValue("listTypeColumnWidth", m_listTypeColumnWidth);
    m_maximumIconSize = int(s.intValue("maximumIconSize", m_maximumIconSize));
    m_minimumIconSize = int(s.intValue("minimumIconSize", m_minimumIconSize));
    m_gridIconSize = s.doubleValue("gridIconSize", m_gridIconSize);
    m_sidebarWidth = s.doubleValue("sidebarWidth", m_sidebarWidth);
    m_showHiddenFiles = s.boolValue("showHiddenFiles", m_showHiddenFiles);
    // Type sorting used to be stored as 6, which is not a model column.
    if (m_sortMode == 6)
        m_sortMode = Type;
}

Preferences::~Preferences() = default;

static std::string iconPositionsKey(const std::string& url)
{
    return "IconPositions/" + base::CryptographicHash::hashHex(url, base::CryptographicHash::Algorithm::Sha256);
}

static std::string viewOptionsKey(const std::string& url, int mode)
{
    return "ViewOptions/" + std::to_string(mode) + "/" + (url.empty() ? "default" :
            base::CryptographicHash::hashHex(url, base::CryptographicHash::Algorithm::Sha256));
}

std::optional<Preferences::ViewOptions> Preferences::storedViewOptions(const std::string& key) const
{
    if (const auto it = m_viewOptions.find(key); it != m_viewOptions.end())
        return it->second;
    const auto flags = m_settings->intValue(key + ".flags", -1);
    std::optional<ViewOptions> result;
    if (flags >= 0) {
        auto& value = result.emplace();
        value.flags = unsigned(flags);
        value.sort = std::clamp(int(m_settings->intValue(key + ".sort", Name)), int(Manual), int(AccessedTime));
        value.group = std::clamp(int(m_settings->intValue(key + ".group", NoGroup)), int(NoGroup), int(TagsGroup));
        value.descending = m_settings->boolValue(key + ".descending", false);
        const auto size = m_settings->doubleValue(key + ".iconSize", 64);
        value.iconSize = std::isfinite(size) ? std::clamp(size, 22.0, 256.0) : 64;
        value.spacing = std::clamp(int(m_settings->intValue(key + ".spacing", 10)), 4, 42);
        value.textSize = std::clamp(int(m_settings->intValue(key + ".textSize", 13)), 10, 18);
        value.background = m_settings->stringValue(key + ".background");
    }
    return m_viewOptions.emplace(key, std::move(result)).first->second;
}

Preferences::ViewOptions Preferences::viewOptions(const std::string& url, int mode) const
{
    if (!url.empty()) {
        if (const auto options = storedViewOptions(viewOptionsKey(url, mode)))
            return *options;
    }
    if (const auto options = storedViewOptions(viewOptionsKey({}, mode)))
        return *options;
    return {.flags = m_options | (1u << IconPreview) | (1u << ListPreview) | (1u << ColumnIcons)
                     | (1u << ColumnIconPreview) | (1u << ListRelativeDates),
            .sort = mode == Grid ? Manual : m_sortMode, .group = m_groupBy, .descending = m_sortDesc, .iconSize = m_gridIconSize,
            .textSize = mode == Grid ? 12 : 13, .background = {}};
}

void Preferences::storeViewOptions(const std::string& key, const ViewOptions& value)
{
    m_viewOptions[key] = value;
    if (!m_readOnly) {
        m_settings->setValue(key + ".flags", int(value.flags));
        m_settings->setValue(key + ".sort", value.sort);
        m_settings->setValue(key + ".group", value.group);
        m_settings->setValue(key + ".descending", value.descending);
        m_settings->setValue(key + ".iconSize", value.iconSize);
        m_settings->setValue(key + ".spacing", value.spacing);
        m_settings->setValue(key + ".textSize", value.textSize);
        m_settings->setValue(key + ".background", value.background);
        m_settings->sync();
    }
    changed.emit();
}

void Preferences::setViewOptions(const std::string& url, int mode, const ViewOptions& options)
{
    if (options != viewOptions(url, mode))
        storeViewOptions(viewOptionsKey(url, mode), options);
}

void Preferences::useViewDefaults(const std::string& url, int mode)
{
    storeViewOptions(viewOptionsKey({}, mode), viewOptions(url, mode));
}

static std::string pinnedViewKey(const std::string& url)
{
    return "PinnedViews/" + base::CryptographicHash::hashHex(url, base::CryptographicHash::Algorithm::Sha256);
}

int Preferences::pinnedView(const std::string& url) const
{
    if (url.empty())
        return -1;
    if (const auto it = m_pinnedViews.find(url); it != m_pinnedViews.end())
        return it->second.first;
    const auto key = pinnedViewKey(url);
    const int mode = std::clamp(int(m_settings->intValue(key + ".mode", -1)), -1, int(Gallery));
    const bool browse = mode >= 0 && m_settings->boolValue(key + ".browse", false);
    m_pinnedViews.emplace(url, std::pair{mode, browse});
    return mode;
}

bool Preferences::browseInPinnedView(const std::string& url) const
{
    return pinnedView(url) >= 0 && m_pinnedViews.at(url).second;
}

void Preferences::setPinnedView(const std::string& url, int mode, bool browse)
{
    if (url.empty() || mode < -1 || mode > Gallery)
        return;
    browse = mode >= 0 && browse;
    if (mode == pinnedView(url) && browse == browseInPinnedView(url))
        return;
    m_pinnedViews[url] = {mode, browse};
    if (!m_readOnly) {
        const auto key = pinnedViewKey(url);
        m_settings->setValue(key + ".mode", mode);
        m_settings->setValue(key + ".browse", browse);
        m_settings->sync();
    }
    changed.emit();
}

std::vector<std::string> Preferences::iconPositions(const std::string& url) const
{
    return m_settings->stringListValue(iconPositionsKey(url));
}

void Preferences::setIconPositions(const std::string& url, const std::vector<std::string>& positions)
{
    if (m_readOnly)
        return;
    const auto key = iconPositionsKey(url);
    m_settings->setValue(key, positions);
    m_settings->sync();
}

void Preferences::save()
{
    if (m_readOnly) {
        changed.emit();
        return;
    }
    base::Settings& s = *m_settings;
    s.setValue("viewMethod", m_viewMethod);
    s.setValue("groupBy", m_groupBy);
    s.setValue("browserOptions", int(m_options));
    s.setValue("toolbarDisplay", m_toolbarDisplay);
    s.setValue("toolbarOrder", m_toolbarOrder);
    s.setValue("sortMode", m_sortMode);
    s.setValue("sortDesc", m_sortDesc);
    s.setValue("trashSortMode", m_trashSortMode);
    s.setValue("trashSortDesc", m_trashSortDesc);
    s.setValue("listOriginColumnWidth", m_listOriginColumnWidth);
    s.setValue("listDateColumnWidth", m_listDateColumnWidth);
    s.setValue("listSizeColumnWidth", m_listSizeColumnWidth);
    s.setValue("listTypeColumnWidth", m_listTypeColumnWidth);
    s.setValue("maximumIconSize", m_maximumIconSize);
    s.setValue("minimumIconSize", m_minimumIconSize);
    s.setValue("gridIconSize", m_gridIconSize);
    s.setValue("sidebarWidth", m_sidebarWidth);
    s.setValue("showHiddenFiles", m_showHiddenFiles);
    s.sync();
    changed.emit();
}

void Preferences::setShowHiddenFiles(bool show)
{
    if (m_showHiddenFiles == show)
        return;
    m_showHiddenFiles = show;
    save();
}

void Preferences::setViewMethod(int method)
{
    if (method < List || method > Gallery || m_viewMethod == method)
        return;
    m_viewMethod = method;
    save();
}

const std::vector<std::string>& Preferences::defaultToolbar()
{
    static const std::vector<std::string> order {"views", "group", "share", "tags", "action", "search"};
    return order;
}

void Preferences::setGroupBy(int group)
{
    if (group < NoGroup || group > TagsGroup || group == m_groupBy)
        return;
    m_groupBy = group;
    save();
}

void Preferences::setOption(Option key, bool enabled)
{
    if (option(key) == enabled)
        return;
    m_options ^= (1u << key);
    save();
}

void Preferences::setToolbarDisplay(int display)
{
    if (display < 0 || display > 2 || display == m_toolbarDisplay)
        return;
    m_toolbarDisplay = display;
    save();
}

void Preferences::setToolbarOrder(std::vector<std::string> order)
{
    if (order == m_toolbarOrder)
        return;
    m_toolbarOrder = std::move(order);
    save();
}

void Preferences::setSort(int mode, bool descending)
{
    if (m_sortMode == mode && m_sortDesc == descending)
        return;
    m_sortMode = mode;
    m_sortDesc = descending;
    save();
}

void Preferences::setTrashSort(int mode, bool descending)
{
    if (m_trashSortMode == mode && m_trashSortDesc == descending)
        return;
    m_trashSortMode = mode;
    m_trashSortDesc = descending;
    save();
}

void Preferences::setListColumnWidths(double origin, double date, double size, double type)
{
    if (m_listOriginColumnWidth == origin && m_listDateColumnWidth == date && m_listSizeColumnWidth == size
        && m_listTypeColumnWidth == type)
        return;
    m_listOriginColumnWidth = origin;
    m_listDateColumnWidth = date;
    m_listSizeColumnWidth = size;
    m_listTypeColumnWidth = type;
    save();
}

void Preferences::setGridIconSize(double size)
{
    if (m_gridIconSize == size)
        return;
    m_gridIconSize = size;
    save();
}

void Preferences::setSidebarWidth(double width)
{
    if (m_sidebarWidth == width)
        return;
    m_sidebarWidth = width;
    save();
}

std::vector<std::string> Preferences::recentServers() const
{
    m_settings->reload();
    return m_settings->stringListValue("Network/recentServers");
}

void Preferences::addRecentServer(const std::string& address)
{
    std::vector<std::string> servers = recentServers();
    std::erase(servers, address);
    servers.insert(servers.begin(), address);
    if (servers.size() > kMaxRecentServers)
        servers.resize(kMaxRecentServers);
    m_settings->setValue("Network/recentServers", servers);
    m_settings->sync();
}

} // namespace fm
