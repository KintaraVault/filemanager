// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "views/gridview.h"
#include "core/filetags.h"
#include "core/preferences.h"
#include "core/dates.h"
#include "model/foldermodel.h"
#include "views/style.h"

#include "ui/image_view.h"
#include "ui/drag.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/cell_layout.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>

namespace fm {

using ui::rounding::centeredStart;

static constexpr double kSmallSpacing = 6;
static constexpr double kLargeSpacing = 12;
static constexpr double kMinimumIconSize = 22;
// The narrowest cell, which holds a name of 100 px.
static constexpr double kMinimumCellWidth = 104;
static constexpr double kLabelInset = 4;
static constexpr double kTagDiameter = 9;
static constexpr double kTagStep = 6;
// A column takes its cell and 10 px; the width keeps another 6.
static constexpr double kEndMargin = 6;
static constexpr double kSectionHeight = 32;

GridColumns GridColumns::forWidth(double width, double iconSize, double spacing, bool labelsRight)
{
    GridColumns columns;
    columns.cellWidth = std::max(kMinimumCellWidth, std::ceil(iconSize * 1.125 + 2)) + (labelsRight ? 105 : 0);
    columns.count = std::max(1, int(std::floor((width - kEndMargin) / (columns.cellWidth + spacing))));
    columns.gap = std::max(0.0, (width - columns.count * columns.cellWidth) / (columns.count + 1));
    return columns;
}

// The selection's ColorOverlay over a thumbnail: its rounded shape in the colour.
class ThumbnailTint : public ui::View
{
public:
    void setRadius(double radius)
    {
        m_radius = radius;
        requestPaint();
    }

protected:
    void paint(ui::Painter& painter) override
    {
        painter.drawSmoothRectangle(localRect(), m_radius, ui::Theme::current().highlight);
    }

private:
    double m_radius = 0;
};

// One item's cell in the grid.
class GridItem : public IconItem
{
public:
    explicit GridItem(FolderGridView* view)
        : IconItem(view, kSmallSpacing / 2, 0.5)
        , m_grid(view)
    {
        addIconViews();
        m_thumbnailTint = addChild<ThumbnailTint>();
        m_thumbnailTint->setVisible(false);
        addLabelView();
        m_info = addChild<ui::Label>();
        m_info->setFontSize(10);
        m_info->setElide(ui::Label::Elide::Right);
    }

protected:
    bool bindItem(const FileItem& item, int row) override
    {
        std::string name = m_grid->model().displayName(row);
        const bool renamed = name != m_label->text();
        m_label->setText(std::move(name));
        const auto& options = m_grid->options();
        m_labelCentered = !options.option(Preferences::IconRight);
        const auto alignment = m_labelCentered ? ui::Label::TextAlignment::Center : ui::Label::TextAlignment::Left;
        m_label->setTextAlignment(alignment);
        m_label->setFontSize(options.textSize);
        m_label->setMaximumLineCount(m_selected ? 3 : 2);
        m_label->setColor(m_selected ? style::selectedTextColor() : ui::Theme::current().text);
        const bool info = options.option(Preferences::IconInfo) && !item.isDir;
        m_info->setVisible(info);
        m_info->setTextAlignment(alignment);
        m_info->setColor(ui::Theme::current().disabledText);
        m_info->setText(info ? formatSize(item.size) : std::string());
        m_tags = item.isLocalFile() && !item.isLink ? filetags::read(item.path) : 0;
        return renamed;
    }

    void paint(ui::Painter& painter) override
    {
        IconItem::paint(painter);
        if (!m_tags || !m_label->isVisible())
            return;
        const auto label = labelRect();
        const double y = label.y + (std::min(label.height, m_grid->options().textSize * 1.35) - kTagDiameter) / 2;
        double x = label.x - tagGutter();
        int remaining = 3;
        for (size_t i = 0; i < filetags::colors.size() && remaining; ++i) {
            if (!(m_tags & (1 << i)))
                continue;
            painter.drawSmoothRectangle({x, y, kTagDiameter, kTagDiameter}, kTagDiameter / 2,
                                        ui::Color::rgb(filetags::colors[i]), 1, ui::Theme::current().background);
            x += kTagStep;
            --remaining;
        }
    }

