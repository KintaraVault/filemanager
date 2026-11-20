// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "core/folderappearance.h"
#include "ui/icon.h"
#include "ui/icon_image.h"
#include "ui/label.h"
#include "ui/theme.h"

namespace fm {

void paintFolderAppearance(ui::Painter& painter, const ui::RectF& box, const folderappearance::Appearance& look);
ui::Color folderStampColor(const folderappearance::Appearance& look);
std::string standardFolderStamp(const std::string& path);

template<class Base>
class FolderIcon : public Base {
public:
    template<class... Args>
    explicit FolderIcon(Args&&... args) : Base(std::forward<Args>(args)...)
    {
        m_symbol = this->template addChild<ui::Label>();
        m_symbol->setTextAlignment(ui::Label::TextAlignment::Center);
        m_symbol->setVerticallyCentered(true);
        m_symbol->setColor(ui::Color::rgb(0xffffff));
        m_symbol->setVisible(false);
        m_stampLight = this->template addChild<ui::Icon>();
        m_stampLight->setVisible(false);
        m_stamp = this->template addChild<ui::Icon>();
        m_stamp->setVisible(false);
        m_changed = folderappearance::changed().connect([this](const std::string& path) {
            if (m_path == path)
                setAppearance(folderappearance::read(path));
        });
    }

    void setFolderPath(std::string path)
    {
        if (m_path == path)
            return;
        m_path = std::move(path);
        setAppearance(folderappearance::read(m_path));
        this->requestLayout();
        this->requestPaint();
    }

    void setAppearance(folderappearance::Appearance look)
    {
        if (m_look == look)
            return;
        m_look = std::move(look);
        m_symbol->setText(std::string(folderappearance::glyph(m_look.symbol)));
        this->requestLayout();
        this->requestPaint();
        if (appearanceChanged)
            appearanceChanged();
    }

    bool customized() const { return m_look.color || !m_look.symbol.empty(); }
    bool paintsFace() const
    {
        const auto& style = ui::Theme::appearanceStyle();
        return customized() || (!m_path.empty()
                && ((ui::Theme::current().iconTheme == "Cutefish" && this->localRect().width >= 32)
                    || style.folderColor || style.icons == ui::IconStyle::Tinted));
    }
    std::function<void()> appearanceChanged;

protected:
    void paint(ui::Painter& painter) override
    {
        if (paintsFace())
            paintFolderAppearance(painter, this->localRect(), m_look);
        else
            Base::paint(painter);
    }

    void layout() override
    {
        const auto box = this->localRect();
        const double side = std::min(box.width, box.height);
        m_symbol->setVisible(!m_look.symbol.empty());
        m_symbol->setFontSize(std::max(6.0, side * 0.39));
        m_symbol->setColor(folderStampColor(m_look));
        m_symbol->setGeometry({0, box.height / 2 - side * .18, box.width, side * .54});
        const std::string stamp = paintsFace() && m_look.symbol.empty() ? standardFolderStamp(m_path) : std::string();
        m_stamp->setIconName(stamp);
        m_stamp->setVisible(!stamp.empty());
        m_stamp->setColor(folderStampColor(m_look));
        m_stamp->setSize(side * .41);
        const ui::RectF shape{(box.width - side * .41) / 2, (box.height - side * .41) / 2 + .07 * side,
                              side * .41, side * .41};
        m_stamp->setGeometry(shape);
        m_stampLight->setVisible(!stamp.empty());
        m_stampLight->setIconName(stamp);
        m_stampLight->setColor(ui::Color::rgb(0xffffff).withAlpha(.24));
        m_stampLight->setSize(side * .41);
        m_stampLight->setGeometry({shape.x, shape.y + std::max(1.0, side * .012), shape.width, shape.height});
    }

private:
    std::string m_path;
    folderappearance::Appearance m_look;
    ui::Label* m_symbol;
    ui::Icon* m_stampLight;
    ui::Icon* m_stamp;
    base::Subscription m_changed;
};

using FolderThemeIcon = FolderIcon<ui::ThemeIcon>;
using FolderImageIcon = FolderIcon<ui::IconImage>;

} // namespace fm
