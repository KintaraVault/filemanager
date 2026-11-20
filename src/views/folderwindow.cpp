// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "views/colors.h"
#include "views/folderwindow.h"
#include "dialogs/viewoptionsdialog.h"
#include "core/preferences.h"
#include "core/filetags.h"
#include "views/folderview.h"
#include "dialogs/archivepasswordprompt.h"
#include "views/foldermenu.h"
#include "dialogs/folderappearancedialog.h"
#include "views/gridview.h"
#include "views/pathbar.h"
#include "views/sharepanel.h"
#include "views/sidebar.h"
#include "dialogs/gotofolder.h"
#include "views/style.h"

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/url.h"
#include "ui/about_dialog.h"
#include "ui/application.h"
#include "ui/color_provider.h"
#include "ui/combo_box.h"
#include "ui/drop_down_popup.h"
#include "ui/progress_bar.h"
#include "ui/dialog_window.h"
#include "ui/header_button.h"
#include "ui/header_button_group.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/menu.h"
#include "ui/navigation_buttons.h"
#include "ui/painter.h"
#include "ui/shortcut_convention.h"
#include "ui/sidebar_divider.h"
#include "ui/segmented_control.h"
#include "ui/cell_layout.h"
#include "ui/theme.h"
#include "ui/toast.h"
#include "ui/toolbar.h"
#include "ui/toolbar_surface.h"
#include "ui/drag.h"
#include "ui/window.h"
#include "ui/window_controls.h"
#include "ui/window_header.h"
#include "ui/window_tab_bar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

namespace fm {

using ui::rounding::centeredStart;
using cutefish::i18n::format;
using cutefish::i18n::tr;

static std::vector<std::string> toolbarDisplays()
{
    return {tr("Icon Only"), tr("Icon and Text"), tr("Text Only")};
}
using cutefish::i18n::trNoop;

static constexpr double kSmallSpacing = 6;
static constexpr double kLargeSpacing = 12;
static constexpr double kButtonSize = 32;
static constexpr double kHeaderEndMargin = 19.0 / 3;
// From the sidebar's edge to the back and forward plate.
static constexpr double kNavigationStart = 7;
static constexpr double kNavigationTitleGap = 10;
static constexpr ui::SizeF kNavigationPlate{71, 34};
static constexpr double kBarHeight = 34;
static constexpr double kScopeBarHeight = 29;
static constexpr double kMinimumWidth = 600;
static constexpr double kMinimumContentWidth = kMinimumWidth - SideBar::kDefaultWidth;
static constexpr double kNewTabButtonSize = 28;
// From the track's end and from the window's edge to the button.
static constexpr double kNewTabStartGap = 2;
static constexpr double kNewTabEndMargin = 10;
static constexpr const char* kTabMimeType = "application/x-cutefish-filemanager-tab";

static constexpr std::array<int, 4> kViews {Preferences::Grid, Preferences::List, Preferences::Columns, Preferences::Gallery};
static std::vector<std::string> viewNames()
{
    return {tr("Icons"), tr("List"), tr("Columns"), tr("Gallery")};
}

static int viewIndex(int mode)
{
    return int(std::find(kViews.begin(), kViews.end(), mode) - kViews.begin());
}

// Where a search looks, home or the folder it started from.
class SearchScopeBar : public ui::View
{
public:
    SearchScopeBar()
    {
        setName("searchScopeBar");
        m_label = addChild<ui::Label>(tr("Search:"));
        m_home = addChild<ui::Label>(tr("Home"));
        m_folder = addChild<ui::Label>();
        m_folder->setElide(ui::Label::Elide::Middle);
        for (auto* label : {m_label, m_home, m_folder}) {
            label->setFontSize(11.5);
            label->setWeight(650);
        }
        m_busy = addChild<ui::BusyIndicator>();
    }

    void retranslate()
    {
        m_label->setText(tr("Search:"));
        m_home->setText(tr("Home"));
        requestLayout();
    }

    void setModel(FolderModel* model, const std::string& folderName)
    {
        m_model = model;
        // TRANSLATORS: the folder a search looks in, in quotation marks.
        m_folder->setText(format(tr("“{name}”"), {{"name", folderName}}));
        requestLayout();
        requestPaint();
    }

    bool acceptsPointer() const override { return true; }

protected:
    bool folderIsHome() const
    {
        return m_model && m_model->resolvedUrl() == FolderModel::resolve("~");
    }

    void layout() override
    {
        const ui::Theme& theme = ui::Theme::current();
        const double height = geometry().height;
        double x = 10;
        m_label->setColor(theme.disabledText);
        const ui::SizeF label = m_label->sizeHint();
        m_label->setGeometry({x, centeredStart(height, label.height), std::ceil(label.width), label.height});
        x += std::ceil(label.width) + kSmallSpacing;

        const bool home = folderIsHome() || (m_model && m_model->searchHome());
        const bool folder = m_model && !m_model->searchHome();
        m_homeRect = place(m_home, x, home);
        x = m_homeRect.x + m_homeRect.width + kSmallSpacing;
        m_folder->setVisible(!folderIsHome());
        if (m_folder->isVisible())
            m_folderRect = place(m_folder, x, folder, 240);
        else
            m_folderRect = {};

        m_busy->setVisible(m_model && m_model->searchBusy());
        m_busy->setGeometry({geometry().width - kLargeSpacing - 16, centeredStart(height, 16), 16, 16});
    }

    ui::RectF place(ui::Label* label, double x, bool selected, double maximum = 0)
    {
        const ui::Theme& theme = ui::Theme::current();
        const double height = geometry().height;
        const ui::SizeF hint = label->sizeHint();
        constexpr double inset = 9;
        constexpr double scopeHeight = 18;
        double width = std::ceil(hint.width + inset * 2);
        if (maximum > 0)
            width = std::min(width, maximum);
        label->setColor(selected ? theme.text : theme.disabledText);
        const double textWidth = std::min(hint.width, width - inset * 2);
        const ui::RectF rect {x, centeredStart(height, scopeHeight), width, scopeHeight};
        label->setGeometry({rect.x + centeredStart(width, textWidth), rect.y + centeredStart(scopeHeight, hint.height),
                            textWidth, hint.height});
        return rect;
    }

    void paint(ui::Painter& painter) override
    {
        painter.drawHorizontalLine(0, geometry().height - 1, geometry().width, style::textAlpha(.08));
        const auto plate = [&](const ui::RectF& rect, bool selected, int index) {
            ui::Color fill {0, 0, 0, 0};
            if (selected)
                fill = ui::color(FmColor::SearchScopeSelected);
            else if (m_pressed == index)
                fill = style::textAlpha(0.1);
            else if (m_hovered == index)
                fill = style::textAlpha(0.05);
            if (fill.a)
                painter.drawSmoothRectangle(rect, 4, fill);
        };
        plate(m_homeRect, folderIsHome() || (m_model && m_model->searchHome()), 0);
        if (m_folder->isVisible())
            plate(m_folderRect, m_model && !m_model->searchHome(), 1);
    }

    int scopeAt(ui::PointF point) const
    {
        if (m_homeRect.contains(point))
            return 0;
        if (m_folder->isVisible() && m_folderRect.contains(point))
            return 1;
        return -1;
    }

    void hoverMove(const ui::PointerEvent& event) override
    {
        const int hovered = scopeAt(event.position);
        if (hovered != m_hovered) {
            m_hovered = hovered;
            requestPaint();
        }
    }
    void pointerMove(const ui::PointerEvent& event) override { hoverMove(event); }
    void pointerLeave() override
    {
        m_hovered = -1;
        requestPaint();
    }
    void pointerPress(const ui::PointerEvent& event) override
    {
        m_pressed = scopeAt(event.position);
        requestPaint();
    }
    void pointerRelease(const ui::PointerEvent& event) override
    {
        const int pressed = m_pressed;
        m_pressed = -1;
        requestPaint();
        if (pressed < 0 || pressed != scopeAt(event.position) || !m_model)
            return;
        m_model->setSearchHome(pressed == 0);
        requestLayout();
    }

private:
    FolderModel* m_model = nullptr;
    ui::Label* m_label;
    ui::Label* m_home;
    ui::Label* m_folder;
    ui::BusyIndicator* m_busy;
    ui::RectF m_homeRect;
    ui::RectF m_folderRect;
    int m_hovered = -1;
    int m_pressed = -1;
};

// "Trash" and its Empty button.
class TrashBar : public ui::View
{
public:
    TrashBar()
    {
        setName("trashBar");
        m_title = addChild<ui::Label>(tr("Trash"));
        m_title->setWeight(600);
        // TRANSLATORS: a button that empties the Trash.
        m_empty = addChild<ui::DialogButton>(tr("Empty"));
        m_empty->setName("emptyTrashButton");
        m_empty->onClicked = [this] {
            if (m_model)
                m_model->emptyTrash();
        };
    }

