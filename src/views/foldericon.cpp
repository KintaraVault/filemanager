// SPDX-License-Identifier: BSD-3-Clause
#include "views/foldericon.h"

#include "core/standardfolders.h"
#include "ui/painter.h"
#include "ui/path.h"

namespace fm {

namespace {
namespace material = ui::appearance_style;

ui::Color folderBase(const folderappearance::Appearance& look)
{
    const auto& style = ui::Theme::appearanceStyle();
    if (look.color)
        return ui::Color::rgb(*look.color);
    return style.icons == ui::IconStyle::Tinted
            ? material::color(style.tintColor, ui::Theme::current().highlight)
             : material::color(style.folderColor, ui::Color::rgb(0x45b5e4));
}

bool darkFolder()
{
    const auto& style = ui::Theme::appearanceStyle();
    return style.icons == ui::IconStyle::Tinted && material::dark(style, ui::Theme::current().darkMode);
}
}

ui::Color folderStampColor(const folderappearance::Appearance& look)
{
    const auto base = folderBase(look);
    return darkFolder() ? material::blend(base, ui::Color::rgb(0xffffff), .63).withAlpha(.79)
                         : material::blend(base, ui::Color::rgb(0x132f40), .66).withAlpha(.53);
}

std::string standardFolderStamp(const std::string& path)
{
    const auto name = standardFolderIconName(path);
    if (name.empty())
        return {};
    static const bool registered = [] {
        const auto glyph = [](const char* name, const char* drawing) {
            ui::Icon::registerGlyph(name, std::string(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" "
                "stroke=\"currentColor\" stroke-width=\"1.65\" stroke-linecap=\"round\" stroke-linejoin=\"round\">")
                + drawing + "</svg>");
        };
        glyph("folder-desktop-stamp", R"(<rect x="1" y="3.5" width="22" height="17" rx="2.5"/>
            <path d="M1.2 6.5h21.6M5.5 17.2h13"/>)");
        glyph("folder-documents-stamp", R"(<path d="M6 2h7l6 6v12.5c0 .9-.7 1.5-1.5 1.5h-11
            A1.5 1.5 0 0 1 5 20.5v-17C5 2.7 5.3 2 6 2Zm7 0v6h6"/>)");
        glyph("folder-download-stamp", R"(<circle cx="12" cy="12" r="9.5"/><path d="M12 7.5v9m-4-4 4 4 4-4"/>)");
        glyph("folder-video-stamp", R"(<rect x="1" y="3" width="22" height="18" rx="1.4"/>
            <path d="M5 3v18M19 3v18M5 12h14M1 7.5h4m-4 4.5h4m-4 4.5h4m14-9h4m-4 4.5h4m-4 4.5h4"/>)");
        glyph("folder-music-stamp", R"(<path fill="currentColor" stroke="none"
            d="M7 5.4v11.4c-.6-.24-1.38-.27-2.16-.06C3 17.22 2 18.55 2.3 19.8c.32 1.36 2 2.03
            3.7 1.6 1.6-.4 2.76-1.54 2.76-2.89V9.12l9.47-2.06v6.97c-.61-.24-1.38-.27-2.16-.06
            -1.8.48-2.91 1.8-2.59 3.06.34 1.36 2 2.02 3.72 1.6 1.58-.4 2.76-1.55 2.76-2.9V3.35
            c0-.56-.54-.99-1.09-.86L7.5 4.97c-.31.07-.5.16-.5.43Z"/>)");
        glyph("folder-picture-stamp", R"(<rect x="1" y="3.5" width="22" height="17" rx="2.5"/>
            <circle cx="7.2" cy="9" r="2" fill="currentColor" stroke="none"/>
            <path fill="currentColor" stroke="none" d="m2 16 5-4 3 2.5 5.5-5L22 16v3.5H2Z"/>)");
        glyph("folder-publicshare-stamp", R"(<path d="M11.2 1.7a1.1 1.1 0 0 1 1.6 0l9.5 9.5a1.1 1.1 0 0 1 0 1.6l-9.5 9.5
            a1.1 1.1 0 0 1-1.6 0l-9.5-9.5a1.1 1.1 0 0 1 0-1.6Z"/><circle cx="13" cy="7.6" r="1.25" fill="currentColor" stroke="none"/>
            <path d="m7.5 13 3-2.4 2.1.6 1.8 2.4H17m-4.4-2.4-1.2 3.6-2.4 3m2.4-3 2.8 3"/>)");
        glyph("folder-home-stamp", R"(<path d="m2 11 10-8 10 8M5 9v12h14V9M10 21v-7h4v7"/>)");
        glyph("folder-templates-stamp", R"(<rect x="4" y="3" width="16" height="18" rx="1.5"/><path d="M7.5 8h9m-9 4h9m-9 4H14"/>)");
        return true;
    }();
    (void)registered;
    return name == "folder" ? std::string() : name + "-stamp";
}

void paintFolderAppearance(ui::Painter& painter, const ui::RectF& box, const folderappearance::Appearance& look)
{
    const double side = std::min(box.width, box.height);
    if (side <= 0)
        return;
    const double x = box.x + (box.width - side) / 2, y = box.y + (box.height - side) / 2;
    const auto base = folderBase(look);
    const bool dark = darkFolder();
    const auto tone = [&](double white, double black) {
        return material::blend(material::blend(base, ui::Color::rgb(0xffffff), white),
                               ui::Color::rgb(0), black);
    };
    const ui::RectF face{x + .055 * side, y + .263 * side, .89 * side, .58 * side};
    const double corner = .062 * side;
    painter.drawShadow(face, corner, .014 * side, ui::Color::rgb(0).withAlpha(.29), {0, .013 * side});

    ui::Path tab;
    tab.moveTo(x + .055 * side, y + .34 * side);
    tab.lineTo(x + .055 * side, y + .196 * side);
    tab.quadTo(x + .055 * side, y + .124 * side, x + .124 * side, y + .124 * side);
    tab.lineTo(x + .30 * side, y + .124 * side);
    tab.quadTo(x + .346 * side, y + .124 * side, x + .379 * side, y + .164 * side);
    tab.quadTo(x + .405 * side, y + .191 * side, x + .444 * side, y + .191 * side);
    tab.lineTo(x + .877 * side, y + .191 * side);
    tab.quadTo(x + .945 * side, y + .191 * side, x + .945 * side, y + .26 * side);
    tab.lineTo(x + .945 * side, y + .34 * side);
    tab.close();
    const ui::RectF tabBox{face.x, y + .124 * side, face.width, .20 * side};
    painter.fillPath(tab, ui::LinearGradient::vertical(tabBox, tone(.26, dark ? .28 : .02), tone(0, .21)));
    const ui::RectF paper = face.adjusted(.026 * side, -.038 * side, -.026 * side, -.16 * side);
    painter.fillRoundedRect(paper, corner * .6, ui::Color::rgb(dark ? 0x9ea9b5 : 0xf4fdff));
    painter.fillRoundedRect(face, corner, ui::LinearGradient::vertical(
        face, tone(.29, dark ? .42 : 0), tone(.015, dark ? .65 : .025)));
    painter.strokeRoundedRect(face, corner, tone(0, .28).withAlpha(.30));
    const auto rim = face.shrunk(ui::Margins::all(std::max(.4, side * .007)));
    if (rim.width > 0 && rim.height > 0)
        painter.strokeRoundedRect(rim, corner, ui::Color::rgb(0xffffff).withAlpha(dark ? .10 : .15));
}

} // namespace fm
