// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "testutil.h"

#include "core/preferences.h"
#include "model/foldermodel.h"
#include "views/folderpage.h"
#include "views/folderview.h"
#include "views/folderwindow.h"
#include "views/gridview.h"
#include "views/listview.h"
#include "views/renameeditor.h"

#include "base/url.h"
#include "ui/application.h"
#include "ui/menu.h"
#include "ui/test_input.h"
#include "ui/text_area.h"
#include "ui/window.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace cutefish;
using fmtest::runUntil;

extern ui::Application* s_app;

namespace {

class WindowHost : public fm::FileManagerHost
{
public:
    fm::FolderWindow* openFolderWindow(const fm::FolderPage::Start&, ui::Window*) override { return nullptr; }
    void openFolders(const std::vector<std::string>&) override {}
    void showProperties(const fm::FileItemList&, ui::Window*) override {}
    void showOpenWith(const std::string&, ui::Window*) override {}
    void confirmDelete(const std::vector<std::string>&, ui::Window*) override {}
    void confirmEmptyTrash(ui::Window*) override {}
    void confirmLaunch(const std::string&, ui::Window*) override {}
    void installBundleRunner(const std::string&, ui::Window*) override {}
    void windowClosed(fm::FolderWindow*) override {}
};

} // namespace

static ui::View* findView(ui::View* view, const std::string& name)
{
    if (view->name() == name)
        return view;
    for (const auto& child : view->children()) {
        if (ui::View* found = findView(child.get(), name))
            return found;
    }
    return nullptr;
}

static void dragBy(ui::Window& window, ui::PointF from, ui::PointF delta)
{
    const ui::PointF to = {from.x + delta.x, from.y + delta.y};
    ui::TestInput::movePointer(window, from);
    ui::TestInput::press(window, from);
    ui::TestInput::movePointer(window, to);
    ui::TestInput::release(window, to);
}

// A maximized window still moves from the path bar, as from the rest of the
// header; a click there still opens the editor.
TEST(HeaderTest, PathBarDragsMoveTheWindow)
{
    base::EventLoop& loop = s_app->loop();
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::filesystem::create_directories(directory.filePath("a/b"));
    const std::string url = base::url::fromLocalFile(directory.filePath("a/b"));

    WindowHost host;
    fm::FolderPage::Start start;
    start.url = url;
    fm::FolderWindow folderWindow(&host, start);
    ui::Window& window = folderWindow.window();
    folderWindow.show();
    fm::FolderModel& model = folderWindow.currentPage()->model();
    ASSERT_TRUE(runUntil(loop, [&] { return model.status() == fm::FolderModel::Status::Ready; }));
    window.setMaximized(true);
    window.grab();
    ASSERT_TRUE(window.states().maximized);

    ui::View* pathBar = findView(window.contentView(), "pathBar");
    ui::View* editor = findView(window.contentView(), "pathEditor");
    ASSERT_NE(pathBar, nullptr);
    ASSERT_NE(editor, nullptr);
    const double middle = pathBar->geometry().height / 2;
    const ui::PointF empty = pathBar->mapToWindow({pathBar->geometry().width - 10, middle});
    const ui::PointF crumb = pathBar->mapToWindow({20, middle});

    dragBy(window, empty, {0, 20});
    EXPECT_EQ(ui::TestInput::moveRequests(window), 1);
    window.grab();
    EXPECT_FALSE(editor->isVisible());

    dragBy(window, crumb, {30, 0});
    EXPECT_EQ(ui::TestInput::moveRequests(window), 2);
    fmtest::runFor(loop, 100);
    EXPECT_EQ(folderWindow.currentPage()->currentUrl(), directory.filePath("a/b"));

    ui::TestInput::click(window, empty);
    window.grab();
    EXPECT_TRUE(editor->isVisible());
    EXPECT_EQ(ui::TestInput::moveRequests(window), 2);
}