    void retranslate()
    {
        m_title->setText(tr("Trash"));
        m_empty->setText(tr("Empty"));
        requestLayout();
    }

    void setModel(FolderModel* model)
    {
        m_model = model;
        requestLayout();
    }

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override
    {
        const double height = geometry().height;
        const ui::SizeF title = m_title->sizeHint();
        m_title->setGeometry({kSmallSpacing * 1.5 + kSmallSpacing, centeredStart(height, title.height),
                              std::ceil(title.width), title.height});
        const ui::SizeF button = m_empty->sizeHint();
        const double width = std::ceil(button.width);
        m_empty->setGeometry({geometry().width - kLargeSpacing - width, centeredStart(height, 24), width, 24});
        m_empty->setEnabled(m_model && m_model->count() > 0);
    }

private:
    FolderModel* m_model = nullptr;
    ui::Label* m_title;
    ui::DialogButton* m_empty;
};

// The header's own part: back, forward, the path bar and search, from the
// sidebar's edge.
class HeaderRow : public ui::View
{
public:
    HeaderRow()
    {
        navigation = addChild<ui::NavigationButtons>();
        back = navigation->back();
        back->setName("backButton");
        forward = navigation->forward();
        forward->setName("forwardButton");
        navigationCaption = addChild<ui::Label>();
        navigationCaption->setFontSize(10);
        navigationCaption->setColor(ui::ColorId::DisabledText);
        navigationCaption->setTextAlignment(ui::Label::TextAlignment::Center);
        navigationCaption->setVerticallyCentered(true);
        pathBar = addChild<PathBar>();
        pathBar->onEditorVisibilityChanged = [this] { requestLayout(); };
        searchTitle = addChild<ui::Label>();
        searchTitle->setName("searchTitle");
        searchTitle->setFontSize(14);
        searchTitle->setWeight(600);
        searchTitle->setVerticallyCentered(true);
        searchTitle->setElide(ui::Label::Elide::Middle);
        searchTitle->setVisible(false);
        searchTitle->setMovesWindow(true);
        toolbar = addChild<ui::Toolbar>();
        toolbar->setDefaults(Preferences::defaultToolbar());
        setupItems();
    }

    void setupItems()
    {
        navigationCaption->setText(tr("Back/Forward"));
        items = {{"views", tr("View"), "view-icons", 154, [this](ui::Toolbar& owner) -> ui::View* {
            auto* control = owner.addChild<ui::SegmentedControl>(viewNames());
            control->setName("viewModeControl");
            control->setToolbarAppearance(true);
            const bool labels = owner.display() == ui::Toolbar::Display::IconsAndText;
            control->setContentHeight(24);
            control->setFontSize(10);
            control->setGlyphs({"view-icons", "view-list", "view-columns", "view-gallery"});
            if (labels)
                control->setToolbarCaption(tr("View"));
            control->onCurrentChanged = [this](int index) { if (onViewChanged) onViewChanged(kViews.at(size_t(index))); };
            control->setCurrentIndex(viewIndex(mode));
            return control;
        }, {"view-icons", "view-list", "view-columns", "view-gallery"}}, {"group", tr("Group"), "view-group", 46, {}, {}, true},
                   {"share", tr("Share"), "share", 36, {}, {}, false, "actions"},
                   {"tags", tr("Add Tags"), "tag", 36, {}, {}, false, "actions"},
                   {"action", tr("Action"), "ellipsis", 36, {}, {}, false, "actions"}, {"search", tr("Search"), "search", 36},
                  {"info", tr("Get Info"), "info"}, {"quicklook", tr("Quick Look"), "view-quick-look"},
                 {"preview", tr("Preview"), "sidebar"}, {"newfolder", tr("New Folder"), "folder"}};
        toolbar->setItems(items);
        syncToolbar();
    }

    void syncToolbar()
    {
        auto& prefs = Preferences::instance();
        toolbar->setDisplay(static_cast<ui::Toolbar::Display>(prefs.toolbarDisplay()));
        setToolbarOrder(searchExpanded());
        toolbar->setPreviewSelection("views", viewIndex(mode));
        for (const auto& child : toolbar->children())
            if (auto* control = dynamic_cast<ui::SegmentedControl*>(child.get()))
                control->setCurrentIndex(viewIndex(mode));
    }

    ui::NavigationButtons* navigation;
    ui::HeaderButton* back;
    ui::HeaderButton* forward;
    ui::Label* navigationCaption;
    PathBar* pathBar;
    ui::Label* searchTitle;
    ui::Toolbar* toolbar;
    std::vector<ui::Toolbar::Item> items;
    std::function<void(int)> onViewChanged;
    int mode = Preferences::Grid;
    double sidebarWidth = SideBar::kDefaultWidth;
    bool customizing = false;

private:
    bool searchExpanded() const
    {
        return !customizing && pathBar->editorVisible() && geometry().width - sidebarWidth >= 625;
    }

