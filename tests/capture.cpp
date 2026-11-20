// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

// Renders a browser window headless with its layout:
//   filemanager-capture OUT.png FOLDER [WIDTH HEIGHT SCALE [dark] [list|columns|gallery] [select=NAME...]]
#include "chooser/chooserwindow.h"
#include "chooser/filechooser.h"
#include "model/foldermodel.h"
#include "desktop/desktopview.h"
#include "views/folderwindow.h"
#include "views/pathbar.h"
#include "dialogs/folderappearancedialog.h"
#include "dialogs/gotofolder.h"
#include "dialogs/iotaskdialog.h"
#include "dialogs/openwithdialog.h"
#include "dialogs/serverdialogs.h"
#include "model/places.h"

#include "base/application_registry.h"
#include "base/event_loop.h"
#include "base/paths.h"
#include "base/timer.h"
#include "base/url.h"
#include "ui/application.h"
#include "ui/dialog_window.h"
#include "ui/test_input.h"
#include "ui/text_field.h"
#include "ui/theme.h"
#include "ui/toolbar.h"
#include "ui/window.h"

#include "core/preferences.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace cutefish;
using namespace std::chrono_literals;

namespace {

class Host : public fm::FileManagerHost
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

template<class T>
static T* findView(ui::View* root)
{
    if (auto* view = dynamic_cast<T*>(root))
        return view;
    for (const auto& child : root->children()) {
        if (auto* view = findView<T>(child.get()))
            return view;
    }
    return nullptr;
}

// The desktop's icons of ~/Desktop alone, transparent, under a 32 px bar.
static int captureDesktop(ui::Application& app, const std::string& output, double width, double height,
                          double scale, const std::vector<std::string>& select, bool lateWorkArea)
{
    const std::string path = base::paths::userDirectory(base::paths::UserDirectory::Desktop);
    fm::FolderModel model(nullptr, true);
    model.setSortMode(-1);
    model.setUrl(base::url::fromLocalFile(path));

    ui::Window window;
    window.setScale(scale);
    window.resize({width, height});
    window.setBackgroundColor(ui::Color {0, 0, 0, 0});
    auto view = std::make_unique<fm::DesktopGridView>(model);
    fm::DesktopGridView* raw = view.get();
    window.setContentView(std::move(view));
    // The compositor's work area comes after the first layout on a live session.
    if (!lateWorkArea)
        raw->setWorkArea({0, 32, width, height - 32});
    window.show();

    base::Timer poll;
    int ticks = 0;
    poll.onTimeout = [&] {
        if (model.status() == fm::FolderModel::Status::Ready || ++ticks > 200)
            app.quit();
    };
    poll.start(20ms);
    app.run();
    if (lateWorkArea) {
        window.grab();
        raw->setWorkArea({0, 32, width, height - 32});
    }
    for (const std::string& name : select)
        model.setSelected(model.rowForUrl(base::url::fromLocalFile(path + "/" + name)));
    window.grab();
    base::Timer settle;
    settle.setSingleShot(true);
    settle.onTimeout = [&] { app.quit(); };
    settle.start(1500ms);
    app.run();
    window.grab().savePng(output);
    std::ofstream(output + ".txt") << window.layoutDump();
    return 0;
}

static int captureDialog(ui::Application& app, const std::string& output, const std::string& folder,
                         double scale, const std::string& mode)
{
    auto save = [&](ui::Window* window) {
        window->setScale(scale);
        return window->grab().savePng(output) ? 0 : 1;
    };
    if (mode == "goto") {
        fm::GoToFolderDialog dialog(nullptr, base::url::fromLocalFile(folder));
        dialog.show();
        if (auto* field = findView<ui::TextField>(dialog.window()->contentView()))
            field->setText(folder + "/");
        return save(dialog.window());
    }
    if (mode == "openwith") {
        fm::OpenWithDialog dialog(folder + "/Notes.txt", "text/plain");
        dialog.show();
        auto& registry = base::ApplicationRegistry::instance();
        if (registry.isLoading()) {
            base::Timer timeout;
            timeout.setSingleShot(true);
            timeout.onTimeout = [&] { app.quit(); };
            auto loaded = registry.changed.connect([&] { app.quit(); });
            timeout.start(1500ms);
            app.run();
        }
        return save(dialog.window());
    }
    if (mode == "replace") {
        fm::ConflictDialog dialog;
        dialog.open("copy", 1, folder + "/Notes.txt", folder + "/Notes.txt");
        return save(dialog.window());
    }
    if (mode == "server" || mode == "password") {
        fm::PlacesModel places;
        if (mode == "password") {
            fm::ServerPasswordDialog dialog(nullptr, places, {.address = "sftp://example.net", .user = "koifish",
                                                            .anonymousAllowed = true});
            dialog.show();
            return save(dialog.window());
        }
        fm::ConnectServerDialog dialog(nullptr, places);
        dialog.show();
        if (auto* field = findView<ui::TextField>(dialog.window()->contentView()))
            field->setText("smb://server/share");
        return save(dialog.window());
    }
    if (mode == "confirm") {
        ui::ConfirmDialog dialog(nullptr, "Replace the existing file?",
                                 "A file with this name already exists. Replacing it cannot be undone.");
        dialog.setCancelText("Cancel");
        dialog.setConfirmText("Replace");
        dialog.setDestructive(true);
        dialog.show();
        dialog.window()->setScale(scale);
        return dialog.window()->grab().savePng(output) ? 0 : 1;
    }

    auto request = *base::json::Value::parse(R"({"current_name":"Notes.txt",
        "filters":[{"name":"Text documents","rules":[{"type":0,"value":"*.txt"}]}]})");
    request.insert("mode", mode == "save" ? "SaveFile" : "OpenFile");
    request.insert("current_folder", folder);
    fm::FileChooser chooser(std::move(request));
    fm::ChooserWindow dialog(chooser);
    dialog.window()->setScale(scale);
    dialog.show();
    base::Timer poll;
    int ticks = 0;
    poll.onTimeout = [&] {
        if (chooser.folder().status() == fm::FolderModel::Status::Ready || ++ticks > 200)
            app.quit();
    };
    poll.start(20ms);
    app.run();
    return dialog.window()->grab().savePng(output) ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s OUT.png FOLDER [WIDTH HEIGHT SCALE [dark] [list|columns|gallery] [dialog=open|save|confirm|goto|server|password|openwith|replace] [share] [tabs] [tab=PATH] [style=0..3] [tone=0..2] [tint=0..9] [glass=0..1] [search=QUERY] [select=NAME]]\n", argv[0]);
        return 1;
    }
    const std::string output = argv[1];
    const std::string folder = argv[2];
    const double width = argc > 3 ? std::atof(argv[3]) : 900;
    const double height = argc > 4 ? std::atof(argv[4]) : 580;
    const double scale = argc > 5 ? std::atof(argv[5]) : 1.5;
    bool dark = false;
    int mode = fm::Preferences::Grid;
    bool lateWorkArea = false;
    bool inspector = false;
    bool palette = false;
    bool options = false;
    bool share = false;
    int toolbarDisplay = 0;
    std::string appearance;
    std::string dialog;
    std::string branch;
    std::string search;
    bool tabs = false;
    std::vector<std::string> tabPaths;
    ui::AppearanceStyle style;
    std::vector<std::string> select;
    for (int i = 6; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "dark")
            dark = true;
        else if (argument == "list")
            mode = fm::Preferences::List;
        else if (argument == "columns")
            mode = fm::Preferences::Columns;
        else if (argument == "gallery")
            mode = fm::Preferences::Gallery;
        else if (argument == "late-work-area")
            lateWorkArea = true;
        else if (argument == "inspector")
            inspector = true;
        else if (argument == "palette")
            palette = true;
        else if (argument == "options")
            options = true;
        else if (argument == "share")
            share = true;
        else if (argument == "tabs")
            tabs = true;
        else if (argument.starts_with("tab="))
            tabPaths.push_back(argument.substr(4));
        else if (argument.starts_with("style="))
            style.icons = ui::appearance_style::iconStyle(std::atoi(argument.c_str() + 6));
        else if (argument.starts_with("tone="))
            style.tone = ui::appearance_style::iconTone(std::atoi(argument.c_str() + 5));
        else if (argument.starts_with("tint="))
            style.tintColor = ui::appearance_style::colorIndex(std::atoi(argument.c_str() + 5));
        else if (argument.starts_with("glass="))
            style.glass = ui::appearance_style::glassValue(std::atof(argument.c_str() + 6));
        else if (argument == "toolbar-labels")
            toolbarDisplay = 1;
        else if (argument == "toolbar-text")
            toolbarDisplay = 2;
        else if (argument.starts_with("appearance="))
            appearance = argument.substr(11);
        else if (argument.starts_with("dialog="))
            dialog = argument.substr(7);
        else if (argument.starts_with("branch="))
            branch = argument.substr(7);
        else if (argument.starts_with("search="))
            search = argument.substr(7);
        else if (argument.starts_with("select="))
            select.push_back(argument.substr(7));
    }

    ui::Application app(argc, argv, ui::Application::Platform::Headless);
    ui::Theme::setCurrent(dark ? ui::Theme::dark() : ui::Theme::light(), style);
    if (folder == "desktop")
        return captureDesktop(app, output, width, height, scale, select, lateWorkArea);
    fm::Preferences::instance().setReadOnly(true);
    fm::Preferences::instance().setViewMethod(mode);
    fm::Preferences::instance().setToolbarDisplay(toolbarDisplay);
    fm::Preferences::instance().setOption(fm::Preferences::PreviewPane, inspector);
    if (dialog == "open" || dialog == "save" || dialog == "confirm" || dialog == "goto"
        || dialog == "server" || dialog == "password" || dialog == "openwith" || dialog == "replace")
        return captureDialog(app, output, folder, scale, dialog);

    Host host;
    fm::FolderWindow window(&host, {folder, {}, {}, {}});
    if (tabs)
        window.openTab(base::url::fromLocalFile("/"));
    for (const auto& path : tabPaths)
        window.openTab(base::url::fromLocalFile(path));
    if (window.tabCount() > 1)
        window.setCurrentTab(0);
    window.window().setScale(scale);
    window.window().resize({width, height});
    window.show();
    if (!appearance.empty()) {
        auto dialog = fm::showFolderAppearance(&window.window(), folder + "/" + appearance, appearance);
        return dialog->window()->grab().savePng(output) ? 0 : 1;
    }

    base::Timer poll;
    int ticks = 0;
    poll.onTimeout = [&] {
        fm::FolderPage* page = window.currentPage();
        if ((page && page->model().status() == fm::FolderModel::Status::Ready && !page->model().searchBusy())
                || ++ticks > 200)
            app.quit();
    };
    poll.start(20ms);
    app.run();
    const auto directory = branch.empty() ? folder : folder + "/" + branch;
    if (!branch.empty() && window.currentPage()) {
        window.currentPage()->model().setUrl(base::url::fromLocalFile(directory));
        ticks = 0;
        app.run();
    }

    if (fm::FolderPage* page = window.currentPage()) {
        if (!search.empty()) {
            page->model().setSearchHome(false);
            page->model().setSearchText(search);
            if (auto* field = findView<fm::PathBar>(window.window().contentView()))
                field->openSearch();
            ticks = 0;
            app.run();
        }
        for (const std::string& name : select) {
            const int row = page->model().rowForUrl(base::url::fromLocalFile(directory + "/" + name));
            page->model().setSelected(row);
        }
    }
    if (palette)
        window.setToolbarEditing(true);
    if (options) {
        window.toggleViewOptions();
        auto* panel = window.viewOptionsWindow();
        if (!panel) return 1;
        panel->setScale(scale);
        std::ofstream(output + ".txt") << panel->layoutDump();
        return panel->grab().savePng(output) ? 0 : 1;
    }

    // A headless window lays out only when grabbed; what it asks for then
    // (thumbnails, folder sizes) arrives from workers.
    window.window().grab();
    if (share) {
        auto* toolbar = findView<ui::Toolbar>(window.window().contentView());
        auto* button = toolbar ? toolbar->itemView("share") : nullptr;
        if (!button)
            return 1;
        ui::TestInput::click(window.window(), button->mapToWindow({button->localRect().width / 2, button->localRect().height / 2}));
        auto* popup = window.sharePopupWindow();
        if (!popup)
            return 1;
        popup->setScale(scale);
    }
    base::Timer settle;
    settle.setSingleShot(true);
    settle.onTimeout = [&] { app.quit(); };
    settle.start(1500ms);
    app.run();

    if (share) {
        auto* popup = window.sharePopupWindow();
        if (!popup)
            return 1;
        const auto image = popup->grab();
        std::ofstream(output + ".txt") << popup->layoutDump();
        return image.savePng(output) ? 0 : 1;
    }
    window.window().grab().savePng(output);
    std::ofstream(output + ".txt") << window.window().layoutDump();
    return 0;
}
