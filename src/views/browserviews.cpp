// SPDX-License-Identifier: BSD-3-Clause

#include "views/browserviews.h"

#include "core/dates.h"
#include "core/preferences.h"
#include "core/thumbnails.h"
#include "model/foldermodel.h"
#include "model/pathbarmodel.h"
#include "views/listview.h"
#include "views/style.h"

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/locale.h"
#include "ui/dialog_window.h"
#include "ui/icon.h"
#include "ui/image_view.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fm {

using cutefish::i18n::tr;
using cutefish::i18n::trNoop;

static std::string iconFor(const FileItem& item)
{
    return !item.iconName.empty() && !ui::themeIconPath(item.iconName, 96).empty()
            ? item.iconName : item.isDir ? "folder" : "unknown";
}

FilePreviewPane::FilePreviewPane(bool inspector)
    : m_inspector(inspector)
{
    setName(inspector ? "previewInspector" : "galleryPreview");
    setClipsChildren(true);
    m_fallback = addChild<FolderThemeIcon>("unknown", 84);
    m_image = addChild<ui::ImageView>();
    m_image->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
    m_image->setAutoTransform(true);
    m_image->setCornerRadius(5);
    m_image->onStatusChanged = [this](ui::ImageView::Status) { updateMetadata(); requestLayout(); };
    m_title = addChild<ui::Label>();
    m_title->setTextAlignment(ui::Label::TextAlignment::Center);
    m_title->setElide(ui::Label::Elide::Middle);
    m_title->setWeight(600);
    m_metadata = addChild<ui::Label>();
    m_metadata->setWrap(true);
    m_metadata->setColor(ui::ColorId::DisabledText);
    m_information = addChild<ui::Label>(tr("Information"));
    m_information->setWeight(650);
    m_quickActions = addChild<ui::Label>(tr("Quick Actions"));
    m_quickActions->setWeight(650);
    const char* detailNames[] = {trNoop("Created"), trNoop("Modified"), trNoop("Last Accessed"), trNoop("Dimensions")};
    for (size_t i = 0; i < m_detailLabels.size(); ++i) {
        m_detailLabels[i] = addChild<ui::Label>(tr(detailNames[i]));
        m_detailLabels[i]->setColor(ui::ColorId::DisabledText);
        m_detailValues[i] = addChild<ui::Label>();
        m_detailValues[i]->setTextAlignment(ui::Label::TextAlignment::Right);
        m_detailValues[i]->setElide(ui::Label::Elide::Right);
    }
    m_excerpt = addChild<ui::Label>();
    m_excerpt->setFontFamily("monospace");
    m_excerpt->setWrap(true);
    m_excerpt->setVerticallyCentered(false);
    m_primary = addChild<ui::DialogButton>(tr("Quick Look"));
    m_primary->onClicked = [this] {
        if (m_item && m_item->isDir) {
            if (onOpen)
                onOpen();
        } else if (onQuickLook) {
            onQuickLook();
        }
    };
    m_info = addChild<ui::DialogButton>(tr("Get Info"));
    m_info->onClicked = [this] { if (onProperties) onProperties(); };
    m_rotate = addChild<ui::DialogButton>(tr("Rotate Preview"));
    m_rotate->onClicked = [this] { m_image->setQuarterTurns(++m_turns % 4); requestLayout(); };
    setItem(nullptr);
}

FilePreviewPane::~FilePreviewPane()
{
    *m_alive = false;
    m_textJob.cancel();
}

static std::string textExcerpt(const std::string& path)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return {};
    struct stat st {};
    char buffer[4096];
    const ssize_t size = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) ? ::read(fd, buffer, sizeof(buffer)) : -1;
    ::close(fd);
    if (size <= 0 || std::find(buffer, buffer + size, '\0') != buffer + size)
        return {};
    return {buffer, size_t(size)};
}