    void setToolbarOrder(bool expanded)
    {
        auto order = Preferences::instance().toolbarOrder();
        if (expanded)
            std::erase(order, "search");
        toolbar->setOrder(order);
    }

protected:
    void layout() override
    {
        double x = sidebarWidth + kNavigationStart;
        const double right = geometry().width;
        // The plate is centred on the window controls' line.
        const bool expanded = searchExpanded();
        setToolbarOrder(expanded);
        const auto hint = toolbar->sizeHint();
        const bool labels = toolbar->display() == ui::Toolbar::Display::IconsAndText;
        // Align the glyphs and title to the centreline of the navigation
        // plate. Labelled toolbars also need room underneath the buttons.
        const double toolbarY = (geometry().height - hint.height) / 2 + (labels ? 0 : 3);
        const ui::SizeF plate = labels ? navigation->sizeHint() : kNavigationPlate;
        const double navigationY = toolbarY + (std::min(hint.height, 30.0) - plate.height) / 2;
        navigation->setGeometry({x, navigationY, plate.width, plate.height});
        navigationCaption->setVisible(labels);
        navigationCaption->setGeometry({x - 8, navigationY + plate.height, plate.width + 16, 10});
        x += plate.width + kNavigationTitleGap;
        const double rightEdge = right - kHeaderEndMargin;
        const double searchWidth = std::clamp(rightEdge - x - hint.width - 145, 145.0, 244.0);
        const double end = expanded ? rightEdge - searchWidth - 9 : rightEdge;
        const auto& visibleOrder = toolbar->order();
        const bool flexible = std::find(visibleOrder.begin(), visibleOrder.end(), ui::Toolbar::FlexibleSpace) != visibleOrder.end();
        const double width = flexible ? std::max(hint.width, end - x - 140) : hint.width;
        const double toolbarX = std::max(x + 48, end - width);
        toolbar->setGeometry({toolbarX, toolbarY, std::max(0.0, end - toolbarX), hint.height});
        const double y = navigationY + (plate.height - kButtonSize) / 2;
        const ui::RectF titleRect{x, y, std::max(0.0, toolbarX - x - kSmallSpacing), kButtonSize};
        searchTitle->setVisible(expanded);
        searchTitle->setGeometry(titleRect);
        pathBar->setSearchCapsule(expanded);
        pathBar->setGeometry(expanded ? ui::RectF{rightEdge - searchWidth, y, searchWidth, kButtonSize} : titleRect);
    }
};

class ToolbarPalette : public ui::View
{
public:
    explicit ToolbarPalette(HeaderRow& header)
    {
        setName("toolbarCustomizationPalette");
        m_help = addChild<ui::Label>(tr("Drag your favorite items into the toolbar…"));
        m_help->setWrap(true);
        for (const auto& item : header.items)
            m_items.push_back(header.toolbar->makePaletteItem(*this, item.id, item.label, item.glyph));
        m_items.push_back(header.toolbar->makePaletteItem(*this, ui::Toolbar::Space, tr("Space"), ""));
        m_items.push_back(header.toolbar->makePaletteItem(*this, ui::Toolbar::FlexibleSpace, tr("Flexible Space"), ""));
        m_default = header.toolbar->makePaletteItem(*this, ui::Toolbar::DefaultSet, tr("Default Set"), "");
        m_resetHint = addChild<ui::Label>(tr("…or drag the default set into the toolbar."));
        m_resetHint->setWrap(true);
        m_show = addChild<ui::Label>(tr("Show:"));
        display = addChild<ui::ComboBox>(toolbarDisplays());
        display->setName("toolbarDisplayPicker");
        display->setCurrentIndex(Preferences::instance().toolbarDisplay());
        display->onActivated = [](int index) { Preferences::instance().setToolbarDisplay(index); };
        done = addChild<ui::DialogButton>(tr("Done"));
    }
    ui::DialogButton* done;
    ui::ComboBox* display;
    bool acceptsPointer() const override { return true; }

protected:
    void layout() override
    {
        const double width = geometry().width;
        m_help->setGeometry({18, 14, width - 36, 38});
        const int columns = width >= 570 ? 6 : 4;
        for (size_t i = 0; i < m_items.size(); ++i) {
            const double cell = (width - 36) / columns;
            m_items[i]->setGeometry({18 + cell * (i % columns), 55 + 47.0 * (i / columns), cell - 5, 43});
        }
        m_resetHint->setGeometry({18, geometry().height - 106, width - 36, 25});
        m_default->setGeometry({18, geometry().height - 78, width - 36, 32});
        const double labelWidth = std::ceil(m_show->sizeHint().width);
        m_show->setGeometry({18, geometry().height - 37, labelWidth, 27});
        display->setGeometry({28 + labelWidth, geometry().height - 37, 142, 27});
        done->setGeometry({width - 94, geometry().height - 38, 76, 28});
    }
    void paint(ui::Painter& painter) override
    {
        painter.drawRoundedRect(localRect(), {14}, ui::Theme::current().secondBackground, 1, style::textAlpha(0.2));
    }

private:
    ui::Label* m_show;
    ui::Label* m_help;
    ui::Label* m_resetHint;
    std::vector<ui::View*> m_items;
    ui::View* m_default;
};

class FolderWindowRoot : public ui::View
{
public:
    explicit FolderWindowRoot(FolderWindow* window)
        : m_window(window)
    {
        sidebar = addChild<SideBar>(FolderWindow::kHeaderInset);
        pages = addChild<ui::View>();
        pages->setName("pages");
        tabBar = addChild<ui::WindowTabBar>();
        tabBar->setName("folderTabBar");
        tabBar->setTabWidthRange(140, 0);
        tabBar->setTrackInsets({kSmallSpacing, 4, kNewTabStartGap + kNewTabButtonSize + kNewTabEndMargin, 4});
        tabBar->setPressedFeedback(true);
        newTab = addChild<ui::HeaderButton>("add", kNewTabButtonSize);
        newTab->setName("newTab");
        newTab->setToolbarAppearance(true);
        scopeBar = addChild<SearchScopeBar>();
        trashBar = addChild<TrashBar>();
        header = addChild<ui::WindowHeader>(FolderWindow::kHeaderInset);
        header->setButtonMargin(9);
        row = header->setContent<HeaderRow>();
        header->setContentUnderButtons(true);
        divider = addChild<ui::SidebarDivider>(SideBar::kMinimumWidth, SideBar::kMaximumWidth,
                                               SideBar::kDefaultWidth);
        divider->setName("sidebarDivider");
        sidebarWidth = std::clamp(Preferences::instance().sidebarWidth(), SideBar::kMinimumWidth,
                                  SideBar::kMaximumWidth);
        divider->onResized = [this](double width) {
            sidebarWidth = width;
            requestLayout();
        };
        divider->onFinished = [this](double width) {
            sidebarWidth = width;
            requestLayout();
            Preferences::instance().setSidebarWidth(width);
        };
    }

    SideBar* sidebar;
    ui::SidebarDivider* divider;
    double sidebarWidth;
    ui::View* pages;
    ui::WindowTabBar* tabBar;
    ui::HeaderButton* newTab;
    SearchScopeBar* scopeBar;
    TrashBar* trashBar;
    ui::WindowHeader* header;
    HeaderRow* row;
    ToolbarPalette* palette = nullptr;
    bool tabsShown = false;
    bool scopeShown = false;
    bool trashShown = false;

    double barsHeight() const
    {
        return FolderWindow::kHeaderInset + (tabsShown ? FolderWindow::kTabBarHeight : 0) + (scopeShown ? kScopeBarHeight : 0)
                + (trashShown ? kBarHeight : 0);
    }

protected:
    void layout() override
    {
        const double width = geometry().width;
        const double height = geometry().height;
        // The folder view keeps the width it has at the window's minimum size.
        const double widest = std::max(SideBar::kMinimumWidth,
                                       std::min(SideBar::kMaximumWidth, width - kMinimumContentWidth));
        const double left = std::min(sidebarWidth, widest);
        sidebar->setGeometry({0, 0, left, height});
        row->sidebarWidth = left;
        row->customizing = palette != nullptr;
        row->requestLayout();
        divider->setSidebarWidth(left);
        divider->setGeometry({left - ui::SidebarDivider::kWidth / 2, 0, ui::SidebarDivider::kWidth, height});
        const ui::RectF content {left, 0, std::max(0.0, width - left), height};
        pages->setGeometry(content);
        for (const auto& page : pages->children())
            page->setGeometry({0, 0, content.width, content.height});

        double y = FolderWindow::kHeaderInset;
        const double tabBarHeight = tabsShown ? FolderWindow::kTabBarHeight : 0;
        tabBar->setVisible(tabsShown);
        tabBar->setGeometry({left, y, content.width, tabBarHeight});
        newTab->setVisible(tabsShown);
        newTab->setGeometry({left + content.width - kNewTabEndMargin - kNewTabButtonSize,
                             y + centeredStart(tabBarHeight, kNewTabButtonSize), kNewTabButtonSize,
                             kNewTabButtonSize});
        y += tabBarHeight;
        scopeBar->setVisible(scopeShown);
        scopeBar->setGeometry({left, y, content.width, scopeShown ? kScopeBarHeight : 0});
        y += scopeShown ? kScopeBarHeight : 0;
        trashBar->setVisible(trashShown);
        trashBar->setGeometry({left, y, content.width, trashShown ? kBarHeight : 0});

        header->setGeometry({0, 0, width, FolderWindow::kHeaderInset});
        if (palette) {
            const double panelWidth = std::min(750.0, width - 24);
            palette->setGeometry({(width - panelWidth) / 2, FolderWindow::kHeaderInset + 4, panelWidth, 284});
        }
        m_window->updateBlur();
    }

    // Takes a tab pulled out of a window of this process over the header or
    // the tabs; while it is there the tab bar shows it where it would drop.
    bool canDrop(const ui::DropOffer& offer) const override
    {
        return sourceOf(offer) != nullptr;
    }

    void dragEnter(const ui::DragEvent& event) override
    {
        dragUpdate(event);
    }

    uint32_t dragUpdate(const ui::DragEvent& event) override
    {
        const double bottom = FolderWindow::kHeaderInset + (tabsShown ? FolderWindow::kTabBarHeight : 0);
        const bool over = event.position.y < bottom;
        const double x = event.position.x - tabBar->geometry().x;
        if (over && !m_window->m_incoming) {
            FolderWindow* source = sourceOf(*event.offer);
            FolderPage* page = source ? source->currentPage() : nullptr;
            m_window->setIncoming(true);
            tabBar->beginIncoming(page ? page->tabTitle() : std::string(), x);
        } else if (over) {
            tabBar->setIncomingX(x);
        } else if (m_window->m_incoming) {
            dragLeave();
        }
        return over ? ui::MoveAction : ui::NoAction;
    }

    void dragLeave() override
    {
        if (!m_window->m_incoming)
            return;
        tabBar->endIncoming(false);
        m_window->setIncoming(false);
    }

    uint32_t drop(const ui::DragEvent& event) override
    {
        FolderWindow* source = sourceOf(*event.offer);
        if (!source || !m_window->m_incoming) {
            dragLeave();
            return ui::NoAction;
        }
        const int index = tabBar->incomingIndex();
        tabBar->endIncoming(true);
        m_window->m_incoming = false;
        m_window->adoptTab(source, index);
        return ui::MoveAction;
    }

private:
    FolderWindow* sourceOf(const ui::DropOffer& offer) const
    {
        const ui::DragSource* source = offer.localSource();
        if (!source || !offer.hasMimeType(kTabMimeType) || source->context == m_window)
            return nullptr;
        return static_cast<FolderWindow*>(const_cast<void*>(source->context));
    }