// A window can go in the turn its tabs changed: what the tab bar left for the
// next turn must not reach it.
TEST(HeaderTest, WindowGoesRightAfterItsTabsChange)
{
    base::EventLoop& loop = s_app->loop();
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::string url = base::url::fromLocalFile(directory.path());

    WindowHost host;
    fm::FolderPage::Start start;
    start.url = url;
    auto folderWindow = std::make_unique<fm::FolderWindow>(&host, start);
    folderWindow->show();
    fmtest::runFor(loop, 50);

    folderWindow->openTab(url);
    EXPECT_EQ(folderWindow->tabCount(), 2);
    folderWindow.reset();
    fmtest::runFor(loop, 50);
}

TEST(HeaderTest, WindowShowsItsMenusInTheMenuBar)
{
    base::EventLoop& loop = s_app->loop();
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    WindowHost host;
    fm::FolderPage::Start start;
    start.url = base::url::fromLocalFile(directory.path());
    auto folderWindow = std::make_unique<fm::FolderWindow>(&host, start);
    folderWindow->show();
    fmtest::runFor(loop, 50);

    ui::Menu* menu = folderWindow->window().mainMenu();
    ASSERT_TRUE(menu);
    const std::vector<ui::MenuRole> roles {ui::MenuRole::Application, ui::MenuRole::None, ui::MenuRole::None,
                                           ui::MenuRole::None, ui::MenuRole::None, ui::MenuRole::Windows};
    for (size_t i = 0; i < roles.size(); ++i) {
        ASSERT_TRUE(menu->submenu(i)) << i;
        EXPECT_EQ(menu->submenu(i)->role(), roles[i]) << i;
    }
    EXPECT_FALSE(menu->submenu(roles.size()));
    EXPECT_FALSE(findView(folderWindow->window().contentView(), "optionsButton"));

    // Opening a menu fills its rows from the folder's actions.
    ui::Menu* file = menu->submenu(1);
    ASSERT_TRUE(file->onAboutToShow);
    file->onAboutToShow();
    folderWindow.reset();
    fmtest::runFor(loop, 50);
}

TEST(HeaderTest, FourViewsKeepTheOpenFolderAndSelection)
{
    struct RestoreView {
        int saved = fm::Preferences::instance().viewMethod();
        ~RestoreView() { fm::Preferences::instance().setViewMethod(saved); }
    } restore;
    base::EventLoop& loop = s_app->loop();
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::filesystem::create_directories(directory.filePath("branch"));
    ASSERT_TRUE(fmtest::writeFile(directory.filePath("branch/notes.txt")));
    const auto selected = base::url::fromLocalFile(directory.filePath("branch/notes.txt"));
    WindowHost host;
    fm::FolderPage::Start start;
    start.url = directory.path();
    fm::FolderWindow folderWindow(&host, start);
    folderWindow.show();
    auto* page = folderWindow.currentPage();
    auto& model = page->model();
    page->openUrl(directory.filePath("branch"));
    ASSERT_TRUE(runUntil(loop, [&] { return model.rowForUrl(selected) >= 0; }));
    model.setSelected(model.rowForUrl(selected));

    for (const auto mode : {fm::Preferences::List, fm::Preferences::Columns, fm::Preferences::Gallery, fm::Preferences::Grid}) {
        fm::Preferences::instance().setViewMethod(mode);
        folderWindow.window().grab();
        EXPECT_TRUE(page->view());
        EXPECT_EQ(page->currentUrl(), directory.filePath("branch"));
        EXPECT_EQ(model.selectedUrls(), std::vector<std::string>{selected});
        EXPECT_TRUE(model.canGoBack());
    }
}

TEST(HeaderTest, ListExpansionDoesNotNavigate)
{
    base::EventLoop& loop = s_app->loop();
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::filesystem::create_directories(directory.filePath("branch"));
    ASSERT_TRUE(fmtest::writeFile(directory.filePath("branch/notes.txt")));
    const auto branch = base::url::fromLocalFile(directory.filePath("branch"));
    const auto notes = base::url::fromLocalFile(directory.filePath("branch/notes.txt"));
    fm::FolderModel model;
    model.setUrl(directory.path());
    ASSERT_TRUE(runUntil(loop, [&] { return model.rowForUrl(branch) >= 0; }));
    model.contents().toggleExpansion(branch, false);
    ASSERT_TRUE(runUntil(loop, [&] { return model.rowForUrl(notes) >= 0; }));
    EXPECT_EQ(model.contents().depth(model.rowForUrl(notes)), 1);
    EXPECT_FALSE(model.canGoBack());
    model.contents().toggleExpansion(branch, false);
    EXPECT_LT(model.rowForUrl(notes), 0);
    EXPECT_GE(model.rowForUrl(branch), 0);
}