void FilePreviewPane::setItem(FileItemPtr item, std::string name)
{
    if (m_item == item && item && m_title->text() == (name.empty() ? item->text() : name))
        return;
    m_textJob.cancel();
    m_item = std::move(item);
    m_imageInfo = {};
    m_excerpt->setText({});
    m_turns = 0;
    m_image->setQuarterTurns(0);
    m_image->setSource({});
    m_image->setImage({});
    m_title->setText(m_item ? (name.empty() ? m_item->text() : std::move(name)) : std::string(tr("No Selection")));
    const bool local = m_item && m_item->isLocalFile() && m_item->isFile && !thumbnails::isOnMount(m_item->path);
    if (m_item)
        m_fallback->setIconName(iconFor(*m_item));
    m_fallback->setFolderPath(m_item && m_item->isDir && !m_item->isLink && m_item->isLocalFile() ? m_item->path : std::string());
    if (local && m_item->mimeType.starts_with("image/")) {
        m_image->setSource(m_item->path);
        if (m_inspector) {
            m_textJob = base::ThreadPool::global().run([item = m_item] { return ui::readImageInfo(item->path, item->mimeType); },
                [this, alive = m_alive, item = m_item](ui::ImageInfo info) {
                    if (*alive && item == m_item) {
                        m_imageInfo = info;
                        updateMetadata();
                        requestLayout();
                    }
                });
        }
    } else if (local && m_item->mimeType.starts_with("text/")) {
        m_textJob = base::ThreadPool::global().run([path = m_item->path] { return textExcerpt(path); },
                [this, alive = m_alive, item = m_item](std::string text) {
                    if (*alive && item == m_item) {
                        m_excerpt->setText(std::move(text));
                        requestLayout();
                    }
                });
    } else if (local && thumbnails::canThumbnail(m_item->mimeType)) {
        const int size = m_inspector ? 400 : 1024;
        thumbnails::load(m_item->path, m_item->modified, size, size,
                [this, alive = m_alive, item = m_item](const ui::Bitmap& image) {
                    if (*alive && item == m_item && !image.isNull())
                        m_image->setPixels(image.width(), image.height(), reinterpret_cast<const uint32_t*>(image.bits()), image.stride(), true);
                });
    }
    updateMetadata();
    requestLayout();
}

void FilePreviewPane::updateMetadata()
{
    if (!m_item) {
        m_metadata->setText({});
        for (auto* value : m_detailValues)
            value->setText({});
        return;
    }
    std::string text = m_item->isDir ? tr("Folder") : m_item->typeDescription.empty() ? m_item->mimeType : m_item->typeDescription;
    if (!m_item->isDir)
        text += " · " + formatSize(m_item->size);
    const int64_t times[] = {m_item->created, m_item->modified, m_item->accessed};
    for (size_t i = 0; i < 3; ++i)
        m_detailValues[i]->setText(times[i] > 0 ? dates::columnTime(times[i]) : std::string());
    m_detailValues[3]->setText(m_imageInfo.width > 0 && m_imageInfo.height > 0
        ? std::to_string(m_imageInfo.width) + " × " + std::to_string(m_imageInfo.height) : std::string());
    m_metadata->setText(std::move(text));
}

