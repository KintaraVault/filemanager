// SPDX-License-Identifier: BSD-3-Clause
#include "dialogs/viewoptionsdialog.h"

#include "base/i18n.h"
#include "model/foldermodel.h"
#include "ui/button.h"
#include "ui/check_box.h"
#include "ui/combo_box.h"
#include "ui/file_chooser.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/slider.h"
#include "ui/text_field.h"
#include "ui/theme.h"
#include "views/folderpage.h"

#include <array>
#include <charconv>
#include <cmath>

namespace fm {
using cutefish::i18n::tr;

class ViewOptionsDialog::Panel : public ui::View
{
public:
    explicit Panel(ViewOptionsDialog& dialog) : m_dialog(dialog)
    {
        setName("viewOptionsPanel");
        const auto mode = m_dialog.m_mode;
        if (!m_dialog.m_trash) {
            const char* always[] = {tr("Always open in list view"), tr("Always open in icon view"),
                                    tr("Always open in column view"), tr("Always open in gallery view")};
            const char* browse[] = {tr("Browse in list view"), tr("Browse in icon view"),
                                     tr("Browse in column view"), tr("Browse in gallery view")};
            m_always = addChild<ui::CheckBox>(always[mode]);
            m_browse = addChild<ui::CheckBox>(browse[mode]);
            m_always->setName("alwaysOpenInView");
            m_browse->setName("browseInView");
            m_always->onToggled = [this, mode](bool on) { setPinned(on ? mode : -1, false); };
            m_browse->onToggled = [this, mode](bool on) { setPinned(mode, on); };
            row({m_always}, 25);
            row({m_browse}, 25);
            separator();
            m_group = combo(tr("Group By:"), {tr("None"), tr("Kind"), tr("Name"), tr("Date Modified"), tr("Date Created"),
                                             tr("Application"), tr("Size"), tr("Tags")});
            m_group->onActivated = [this](int value) { change([&](auto& options) { options.group = value; }); };
            m_sortOffset = mode == Preferences::Grid ? 1 : 0;
            std::vector<std::string> sorts {tr("Name"), tr("Size"), tr("Date Modified"), tr("Kind"), tr("Date Created"), tr("Date Accessed")};
            if (m_sortOffset)
                sorts.insert(sorts.begin(), tr("None"));
            m_sort = combo(tr("Sort By:"), std::move(sorts));
            m_sort->onActivated = [this](int value) { change([&](auto& options) { options.sort = value - m_sortOffset; }); };
            separator();
        }
        if (mode == Preferences::Grid) {
            m_sizeLabel = label(tr("Icon size:"));
            m_size = slider(24, 256, 8);
            m_size->onMoved = [this](double value) { change([&](auto& options) { options.iconSize = value; }); };
            label(tr("Grid spacing:"));
            m_spacing = slider(4, 40, 4);
            m_spacing->onMoved = [this](double value) { change([&](auto& options) { options.spacing = int(value); }); };
            separator();
        } else if (mode == Preferences::List) {
            m_iconSize = combo(tr("Icon size:"), {tr("Small"), tr("Large")});
            m_iconSize->onActivated = [this](int index) { setOption(Preferences::ListLargeIcons, index == 1); };
        }
        if (mode != Preferences::Gallery) {
            std::vector<std::string> sizes;
            for (int size = 10; size <= 18; ++size)
                sizes.push_back(std::to_string(size));
            m_text = combo(tr("Text size:"), std::move(sizes));
            m_text->onActivated = [this](int index) { change([&](auto& options) { options.textSize = index + 10; }); };
        }
        if (mode == Preferences::Grid) {
            label(tr("Label position:"));
            m_bottom = addChild<ui::RadioButton>(tr("Bottom"));
            m_right = addChild<ui::RadioButton>(tr("Right"));
            m_bottom->setGroup(&m_positions);
            m_right->setGroup(&m_positions);
            m_positions.onChanged = [this](int index) { setOption(Preferences::IconRight, index == 1); };
            row({m_bottom, m_right}, 25);
            separator();
            check(tr("Show item info"), Preferences::IconInfo);
            check(tr("Show icon preview"), Preferences::IconPreview);
            if (!m_dialog.m_trash) {
                separator();
                label(tr("Background:"));
                m_bgDefault = addChild<ui::RadioButton>(tr("Default"));
                m_bgColor = addChild<ui::RadioButton>(tr("Color"));
                m_bgPicture = addChild<ui::RadioButton>(tr("Picture"));
                for (auto* button : {m_bgDefault, m_bgColor, m_bgPicture})
                    button->setGroup(&m_backgrounds);
                m_hex = addChild<ui::TextField>();
                m_hex->setPlaceholder("#DDE8F4");
                m_hex->onAccepted = [this] {
                    auto hex = m_hex->text();
                    if (!hex.starts_with('#')) hex.insert(hex.begin(), '#');
                    unsigned rgb = 0;
                    const auto end = hex.data() + hex.size();
                    if (hex.size() == 7) {
                        const auto result = std::from_chars(hex.data() + 1, end, rgb, 16);
                        if (result.ec == std::errc() && result.ptr == end)
                            change([&](auto& value) { value.background = hex; });
                    }
                    sync();
                };
                auto* choose = addChild<ui::Button>(tr("Choose…"));
                choose->onClicked = [this] { choosePicture(); };
                m_backgrounds.onChanged = [this](int index) {
                    if (index == 2) {
                        choosePicture();
                        sync();
                    } else {
                        change([index](auto& value) { value.background = index == 1 ? "#DDE8F4" : ""; });
                    }
                };
                row({m_bgDefault}, 25);
                row({m_bgColor, m_hex}, 28);
                row({m_bgPicture, choose}, 28);
            }
        } else if (mode == Preferences::List) {
            label(tr("Show Columns:"));
            check(tr("Date Modified"), Preferences::ListDate);
            if (!m_dialog.m_trash) {
                check(tr("Date Created"), Preferences::ListCreated);
                check(tr("Date Accessed"), Preferences::ListAccessed);
            }
            check(tr("Size"), Preferences::ListSize);
            check(tr("Kind"), Preferences::ListKind);
            separator();
            if (!m_dialog.m_trash)
                check(tr("Use relative dates"), Preferences::ListRelativeDates);
            check(tr("Show icon preview"), Preferences::ListPreview);
        } else if (mode == Preferences::Columns) {
            check(tr("Show icons"), Preferences::ColumnIcons);
            check(tr("Show icon preview"), Preferences::ColumnIconPreview);
            check(tr("Show preview column"), Preferences::ColumnPreview);
            check(tr("Resize columns to fit filenames"), Preferences::ColumnFit);
        } else {
            check(tr("Show Filenames"), Preferences::GalleryNames);
        }
        if (!m_dialog.m_trash) {
            separator();
            m_defaults = addChild<ui::Button>(tr("Use as Defaults"));
            m_defaults->setName("viewOptionsDefaults");
            m_defaults->onClicked = [this] {
                if (const auto* page = m_dialog.m_page)
                    Preferences::instance().useViewDefaults(page->model().resolvedUrl(), page->viewMethod());
            };
            row({m_defaults}, 31);
        }
        sync();
    }

