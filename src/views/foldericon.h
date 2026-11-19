// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "core/folderappearance.h"
#include "ui/icon.h"
#include "ui/icon_image.h"
#include "ui/label.h"

namespace fm {

void paintFolderAppearance(ui::Painter& painter, const ui::RectF& box, const folderappearance::Appearance& look);

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
    std::function<void()> appearanceChanged;

protected:
    void paint(ui::Painter& painter) override
    {
        if (customized())
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
        m_symbol->setGeometry({0, box.height / 2 - side * .2, box.width, side * .54});
    }

private:
    std::string m_path;
    folderappearance::Appearance m_look;
    ui::Label* m_symbol;
    base::Subscription m_changed;
};

using FolderThemeIcon = FolderIcon<ui::ThemeIcon>;
using FolderImageIcon = FolderIcon<ui::IconImage>;

} // namespace fm