void FilePreviewPane::layout()
{
    const auto box = localRect();
    const double margin = 14;
    const double width = std::max(0.0, box.width - 2 * margin);
    const bool image = m_item && m_image->status() == ui::ImageView::Status::Ready;
    const bool excerpt = m_item && !m_excerpt->text().empty();
    const double heroHeight = std::max(0.0, m_inspector ? std::min(260.0, box.height * 0.45) : box.height - 52);
    const double iconSize = std::max(0.0, std::min({m_inspector ? 88.0 : 132.0, width, heroHeight}));
    m_image->setVisible(m_item != nullptr);
    m_image->setGeometry({margin, 16, width, std::max(0.0, heroHeight - 22)});
    m_fallback->setVisible(m_item && !image && !excerpt);
    m_fallback->setGeometry({(box.width - iconSize) / 2, (heroHeight - iconSize) / 2, iconSize, iconSize});
    m_excerpt->setVisible(excerpt);
    m_excerpt->setClipsChildren(true);
    m_excerpt->setMaximumLineCount(std::max(1, int((heroHeight - 8) / 15)));
    m_excerpt->setGeometry({margin, 8, width, std::max(0.0, heroHeight - 8)});
    const double y = m_item ? heroHeight : box.height / 2 - 11;
    m_title->setTextAlignment(m_inspector && m_item ? ui::Label::TextAlignment::Left : ui::Label::TextAlignment::Center);
    m_title->setFontSize(m_inspector && m_item ? 15 : 13);
    m_title->setGeometry({margin, y, width, 22});
    const double metadata = m_metadata->heightForWidth(width);
    m_metadata->setGeometry({margin, y + 23, width, metadata});
    m_metadata->setTextAlignment(m_inspector ? ui::Label::TextAlignment::Left : ui::Label::TextAlignment::Center);
    double actions = y + 34 + metadata;
    const bool details = m_inspector && m_item;
    bool heading = false;
    for (size_t i = 0; i < m_detailValues.size(); ++i) {
        const bool shown = details && !m_detailValues[i]->text().empty();
        m_detailLabels[i]->setVisible(shown);
        m_detailValues[i]->setVisible(shown);
        if (!shown) continue;
        if (!heading) {
            m_information->setGeometry({margin, actions, width, 20});
            actions += 23;
            heading = true;
        }
        m_detailLabels[i]->setGeometry({margin, actions, width * 0.38, 21});
        m_detailValues[i]->setGeometry({margin + width * 0.38, actions, width * 0.62, 21});
        actions += 21;
    }
    m_information->setVisible(heading);
    m_quickActions->setVisible(details && actions + 58 <= box.height);
    m_quickActions->setGeometry({margin, actions + 7, width, 20});
    if (m_quickActions->isVisible()) actions += 31;
    m_primary->setText(m_item && m_item->isDir ? tr("Open") : tr("Quick Look"));
    m_primary->setVisible(m_inspector && m_item && actions + 24 <= box.height);
    m_info->setVisible(m_primary->isVisible());
    m_primary->setGeometry({margin, actions, std::max(0.0, width * 0.55 - 3), 24});
    m_info->setGeometry({margin + width * 0.55 + 3, actions, std::max(0.0, width * 0.45 - 3), 24});
    m_rotate->setVisible(m_primary->isVisible() && image && m_item->mimeType.starts_with("image/") && actions + 56 <= box.height);
    m_rotate->setGeometry({margin, actions + 30, width, 24});
}

void FilePreviewPane::paint(ui::Painter& painter)
{
    painter.fillRect(localRect(), ui::Theme::current().secondBackground);
    if (m_inspector)
        painter.fillRect({0, 0, 1, geometry().height}, style::textAlpha(0.13));
}

static constexpr double kTileWidth = 94;
static constexpr double kTileMargin = 8;

class Filmstrip::Tile : public ui::View
{
public:
    Tile()
    {
        m_icon = addChild<FolderThemeIcon>();
        m_photo = addChild<ui::ImageView>();
        m_photo->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
        m_photo->setCornerRadius(4);
        m_photo->setThumbnail(true);
        m_photo->onStatusChanged = [this](ui::ImageView::Status status) { m_icon->setVisible(status != ui::ImageView::Status::Ready); };
        m_name = addChild<ui::Label>();
        m_name->setElide(ui::Label::Elide::Middle);
        m_name->setTextAlignment(ui::Label::TextAlignment::Center);
        m_name->setFontSize(11);
    }

    void bind(FileItemPtr item, bool thumbnail, std::string name, bool selected, bool showName)
    {
        if (m_item != item) {
            m_item = item;
            m_icon->setIconName(iconFor(*item));
            m_icon->setFolderPath(item->isDir && !item->isLink && item->isLocalFile() ? item->path : std::string());
            m_photo->setSource(thumbnail && item->mimeType.starts_with("image/") ? item->path : std::string());
        }
        m_name->setText(std::move(name));
        m_name->setColor(selected ? style::selectedTextColor() : ui::Theme::current().text);
        m_name->setVisible(showName);
        m_selected = selected;
        requestPaint();
    }
    ui::RectF labelRect() const { return m_name->geometry(); }

protected:
    void layout() override
    {
        const auto r = localRect();
        m_icon->setGeometry({(r.width - 44) / 2, 7, 44, 48});
        m_photo->setGeometry({5, 5, std::max(0.0, r.width - 10), 54});
        m_name->setGeometry({3, 60, r.width - 6, 16});
    }
    void paint(ui::Painter& painter) override
    {
        const auto r = localRect();
        if (m_selected)
            painter.drawSmoothRectangle({1, 2, r.width - 2, 60}, 7, style::textAlpha(0.13));
        if (m_selected && m_name->isVisible())
            painter.drawSmoothRectangle({1, 59, r.width - 2, 17}, 5, ui::Theme::current().highlight);
    }

private:
    FileItemPtr m_item;
    FolderThemeIcon* m_icon;
    ui::ImageView* m_photo;
    ui::Label* m_name;
    bool m_selected = false;
};

