// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "core/common.h"
#include "model/foldermodel.h"
#include "views/folderpage.h"

#include "base/signal.h"
#include "base/timer.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cutefish::ui {
class AboutDialog;
class DropDownPopup;
class HeaderButton;
class Menu;
class Toast;
class Window;
class WindowTabBar;
}

namespace fm {

class FolderWindowRoot;
class GoToFolderDialog;
class PathBar;
class SideBar;

// What a browser window asks of the process: other windows and the dialogs.
class FileManagerHost
{
public:
    virtual ~FileManagerHost() = default;

    // Shown at once; with `from`, at its size rather than the one restored.
    virtual class FolderWindow* openFolderWindow(const FolderPage::Start& start, ui::Window* from) = 0;
    virtual void openFolders(const std::vector<std::string>& paths) = 0;
    virtual void showProperties(const FileItemList& items, ui::Window* parent) = 0;
    virtual void showOpenWith(const std::string& url, ui::Window* parent) = 0;
    virtual void confirmDelete(const std::vector<std::string>& urls, ui::Window* parent) = 0;
    virtual void confirmEmptyTrash(ui::Window* parent) = 0;
    virtual void confirmLaunch(const std::string& path, ui::Window* parent) = 0;
    virtual void installBundleRunner(const std::string& path, ui::Window* parent) = 0;
    virtual void windowClosed(class FolderWindow* window) = 0;
};

// The browser window. The sidebar, the header with back, forward,
// the path bar and search, the tab bar, the search scope and
// trash bars over the pages, which run up under them.
class FolderWindow : public FolderModelHost
{
public:
    // An empty size restores the last window's.
    FolderWindow(FileManagerHost* host, const FolderPage::Start& start, ui::SizeF size = {});
    ~FolderWindow() override;

    ui::Window& window() const { return *m_window; }
    FolderPage* currentPage() const;
    int tabCount() const { return int(m_tabs.size()); }
    int currentTab() const { return m_currentTab; }

    void openTab(const std::string& url);
    void openTabSelecting(const FolderPage::Start& start, int index = -1);
    void closeTab(int index);
    void setCurrentTab(int index);
    void moveTab(int from, int to);
    void show();
    void close();
    void setToolbarEditing(bool editing);
    void toggleViewOptions();
    ui::Window* viewOptionsWindow() const;
    ui::Window* sharePopupWindow() const;

    // FolderModelHost.
    void openFolders(const std::vector<std::string>& paths) override;
    void showProperties(const FileItemList& items) override;
    void showOpenWith(const std::string& url) override;
    void confirmDelete(const std::vector<std::string>& urls) override;
    void confirmEmptyTrash() override;
    void confirmLaunch(const std::string& path) override;
    void installBundleRunner(const std::string& path) override;
    void notify(const std::string& message) override;

    // The header leaves room around the toolbar capsules, independent of the tab track.
    static constexpr double kHeaderInset = 46;
    static constexpr double kTabBarHeight = 34;

private:
    friend class FolderWindowRoot;

    struct Tab
    {
        FolderPage* page;
        std::vector<base::Subscription> subscriptions;
    };

    void insertTab(Tab tab, int index);
    void detachTab(int index, ui::PointF position);
    void adoptTab(FolderWindow* source, int index);
    void setIncoming(bool incoming);
    void showTabMenu(int index, ui::PointF position);
    void syncNavigation();
    void updateHeader();
    void retranslate();
    void updateBackground();
    void updateBlur();
    void updateTabBar();
    void showContextMenu(ui::PointF position, bool fromToolbar = false);
    void toolbarAction(const std::string& id, ui::PointF position);
    void showSharePopup();
    void populateGrouping(ui::Menu& menu);
    void populateSidebarSettings(ui::Menu& menu);
    // The menus the menu bar shows while the window is active.
    void buildMainMenu();
    void showAbout();
    double headerHeight() const;
    void addShortcuts();
    void goToFolder();
    void selectView(int mode);

    FileManagerHost* m_host;
    // The window shows it until the window goes, so it outlives the window.
    std::unique_ptr<ui::Menu> m_mainMenu;
    std::unique_ptr<ui::Window> m_window;
    FolderWindowRoot* m_root = nullptr;
    SideBar* m_sidebar = nullptr;
    std::unique_ptr<GoToFolderDialog> m_goToFolder;
    PathBar* m_pathBar = nullptr;
    ui::WindowTabBar* m_tabBar = nullptr;
    ui::HeaderButton* m_back = nullptr;
    ui::HeaderButton* m_forward = nullptr;
    std::vector<Tab> m_tabs;
    int m_currentTab = 0;
    // A tab pulled out into the window it is carried in, kept until the drag
    // ends so that Escape or a drop back here puts it back as it was.
    std::optional<Tab> m_detachedTab;
    int m_detachedIndex = -1;
    FolderWindow* m_detachedWindow = nullptr;
    bool m_detaching = false;
    bool m_incoming = false;
    bool m_closed = false;
    std::unique_ptr<ui::Menu> m_menu;
    std::unique_ptr<ui::DropDownPopup> m_sharePopup;
    std::unique_ptr<class FramelessDialog> m_folderAppearance;
    std::unique_ptr<class ViewOptionsDialog> m_viewOptions;
    std::unique_ptr<ui::Toast> m_toast;
    std::unique_ptr<ui::AboutDialog> m_aboutDialog;
    std::unique_ptr<class ArchivePasswordPrompt> m_archivePassword;
    std::vector<base::Subscription> m_pageSubscriptions;
    std::vector<base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace fm