    FolderWindow* m_window;
};

FolderWindow::FolderWindow(FileManagerHost* host, const FolderPage::Start& start, ui::SizeF size)
    : m_host(host)
    , m_window(std::make_unique<ui::Window>())
{
    m_window->setTitle(tr("Files"));
    m_window->setMinimumSize({kMinimumWidth, 500});
    if (size.width > 0 && size.height > 0) {
        m_window->resize(size);
    } else {
        m_window->resize({894, 572});
        m_window->setRestoresSize("main");
    }

    auto root = std::make_unique<FolderWindowRoot>(this);
    m_root = root.get();
    m_window->setContentView(std::move(root));
    m_sidebar = m_root->sidebar;
    m_sidebar->setTitle(tr("Files"));
    m_pathBar = m_root->row->pathBar;
    m_tabBar = m_root->tabBar;
    m_back = m_root->row->back;
    m_forward = m_root->row->forward;
    m_toast = std::make_unique<ui::Toast>(m_window.get());

    m_back->onClicked = [this] {
        if (FolderPage* page = currentPage())
            page->goBack();
    };
    m_forward->onClicked = [this] {
        if (FolderPage* page = currentPage())
            page->goForward();
    };
    m_root->row->toolbar->onActivated = [this](const std::string& id, ui::PointF point) { toolbarAction(id, point); };
    m_root->row->onViewChanged = [this](int mode) { selectView(mode); };
    m_root->row->toolbar->onOrderChanged = [](const std::vector<std::string>& order) { Preferences::instance().setToolbarOrder(order); };

    m_pathBar->onItemClicked = [this](const std::string& path) {
        if (FolderPage* page = currentPage())
            page->openUrl(path);
    };
    m_pathBar->onEditorAccepted = [this](const std::string& path) {
        if (FolderPage* page = currentPage())
            page->openUrl(path);
    };
    m_pathBar->onOpenInNewTab = [this](const std::string& path) { openTab(path); };
    m_pathBar->onOpenInNewWindow = [this](const std::string& path) {
        if (FolderPage* page = currentPage())
            page->model().openInNewWindow(path);
    };
    m_pathBar->onFinished = [this] {
        if (FolderPage* page = currentPage())
            page->focusView();
    };

    m_sidebar->onClicked = [this](const std::string& path) {
        if (FolderPage* page = currentPage()) {
            if (page->model().tagFilter() >= 0)
                page->model().setTagFilter(-1);
            m_sidebar->setSelectedTag(-1);
            page->openUrl(path);
        }
    };
    m_sidebar->onTagClicked = [this](int tag) {
        if (auto* page = currentPage()) {
            auto& model = page->model();
            model.setTagFilter(model.tagFilter() == tag ? -1 : tag);
            m_sidebar->setSelectedTag(model.tagFilter());
        }
    };
    m_sidebar->onOpenInNewTab = [this](const std::string& path) { openTab(path); };
    m_sidebar->onOpenInNewWindow = [this](const std::string& path) {
        if (FolderPage* page = currentPage())
            page->model().openInNewWindow(path);
    };

    m_root->newTab->onClicked = [this] {
        if (FolderPage* page = currentPage())
            openTab(page->currentUrl());
    };
    m_tabBar->onCurrentChanged = [this](int index) { setCurrentTab(index); };
    m_tabBar->onCloseRequested = [this](int index) { closeTab(index); };
    m_tabBar->onMoveRequested = [this](int from, int to) { moveTab(from, to); };
    m_tabBar->onContextMenuRequested = [this](int index, ui::PointF position) { showTabMenu(index, position); };
    m_tabBar->onDetachRequested = [this](int index, ui::PointF position) {
        // Later, not from inside the tab bar's own handler: taking the tab out
        // changes the tabs it is running over.
        base::EventLoop::current()->post([this, alive = m_alive, index, position] {
            if (*alive)
                detachTab(index, position);
        });
    };

    m_window->onActiveChanged = [this] {
        updateBackground();
        if (m_window->isActive()) {
            if (FolderPage* page = currentPage())
                page->activatePreview();
        }
    };
    m_window->onStatesChanged = [this] { updateBackground(); };
    m_window->onCloseRequested = [this] { close(); };
    m_subscriptions.push_back(Preferences::instance().changed.connect([this] {
        base::EventLoop::current()->post([this, alive = m_alive] { if (*alive) updateHeader(); });
    }));
    m_subscriptions.push_back(ui::Application::instance()->languageChanged.connect([this] { retranslate(); }));

    addShortcuts();
    buildMainMenu();
    openTabSelecting(start);
    updateBackground();
}

FolderWindow::~FolderWindow()
{
    *m_alive = false;
}

FolderPage* FolderWindow::currentPage() const
{
    return m_tabs.empty() ? nullptr : m_tabs[size_t(std::clamp(m_currentTab, 0, int(m_tabs.size()) - 1))].page;
}

// Every tab keeps its folder, history, view, selection, scroll position and
// search, and an open rename or path editor its text.
void FolderWindow::retranslate()
{
    m_window->setTitle(tr("Files"));
    m_sidebar->setTitle(tr("Files"));
    m_sidebar->retranslate();
    m_pathBar->retranslate();
    setToolbarEditing(false);
    m_root->row->setupItems();
    m_root->scopeBar->retranslate();
    m_root->trashBar->retranslate();
    for (const Tab& tab : m_tabs)
        tab.page->retranslate();
    if (m_detachedTab)
        m_detachedTab->page->retranslate();
    if (FolderPage* page = currentPage())
        m_pathBar->updateUrl(page->currentUrl());
    if (m_archivePassword)
        m_archivePassword->retranslate();
    if (m_aboutDialog) {
        m_aboutDialog.reset();
        showAbout();
    }
    buildMainMenu();
    updateTabBar();
    updateHeader();
}

void FolderWindow::show()
{
    m_window->show();
}

void FolderWindow::close()
{
    // The window goes on the next turn, not from inside its own handlers.
    m_closed = true;
    if (m_sharePopup)
        m_sharePopup->close();
    m_window->hide();
    base::EventLoop::current()->post([this, alive = m_alive] {
        if (*alive)
            m_host->windowClosed(this);
    });
}

void FolderWindow::addShortcuts()
{
    ui::Window& window = *m_window;
    window.addShortcut(ui::ControlModifier, 't', [this] {
        if (FolderPage* page = currentPage())
            openTab(page->currentUrl());
    });
    window.addShortcut(ui::ControlModifier, 'w', [this] { closeTab(m_currentTab); });
    window.addShortcut(ui::ControlModifier, ui::key::Tab, [this] {
        setCurrentTab((m_currentTab + 1) % std::max(1, tabCount()));
    });
    window.addShortcut(ui::ControlModifier | ui::ShiftModifier, ui::key::Tab, [this] {
        setCurrentTab((m_currentTab + tabCount() - 1) % std::max(1, tabCount()));
    });
    window.addShortcut(ui::ControlModifier, 'f', [this] { m_pathBar->openSearch(); });
    window.addShortcut(ui::ControlModifier, 'j', [this] { toggleViewOptions(); });
    for (size_t i = 0; i < kViews.size(); ++i)
        window.addShortcut(ui::ControlModifier, '1' + i, [this, i] { selectView(kViews[i]); });
    window.addShortcut(ui::ControlModifier, 'k', [this] { m_sidebar->connectToServer(); });
    window.addShortcut(ui::ControlModifier | ui::ShiftModifier, 'g', [this] { goToFolder(); });
}

void FolderWindow::selectView(int mode)
{
    Preferences::instance().setViewMethod(mode);
    if (auto* page = currentPage())
        page->chooseView(mode);
    updateHeader();
}

void FolderWindow::openTab(const std::string& url)
{
    openTabSelecting({url, {}, {}, {}});
}

void FolderWindow::openTabSelecting(const FolderPage::Start& start, int index)
{
    auto page = std::make_unique<FolderPage>(this, start);
    FolderPage* raw = page.get();
    m_root->pages->addChild(std::move(page));

    Tab tab {raw, {}};
    tab.subscriptions.push_back(raw->currentUrlChanged.connect([this, raw] {
        if (currentPage() == raw)
            syncNavigation();
        updateTabBar();
    }));
    tab.subscriptions.push_back(raw->model().historyChanged.connect([this, raw] {
        if (currentPage() == raw)
            updateHeader();
    }));
    tab.subscriptions.push_back(raw->model().notification.connect([this](const std::string& message) {
        notify(message);
    }));
    tab.subscriptions.push_back(raw->model().countChanged.connect([this, raw] {
        if (currentPage() == raw)
            m_root->trashBar->requestLayout();
    }));
    tab.subscriptions.push_back(raw->model().searchChanged.connect([this, raw] {
        if (currentPage() == raw)
            updateHeader();
        updateTabBar();
    }));
    raw->onOpenInNewTab = [this](const std::string& url) { openTab(url); };
    raw->onCloseRequested = [this] { closeTab(m_currentTab); };
    raw->onRequestPathEditor = [this] { m_pathBar->openEditor(); };
    raw->onContextMenu = [this](ui::PointF position) { showContextMenu(position); };

    insertTab(std::move(tab), index < 0 || index > int(m_tabs.size()) ? int(m_tabs.size()) : index);
}

void FolderWindow::insertTab(Tab tab, int index)
{
    const int at = std::clamp(index, 0, int(m_tabs.size()));
    const std::string title = tab.page->tabTitle();
    m_tabs.insert(m_tabs.begin() + at, std::move(tab));
    m_tabBar->insertTab(at, title);
    m_currentTab = at;
    syncNavigation();
}

// Opens the tab in a window of its own, which follows the pointer until it is
// dropped; Escape puts the tab back. `position` is the pointer in this window.
void FolderWindow::detachTab(int index, ui::PointF position)
{
    if (index < 0 || index >= tabCount() || tabCount() < 2 || m_detachedTab) {
        m_tabBar->cancelDetach();
        return;
    }

    m_detaching = true;
    m_detachedIndex = index;
    m_detachedTab = std::move(m_tabs[size_t(index)]);
    m_tabs.erase(m_tabs.begin() + index);
    m_tabBar->removeTab(index);
    m_currentTab = std::max(0, m_currentTab - (index <= m_currentTab ? 1 : 0));
    FolderPage* page = m_detachedTab->page;
    page->setVisible(false);
    syncNavigation();

    const FolderModel::TabState state = page->model().tabState();
    FolderWindow* detached = m_host->openFolderWindow({page->currentUrl(), state.selection, state.back, state.forward},
                                                      m_window.get());
    const auto putBack = [this] {
        Tab tab = std::move(*m_detachedTab);
        m_detachedTab.reset();
        m_detachedWindow = nullptr;
        m_detaching = false;
        insertTab(std::move(tab), m_detachedIndex);
    };
    if (!detached) {
        putBack();
        return;
    }
    m_detachedWindow = detached;

    // The pointer stays over the new window's header, as far across as it
    // is over this one.
    ui::DragSource source;
    source.mimeTypes = {kTabMimeType};
    source.data = [](const std::string&) { return std::string(); };
    source.actions = ui::MoveAction;
    source.attachedWindow = &detached->window();
    source.attachedOffset = {std::min(position.x, detached->window().size().width - 1), kHeaderInset / 2};
    source.context = detached;

    // Escape puts the tab back; let go anywhere, it stays in its window.
    source.onEnded = [this, alive = m_alive, detached, detachedAlive = detached->m_alive,
                      putBack](ui::DropAction, bool dropped) {
        if (!*alive)
            return;
        m_detaching = false;
        const bool open = *detachedAlive && !detached->m_closed;
        if (m_detachedTab && !dropped && open) {
            detached->close();
            putBack();
            return;
        }
        if (m_detachedTab) {
            std::shared_ptr<ui::View> gone = m_root->pages->takeChild(m_detachedTab->page);
            base::EventLoop::current()->post([gone] {});
            m_detachedTab.reset();
        }
        m_detachedWindow = nullptr;
        updateHeader();
        if (open)
            detached->window().activate();
    };

    if (!m_tabBar->startDrag(std::move(source))) {
        detached->close();
        putBack();
        return;
    }
    updateHeader();
}

// Takes the tab of a window pulled out of another, which closes; the window
// it came from gets its own tab back as it was.
void FolderWindow::adoptTab(FolderWindow* source, int index)
{
    if (index < 0)
        index = tabCount();
    if (m_detachedTab && m_detachedWindow == source) {
        Tab tab = std::move(*m_detachedTab);
        m_detachedTab.reset();
        m_detachedWindow = nullptr;
        insertTab(std::move(tab), index);
    } else if (FolderPage* page = source->currentPage()) {
        const FolderModel::TabState state = page->model().tabState();
        openTabSelecting({page->currentUrl(), state.selection, state.back, state.forward}, index);
    }
    source->close();
    updateHeader();
    m_window->activate();
}

void FolderWindow::setIncoming(bool incoming)
{
    m_incoming = incoming;
    updateHeader();
}

void FolderWindow::showTabMenu(int index, ui::PointF position)
{
    if (index < 0 || index >= tabCount())
        return;
    m_menu = std::make_unique<ui::Menu>();
    m_menu->addItem(tr("Open In New Tab"), [this, index] {
        if (index < tabCount())
            openTab(m_tabs[size_t(index)].page->currentUrl());
    });
    m_menu->addItem(tr("Close Tab"), [this, index] { closeTab(index); });
    m_menu->popup(m_window.get(), position);
}

void FolderWindow::closeTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    if (m_tabs.size() == 1) {
        close();
        return;
    }
    FolderPage* page = m_tabs[size_t(index)].page;
    m_tabs.erase(m_tabs.begin() + index);
    m_tabBar->removeTab(index);
    m_currentTab = std::max(0, m_currentTab - (index <= m_currentTab ? 1 : 0));
    std::shared_ptr<ui::View> gone = m_root->pages->takeChild(page);
    base::EventLoop::current()->post([gone] {});
    syncNavigation();
}