    void layout() override
    {
        const double width = geometry().width;
        const double iconSize = m_grid->iconSize();
        const double cellWidth = m_grid->cellWidth();
        const double iconItemWidth = std::min(iconSize + kLargeSpacing * 2, cellWidth);
        const bool right = m_grid->options().option(Preferences::IconRight);
        layoutIcon({right ? 0 : centeredStart(width, iconItemWidth), kSmallSpacing, iconItemWidth, iconSize}, 16);

        const bool thumbnailShown = m_thumbnail->isVisible();
        m_thumbnailTint->setVisible(m_selected && thumbnailShown);
        if (thumbnailShown) {
            m_thumbnailTint->setGeometry(m_thumbnail->geometry());
            m_thumbnailTint->setRadius(m_thumbnail->geometry().height * 0.1);
            m_thumbnailTint->setOpacity(0.5 * (m_hidden ? 0.5 : 1.0));
        }

        const double gutter = tagGutter();
        const double labelWidth = std::max(0.0, cellWidth - kLabelInset - gutter - (right ? iconItemWidth : 0));
        const double labelHeight = m_label->heightForWidth(labelWidth);
        const double x = right ? iconItemWidth + gutter : centeredStart(width, labelWidth) + gutter / 2;
        const double y = right ? std::max(kSmallSpacing, (geometry().height - labelHeight - (m_info->isVisible() ? 14 : 0)) / 2)
                               : kSmallSpacing + iconSize + kSmallSpacing;
        m_label->setGeometry({x, y, labelWidth, labelHeight});
        m_info->setGeometry({x, y + labelHeight + 1, labelWidth, 14});
    }

private:
    double tagGutter() const
    {
        const int count = std::min(3, std::popcount(m_tags));
        return count ? kTagDiameter + (count - 1) * kTagStep + 4 : 0;
    }