Filmstrip::Filmstrip(FolderModel& model) : FolderView(model)
{
    setName("galleryFilmstrip");
    setScrollDirections(true, false);
    contentView()->setLayoutHints({.fillWidth = true});
}

double Filmstrip::minimumContentWidth() const { return 2 * kTileMargin + kTileWidth * model().count(); }

int Filmstrip::indexAt(ui::PointF point) const
{
    if (point.x < kTileMargin || point.y < 0 || point.y >= 81)
        return -1;
    const int index = int((point.x - kTileMargin) / kTileWidth);
    return index < model().count() ? index : -1;
}

bool Filmstrip::hitsItem(int index, ui::PointF point) const { return indexAt(point) == index; }
int Filmstrip::dropRowAt(ui::PointF point) const
{
    const int row = indexAt(point);
    auto item = model().item(row);
    return item && item->isDir ? row : -1;
}

void Filmstrip::rectangleSelect(const ui::RectF& band)
{
    std::vector<int> rows;
    const int first = std::max(0, int(std::floor((band.x - kTileMargin) / kTileWidth)));
    const int last = std::min(model().count() - 1, int(std::floor((band.x + band.width - kTileMargin) / kTileWidth)));
    for (int i = first; i <= last; ++i)
        rows.push_back(i);
    applyRectangleSelection(rows);
}

std::optional<ui::RectF> Filmstrip::dragRect(int index) const
{
    if (index < 0 || index >= model().count())
        return {};
    return ui::RectF {kTileMargin + index * kTileWidth, 1, kTileWidth - 6, 78};
}

std::optional<ui::RectF> Filmstrip::labelRect(int index) const
{
    if (const auto tile = m_tiles.find(index); tile != m_tiles.end()) {
        const auto label = tile->second->labelRect();
        const auto start = fromContent({tile->second->geometry().x + label.x, 1 + label.y});
        return ui::RectF {start.x, start.y, label.width, label.height};
    }
    return {};
}

void Filmstrip::forEachItemView(const std::function<void(int, ui::View*)>& visit) const
{
    for (const auto& [row, tile] : m_tiles)
        visit(row, tile);
}

void Filmstrip::updateItems()
{
    const auto options = Preferences::instance().viewOptions(model().resolvedUrl(), Preferences::Gallery);
    const int count = model().count();
    const double x = scrollPosition().x - kTileMargin;
    const int first = std::clamp(int(std::floor(x / kTileWidth)), 0, count);
    const int end = std::clamp(int(std::ceil((x + geometry().width) / kTileWidth)), first, count);
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (it->first >= first && it->first < end) {
            ++it;
        } else {
            it->second->setVisible(false);
            m_pool.push_back(it->second);
            it = m_tiles.erase(it);
        }
    }
    for (int row = first; row < end; ++row) {
        auto& tile = m_tiles[row];
        if (!tile) {
            tile = m_pool.empty() ? contentView()->addChild<Tile>() : m_pool.back();
            if (!m_pool.empty())
                m_pool.pop_back();
            tile->setVisible(true);
        }
        tile->setGeometry(*dragRect(row));
        tile->bind(model().item(row), model().showsThumbnail(row), model().displayName(row), model().isSelected(row),
                   options.option(Preferences::GalleryNames));
        tile->setVisible(!isRenaming() || currentIndex() != row);
    }
}

void Filmstrip::revealIndex(int index)
{
    if (const auto rect = dragRect(index))
        ensureVisible(*rect);
}

