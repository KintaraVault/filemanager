// SPDX-License-Identifier: BSD-3-Clause
#include "views/foldericon.h"

#include "ui/painter.h"
#include "ui/path.h"

namespace fm {

void paintFolderAppearance(ui::Painter& painter, const ui::RectF& box, const folderappearance::Appearance& look)
{
    const double side = std::min(box.width, box.height);
    if (side <= 0)
        return;
    const double x = (box.width - side) / 2, y = (box.height - side) / 2;
    const auto tone = [&](double white, double black) {
        const auto base = look.color.value_or(0x309de2);
        const auto mix = [&](int shift) {
            const auto component = double((base >> shift) & 255);
            return uint32_t((component + (255 - component) * white) * (1 - black)) << shift;
        };
        return ui::Color::rgb(mix(16) | mix(8) | mix(0));
    };
    ui::Path tab;
    tab.moveTo(x + .05 * side, y + .34 * side);
    tab.lineTo(x + .05 * side, y + .23 * side);
    tab.quadTo(x + .05 * side, y + .16 * side, x + .12 * side, y + .16 * side);
    tab.lineTo(x + .32 * side, y + .16 * side);
    tab.lineTo(x + .44 * side, y + .245 * side);
    tab.lineTo(x + .86 * side, y + .245 * side);
    tab.quadTo(x + .95 * side, y + .245 * side, x + .95 * side, y + .35 * side);
    tab.close();
    painter.fillPath(tab, tone(.08, .16));
    const ui::RectF face{x + .05 * side, y + .29 * side, .90 * side, .56 * side};
    painter.fillRoundedRect(face, .065 * side, ui::LinearGradient::vertical(face, tone(.23, 0), tone(0, .08)));
    painter.strokeRoundedRect(face, .065 * side, tone(0, .28).withAlpha(.40));
}

} // namespace fm