    FolderGridView* m_grid;
    ThumbnailTint* m_thumbnailTint;
    ui::Label* m_info;
    uint8_t m_tags = 0;
};

FolderGridView::FolderGridView(FolderModel& model)
    : IconGridView(model)
{
    setName("FolderGridView");
    updateMetrics();
    m_preferences = Preferences::instance().changed.connect([this] { requestLayout(); });
}

FolderGridView::~FolderGridView() = default;

std::unique_ptr<IconItem> FolderGridView::makeItem()
{
    return std::make_unique<GridItem>(this);
}

void FolderGridView::setIconSize(double size)
{
    if (m_iconSize == size)
        return;
    auto options = Preferences::instance().viewOptions(model().resolvedUrl(), Preferences::Grid);
    if (!model().searchText().empty() && !options.option(Preferences::IconRight)) {
        m_searchIconSize = size;
        requestLayout();
        return;
    }
    options.iconSize = size;
    Preferences::instance().setViewOptions(model().resolvedUrl(), Preferences::Grid, options);
}

void FolderGridView::increaseIconSize()
{
    const double maximum = Preferences::instance().maximumIconSize();
    setIconSize(m_iconSize >= maximum ? maximum : std::min(maximum, m_iconSize + m_iconSize * 0.1));
}

void FolderGridView::decreaseIconSize()
{
    setIconSize(m_iconSize <= kMinimumIconSize ? kMinimumIconSize
                                                : std::max(kMinimumIconSize, m_iconSize - m_iconSize * 0.1));
}

double FolderGridView::topMargin() const
{
    // The icon view has a gutter under the toolbar. Search results start
    // closer to the scope strip, as do folders shown with small icons.
    return topInset() + (model().searchText().empty() && m_iconSize >= 96 ? 17 : 6);
}

void FolderGridView::updateMetrics()
{
    const int fromColumns = m_grid.count;
    const double fromRowHeight = m_cellHeight;

    m_options = Preferences::instance().viewOptions(model().resolvedUrl(), Preferences::Grid);
    const bool search = !model().searchText().empty();
    if (search && !m_options.option(Preferences::IconRight)) {
        m_options.iconSize = m_searchIconSize;
        m_options.spacing = std::min(m_options.spacing, 8);
    }
    m_iconSize = m_options.iconSize;
    const bool right = m_options.option(Preferences::IconRight);
    m_grid = GridColumns::forWidth(geometry().width, m_iconSize, m_options.spacing, right);
    if (search)
        m_grid.gap = std::min(8.0, m_grid.gap);
    const double labelArea = m_options.textSize * 2.45 + (m_options.option(Preferences::IconInfo) ? 15 : 0);
    const double padding = !search && !right && m_iconSize >= 96 ? 16 : 14;
    m_cellHeight = (right ? std::max(m_iconSize, labelArea) : m_iconSize + labelArea) + padding + m_options.spacing;

    if (fromRowHeight > 0)
        reflow(fromColumns, fromRowHeight);
}

void FolderGridView::reflow(int fromColumns, double fromRowHeight)
{
    if (fromColumns <= 0 || fromRowHeight <= 0 || fromRowHeight == m_cellHeight)
        return;

    // The offset of the view's top below the first row, in content
    // coordinates, which start at the first row.
    const double top = scrollPosition().y - topMargin() + headerHeight();
    if (top <= 0)
        return;
    const int row = int(std::floor(top / fromRowHeight));
    const double within = (top - row * fromRowHeight) * m_cellHeight / fromRowHeight;
    const double y = std::floor(double(row) * fromColumns / m_grid.count) * m_cellHeight + within - headerHeight();
    // The content height changes with the new metrics: lay it out first.
    ScrollView::layout();
    setScrollPosition({0, std::max(0.0, y + topMargin())});
}

void FolderGridView::layout()
{
    updateMetrics();
    updatePositions();
    FolderView::layout();
}

void FolderGridView::updatePositions()
{
    if (m_folder != model().resolvedUrl()) {
        m_folder = model().resolvedUrl();
        m_saved.clear();
        const auto list = Preferences::instance().iconPositions(m_folder);
        for (size_t i = 1; i < list.size(); i += 2) {
            int cell = 0;
            const auto& number = list[i];
            auto result = std::from_chars(number.data(), number.data() + number.size(), cell);
            if (result.ec == std::errc() && result.ptr == number.data() + number.size() && cell >= 0)
                m_saved.emplace(list[i - 1], cell);
        }
    }
    m_sections.clear();
    m_cells.clear();
    m_itemCells.clear();
    int gridRow = 0;
    const auto& sections = model().contents().sections();
    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& [first, name] = sections[i];
        const int end = i + 1 == sections.size() ? model().count() : sections[i + 1].first;
        m_sections.push_back({first, gridRow, name});
        gridRow += (end - first + m_grid.count - 1) / m_grid.count;
    }
    if (m_options.sort != Preferences::Manual || !sections.empty() || m_saved.empty() || model().isTrash() || model().isSearching())
        return;
    m_itemCells.resize(model().count(), -1);
    const int limit = model().count() + 512;
    for (const auto& [url, cell] : m_saved) {
        const int row = model().rowForUrl(url);
        if (row < 0 || cell >= limit)
            continue;
        if (cell >= int(m_cells.size()))
            m_cells.resize(cell + 1, -1);
        if (m_cells[cell] < 0) {
            m_cells[cell] = row;
            m_itemCells[row] = cell;
        }
    }
    int free = 0;
    for (int row = 0; row < model().count(); ++row) {
        if (m_itemCells[row] >= 0)
            continue;
        while (free < int(m_cells.size()) && m_cells[free] >= 0)
            ++free;
        if (free == int(m_cells.size()))
            m_cells.push_back(-1);
        m_cells[free] = row;
        m_itemCells[row] = free++;
    }
}