void FolderWindow::setCurrentTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()) || index == m_currentTab)
        return;
    m_currentTab = index;
    syncNavigation();
    if (FolderPage* page = currentPage())
        page->activatePreview();
}

void FolderWindow::moveTab(int from, int to)
{
    const int count = int(m_tabs.size());
    if (from == to || from < 0 || to < 0 || from >= count || to >= count)
        return;
    FolderPage* active = currentPage();
    Tab tab = std::move(m_tabs[size_t(from)]);
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, std::move(tab));
    m_tabBar->moveTab(from, to);
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].page == active)
            m_currentTab = int(i);
    }
    syncNavigation();
}

void FolderWindow::syncNavigation()
{
    if (m_sharePopup)
        m_sharePopup->close();
    FolderPage* page = currentPage();
    if (!page)
        return;
    for (const Tab& tab : m_tabs)
        tab.page->setVisible(tab.page == page);
    m_pathBar->setFolderModel(&page->model());
    if (!m_archivePassword)
        m_archivePassword = std::make_unique<ArchivePasswordPrompt>();
    m_archivePassword->setModel(&page->model(), m_window.get());
    m_pathBar->closeEditor();
    m_sidebar->updateSelection(page->currentUrl());
    m_sidebar->setSelectedTag(page->model().tagFilter());
    m_pathBar->updateUrl(page->currentUrl());
    updateTabBar();
    updateHeader();
    page->focusView();
}

void FolderWindow::updateTabBar()
{
    for (int i = 0; i < tabCount() && i < m_tabBar->count(); ++i) {
        const std::string title = m_tabs[size_t(i)].page->tabTitle();
        if (m_tabBar->tabTitle(i) != title)
            m_tabBar->setTabTitle(i, title);
    }
    if (m_tabBar->currentIndex() != m_currentTab)
        m_tabBar->setCurrentIndex(m_currentTab);
}

double FolderWindow::headerHeight() const
{
    return m_root->barsHeight();
}

void FolderWindow::updateHeader()
{
    FolderPage* page = currentPage();
    if (!page)
        return;
    FolderModel& model = page->model();
    m_window->setTitle(page->folderName());
    m_pathBar->setFolderTitle(page->folderName());
    m_root->row->searchTitle->setText(model.searchText().empty() ? page->folderName()
        : format(tr("Searching “{text}”"), {{"text", model.searchHome() ? tr("Home") : page->folderName()}}));
    if (m_viewOptions && m_viewOptions->isShown())
        m_viewOptions->sync(page);
    m_root->row->mode = page->viewMethod();
    m_root->row->syncToolbar();
    if (m_root->palette)
        m_root->palette->display->setCurrentIndex(Preferences::instance().toolbarDisplay());
    m_back->setEnabled(model.canGoBack());
    m_forward->setEnabled(model.canGoForward());

    // A tab being dragged out or in keeps the bar up even with one tab, so
    // it can be dragged back.
    m_root->tabsShown = m_tabs.size() > 1 || m_incoming || m_detaching;
    m_root->scopeShown = !model.searchText().empty();
    m_root->trashShown = model.isTrash() && model.searchText().empty();
    m_root->scopeBar->setModel(&model, page->folderName());
    m_root->trashBar->setModel(&model);
    const double height = headerHeight();
    for (const Tab& tab : m_tabs)
        tab.page->setHeaderHeight(height);
    m_root->requestLayout();
}

