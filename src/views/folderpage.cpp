// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "views/folderpage.h"
#include "views/browserviews.h"
#include "views/previewclient.h"
#include "core/preferences.h"
#include "core/standardfolders.h"
#include "model/foldermodel.h"
#include "views/gridview.h"
#include "views/listview.h"
#include "views/pathbar.h"
#include "views/style.h"

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/url.h"
#include "ui/color_provider.h"
#include "ui/header_backdrop.h"
#include "ui/image_view.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/shortcut_convention.h"
#include "ui/cell_layout.h"
#include "ui/theme.h"

#include <algorithm>
#include <charconv>
#include <sys/statvfs.h>

using cutefish::i18n::format;
using cutefish::i18n::tr;

namespace fm {

static constexpr double kSmallSpacing = 6;
static constexpr double kLargeSpacing = 12;

FolderPage::FolderPage(FolderModelHost* host, const Start& start)
    : m_model(std::make_unique<FolderModel>(host))
    , m_preview(std::make_unique<PreviewClient>())
{
    setName("folderPage");
    setClipsChildren(true);

    m_backgroundImage = addChild<ui::ImageView>();
    m_backgroundImage->setFillMode(ui::ImageView::FillMode::PreserveAspectCrop);
    m_backgroundImage->setAutoTransform(true);
    m_backgroundImage->setVisible(false);

    m_noResults = addChild<ui::Label>(tr("No Results"));
    m_noResults->setFontSize(15 * 4.0 / 3);
    m_noResults->setVisible(false);
    m_inspector = addChild<FilePreviewPane>(true);
    m_inspector->onQuickLook = [this] { quickLook(); };
    m_inspector->onOpen = [this] { m_model->openSelected(); };
    m_inspector->onProperties = [this] { m_model->openPropertiesDialog(); };
    m_status = addChild<ui::Label>();
    m_status->setColor(ui::ColorId::DisabledText);
    m_status->setTextAlignment(ui::Label::TextAlignment::Center);
    m_status->setVerticallyCentered(true);
    m_status->setFontSize(11);

    FolderModel& model = *m_model;
    m_subscriptions.push_back(Preferences::instance().changed.connect([this] {
        const int mode = Preferences::instance().viewMethod();
        if (m_lastDefaultMode != mode) {
            m_lastDefaultMode = mode;
            chooseView(mode);
        }
        applySort();
        updateDetails();
    }));
    m_subscriptions.push_back(model.urlChanged.connect([this] {
        const int mode = locationView();
        if (mode != m_viewMethod)
            createView(mode);
        applySort();
        if (m_view) {
            m_view->reset();
            if (m_columns)
                m_columns->updatePath();
            m_pendingViewPosition = m_model->viewPosition();
            focusView();
        }
        updateDetails();
        currentUrlChanged.emit();
    }));
    m_subscriptions.push_back(model.aboutToLeaveFolder.connect([this] {
        // Left again before the offset was reached: it still stands.
        if (m_view)
            m_model->setViewPosition(std::isnan(m_pendingViewPosition) ? m_view->contentY() : m_pendingViewPosition);
        m_pendingViewPosition = NAN;
    }));
    m_subscriptions.push_back(model.listingCompleted.connect([this] {
        restoreViewPosition(true);
        requestLayout();
    }));
    m_subscriptions.push_back(model.countChanged.connect([this] {
        restoreViewPosition(false);
        updateDetails();
        requestLayout();
    }));
    m_subscriptions.push_back(model.selectionChanged.connect([this] { updateDetails(); }));
    // An item asked for, such as one shown in its folder, wins over the saved offset.
    m_subscriptions.push_back(model.scrollToItem.connect([this](int) {
        m_pendingViewPosition = NAN;
    }));
    // Not a new location: the path editor keeps the keys while the scope changes.
    m_subscriptions.push_back(model.searchChanged.connect([this] { requestLayout(); }));
    m_subscriptions.push_back(model.openTabRequested.connect([this](const std::string& url) {
        if (onOpenInNewTab)
            onOpenInNewTab(url);
    }));

    m_lastDefaultMode = Preferences::instance().viewMethod();

    m_model->setUrl(start.url.empty() ? FolderModel::resolve("~") : start.url);
    if (!m_view)
        createView(locationView());
    if (!start.back.empty() || !start.forward.empty())
        m_model->restoreHistory(start.back, start.forward);
    if (!start.selection.empty())
        m_model->selectUrlsWhenListed(start.selection);
    if (!start.extract.empty())
        m_model->extractArchive(start.extract);
    applySort();
}

FolderPage::~FolderPage() = default;

void FolderPage::activatePreview()
{
    m_preview->activate();
}

void FolderPage::quickLook() { m_preview->toggle(); }

std::string FolderPage::currentUrl() const
{
    return m_model->url();
}

Preferences::ViewOptions FolderPage::viewOptions() const
{
    return Preferences::instance().viewOptions(m_model->resolvedUrl(), m_viewMethod);
}

void FolderPage::setViewOptions(const Preferences::ViewOptions& options)
{
    Preferences::instance().setViewOptions(m_model->resolvedUrl(), m_viewMethod, options);
}

int FolderPage::locationView() const
{
    const auto& prefs = Preferences::instance();
    const auto url = m_model->resolvedUrl();
    if (const int mode = prefs.pinnedView(url); mode >= 0)
        return mode;
    if (const auto local = base::url::toLocalFile(url); local && local->starts_with('/')) {
        for (auto path = *local; path != "/";) {
            auto parent = parentPath(path);
            if (parent.empty() || parent == path)
                break;
            const auto parentUrl = FolderModel::resolve(parent);
            if (prefs.browseInPinnedView(parentUrl))
                return prefs.pinnedView(parentUrl);
            path = std::move(parent);
        }
    }
    return prefs.viewMethod();
}

void FolderPage::chooseView(int mode)
{
    if (mode == m_viewMethod)
        return;
    createView(mode);
    applySort();
}

void FolderPage::applySort()
{
    const auto& preferences = Preferences::instance();
    const auto options = viewOptions();
    const bool trash = m_model->isTrash();
    m_model->setSortMode(trash ? preferences.trashSortMode() : std::max(int(Preferences::Name), options.sort));
    m_model->setSortDesc(trash ? preferences.trashSortDesc() : options.sort != Preferences::Manual && options.descending);
    m_model->setSortDirsFirst(preferences.option(Preferences::FoldersFirst));
    m_model->setGroupBy(trash ? Preferences::NoGroup : options.group);
}

std::string FolderPage::folderName() const
{
    return m_model->folderName();
}

std::string FolderPage::tabTitle() const
{
    if (!m_model->searchText().empty())
        return format(tr("Searching “{text}”"), {{"text", m_model->searchText()}});
    return folderName();
}

// Under search results, where the selected result is.
// A crumb opens its folder; the result's own crumb opens its folder with it
// selected. Presses on it do not reach the rows scrolling beneath.
class SearchPathStrip : public ui::View
{
public:
    explicit SearchPathStrip(FolderPage* page)
        : m_page(page)
    {
        m_crumbs = addChild<Breadcrumbs>(false);
        m_crumbs->onItemClicked = [this](const std::string& path) {
            if (path == m_path) {
                if (!m_page->model().searchText().empty())
                    m_page->model().showInFolder();
                else
                    m_page->model().openSelected();
            }
            else
                m_page->openUrl(path);
        };
    }