    void sync()
    {
        const auto options = m_dialog.m_page->viewOptions();
        if (m_always) {
            const auto& url = m_dialog.m_page->model().resolvedUrl();
            const auto& prefs = Preferences::instance();
            const bool pinned = prefs.pinnedView(url) == m_dialog.m_mode;
            m_always->setChecked(pinned);
            m_browse->setEnabled(pinned);
            m_browse->setChecked(pinned && prefs.browseInPinnedView(url));
        }
        if (m_group) m_group->setCurrentIndex(options.group);
        if (m_sort) m_sort->setCurrentIndex(options.sort + m_sortOffset);
        if (m_text) m_text->setCurrentIndex(options.textSize - 10);
        if (m_iconSize) m_iconSize->setCurrentIndex(options.option(Preferences::ListLargeIcons) ? 1 : 0);
        if (m_bottom)
            (options.option(Preferences::IconRight) ? m_right : m_bottom)->setChecked(true);
        if (m_size) {
            m_size->setValue(options.iconSize);
            const auto number = std::to_string(std::lround(options.iconSize));
            m_sizeLabel->setText(tr("Icon size:") + std::string("  ") + number + " × " + number);
            m_spacing->setValue(options.spacing);
        }
        for (const auto& [box, key] : m_checks)
            box->setChecked(options.option(key));
        if (m_bgDefault) {
            const bool color = options.background.starts_with('#');
            const bool picture = options.background.starts_with('/');
            (color ? m_bgColor : picture ? m_bgPicture : m_bgDefault)->setChecked(true);
            m_hex->setEnabled(color);
            if (m_hex->text() != (color ? options.background : ""))
                m_hex->setText(color ? options.background : "");
        }
    }

    ui::SizeF sizeHint() const override { return {300, m_height}; }

private:
    struct Row { std::array<ui::View*, 2> views; double y, height; };

    void row(std::array<ui::View*, 2> views, double height)
    {
        m_rows.push_back({views, m_height, height});
        m_height += height;
    }

    void separator() { row({}, 19); }

    ui::Label* label(std::string text, bool ownRow = true)
    {
        auto* view = addChild<ui::Label>(std::move(text));
        view->setVerticallyCentered(true);
        if (ownRow) row({view}, 24);
        return view;
    }

    ui::ComboBox* combo(std::string title, std::vector<std::string> choices)
    {
        auto* text = label(std::move(title), false);
        auto* view = addChild<ui::ComboBox>(std::move(choices));
        row({text, view}, 32);
        return view;
    }