void FolderWindow::updateBackground()
{
    const bool active = m_window->isActive();
    m_window->setBackgroundColor(ui::Theme::appearanceStyle().reduceTransparency ? ui::ColorId::Background
                : active ? ui::ColorId::TranslucentWindowBackground : ui::ColorId::TranslucentWindowBackgroundInactive);
    updateBlur();
    // Views colour their text by it as they lay out.
    m_window->requestLayout();
}

void FolderWindow::updateBlur()
{
    ui::toolbar_surface::applyWindowGlass(*m_window);
}

void FolderWindow::showContextMenu(ui::PointF position, bool fromToolbar)
{
    FolderPage* page = currentPage();
    if (!page)
        return;
    m_menu = createFolderMenu(page->model(), [this](std::string path, std::string name) {
        m_folderAppearance = showFolderAppearance(m_window.get(), std::move(path), std::move(name));
    });
    m_menu->addSeparator();
    const auto quick = m_menu->addItem(tr("Quick Look"), [this] { if (auto* p = currentPage()) p->quickLook(); });
    m_menu->setItemEnabled(quick, page->model().hasSelection());
    if (fromToolbar) {
        m_menu->addSeparator();
        const auto location = m_menu->addItem(tr("Go to Folder…"), [this] { goToFolder(); });
        m_menu->setItemKeyEquivalent(location, "g", ui::ControlModifier | ui::ShiftModifier);
        m_menu->addSeparator();
    }
    m_menu->addItem(tr("Show View Options"), [this] { toggleViewOptions(); });
    m_menu->addItem(tr("Customize Toolbar…"), [this] { setToolbarEditing(true); });
    m_menu->popup(m_window.get(), position);
}

static void toggleOption(ui::Menu& menu, const char* label, Preferences::Option option)
{
    auto& prefs = Preferences::instance();
    const auto row = menu.addItem(tr(label), [option] {
        auto& prefs = Preferences::instance();
        prefs.setOption(option, !prefs.option(option));
    });
    menu.setItemChecked(row, prefs.option(option));
}

void FolderWindow::populateGrouping(ui::Menu& menu)
{
    const std::pair<const char*, int> groups[] {{trNoop("None"), Preferences::NoGroup}, {trNoop("Kind"), Preferences::KindGroup},
        {trNoop("Name"), Preferences::NameGroup}, {trNoop("Date Modified"), Preferences::ModifiedGroup},
        {trNoop("Date Created"), Preferences::CreatedGroup}, {trNoop("Application"), Preferences::ApplicationGroup},
        {trNoop("Size"), Preferences::SizeGroup}, {trNoop("Tags"), Preferences::TagsGroup}};
    for (const auto& [name, group] : groups) {
        const auto index = menu.addItem(tr(name), [this, group] {
            if (auto* page = currentPage()) {
                auto options = page->viewOptions();
                options.group = group;
                page->setViewOptions(options);
            }
        });
        menu.setItemChecked(index, currentPage() && currentPage()->viewOptions().group == group);
    }
}

void FolderWindow::toggleViewOptions()
{
    auto* page = currentPage();
    if (!page)
        return;
    if (!m_viewOptions)
        m_viewOptions = std::make_unique<ViewOptionsDialog>(m_window.get());
    if (m_viewOptions->isShown()) {
        m_viewOptions->hide();
    } else {
        m_viewOptions->sync(page);
        m_viewOptions->show();
    }
}

ui::Window* FolderWindow::viewOptionsWindow() const { return m_viewOptions ? m_viewOptions->window() : nullptr; }

void FolderWindow::populateSidebarSettings(ui::Menu& menu)
{
    toggleOption(menu, trNoop("Favorites"), Preferences::SidebarFavorites);
    toggleOption(menu, trNoop("Locations"), Preferences::SidebarLocations);
    toggleOption(menu, trNoop("Tags"), Preferences::SidebarTags);
}

void FolderWindow::setToolbarEditing(bool editing)
{
    if (m_sharePopup)
        m_sharePopup->close();
    auto& row = *m_root->row;
    if (editing == bool(m_root->palette))
        return;
    row.toolbar->setEditing(editing);
    if (editing) {
        m_root->palette = m_root->addChild<ToolbarPalette>(row);
        m_root->palette->done->onClicked = [this] { setToolbarEditing(false); };
    } else {
        std::shared_ptr<ui::View> old = m_root->takeChild(m_root->palette);
        m_root->palette = nullptr;
        base::EventLoop::current()->post([old] {});
    }
    m_root->requestLayout();
}

ui::Window* FolderWindow::sharePopupWindow() const
{
    return m_sharePopup ? m_sharePopup->window() : nullptr;
}

void FolderWindow::showSharePopup()
{
    if (m_sharePopup && m_sharePopup->isOpen()) {
        m_sharePopup->close();
        return;
    }
    auto* page = currentPage();
    auto* anchor = m_root->row->toolbar->itemView("share");
    if (!page || !anchor || !page->model().action("copy").enabled)
        return;

    auto& model = page->model();
    FileItemList items;
    const auto rows = model.selectedRows();
    for (int row : rows) {
        if (auto item = model.item(row))
            items.push_back(std::move(item));
    }
    if (items.empty())
        return;
    if (m_menu)
        m_menu->close();
    if (!m_sharePopup)
        m_sharePopup = std::make_unique<ui::DropDownPopup>();
    m_sharePopup->setContentView(makeSharePanel(std::move(items), model.displayName(rows.front()),
        [this] { m_sharePopup->close(); }));
    m_sharePopup->popoverFor(anchor);
}

void FolderWindow::toolbarAction(const std::string& id, ui::PointF position)
{
    auto* page = currentPage();
    if (!page)
        return;
    auto& model = page->model();
    if (id == "share") { showSharePopup(); return; }
    if (id == "search") { m_pathBar->openSearch(); return; }
    if (id == "info") { model.openPropertiesDialog(); return; }
    if (id == "quicklook") { page->quickLook(); return; }
    if (id == "newfolder") { model.newFolder(); return; }
    if (id == "preview") {
        auto& prefs = Preferences::instance();
        if (page->viewMethod() == Preferences::Columns) {
            auto options = page->viewOptions();
            options.setOption(Preferences::ColumnPreview, !options.option(Preferences::ColumnPreview));
            page->setViewOptions(options);
        } else {
            prefs.setOption(Preferences::PreviewPane, !prefs.option(Preferences::PreviewPane));
        }
        return;
    }
    if (id == "action") { showContextMenu(position, true); return; }

    m_menu = std::make_unique<ui::Menu>();
    if (id == "group") {
        populateGrouping(*m_menu);
    } else if (id == "views") {
        const auto names = viewNames();
        for (size_t i = 0; i < kViews.size(); ++i) {
            const auto row = m_menu->addItem(names[i], [this, i] { selectView(kViews[i]); });
            m_menu->setItemChecked(row, viewIndex(page->viewMethod()) == int(i));
        }
    } else if (id == "tags") {
        std::vector<std::string> paths;
        for (const int row : model.selectedRows()) {
            const auto file = model.item(row);
            if (file && file->isLocalFile() && !model.isTrash() && !file->isLink)
                paths.push_back(file->path);
        }
        if (paths.empty()) {
            m_menu->setItemEnabled(m_menu->addItem(tr("Select local files or folders to tag"), [] {}), false);
        } else {
            uint8_t common = 0x7f;
            for (const auto& path : paths)
                common &= filetags::read(path);
            for (size_t i = 0; i < filetags::names.size(); ++i) {
                const bool checked = common & (1u << i);
                const auto row = m_menu->addItem(tr(filetags::names[i]), [this, paths, i, checked] {
                    bool success = true;
                    for (const auto& path : paths)
                        success &= filetags::set(path, int(i), !checked);
                    if (!success)
                        notify(tr("Some tags could not be changed. Check the file permissions and filesystem support."));
                    if (auto* page = currentPage())
                        page->model().setTagFilter(page->model().tagFilter());
                });
                m_menu->setItemChecked(row, checked);
            }
        }
    } else {
        return;
    }
    m_menu->popup(m_window.get(), position);
}

