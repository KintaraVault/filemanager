// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "core/fileitem.h"
#include "views/folderview.h"
#include "views/foldericon.h"
#include "ui/image_info.h"

#include <array>
#include "base/thread_pool.h"

#include <map>

namespace cutefish::ui {
class DialogButton;
class ImageView;
class Label;
class ThemeIcon;
}

namespace fm {

class FilePreviewPane : public ui::View
{
public:
    explicit FilePreviewPane(bool inspector);
    ~FilePreviewPane() override;
    void setItem(FileItemPtr item, std::string name = {});
    std::function<void()> onQuickLook;
    std::function<void()> onProperties;
    std::function<void()> onOpen;

protected:
    void layout() override;
    void paint(ui::Painter& painter) override;

private:
    void updateMetadata();
    bool m_inspector;
    FileItemPtr m_item;
    ui::ImageView* m_image;
    FolderThemeIcon* m_fallback;
    ui::Label* m_title;
    ui::Label* m_metadata;
    ui::Label* m_information;
    ui::Label* m_quickActions;
    std::array<ui::Label*, 4> m_detailLabels;
    std::array<ui::Label*, 4> m_detailValues;
    ui::Label* m_excerpt;
    ui::DialogButton* m_primary;
    ui::DialogButton* m_info;
    ui::DialogButton* m_rotate;
    base::Job m_textJob;
    ui::ImageInfo m_imageInfo;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    int m_turns = 0;
};

class Filmstrip : public FolderView
{
public:
    explicit Filmstrip(FolderModel& model);
    void revealIndex(int index) override;

protected:
    int indexAt(ui::PointF content) const override;
    bool hitsItem(int index, ui::PointF content) const override;
    int dropRowAt(ui::PointF content) const override;
    void rectangleSelect(const ui::RectF& band) override;
    std::optional<ui::RectF> labelRect(int index) const override;
    std::optional<ui::RectF> dragRect(int index) const override;
    void paintDragItem(int, ui::Painter&, const ui::RectF&) override {}
    void forEachItemView(const std::function<void(int, ui::View*)>& visit) const override;
    void updateItems() override;
    bool navigate(const ui::KeyEvent& event) override;
    bool scroll(const ui::ScrollEvent& event) override;
    double minimumContentWidth() const override;

private:
    class Tile;
    std::map<int, Tile*> m_tiles;
    std::vector<Tile*> m_pool;
};

// Finder-style horizontal branch; each ancestor retains its own viewport.
// The final folder is the page's real FolderModel, not a copy of its selection.
class ColumnBrowser : public ui::ScrollView
{
public:
    explicit ColumnBrowser(FolderModel& active);
    FolderView* activeView() const;
    FilePreviewPane* preview() const { return m_preview; }
    void updatePath(bool resetAnchor = false);
    void updatePreview();

protected:
    void layout() override;
    bool scroll(const ui::ScrollEvent& event) override;

private:
    class Chain;
    FolderModel& m_model;
    Chain* m_chain;
    FilePreviewPane* m_preview;
    std::string m_anchor;
    bool m_reveal = false;
    std::vector<base::Subscription> m_subscriptions;
};

} // namespace fm