int FolderGridView::cellCount() const { return m_cells.empty() ? model().count() : int(m_cells.size()); }
int FolderGridView::rowOf(int index) const
{
    return m_cells.empty() ? index : index >= 0 && index < int(m_cells.size()) ? m_cells[index] : -1;
}
int FolderGridView::indexOfRow(int row) const
{
    return m_itemCells.empty() ? row : row >= 0 && row < int(m_itemCells.size()) ? m_itemCells[row] : -1;
}

void FolderGridView::savePositions()
{
    std::vector<std::string> positions;
    for (const auto& [url, cell] : m_saved) {
        positions.push_back(url);
        positions.push_back(std::to_string(cell));
    }
    Preferences::instance().setIconPositions(m_folder, positions);
    requestLayout();
}

void FolderGridView::cleanUp()
{
    m_saved.clear();
    savePositions();
}

bool FolderGridView::rearrange(const std::vector<std::string>& urls, int destinationRow, uint32_t action, ui::PointF point, bool apply)
{
    if (action != ui::MoveAction || destinationRow >= 0 || model().isTrash() || model().isSearching()
        || m_options.sort != Preferences::Manual || m_options.group != Preferences::NoGroup || urls.empty() || point.y < topMargin()
        || !std::all_of(urls.begin(), urls.end(), [this](const auto& url) { return model().rowForUrl(url) >= 0; }))
        return false;
    if (!apply)
        return true;
    const int column = std::clamp(int((point.x - m_grid.gap / 2) / (m_grid.cellWidth + m_grid.gap)), 0, m_grid.count - 1);
    int cell = int((point.y - topMargin()) / m_cellHeight) * m_grid.count + column;
    cell = std::clamp(cell, 0, model().count() + 500);
    // Reserve the drop cells before placing the other icons at their old cells.
    std::map<std::string, int> next;
    for (const auto& url : urls)
        next.emplace(url, cell++);
    const int start = cell - int(urls.size());
    for (int i = 0; i < model().count(); ++i) {
        const auto item = model().item(i);
        const int old = indexOfRow(i);
        if (item && (old < start || old >= cell))
            next.try_emplace(item->url, old);
    }
    m_saved = std::move(next);
    savePositions();
    return true;
}

double FolderGridView::minimumContentHeight() const
{
    const int count = cellCount();
    const double end = count ? cellRect(count - 1).y + m_cellHeight : topMargin();
    return std::max(geometry().height, end + bottomMargin());
}

ui::RectF FolderGridView::cellRect(int index) const
{
    int offset = index;
    int gridRow = 0;
    size_t headers = 0;
    const auto section = std::upper_bound(m_sections.begin(), m_sections.end(), index,
            [](int value, const Section& section) { return value < section.first; });
    if (section != m_sections.begin()) {
        offset -= (section - 1)->first;
        gridRow = (section - 1)->gridRow;
        headers = section - m_sections.begin();
    }
    const int column = offset % m_grid.count;
    const int row = gridRow + offset / m_grid.count;
    const ui::Window* window = this->window();
    const double scale = window ? window->scale() : 1;
    // The gaps are a fraction of a pixel wide; each cell sits on whole device pixels.
    const double x = std::floor(m_grid.x(column) * scale) / scale;
    return {x, topMargin() + row * m_cellHeight + headers * kSectionHeight, m_grid.cellWidth, m_cellHeight};
}

int FolderGridView::columnAt(double x) const
{
    const double pitch = m_grid.cellWidth + m_grid.gap;
    const int column = int(std::floor((x - m_grid.gap) / pitch));
    if (column < 0 || column >= m_grid.count || x - m_grid.x(column) >= m_grid.cellWidth)
        return -1;
    return column;
}

int FolderGridView::indexAt(ui::PointF content) const
{
    const int column = columnAt(content.x);
    if (column < 0)
        return -1;
    const int first = firstCellAt(content.y);
    const int index = first + column;
    if (index >= cellCount() || first >= cellCount())
        return -1;
    const auto box = cellRect(index);
    return box.y <= content.y && content.y < box.y + box.height && rowOf(index) >= 0 ? index : -1;
}