bool Filmstrip::navigate(const ui::KeyEvent& event)
{
    const bool back = event.key == ui::key::Left || event.key == ui::key::Up;
    if (!back && event.key != ui::key::Right && event.key != ui::key::Down)
        return false;
    if (model().count()) {
        const int from = keyboardIndex(!back);
        const int next = std::clamp(from + (back ? -1 : 1), 0, model().count() - 1);
        if (!(event.modifiers & ui::ShiftModifier))
            setAnchorIndex(next);
        setCurrentIndex(next);
        updateSelection(event.modifiers);
        revealIndex(next);
    }
    return true;
}

bool Filmstrip::scroll(const ui::ScrollEvent& event)
{
    auto horizontal = event;
    if (!event.deltaX && !event.value120X) {
        horizontal.deltaX = event.deltaY;
        horizontal.value120X = event.value120Y;
        horizontal.deltaY = 0;
        horizontal.value120Y = 0;
    }
    return FolderView::scroll(horizontal);
}

class ColumnBrowser::Chain : public ui::View
{
public:
    struct Ancestor {
        std::string url;
        std::shared_ptr<FolderModel> model;
        FolderListView* view;
    };

    explicit Chain(FolderModel& model) : active(addChild<FolderListView>(model, true)) {}

    void trim(size_t size)
    {
        while (ancestors.size() > size) {
            std::shared_ptr<ui::View> view = takeChild(ancestors.back().view);
            base::EventLoop::current()->post([view, model = ancestors.back().model] {});
            ancestors.pop_back();
        }
    }

    ~Chain() override
    {
        for (const auto& ancestor : ancestors)
            takeChild(ancestor.view);
    }

    double width(const FolderModel& model) const
    {
        const auto options = Preferences::instance().viewOptions(model.resolvedUrl(), Preferences::Columns);
        if (!options.option(Preferences::ColumnFit))
            return 218;
        double width = 218;
        // Measure a bounded sample so a large network folder never blocks layout.
        for (int i = 0; i < std::min(model.count(), 250); ++i)
            width = std::max(width, style::advanceWidth(model.displayName(i)) + 62);
        return std::min(360.0, width);
    }

    ui::SizeF sizeHint() const override
    {
        double total = width(active->model());
        for (const auto& ancestor : ancestors)
            total += width(*ancestor.model);
        if (preview && preview->isVisible())
            total += 246;
        return {total, 0};
    }

    void layout() override
    {
        double x = 0;
        const auto put = [&](FolderListView* view) {
            const double w = width(view->model());
            view->setGeometry({x, 0, w, geometry().height});
            x += w;
        };
        for (const auto& ancestor : ancestors)
            put(ancestor.view);
        put(active);
        if (preview->isVisible())
            preview->setGeometry({x, 0, std::max(246.0, geometry().width - x), geometry().height});
        requestPaint();
    }

    void paint(ui::Painter& painter) override
    {
        for (const auto& ancestor : ancestors) {
            const auto rect = ancestor.view->geometry();
            painter.fillRect({rect.x + rect.width - 1, 0, 1, geometry().height}, style::textAlpha(0.13));
        }
        if (!preview->isVisible()) {
            const auto rect = active->geometry();
            painter.fillRect({rect.x + rect.width - 1, 0, 1, geometry().height}, style::textAlpha(0.13));
        }
    }

    std::vector<Ancestor> ancestors;
    FolderListView* active;
    FilePreviewPane* preview = nullptr;
};

ColumnBrowser::ColumnBrowser(FolderModel& active) : m_model(active)
{
    setName("columnBrowser");
    setScrollDirections(true, false);
    m_chain = setContent<Chain>(active);
    m_chain->setLayoutHints({.fillWidth = true});
    m_preview = m_chain->preview = m_chain->addChild<FilePreviewPane>(true);
    m_chain->active->onItemClicked = [this](int row) {
        auto item = m_model.item(row);
        if (item && item->isDir) {
            if (m_anchor.empty())
                m_anchor = m_model.resolvedUrl();
            m_model.openSelected();
        }
    };
    m_subscriptions.push_back(active.selectionChanged.connect([this] { updatePreview(); }));
    m_subscriptions.push_back(active.rowsChanged.connect([this](const ContentsChange&) { updatePreview(); requestLayout(); }));
    m_subscriptions.push_back(Preferences::instance().changed.connect([this] { updatePath(); updatePreview(); }));
    updatePath(true);
    updatePreview();
}