void FolderWindow::showAbout()
{
    if (m_aboutDialog) {
        m_aboutDialog->show();
        return;
    }

    m_aboutDialog = std::make_unique<ui::AboutDialog>(m_window.get());
    m_aboutDialog->setName(tr("Files"));
    m_aboutDialog->setDescription(tr("Browses and organizes your files."));
    m_aboutDialog->setIconSource("system-file-manager");
    m_aboutDialog->onClosed = [this] {
        std::shared_ptr<ui::AboutDialog> doomed(std::move(m_aboutDialog));
        base::EventLoop::current()->post([doomed] {});
    };
    m_aboutDialog->show();
}

void FolderWindow::buildMainMenu()
{
    // Starts reading the shortcut convention, which some rows' keys follow.
    ui::commandKeys();
    m_window->setMainMenu(nullptr);
    m_mainMenu = std::make_unique<ui::Menu>();
    const uint32_t control = ui::ControlModifier;
    const uint32_t shift = ui::ShiftModifier;
    const uint32_t alt = ui::AltModifier;
    const auto onPage = [this](std::function<void(FolderPage&)> action) {
        return [this, action] {
            if (FolderPage* page = currentPage())
                action(*page);
        };
    };
    const auto onAction = [this](const std::string& name) {
        return [this, name] {
            if (FolderPage* page = currentPage())
                triggerAction(page->model(), name);
        };
    };
    // A row that shows and does what the folder's action of that name does.
    struct ActionRow
    {
        ui::Menu* menu;
        size_t index;
        std::string name;
    };
    auto actionRows = std::make_shared<std::vector<ActionRow>>();
    const auto addAction = [&](ui::Menu* menu, const std::string& name) {
        const size_t index = menu->addItem(name, onAction(name));
        actionRows->push_back({menu, index, name});
        return index;
    };
    const auto prepare = [this, actionRows] {
        FolderPage* page = currentPage();
        if (!page)
            return;
        FolderModel& model = page->model();
        model.prepareContextMenu();
        // Without a selection the folder's actions apply to the folder itself;
        // these act on selected items only.
        static const std::set<std::string> onSelection {"open", "openWith", "showInFolder", "rename",
                                                        "compress", "trash", "restore", "del"};
        for (const ActionRow& row : *actionRows) {
            const FolderModel::Action& action = model.action(row.name);
            row.menu->setItemText(row.index, action.text);
            row.menu->setItemVisible(row.index, action.visible);
            row.menu->setItemEnabled(row.index, action.enabled && (model.hasSelection() || !onSelection.count(row.name)));
        }
    };

    ui::Menu* application = m_mainMenu->addSubmenu(tr("Files"));
    application->setRole(ui::MenuRole::Application);
    application->addItem(tr("About Files"), [this] { showAbout(); });
    application->addSeparator();
    application->addItem(tr("Hide Files"), [this] { m_window->hideApplication(); });

    ui::Menu* file = m_mainMenu->addSubmenu(tr("File"));
    file->addItem(tr("New Window"), onPage([](FolderPage& page) { page.model().openInNewWindow(page.currentUrl()); }));
    const size_t newTabRow = file->addItem(tr("New Tab"), onPage([this](FolderPage& page) { openTab(page.currentUrl()); }));
    file->setItemKeyEquivalent(newTabRow, "t", control);
    addAction(file, "newFolder");
    addAction(file, "newTextFile");
    file->addSeparator();
    const size_t open = addAction(file, "open");
    file->setItemKeyEquivalent(open, "Return", 0);
    addAction(file, "openWith");
    addAction(file, "showInFolder");
    file->addSeparator();
    const size_t properties = addAction(file, "properties");
    const size_t rename = addAction(file, "rename");
    file->setItemKeyEquivalent(rename, "F2", 0);
    addAction(file, "compress");
    file->addSeparator();
    const size_t trash = addAction(file, "trash");
    addAction(file, "restore");
    const size_t deleteRow = addAction(file, "del");
    file->setItemKeyEquivalent(deleteRow, "Delete", 0);
    addAction(file, "emptyTrash");
    file->addSeparator();
    const size_t find = file->addItem(tr("Find"), [this] { m_pathBar->openSearch(); });
    file->setItemKeyEquivalent(find, "f", control);
    file->addSeparator();
    const size_t closeTabRow = file->addItem(tr("Close Tab"), [this] { closeTab(m_currentTab); });
    file->setItemKeyEquivalent(closeTabRow, "w", control);
    file->onAboutToShow = [prepare, file, properties, trash] {
        prepare();
        // The folder's keys differ between the two shortcut conventions.
        if (ui::commandKeys()) {
            file->setItemKeyEquivalent(properties, "i", ui::ControlModifier);
            file->setItemKeyEquivalent(trash, "BackSpace", ui::ControlModifier);
        } else {
            file->setItemKeyEquivalent(properties, "Return", ui::AltModifier);
            file->setItemKeyEquivalent(trash, "Delete", 0);
        }
    };

    ui::Menu* edit = m_mainMenu->addSubmenu(tr("Edit"));
    const size_t undo = addAction(edit, "undo");
    edit->setItemKeyEquivalent(undo, "z", control);
    const size_t redo = addAction(edit, "redo");
    edit->setItemKeyEquivalent(redo, "z", control | shift);
    edit->addSeparator();
    const size_t cut = addAction(edit, "cut");
    edit->setItemKeyEquivalent(cut, "x", control);
    const size_t copy = addAction(edit, "copy");
    edit->setItemKeyEquivalent(copy, "c", control);
    const size_t paste = addAction(edit, "paste");
    edit->setItemKeyEquivalent(paste, "v", control);
    const size_t selectAll = edit->addItem(tr("Select All"), onPage([](FolderPage& page) { page.model().selectAll(); }));
    edit->setItemKeyEquivalent(selectAll, "a", control);
    edit->onAboutToShow = prepare;

    ui::Menu* view = m_mainMenu->addSubmenu(tr("View"));
    const auto names = viewNames();
    std::array<size_t, 4> viewRows {};
    for (size_t i = 0; i < kViews.size(); ++i) {
        viewRows[i] = view->addItem(names[i], [this, i] { selectView(kViews[i]); });
        view->setItemKeyEquivalent(viewRows[i], std::to_string(i + 1), control);
    }
    view->addSeparator();
    ui::Menu* sort = view->addSubmenu(tr("Sort By"));
    ui::Menu* group = view->addSubmenu(tr("Group By"));
    group->onAboutToShow = [this, group] { group->clear(); populateGrouping(*group); };
    const auto options = view->addItem(tr("Show View Options"), [this] { toggleViewOptions(); });
    view->setItemKeyEquivalent(options, "j", control);
    const auto cleanup = view->addItem(tr("Clean Up"), [this] {
        if (auto* page = currentPage())
            if (auto* grid = dynamic_cast<FolderGridView*>(page->view())) grid->cleanUp();
    });
    toggleOption(*view, trNoop("Keep Folders on Top"), Preferences::FoldersFirst);
    ui::Menu* sidebar = view->addSubmenu(tr("Sidebar Settings"));
    sidebar->onAboutToShow = [this, sidebar] { sidebar->clear(); populateSidebarSettings(*sidebar); };
    ui::Menu* toolbar = view->addSubmenu(tr("Toolbar"));
    toolbar->onAboutToShow = [this, toolbar] {
        toolbar->clear();
        const auto displays = toolbarDisplays();
        for (int i = 0; i < int(displays.size()); ++i) {
            const auto row = toolbar->addItem(displays[i], [i] { Preferences::instance().setToolbarDisplay(i); });
            toolbar->setItemChecked(row, Preferences::instance().toolbarDisplay() == i);
        }
        toolbar->addSeparator();
        toolbar->addItem(tr("Customize Toolbar…"), [this] { setToolbarEditing(true); });
        toolbar->addItem(tr("Restore Default Set"), [] { Preferences::instance().setToolbarOrder(Preferences::defaultToolbar()); });
    };
    const auto addToggle = [&](const char* show, const char* hide, Preferences::Option option) {
        return view->addItem(tr(Preferences::instance().option(option) ? hide : show), [option] {
            auto& prefs = Preferences::instance();
            prefs.setOption(option, !prefs.option(option));
        });
    };
    const auto path = addToggle(trNoop("Show Path Bar"), trNoop("Hide Path Bar"), Preferences::PathBar);
    const auto status = addToggle(trNoop("Show Status Bar"), trNoop("Hide Status Bar"), Preferences::StatusBar);
    const auto preview = view->addItem(tr("Show Preview"), [this] { toolbarAction("preview", {}); });
    view->addSeparator();
    const size_t hidden = addAction(view, "showHidden");
    view->addSeparator();
    const size_t refresh = view->addItem(tr("Refresh"), onPage([](FolderPage& page) { page.model().refresh(); }));
    view->setItemKeyEquivalent(refresh, "F5", 0);
    view->onAboutToShow = [this, prepare, view, viewRows, hidden, path, status, preview, cleanup, options] {
        prepare();
        const Preferences& preferences = Preferences::instance();
        const auto* page = currentPage();
        for (size_t i = 0; i < viewRows.size(); ++i)
            view->setItemChecked(viewRows[i], page && viewIndex(page->viewMethod()) == int(i));
        view->setItemText(path, preferences.option(Preferences::PathBar) ? tr("Hide Path Bar") : tr("Show Path Bar"));
        view->setItemText(status, preferences.option(Preferences::StatusBar) ? tr("Hide Status Bar") : tr("Show Status Bar"));
        const bool previewShown = page && page->viewMethod() == Preferences::Columns
                ? page->viewOptions().option(Preferences::ColumnPreview) : preferences.option(Preferences::PreviewPane);
        view->setItemText(preview, previewShown ? tr("Hide Preview") : tr("Show Preview"));
        view->setItemText(options, viewOptionsWindow() ? tr("Hide View Options") : tr("Show View Options"));
        view->setItemEnabled(cleanup, page && page->viewMethod() == Preferences::Grid);
        if (ui::commandKeys())
            view->setItemKeyEquivalent(hidden, ".", ui::ControlModifier | ui::ShiftModifier);
        else
            view->setItemKeyEquivalent(hidden, "h", ui::ControlModifier);
    };
    sort->onAboutToShow = [this, sort] {
        sort->clear();
        FolderPage* page = currentPage();
        const bool trash = page && page->model().isTrash();
        Preferences& preferences = Preferences::instance();
        const int sortMode = trash ? preferences.trashSortMode() : page ? page->viewOptions().sort : Preferences::Name;
        const auto sortItem = [&](const std::string& text, int mode) {
            const size_t index = sort->addItem(text, [this, trash, mode] {
                Preferences& preferences = Preferences::instance();
                if (trash)
                    preferences.setTrashSort(mode, preferences.trashSortDesc());
                else if (auto* page = currentPage()) {
                    auto options = page->viewOptions();
                    options.sort = mode;
                    page->setViewOptions(options);
                }
            });
            sort->setItemChecked(index, sortMode == mode);
        };
        if (!trash && page && page->viewMethod() == Preferences::Grid)
            sortItem(tr("None"), Preferences::Manual);
        sortItem(tr("Name"), Preferences::Name);
        if (trash)
            sortItem(tr("Original Location"), Preferences::OriginalPath);
        sortItem(trash ? tr("Date Deleted") : tr("Date"), trash ? Preferences::DeletionTime : Preferences::ModifiedTime);
        if (!trash)
            sortItem(tr("Type"), Preferences::Type);
        sortItem(tr("Size"), Preferences::Size);
        if (!trash) {
            sortItem(tr("Date Created"), Preferences::CreatedTime);
            sortItem(tr("Date Accessed"), Preferences::AccessedTime);
        }
        sort->addSeparator();
        const bool descending = trash ? preferences.trashSortDesc() : page && page->viewOptions().descending;
        const auto reverse = sort->addItem(tr("Descending"), [this, trash, sortMode, descending] {
            auto& prefs = Preferences::instance();
            if (trash) prefs.setTrashSort(sortMode, !descending);
            else if (auto* page = currentPage()) {
                auto options = page->viewOptions();
                options.descending = !options.descending;
                page->setViewOptions(options);
            }
        });
        sort->setItemChecked(reverse, descending);
        sort->setItemEnabled(reverse, sortMode != Preferences::Manual);
    };

    ui::Menu* go = m_mainMenu->addSubmenu(tr("Go"));
    const size_t back = go->addItem(tr("Back"), onPage([](FolderPage& page) { page.goBack(); }));
    const size_t forward = go->addItem(tr("Forward"), onPage([](FolderPage& page) { page.goForward(); }));
    const size_t enclosing = go->addItem(tr("Enclosing Folder"), onPage([](FolderPage& page) { page.model().up(); }));
    go->addSeparator();
    const size_t location = go->addItem(tr("Go to Folder…"), [this] { goToFolder(); });
    go->setItemKeyEquivalent(location, "g", control | shift);
    const size_t server = go->addItem(tr("Connect to Server…"), [this] { m_sidebar->connectToServer(); });
    go->setItemKeyEquivalent(server, "k", control);
    go->onAboutToShow = [this, go, back, forward, enclosing, alt] {
        FolderPage* page = currentPage();
        go->setItemEnabled(back, page && page->model().canGoBack());
        go->setItemEnabled(forward, page && page->model().canGoForward());
        const bool command = ui::commandKeys();
        go->setItemKeyEquivalent(back, command ? "[" : "Left", command ? ui::ControlModifier : alt);
        go->setItemKeyEquivalent(forward, command ? "]" : "Right", command ? ui::ControlModifier : alt);
        go->setItemKeyEquivalent(enclosing, "Up", command ? ui::ControlModifier : alt);
    };

    ui::Menu* windowMenu = m_mainMenu->addSubmenu(tr("Window"));
    windowMenu->setRole(ui::MenuRole::Windows);
    windowMenu->addItem(tr("Minimize"), [this] { m_window->minimize(); });
    windowMenu->addItem(tr("Zoom"), [this] { m_window->setMaximized(!m_window->states().maximized); });
    windowMenu->addSeparator();
    const size_t previousTab = windowMenu->addItem(tr("Show Previous Tab"), [this] {
        setCurrentTab((m_currentTab + tabCount() - 1) % std::max(1, tabCount()));
    });
    windowMenu->setItemKeyEquivalent(previousTab, "Tab", control | shift);
    const size_t nextTab = windowMenu->addItem(tr("Show Next Tab"), [this] {
        setCurrentTab((m_currentTab + 1) % std::max(1, tabCount()));
    });
    windowMenu->setItemKeyEquivalent(nextTab, "Tab", control);
    windowMenu->onAboutToShow = [this, windowMenu, previousTab, nextTab] {
        windowMenu->setItemEnabled(previousTab, tabCount() > 1);
        windowMenu->setItemEnabled(nextTab, tabCount() > 1);
    };

    m_window->setMainMenu(m_mainMenu.get());
}

