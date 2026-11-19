// SPDX-License-Identifier: BSD-3-Clause

#include "views/sharepanel.h"

#include "core/thumbnails.h"
#include "views/foldericon.h"

#include "base/i18n.h"
#include "base/url.h"
#include "ui/button.h"
#include "ui/clipboard.h"
#include "ui/image_view.h"
#include "ui/keys.h"
#include "ui/painter.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>

namespace fm {
namespace {

using cutefish::i18n::tr;

constexpr double kWidth = 298;
constexpr double kMargin = 14;
constexpr double kIconSize = 44;
constexpr double kTextStart = kMargin + kIconSize + 11;
constexpr double kRowHeight = 28;

class ShareAction : public ui::Button
{
public:
    ShareAction(std::string name, std::string title, const std::string& icon)
    {
        setName(std::move(name));
        m_icon = addChild<ui::ThemeIcon>(icon, 16);
        m_title = addChild<ui::Label>(std::move(title));
        m_title->setFontSize(13);
        m_title->setVerticallyCentered(true);
    }

protected:
    void layout() override
    {
        m_icon->setGeometry({7, (localRect().height - 16) / 2, 16, 16});
        m_title->setGeometry({32, 0, localRect().width - 39, localRect().height});
    }

    void paint(ui::Painter& painter) override
    {
        const auto& theme = ui::Theme::current();
        m_icon->setColor(theme.text);
        if (m_hover || hasVisibleFocus())
            painter.fillRoundedRect(localRect(), 6, theme.text.withAlpha(m_pressed ? 0.20 : 0.12));
    }

    void pointerEnter() override { m_hover = true; requestPaint(); }
    void pointerLeave() override { m_hover = false; requestPaint(); }
    void pointerPress(const ui::PointerEvent& event) override
    {
        m_pressed = event.button == ui::MouseButton::Left;
        ui::Button::pointerPress(event);
    }
    void pointerRelease(const ui::PointerEvent& event) override
    {
        m_pressed = false;
        ui::Button::pointerRelease(event);
    }

private:
    ui::ThemeIcon* m_icon;
    ui::Label* m_title;
    bool m_hover = false;
    bool m_pressed = false;
};

class SharePanel : public ui::View
{
public:
    SharePanel(FileItemList items, std::string name, const std::function<void()>& close)
    {
        setName("sharePanel");
        const auto item = items.size() == 1 ? items.front() : nullptr;
        std::string icon = item ? item->iconName : "edit-copy";
        if (icon.empty() || ui::themeIconPath(icon, int(kIconSize)).empty())
            icon = item && item->isDir ? "folder" : "text-x-generic";
        m_icon = addChild<FolderThemeIcon>(icon, kIconSize);
        m_icon->setName("shareIcon");
        if (item && item->isDir && item->isLocalFile() && !item->isLink)
            m_icon->setFolderPath(item->path);

        if (item && item->isFile && item->isLocalFile() && !thumbnails::isOnMount(item->path)
            && thumbnails::canThumbnail(item->mimeType)) {
            m_image = addChild<ui::ImageView>();
            m_image->setName("shareThumbnail");
            m_image->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
            m_image->setCornerRadius(3);
            thumbnails::load(item->path, item->modified, 144, 144,
                [this, life = std::weak_ptr<bool>(m_life)](const ui::Bitmap& image) {
                    if (life.expired() || image.isNull())
                        return;
                    m_image->setPixels(image.width(), image.height(), reinterpret_cast<const uint32_t*>(image.bits()),
                                      image.stride(), true);
                    m_icon->setVisible(false);
                });
        }

        m_title = addChild<ui::Label>(item ? std::move(name)
            : cutefish::i18n::trn("{n} item", "{n} items", items.size()));
        m_title->setName("shareName");
        m_title->setFontSize(15);
        m_title->setWeight(700);
        m_title->setWrap(true);
        m_title->setMaximumLineCount(2);
        m_title->setToolTip(m_title->text());

        std::string type;
        if (item) {
            type = item->isDir ? tr("Folder") : item->typeDescription.empty() ? item->mimeType : item->typeDescription;
            if (item->isFile) {
                if (!type.empty())
                    type += " · ";
                type += formatSize(item->size);
            }
        }
        m_detail = addChild<ui::Label>(std::move(type));
        m_detail->setName("shareDetails");
        m_detail->setFontSize(12);
        m_detail->setElide(ui::Label::Elide::Right);
        m_detail->setVisible(!m_detail->text().empty());

        std::vector<std::string> urls;
        urls.reserve(items.size());
        std::string locations;
        for (const auto& file : items) {
            const auto& url = file->url;
            urls.push_back(url);
            if (!locations.empty())
                locations += '\n';
            locations += base::url::toLocalFile(url).value_or(url);
        }
        m_copy = addChild<ShareAction>("shareCopy", tr("Copy"), "edit-copy");
        m_copy->onClicked = [close, urls = std::move(urls)] {
            ui::MimeData data;
            data.setUrls(urls);
            ui::Clipboard::setMimeData(data);
            close();
        };
        const std::string pathIcon = ui::themeIconPath("edit-copy-path", 16).empty() ? "insert-link" : "edit-copy-path";
        m_location = addChild<ShareAction>("shareCopyLocation", tr("Copy Location"), pathIcon);
        m_location->onClicked = [close, locations = std::move(locations)] {
            ui::Clipboard::setText(locations);
            close();
        };
    }