int FolderGridView::firstCellAt(double y) const
{
    int lo = 0, hi = cellCount();
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (cellRect(mid).y + m_cellHeight <= y)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void FolderGridView::rectangleSelect(const ui::RectF& band)
{
    std::vector<int> indices;
    for (int index = firstCellAt(band.y); index < cellCount(); ++index) {
        const auto box = cellRect(index);
        if (box.y >= band.y + band.height)
            break;
        if (model().isBlank(rowOf(index)) || box.x >= band.x + band.width || box.x + box.width <= band.x)
            continue;
        const auto* item = itemFor(index);
        if (!item || item->intersectsRect({band.x - box.x, band.y - box.y, band.width, band.height}))
            indices.push_back(index);
    }
    applyRectangleSelection(indices);
}

void FolderGridView::revealIndex(int index)
{
    if (index < 0)
        return;
    const ui::RectF cell = cellRect(index);
    const double top = scrollPosition().y + headerHeight();
    const double bottom = scrollPosition().y + geometry().height - bottomMargin();
    if (cell.y < top)
        setContentY(cell.y - headerHeight());
    else if (cell.y + cell.height > bottom)
        setContentY(cell.y + cell.height - geometry().height + bottomMargin());
}

void FolderGridView::updateItems()
{
    const double top = scrollPosition().y;
    const double bottom = top + geometry().height;
    const int first = firstCellAt(top);
    const int last = std::min(cellCount(), firstCellAt(bottom) + m_grid.count);

    size_t shown = 0;
    for (const auto& section : m_sections) {
        const double y = cellRect(section.first).y;
        if (y < top || y - kSectionHeight >= bottom)
            continue;
        if (shown == m_headers.size()) {
            auto* label = contentView()->addChild<ui::Label>();
            label->setWeight(650);
            label->setFontSize(12);
            label->setVerticallyCentered(true);
            m_headers.push_back(label);
        }
        auto* label = m_headers[shown++];
        label->setVisible(true);
        label->setText(section.name);
        label->setGeometry({std::max(12.0, m_grid.gap), y - kSectionHeight, std::max(0.0, geometry().width - 24), kSectionHeight});
    }
    for (; shown < m_headers.size(); ++shown)
        m_headers[shown]->setVisible(false);

    recycleItems([first, last](int index) { return index >= first && index < last; });
    for (int index = first; index < last; ++index)
        if (rowOf(index) >= 0 && rowOf(index) < model().count())
            showItem(index);
}

bool FolderGridView::navigate(const ui::KeyEvent& event)
{
    if ((event.key == '=' || event.key == '+') && (event.modifiers & ui::ControlModifier)) {
        increaseIconSize();
        return true;
    }
    if (event.key == '-' && (event.modifiers & ui::ControlModifier)) {
        decreaseIconSize();
        return true;
    }

    int step = 0;
    if (event.key == ui::key::Left)
        step = -1;
    else if (event.key == ui::key::Right)
        step = 1;
    else if (event.key == ui::key::Up)
        step = -m_grid.count;
    else if (event.key == ui::key::Down)
        step = m_grid.count;
    else
        return false;

    const int count = cellCount();
    if (count <= 0)
        return true;
    // A packed grid: plain index arithmetic, nothing past either end.
    const int from = keyboardIndex(step > 0);
    int next = from < 0 ? 0 : from + step;
    while (next >= 0 && next < count && rowOf(next) < 0)
        next += from < 0 ? 1 : step;
    if (next < 0 || next >= count)
        return true;
    setCurrentIndex(next);
    updateSelection(event.modifiers);
    return true;
}

bool FolderGridView::scroll(const ui::ScrollEvent& event)
{
    if (event.modifiers & ui::ControlModifier) {
        if (event.deltaY < 0 || event.value120Y < 0)
            increaseIconSize();
        else
            decreaseIconSize();
        return true;
    }
    return FolderView::scroll(event);
}

} // namespace fm