TEST(HeaderTest, ViewOptionsSwitchModesAndApplyToTheCurrentFolder)
{
    auto& prefs = fm::Preferences::instance();
    const int originalMode = prefs.viewMethod();
    struct Restore { fm::Preferences& prefs; int mode; ~Restore() { prefs.setViewMethod(mode); } } restore{prefs, originalMode};
    prefs.setViewMethod(fm::Preferences::Grid);
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::filesystem::create_directory(directory.filePath("next"));
    const auto root = base::url::fromLocalFile(directory.path());
    const auto next = base::url::fromLocalFile(directory.filePath("next"));
    WindowHost host;
    fm::FolderWindow window(&host, {root, {}, {}, {}});
    window.show();
    window.toggleViewOptions();
    auto* panel = window.viewOptionsWindow();
    ASSERT_NE(panel, nullptr);
    panel->grab();
    EXPECT_NE(panel->layoutDump().find("viewOptionsPanel"), std::string::npos);
    const auto gridHeight = panel->size().height;
    auto* page = window.currentPage();
    auto options = page->viewOptions();
    options.iconSize = 128;
    options.spacing = 32;
    options.setOption(fm::Preferences::IconRight, true);
    page->setViewOptions(options);
    window.window().grab();
    auto* grid = dynamic_cast<fm::FolderGridView*>(page->view());
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->iconSize(), 128);
    EXPECT_EQ(grid->options().spacing, 32);
    EXPECT_TRUE(grid->options().option(fm::Preferences::IconRight));
    prefs.setViewMethod(fm::Preferences::List);
    ASSERT_TRUE(runUntil(s_app->loop(), [&] { panel->grab(); return panel->size().height != gridHeight; }));
    EXPECT_EQ(window.viewOptionsWindow(), panel);
    auto listOptions = page->viewOptions();
    listOptions.setOption(fm::Preferences::ListCreated, true);
    page->setViewOptions(listOptions);
    window.window().grab();
    auto* list = dynamic_cast<fm::FolderListView*>(page->view());
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->columns().created);
    for (const auto mode : {fm::Preferences::Columns, fm::Preferences::Gallery, fm::Preferences::Grid}) {
        prefs.setViewMethod(mode);
        fmtest::runFor(s_app->loop(), 20);
        EXPECT_EQ(window.viewOptionsWindow(), panel);
        EXPECT_EQ(page->viewMethod(), mode);
    }
    page->openUrl(next);
    EXPECT_NE(page->viewOptions().iconSize, 128);
    page->goBack();
    EXPECT_EQ(page->viewOptions().iconSize, 128);
    ASSERT_TRUE(panel->onCloseRequested);
    panel->onCloseRequested();
    EXPECT_EQ(window.viewOptionsWindow(), nullptr);
}

TEST(HeaderTest, PinnedViewIsUsedOnNavigationAndCanExtendToDescendants)
{
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::filesystem::create_directories(directory.filePath("branch/sub"));
    const auto root = base::url::fromLocalFile(directory.path());
    const auto branch = base::url::fromLocalFile(directory.filePath("branch"));
    const auto sub = base::url::fromLocalFile(directory.filePath("branch/sub"));
    auto& prefs = fm::Preferences::instance();
    const int pinned = prefs.viewMethod() == fm::Preferences::Columns ? fm::Preferences::List : fm::Preferences::Columns;
    prefs.setPinnedView(branch, pinned, true);
    WindowHost host;
    fm::FolderWindow window(&host, {branch, {}, {}, {}});
    auto* page = window.currentPage();
    EXPECT_EQ(page->viewMethod(), pinned);
    page->openUrl(sub);
    EXPECT_EQ(page->viewMethod(), pinned);
    page->openUrl(root);
    EXPECT_EQ(page->viewMethod(), prefs.viewMethod());
    page->goBack();
    EXPECT_EQ(page->viewMethod(), pinned);
}

