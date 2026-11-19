// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "core/common.h"

#include "base/signal.h"

#include <memory>
#include <string>
#include <vector>

namespace cutefish::base {
class Settings;
}

namespace fm {

// The browser's settings in ~/.config/cutefish/filemanager.conf, which this
// process alone writes; every window shares one instance.
class Preferences
{
public:
    enum ViewMethod {
        List = 0,
        Grid = 1,
        Columns = 2,
        Gallery = 3,
    };

    // Sort columns, the directory model's.
    enum SortColumn {
        Name = 0,
        Size = 1,
        ModifiedTime = 2,
        Type = 3,
        CreatedTime = 4,
        AccessedTime = 5,
        OriginalPath = 7,
        DeletionTime = 8,
    };

    enum GroupBy { NoGroup, KindGroup, NameGroup, ModifiedGroup, CreatedGroup, ApplicationGroup, SizeGroup, TagsGroup };
    enum Option { PathBar, StatusBar, PreviewPane, ColumnPreview, ColumnFit, FoldersFirst,
                  ListDate, ListSize, ListKind, ListCreated, ListAccessed, GalleryNames,
                  SidebarFavorites, SidebarLocations, SidebarTags };

    static Preferences& instance();
    // For tests: another file.
    explicit Preferences(const std::string& path);
    ~Preferences();

    int viewMethod() const { return m_viewMethod; }
    void setViewMethod(int method);
    int groupBy() const { return m_groupBy; }
    void setGroupBy(int group);
    bool option(Option key) const { return m_options & (1u << key); }
    void setOption(Option key, bool enabled);
    int toolbarDisplay() const { return m_toolbarDisplay; }
    void setToolbarDisplay(int display);
    const std::vector<std::string>& toolbarOrder() const { return m_toolbarOrder; }
    void setToolbarOrder(std::vector<std::string> order);
    static const std::vector<std::string>& defaultToolbar();
    std::vector<std::string> iconPositions(const std::string& url) const;
    void setIconPositions(const std::string& url, const std::vector<std::string>& positions);

    int sortMode() const { return m_sortMode; }
    bool sortDesc() const { return m_sortDesc; }
    void setSort(int mode, bool descending);
    int trashSortMode() const { return m_trashSortMode; }
    bool trashSortDesc() const { return m_trashSortDesc; }
    void setTrashSort(int mode, bool descending);

    double listOriginColumnWidth() const { return m_listOriginColumnWidth; }
    double listDateColumnWidth() const { return m_listDateColumnWidth; }
    double listSizeColumnWidth() const { return m_listSizeColumnWidth; }
    double listTypeColumnWidth() const { return m_listTypeColumnWidth; }
    void setListColumnWidths(double origin, double date, double size, double type);

    int maximumIconSize() const { return m_maximumIconSize; }
    int minimumIconSize() const { return m_minimumIconSize; }
    double gridIconSize() const { return m_gridIconSize; }
    void setGridIconSize(double size);

    // The width new windows open their sidebar at.
    double sidebarWidth() const { return m_sidebarWidth; }
    void setSidebarWidth(double width);

    // In every folder, the desktop's included.
    bool showHiddenFiles() const { return m_showHiddenFiles; }
    void setShowHiddenFiles(bool show);

    // [Network] recentServers, newest first, at most ten.
    std::vector<std::string> recentServers() const;
    void addRecentServer(const std::string& address);

    // Any of the above changed.
    // Changes stay in memory, the file untouched (tools and tests).
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }

    base::Signal<> changed;

private:
    void save();

    std::unique_ptr<base::Settings> m_settings;
    bool m_readOnly = false;
    int m_viewMethod = Grid;
    int m_groupBy = NoGroup;
    unsigned m_options = (1u << ColumnPreview) | (1u << FoldersFirst) | (1u << ListDate) | (1u << ListSize)
            | (1u << ListKind) | (1u << GalleryNames) | (1u << SidebarFavorites) | (1u << SidebarLocations) | (1u << SidebarTags);
    int m_toolbarDisplay = 0;
    std::vector<std::string> m_toolbarOrder = defaultToolbar();
    int m_sortMode = Name;
    bool m_sortDesc = false;
    int m_trashSortMode = DeletionTime;
    bool m_trashSortDesc = true;
    double m_listOriginColumnWidth = 200;
    double m_listDateColumnWidth = 180;
    double m_listSizeColumnWidth = 96;
    double m_listTypeColumnWidth = 150;
    int m_maximumIconSize = 256;
    int m_minimumIconSize = 64;
    double m_gridIconSize = 64;
    double m_sidebarWidth = 190;
    bool m_showHiddenFiles = false;
};

} // namespace fm