void FolderWindow::goToFolder()
{
    if (m_goToFolder)
        return;
    auto* page = currentPage();
    if (!page)
        return;
    m_goToFolder = std::make_unique<GoToFolderDialog>(m_window.get(), page->model().url());
    m_goToFolder->onFolder = [this](const std::string& url) {
        if (currentPage()) {
            currentPage()->model().setTagFilter(-1);
            m_sidebar->setSelectedTag(-1);
            currentPage()->openUrl(url);
        }
    };
    m_goToFolder->onDone = [this] {
        base::EventLoop::current()->post([this, alive = m_alive] {
            if (*alive)
                m_goToFolder.reset();
        });
    };
    m_goToFolder->show();
}

void FolderWindow::openFolders(const std::vector<std::string>& paths)
{
    m_host->openFolders(paths);
}

void FolderWindow::showProperties(const FileItemList& items)
{
    m_host->showProperties(items, m_window.get());
}

void FolderWindow::showOpenWith(const std::string& url)
{
    m_host->showOpenWith(url, m_window.get());
}

void FolderWindow::confirmDelete(const std::vector<std::string>& urls)
{
    m_host->confirmDelete(urls, m_window.get());
}

void FolderWindow::confirmEmptyTrash()
{
    m_host->confirmEmptyTrash(m_window.get());
}

void FolderWindow::confirmLaunch(const std::string& path)
{
    m_host->confirmLaunch(path, m_window.get());
}

void FolderWindow::installBundleRunner(const std::string& path)
{
    m_host->installBundleRunner(path, m_window.get());
}

void FolderWindow::notify(const std::string& message)
{
    m_toast->show(message, std::chrono::milliseconds(3000));
}

} // namespace fm