TEST(HeaderTest, ViewDefaultsPersistAndDoNotAffectOtherModes)
{
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("preferences.conf");
    {
        fm::Preferences prefs(path);
        auto options = prefs.viewOptions("file:///first", fm::Preferences::Grid);
        options.spacing = 32;
        options.sort = fm::Preferences::ModifiedTime;
        options.group = fm::Preferences::KindGroup;
        prefs.setViewOptions("file:///first", fm::Preferences::Grid, options);
        EXPECT_NE(prefs.viewOptions("file:///second", fm::Preferences::Grid), options);
        prefs.useViewDefaults("file:///first", fm::Preferences::Grid);
        EXPECT_EQ(prefs.viewOptions("file:///second", fm::Preferences::Grid), options);
    }
    fm::Preferences prefs(path);
    EXPECT_EQ(prefs.viewOptions("file:///second", fm::Preferences::Grid).spacing, 32);
    EXPECT_EQ(prefs.viewOptions("file:///first", fm::Preferences::Grid).group, fm::Preferences::KindGroup);
    EXPECT_EQ(prefs.viewOptions("file:///second", fm::Preferences::List).sort, fm::Preferences::Name);
}

// A new session language takes the text again where the user was: the tabs,
// the folder and its history, the selection and an open rename with its text.
TEST(HeaderTest, LanguageChangeKeepsWhereTheUserWas)
{
    base::EventLoop& loop = s_app->loop();
    fmtest::TemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    std::filesystem::create_directories(directory.filePath("a"));
    ASSERT_TRUE(fmtest::writeFile(directory.filePath("a/notes.txt")));
    ASSERT_TRUE(fmtest::writeFile(directory.filePath("a/other.txt")));
    const std::string other = base::url::fromLocalFile(directory.filePath("a/other.txt"));

    WindowHost host;
    fm::FolderPage::Start start;
    start.url = base::url::fromLocalFile(directory.path());
    fm::FolderWindow folderWindow(&host, start);
    ui::Window& window = folderWindow.window();
    folderWindow.show();
    folderWindow.openTab(start.url);
    folderWindow.setCurrentTab(0);
    fm::FolderPage* page = folderWindow.currentPage();
    fm::FolderModel& model = page->model();
    page->openUrl(directory.filePath("a"));
    ASSERT_TRUE(runUntil(loop, [&] { return model.status() == fm::FolderModel::Status::Ready && model.count() == 2; }));
    window.grab();
    const int row = model.rowForUrl(other);
    ASSERT_GE(row, 0);
    fm::FolderView* view = page->view();
    view->setCurrentIndex(row);
    model.setSelected(row);
    window.grab();
    view->rename();
    ASSERT_TRUE(view->isRenaming());
    auto* editor = static_cast<fm::RenameEditor*>(findView(view, "renameEditor"));
    ASSERT_NE(editor, nullptr);
    auto* field = static_cast<ui::TextArea*>(editor->children().front().get());
    field->setText("draft.txt");

    ui::Application::instance()->languageChanged.emit();
    fmtest::runFor(loop, 100);
    ASSERT_TRUE(runUntil(loop, [&] { return model.status() == fm::FolderModel::Status::Ready; }));
    window.grab();

    EXPECT_EQ(folderWindow.tabCount(), 2);
    EXPECT_EQ(folderWindow.currentTab(), 0);
    EXPECT_EQ(folderWindow.currentPage(), page);
    EXPECT_EQ(page->currentUrl(), directory.filePath("a"));
    EXPECT_TRUE(model.canGoBack());
    EXPECT_EQ(model.count(), 2);
    EXPECT_EQ(model.selectedUrls(), std::vector<std::string> {other});
    EXPECT_EQ(page->view(), view);
    EXPECT_TRUE(view->isRenaming());
    EXPECT_EQ(findView(view, "renameEditor"), editor);
    EXPECT_EQ(field->text(), "draft.txt");
}