    bool acceptsFocus() const override { return true; }
    ui::SizeF sizeHint() const override { return {kWidth, heightForWidth(kWidth)}; }
    double heightForWidth(double width) const override { return headHeight(width) + 14 + 2 * kRowHeight; }

protected:
    void layout() override
    {
        const double width = localRect().width;
        const double header = headHeight(width);
        const double textWidth = std::max(0.0, width - kTextStart - kMargin);
        const double nameHeight = std::ceil(m_title->heightForWidth(textWidth));
        const double infoHeight = detailHeight();
        const double textY = (header - nameHeight - infoHeight) / 2;
        const ui::RectF icon {kMargin, (header - kIconSize) / 2, kIconSize, kIconSize};
        m_icon->setGeometry(icon);
        if (m_image)
            m_image->setGeometry(icon);
        m_title->setGeometry({kTextStart, textY, textWidth, nameHeight});
        m_detail->setGeometry({kTextStart, textY + nameHeight + 1, textWidth, std::max(0.0, infoHeight - 1)});
        m_copy->setGeometry({7, header + 6, width - 14, kRowHeight});
        m_location->setGeometry({7, header + 6 + kRowHeight, width - 14, kRowHeight});
    }

    void paint(ui::Painter& painter) override
    {
        const auto& theme = ui::Theme::current();
        m_detail->setColor(theme.text.withAlpha(0.65));
        const double sep = headHeight(localRect().width);
        painter.fillRect({kMargin, sep, localRect().width - 2 * kMargin, 1 / painter.scale()}, theme.text.withAlpha(0.16));
    }

    bool keyPress(const ui::KeyEvent& event) override
    {
        if (event.modifiers != ui::NoModifier)
            return false;
        if (event.key == ui::key::Down || event.key == ui::key::Up) {
            const bool location = event.key == ui::key::Down ? m_copy->hasFocus() : !m_location->hasFocus();
            (location ? m_location : m_copy)->setFocus(ui::FocusReason::Keyboard);
            return true;
        }
        return false;
    }

private:
    double detailHeight() const { return m_detail->isVisible() ? std::ceil(m_detail->sizeHint().height) + 1 : 0; }
    double headHeight(double width) const
    {
        return std::max(kIconSize, std::ceil(m_title->heightForWidth(std::max(0.0, width - kTextStart - kMargin)))
            + detailHeight()) + 22;
    }

    FolderThemeIcon* m_icon;
    ui::ImageView* m_image = nullptr;
    ui::Label* m_title;
    ui::Label* m_detail;
    ShareAction* m_copy;
    ShareAction* m_location;
    std::shared_ptr<bool> m_life = std::make_shared<bool>(true);
};

} // namespace

std::unique_ptr<ui::View> makeSharePanel(FileItemList items, std::string name, std::function<void()> close)
{
    return std::make_unique<SharePanel>(std::move(items), std::move(name), close);
}

} // namespace fm