    ui::Slider* slider(double min, double max, double step)
    {
        auto* view = addChild<ui::Slider>();
        view->setRange(min, max);
        view->setStepped(true);
        view->setStepSize(step);
        row({view}, 31);
        return view;
    }

    void check(std::string name, Preferences::Option key)
    {
        auto* box = addChild<ui::CheckBox>(std::move(name));
        box->onToggled = [this, key](bool value) { setOption(key, value); };
        m_checks.emplace_back(box, key);
        row({box}, 25);
    }

    void setOption(Preferences::Option key, bool value)
    {
        change([&](auto& options) { options.setOption(key, value); });
    }

    void setPinned(int mode, bool browse)
    {
        if (auto* page = m_dialog.m_page)
            Preferences::instance().setPinnedView(page->model().resolvedUrl(), mode, browse);
    }

    void choosePicture()
    {
        auto* page = m_dialog.m_page;
        if (!page || (m_picker && m_picker->isOpen())) return;
        m_picker = std::make_unique<ui::FileChooser>(window());
        m_picker->setTitle(tr("Choose Background Picture"));
        m_picker->setCurrentFolder(page->model().rootItem().path);
        m_picker->setFilters({{tr("Images"), {}, {"image/*"}}});
        const auto url = page->model().resolvedUrl();
        m_picker->onAccepted = [url](const auto& paths, const auto&) {
            if (paths.empty() || !paths.front().starts_with('/')) return;
            auto& prefs = Preferences::instance();
            auto value = prefs.viewOptions(url, Preferences::Grid);
            value.background = paths.front();
            prefs.setViewOptions(url, Preferences::Grid, value);
        };
        m_picker->open();
    }

    void change(const std::function<void(Preferences::ViewOptions&)>& action)
    {
        if (auto* page = m_dialog.m_page) {
            auto options = page->viewOptions();
            action(options);
            page->setViewOptions(options);
        }
    }

    void layout() override
    {
        const double w = geometry().width;
        for (const auto& [views, y, h] : m_rows) {
            auto [first, second] = views;
            if (!first) continue;
            if (first == m_defaults) {
                const double width = std::min(w, std::max(136.0, first->sizeHint().width));
                first->setGeometry({(w - width) / 2, y, width, h});
            } else if (second) {
                const double split = first == m_bottom ? w / 2 : 112;
                first->setGeometry({0, y, split, h});
                second->setGeometry({split, y + 2, w - split, h - 4});
            } else {
                const double inset = first == m_browse ? 14 : 0;
                first->setGeometry({inset, y, w - inset, h});
            }
        }
    }

    void paint(ui::Painter& painter) override
    {
        const auto color = ui::Theme::current().text.withAlpha(0.13);
        for (const auto& [views, y, h] : m_rows)
            if (!views[0])
                painter.fillRect({0, y + std::floor(h / 2), geometry().width, 1}, color);
    }

    ViewOptionsDialog& m_dialog;
    double m_height = 0;
    int m_sortOffset = 0;
    std::vector<Row> m_rows;
    std::vector<std::pair<ui::CheckBox*, Preferences::Option>> m_checks;
    ui::ComboBox *m_group = nullptr, *m_sort = nullptr, *m_text = nullptr, *m_iconSize = nullptr;
    ui::Slider *m_size = nullptr, *m_spacing = nullptr;
    ui::Label* m_sizeLabel = nullptr;
    ui::Button* m_defaults = nullptr;
    ui::CheckBox *m_always = nullptr, *m_browse = nullptr;
    ui::ButtonGroup m_positions;
    ui::RadioButton *m_bottom = nullptr, *m_right = nullptr;
    ui::ButtonGroup m_backgrounds;
    ui::RadioButton *m_bgDefault = nullptr, *m_bgColor = nullptr, *m_bgPicture = nullptr;
    ui::TextField* m_hex = nullptr;
    std::unique_ptr<ui::FileChooser> m_picker;
};

ViewOptionsDialog::ViewOptionsDialog(ui::Window* parent) : FramelessDialog(tr("View Options"), true)
{
    setParentWindow(parent);
    setCenteredHeader(true);
    onCloseRequested = [this] { hide(); };
    setHeaderTitle(tr("View Options"));
    setHeader(35, 8);
    setContentFactory([this] {
        auto panel = std::make_unique<Panel>(*this);
        m_panel = panel.get();
        return panel;
    }, 330, 15);
    setPadding(15, 9, 14);
}

void ViewOptionsDialog::sync(FolderPage* page)
{
    if (!page) return;
    const bool rebuild = m_mode != page->viewMethod() || m_trash != page->model().isTrash();
    m_page = page;
    m_mode = page->viewMethod();
    m_trash = page->model().isTrash();
    if (m_title != page->folderName()) {
        m_title = page->folderName();
        setTitle(m_title);
        setHeaderTitle(m_title);
    }
    if (isShown()) {
        if (rebuild) rebuildContent();
        else m_panel->sync();
    }
}

} // namespace fm