    void setBackdrop(ui::ScrollView* view)
    {
        if (m_backdrop) {
            std::shared_ptr<View> old = takeChild(m_backdrop);
            base::EventLoop::current()->post([old] {});
        }
        m_backdrop = addChild<ui::HeaderBackdrop>(view, ui::HeaderBackdrop::Edge::Bottom);
        m_backdrop->setSeparatorVisible(false);
        raiseChild(m_crumbs);
    }

    void update()
    {
        m_path = m_page->model().selectedPath();
        m_crumbs->setCrumbs(pathCrumbs(m_path.empty() ? m_page->currentUrl() : base::url::fromLocalFile(m_path)));
        requestLayout();
    }

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override
    {
        const ui::RectF rect = localRect();
        if (m_backdrop) {
            m_backdrop->setBaseColor(ui::ColorId::PanelBackground);
            m_backdrop->setGeometry(rect);
        }
        // Crumb text then starts where the scope bar's label does.
        m_crumbs->setGeometry({kSmallSpacing / 2, 0, std::max(0.0, rect.width - kSmallSpacing / 2 - kLargeSpacing),
                               rect.height});
    }

private:
    FolderPage* m_page;
    Breadcrumbs* m_crumbs;
    ui::HeaderBackdrop* m_backdrop = nullptr;
    std::string m_path;
};

void FolderPage::retranslate()
{
    m_noResults->setText(tr("No Results"));
    m_model->retranslate();
    if (m_view)
        m_view->retranslate();
    m_pathStrip->update();
    updateDetails();
    requestLayout();
}

void FolderPage::createView(int mode)
{
    if (m_viewMethod == Preferences::List)
        m_model->contents().collapseAll();
    m_viewMethod = mode;
    if (m_viewRoot) {
        std::shared_ptr<View> old = takeChild(m_viewRoot);
        base::EventLoop::current()->post([old] {});
    }
    if (m_gallery) {
        std::shared_ptr<View> old = takeChild(m_gallery);
        base::EventLoop::current()->post([old] {});
    }
    m_columns = nullptr;
    m_gallery = nullptr;
    m_backdrop = nullptr;

    FolderView* view;
    if (m_viewMethod == Preferences::List)
        view = addChild<FolderListView>(*m_model, false, true);
    else if (m_viewMethod == Preferences::Columns) {
        m_columns = addChild<ColumnBrowser>(*m_model);
        view = m_columns->activeView();
        m_columns->preview()->onQuickLook = [this] { quickLook(); };
        m_columns->preview()->onProperties = [this] { m_model->openPropertiesDialog(); };
    } else if (m_viewMethod == Preferences::Gallery) {
        m_gallery = addChild<FilePreviewPane>(false);
        view = addChild<Filmstrip>(*m_model);
    }
    else
        view = addChild<FolderGridView>(*m_model);
    m_viewRoot = m_columns ? static_cast<ui::View*>(m_columns) : view;
    view->setBottomCornerRadius(ui::Theme::current().windowRadius);
    m_view = view;
    m_preview->attach(m_model.get(), view);
    view->onContextMenu = [this](ui::PointF position) {
        if (onContextMenu)
            onContextMenu(position);
    };
    view->onKeyPressed = [this](const ui::KeyEvent& event) { return handleKey(event); };

    // Inside the view, so the list's column header stays above the blur.
    if (!m_columns && !m_gallery) {
        m_backdrop = view->addChild<ui::HeaderBackdrop>(view);
        m_backdrop->setBaseColor(ui::ColorId::PanelBackground);
        m_backdrop->setSeparatorVisible(m_viewMethod != Preferences::List);
    }
    if (!m_pathStrip) {
        m_pathStrip = addChild<SearchPathStrip>(this);
    }
    raiseChild(m_pathStrip);
    raiseChild(m_status);
    raiseChild(m_inspector);
    raiseChild(m_noResults);
    updateDetails();
    requestLayout();
    focusView();
}

void FolderPage::updateDetails()
{
    if (!m_pathStrip)
        return;
    const auto& prefs = Preferences::instance();
    const auto& model = *m_model;
    m_pathStrip->update();
    const auto rows = model.selectedRows();
    const int row = rows.empty() ? -1 : model.isSelected(model.currentIndex()) ? model.currentIndex() : rows.front();
    const auto item = model.item(row);
    m_inspector->setVisible(!m_columns && prefs.option(Preferences::PreviewPane));
    if (m_inspector->isVisible())
        m_inspector->setItem(rows.size() == 1 ? item : nullptr, row >= 0 ? model.displayName(row) : std::string());
    if (m_gallery) {
        const int hero = row >= 0 ? row : model.count() ? 0 : -1;
        m_gallery->setItem(model.item(hero), hero >= 0 ? model.displayName(hero) : std::string());
    }
    m_status->setVisible(prefs.option(Preferences::StatusBar));
    if (m_status->isVisible()) {
        std::string text = rows.empty()
                ? cutefish::i18n::trn("{n} item", "{n} items", model.count())
                : format(tr("{selected} of {total} selected"), {{"selected", std::to_string(rows.size())}, {"total", std::to_string(model.count())}});
        if (!model.rootItem().path.empty()) {
            struct statvfs space {};
            if (!model.isTrash() && ::statvfs(model.rootItem().path.c_str(), &space) == 0)
                text += " · " + format(tr("{size} available"), {{"size", formatSize(uint64_t(space.f_bavail) * space.f_frsize)}});
        }
        m_status->setText(std::move(text));
    }
    requestLayout();
}

void FolderPage::setHeaderHeight(double height)
{
    if (m_headerHeight == height)
        return;
    m_headerHeight = height;
    requestLayout();
}

void FolderPage::layout()
{
    const ui::RectF rect = localRect();
    if (m_view) {
        const bool path = Preferences::instance().option(Preferences::PathBar) || !m_model->searchText().empty();
        const double statusHeight = m_status->isVisible() ? 23 : 0;
        const double pathHeight = path ? 25 : 0;
        const double bottom = rect.height - statusHeight - pathHeight;
        const double inspectorWidth = m_inspector->isVisible() ? std::min(250.0, rect.width * 0.45) : 0;
        const double width = rect.width - inspectorWidth;
        const auto background = m_viewMethod == Preferences::Grid && !m_model->isTrash() && !m_model->isSearching()
                ? viewOptions().background : std::string();
        m_backgroundBox = {0, m_headerHeight, width, std::max(0.0, bottom - m_headerHeight)};
        m_backgroundColor.reset();
        if (background.size() == 7 && background[0] == '#') {
            unsigned rgb = 0;
            const auto* end = background.data() + background.size();
            if (const auto result = std::from_chars(background.data() + 1, end, rgb, 16);
                result.ec == std::errc() && result.ptr == end)
                m_backgroundColor = ui::Color::rgb(rgb);
        }
        const std::string picture = background.starts_with('/') ? background : std::string();
        m_backgroundImage->setVisible(!picture.empty());
        if (m_backgroundImage->source() != picture)
            m_backgroundImage->setSource(picture);
        m_backgroundImage->setGeometry(m_backgroundBox);
        requestPaint();
        m_inspector->setGeometry({width, m_headerHeight, inspectorWidth, std::max(0.0, bottom - m_headerHeight)});
        if (m_columns) {
            m_columns->setGeometry({0, m_headerHeight, width, std::max(0.0, bottom - m_headerHeight)});
        } else if (m_gallery) {
            const double filmHeight = 93;
            m_view->setGeometry({0, bottom - filmHeight, width, filmHeight});
            m_gallery->setGeometry({0, m_headerHeight, width, std::max(0.0, bottom - filmHeight - m_headerHeight)});
        } else {
            m_view->setGeometry({0, 0, width, std::max(0.0, bottom)});
            m_view->setTopInset(m_headerHeight + (m_viewMethod == Preferences::List ? kSmallSpacing : 0));
            m_backdrop->setBaseColor(ui::ColorId::PanelBackground);
            m_backdrop->setVisible(m_view->topInset() > 0);
            m_backdrop->setGeometry({0, 0, width, m_view->headerHeight()});
        }
        m_pathStrip->setVisible(path);
        m_pathStrip->setGeometry({0, bottom, rect.width, pathHeight});
        m_status->setGeometry({0, rect.height - statusHeight, rect.width, statusHeight});
    }

    const FolderModel& model = *m_model;
    m_noResults->setVisible(!model.searchText().empty() && model.status() == FolderModel::Status::Ready
                            && model.count() == 0 && !model.searchBusy());
    const ui::SizeF size = m_noResults->sizeHint();
    m_noResults->setGeometry({ui::rounding::centeredStart(rect.width, size.width),
                              ui::rounding::centeredStart(rect.height, size.height), size.width, size.height});
}

void FolderPage::paint(ui::Painter& painter)
{
    painter.fillRect(localRect(), ui::color(ui::ColorId::PanelBackground));
    if (m_backgroundColor)
        painter.fillRect(m_backgroundBox, *m_backgroundColor);
}

void FolderPage::openUrl(const std::string& url)
{
    m_model->setUrl(url);
    if (m_columns)
        m_columns->updatePath(true);
    focusView();
}

void FolderPage::focusView()
{
    if (m_view && m_view->contentView())
        m_view->contentView()->setFocus();
}

void FolderPage::goBack()
{
    m_model->goBack();
}

void FolderPage::goForward()
{
    m_model->goForward();
}

// Rows arrive in batches: the saved offset is applied as soon as it is in
// reach, so the folder does not first show its top, and the nearest one
// once the listing is complete.
void FolderPage::restoreViewPosition(bool complete)
{
    if (std::isnan(m_pendingViewPosition) || !m_view)
        return;

    m_view->forceLayout();
    const double maximum = m_view->maximumContentY();
    if (!complete && m_pendingViewPosition > maximum)
        return;
    m_view->setContentY(std::clamp(m_pendingViewPosition, 0.0, maximum));
    m_pendingViewPosition = NAN;
}

// The view's keys before the view itself.
bool FolderPage::handleKey(const ui::KeyEvent& event)
{
    if (m_preview->filterKey(event))
        return true;

    FolderModel& model = *m_model;
    const uint32_t modifiers = event.modifiers;
    const uint32_t key = event.key;
    const bool control = modifiers & ui::ControlModifier;

    if (key == ui::key::Space && modifiers == ui::NoModifier) {
        if (!event.repeat)
            m_preview->toggle();
        return true;
    }

    if (ui::commandKeys()) {
        // Command reaches us as Ctrl; outside text fields these go to the file view.
        if (modifiers == ui::ControlModifier) {
            switch (key) {
            case ui::key::BackSpace:
                model.keyDeletePress();
                return true;
            case ui::key::Up:
                model.up();
                return true;
            case ui::key::Down:
                model.openSelected();
                return true;
            case '[':
                model.goBack();
                return true;
            case ']':
                model.goForward();
                return true;
            case 'i':
                model.openPropertiesDialog();
                return true;
            default:
                break;
            }
        }
        // Command+Shift+.; Shift makes the key ">" on most layouts.
        if (modifiers == (ui::ControlModifier | ui::ShiftModifier) && (key == '.' || key == '>')) {
            model.setShowHiddenFiles(!model.showHiddenFiles());
            return true;
        }
    }

    // Alt+Up, Alt+Left, Alt+Right and Alt+Enter, as in other file managers.
    if (modifiers == ui::AltModifier) {
        switch (key) {
        case ui::key::Up:
            model.up();
            return true;
        case ui::key::Left:
            model.goBack();
            return true;
        case ui::key::Right:
            model.goForward();
            return true;
        case ui::key::Return:
        case ui::key::KeypadEnter:
            model.openPropertiesDialog();
            return true;
        default:
            break;
        }
    }

    if (key == ui::key::Return || key == ui::key::KeypadEnter) {
        model.openSelected();
    } else if (key == 'c' && control) {
        model.copy();
    } else if (key == 'x' && control) {
        model.cut();
    } else if (key == 'w' && control) {
        if (onCloseRequested)
            onCloseRequested();
    } else if (key == 'v' && control) {
        model.paste();
    } else if (key == style::key::F2) {
        model.requestRename.emit();
    } else if (key == 'l' && control) {
        if (onRequestPathEditor)
            onRequestPathEditor();
    } else if (key == 'a' && control) {
        model.selectAll();
    } else if (key == ui::key::BackSpace) {
        model.up();
    } else if (key == ui::key::Delete) {
        model.keyDeletePress();
    } else if (key == style::key::F5) {
        model.refresh();
    } else if (key == 'h' && control) {
        model.setShowHiddenFiles(!model.showHiddenFiles());
    } else if (key == 'z' && control) {
        if (modifiers & ui::ShiftModifier)
            model.redo();
        else
            model.undo();
    } else {
        if (key >= 'a' && key <= 'z' && !control)
            model.keyboardSearch(event.text);
        // The event still reaches the view.
        return false;
    }
    // Taken here, before the view sees the key.
    return true;
}

} // namespace fm