FolderView* ColumnBrowser::activeView() const { return m_chain->active; }

void ColumnBrowser::updatePath(bool resetAnchor)
{
    const auto current = m_model.resolvedUrl();
    if (resetAnchor || m_anchor.empty())
        m_anchor = current;
    auto crumbs = pathCrumbs(current);
    bool belongs = false;
    for (const auto& crumb : crumbs)
        belongs = belongs || FolderModel::resolve(crumb.path) == m_anchor;
    if (!belongs)
        m_anchor = current;

    const auto& prefs = Preferences::instance();
    size_t depth = 0;
    bool started = false;
    for (size_t i = 0; i + 1 < crumbs.size(); ++i) {
        const auto url = FolderModel::resolve(crumbs[i].path);
        started = started || url == m_anchor;
        if (!started)
            continue;
        const auto child = FolderModel::resolve(crumbs[i + 1].path);
        if (depth == m_chain->ancestors.size() || m_chain->ancestors[depth].url != url) {
            m_chain->trim(depth);
            auto model = std::make_shared<FolderModel>();
            auto* view = m_chain->addChild<FolderListView>(*model, true);
            view->setChooserMode(false);
            auto navigate = [this, url, model = model.get()](int row) {
                const auto item = model->item(row);
                if (!item)
                    return;
                if (item->isDir) {
                    if (item->url != m_model.resolvedUrl())
                        m_model.setUrl(item->url);
                } else {
                    m_model.setUrl(url);
                    m_model.selectUrlsWhenListed({item->url});
                }
            };
            view->onItemClicked = navigate;
            view->onActivated = navigate;
            view->onKeyPressed = [this](const ui::KeyEvent& event) { return m_chain->active->sendKey(event); };
            model->setUrl(url);
            m_chain->ancestors.push_back({url, model, view});
        }
        auto& model = *m_chain->ancestors[depth++].model;
        const auto options = prefs.viewOptions(url, Preferences::Columns);
        model.setSortMode(options.sort);
        model.setSortDesc(options.descending);
        model.setSortDirsFirst(prefs.option(Preferences::FoldersFirst));
        model.setGroupBy(options.group);
        if (model.selectedUrls() != std::vector<std::string>{child}) {
            model.clearSelection();
            model.selectUrlsWhenListed({child});
        }
    }
    m_chain->trim(depth);
    m_reveal = true;
    m_chain->requestLayout();
    requestLayout();
}

void ColumnBrowser::updatePreview()
{
    const auto rows = m_model.selectedRows();
    const auto item = rows.size() == 1 ? m_model.item(rows.front()) : nullptr;
    const auto options = Preferences::instance().viewOptions(m_model.resolvedUrl(), Preferences::Columns);
    const bool showing = item && !item->isDir && options.option(Preferences::ColumnPreview);
    if (showing != m_preview->isVisible())
        m_reveal = true;
    m_preview->setVisible(showing);
    m_preview->setItem(showing ? item : nullptr, showing ? m_model.displayName(rows.front()) : std::string());
    requestLayout();
}

void ColumnBrowser::layout()
{
    ScrollView::layout();
    if (std::exchange(m_reveal, false))
        setScrollPosition({std::max(0.0, contentSize().width - geometry().width), 0});
}

bool ColumnBrowser::scroll(const ui::ScrollEvent& event)
{
    if (event.deltaX || event.value120X || event.stop)
        return ScrollView::scroll(event);
    if (event.modifiers & ui::ShiftModifier) {
        auto horizontal = event;
        horizontal.deltaX = event.deltaY;
        horizontal.value120X = event.value120Y;
        horizontal.deltaY = 0;
        horizontal.value120Y = 0;
        return ScrollView::scroll(horizontal);
    }
    return false;
}

} // namespace fm
