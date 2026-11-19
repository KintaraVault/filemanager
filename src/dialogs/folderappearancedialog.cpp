// SPDX-License-Identifier: BSD-3-Clause
#include "dialogs/folderappearancedialog.h"
#include "views/foldericon.h"

#include "base/i18n.h"
#include "ui/dialog_window.h"
#include "ui/keys.h"
#include "ui/painter.h"
#include "ui/scroll_view.h"
#include "ui/text_field.h"
#include "ui/theme.h"

#include <array>
#include <charconv>

using cutefish::i18n::tr;
using cutefish::i18n::trNoop;

namespace fm {
namespace {

class Swatch final : public ui::View {
public:
    explicit Swatch(std::optional<uint32_t> color) : m_color(color) {}
    bool acceptsPointer() const override { return true; }
    bool acceptsFocus() const override { return true; }
    std::function<void()> choose;
    void setSelected(bool value) { m_selected = value; requestPaint(); }
protected:
    void paint(ui::Painter& p) override
    {
        const auto ink = ui::Theme::current().text;
        auto box = localRect().adjusted(4, 4, -4, -4);
        const auto tint = m_color ? ui::Color::rgb(*m_color) : ink.withAlpha(.10);
        p.drawSmoothRectangle(box, box.height / 2, tint, 1, ink.withAlpha(.16));
        const double x = box.width / 2 + box.x, y = box.height / 2 + box.y;
        if (m_selected) {
            const auto check = !m_color ? ink : (tint.r * 299 + tint.g * 587 + tint.b * 114) < 159000
                ? ui::Color::rgb(0xffffff) : ui::Color::rgb(0x222222);
            p.drawLine({x - 5, y}, {x - 1, y + 4}, check, 1.6);
            p.drawLine({x - 1, y + 4}, {x + 6, y - 4}, check, 1.6);
        } else if (!m_color) {
            p.drawLine({x - 5, y + 5}, {x + 5, y - 5}, ink, 1);
        }
    }
    void pointerRelease(const ui::PointerEvent& event) override
    {
        if (event.button == ui::MouseButton::Left && localRect().contains(event.position))
            choose();
    }
    bool keyPress(const ui::KeyEvent& event) override
    {
        if (event.key != ui::key::Space && event.key != ui::key::Return)
            return false;
        if (!event.repeat)
            choose();
        return true;
    }
private:
    std::optional<uint32_t> m_color;
    bool m_selected = false;
};

class SymbolButton final : public ui::DialogButton {
public:
    using ui::DialogButton::DialogButton;
    void setSelected(bool value) { m_selected = value; requestPaint(); }
protected:
    void pointerEnter() override { m_hovered = true; requestPaint(); }
    void pointerLeave() override { m_hovered = false; requestPaint(); }
    void paint(ui::Painter& painter) override
    {
        if (m_selected || m_hovered)
            painter.drawSmoothRectangle(localRect(), 7, ui::Theme::current().text.withAlpha(m_selected ? .17 : .08));
    }
private:
    bool m_hovered = false;
    bool m_selected = false;
};

class SymbolGrid final : public ui::View {
public:
    SymbolGrid()
    {
        for (const auto& entry : folderappearance::symbols()) {
            if (m_groups.empty() || m_groups.back().name != entry.group) {
                auto* label = addChild<ui::Label>(tr(entry.group));
                label->setWeight(600);
                m_groups.push_back({entry.group, label, {}});
            }
            auto* button = addChild<SymbolButton>(entry.glyph);
            button->setName(tr(entry.name));
            button->setToolTip(tr(entry.name));
            const std::string id = entry.id;
            button->onClicked = [this, id] { choose(m_emoji ? "emoji:" + id : id); };
            m_groups.back().buttons.push_back({&entry, button});
        }
    }
    std::function<void(std::string)> choose;

    void setSelected(std::string_view id)
    {
        const bool emoji = id.starts_with("emoji:");
        if (emoji) id.remove_prefix(6);
        for (const auto& group : m_groups)
            for (const auto& [entry, button] : group.buttons)
                button->setSelected(emoji == m_emoji && id == entry->id);
    }

    void update(std::string query, bool emoji)
    {
        m_emoji = emoji;
        const auto lower = [](std::string input) {
            for (auto& c : input)
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            return input;
        };
        query = lower(std::move(query));
        for (auto& group : m_groups) {
            bool any = false;
            for (auto& [entry, button] : group.buttons) {
                const bool show = query.empty() || lower(entry->id).find(query) != std::string::npos
                    || lower(tr(entry->name)).find(query) != std::string::npos;
                button->setVisible(show);
                button->setText(emoji ? entry->emoji : entry->glyph);
                any |= show;
            }
            group.label->setVisible(any);
        }
        requestLayout();
        if (parent()) parent()->requestLayout();
    }
    double heightForWidth(double) const override
    {
        double y = 0;
        for (const auto& group : m_groups)
            if (group.label->isVisible()) y += 63;
        return y;
    }
    ui::SizeF sizeHint() const override { return {0, heightForWidth(0)}; }
protected:
    void layout() override
    {
        double y = 0;
        const double step = std::max(28.0, (geometry().width - 10) / 6);
        for (const auto& group : m_groups) {
            if (!group.label->isVisible()) continue;
            group.label->setGeometry({4, y, geometry().width - 8, 23});
            int col = 0;
            for (const auto& [entry, button] : group.buttons)
                if (button->isVisible()) button->setGeometry({4 + col++ * step, y + 25, step - 5, 29});
            y += 63;
        }
    }
private:
    struct Group {
        std::string name;
        ui::Label* label;
        std::vector<std::pair<const folderappearance::Symbol*, SymbolButton*>> buttons;
    };
    std::vector<Group> m_groups;
    bool m_emoji = false;
};

class AppearancePanel final : public ui::View {
public:
    AppearancePanel(const std::string& path, const std::string& name, std::function<void()> close)
        : m_editor(path)
    {
        setName("folderAppearance");
        m_preview = addChild<FolderThemeIcon>("folder", 96);
        m_title = addChild<ui::Label>(name);
        m_title->setTextAlignment(ui::Label::TextAlignment::Center);
        m_title->setElide(ui::Label::Elide::Middle);
        for (size_t i = 0; i < m_colors.size(); ++i) {
            auto* swatch = addChild<Swatch>(m_colors[i]);
            swatch->setName(tr(m_colorNames[i]));
            swatch->setToolTip(tr(m_colorNames[i]));
            swatch->choose = [this, i] { updated(m_editor.setColor(m_colors[i])); };
            m_swatches.push_back(swatch);
        }
        m_plus = addChild<ui::DialogButton>("+");
        m_plus->setName(tr("Custom Color"));
        m_plus->onClicked = [this] {
            m_custom->setVisible(!m_custom->isVisible());
            m_apply->setVisible(m_custom->isVisible());
            if (m_custom->isVisible()) m_custom->setFocus(ui::FocusReason::Other);
            requestLayout();
        };
        m_custom = addChild<ui::TextField>();
        m_custom->setPlaceholder("#RRGGBB");
        m_custom->setVisible(false);
        m_apply = addChild<ui::DialogButton>(tr("Apply"));
        m_apply->setVisible(false);
        m_apply->onClicked = [this] {
            auto text = m_custom->text();
            if (text.starts_with('#')) text.erase(0, 1);
            uint32_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
            if (text.size() != 6 || error != std::errc() || end != text.data() + text.size()) {
                m_error->setText(tr("Enter a color as #RRGGBB."));
                return;
            }
            updated(m_editor.setColor(value));
        };
        m_search = addChild<ui::TextField>();
        m_search->setPlaceholder(tr("Search Symbols"));
        m_search->onTextChanged = [this] { filter(); };
        m_scroll = addChild<ui::ScrollView>();
        m_grid = m_scroll->setContent<SymbolGrid>();
        m_grid->choose = [this](std::string id) { updated(m_editor.setSymbol(id)); };
        m_clear = addChild<ui::DialogButton>(tr("Clear"));
        m_clear->onClicked = [this] { updated(m_editor.setSymbol({})); };
        m_emoji = addChild<ui::DialogButton>(tr("Emoji"));
        m_emoji->onClicked = [this] {
            m_useEmoji = !m_useEmoji;
            m_emoji->setText(m_useEmoji ? tr("Symbols") : tr("Emoji"));
            filter();
        };
        m_done = addChild<ui::DialogButton>(tr("Done"));
        m_done->onClicked = std::move(close);
        m_error = addChild<ui::Label>();
        m_error->setWrap(true);
        m_error->setFontSize(11);
        const auto look = m_editor.appearance();
        m_useEmoji = look.symbol.starts_with("emoji:");
        if (m_useEmoji) m_emoji->setText(tr("Symbols"));
        filter();
        updated(m_editor.valid());
    }
    ui::SizeF sizeHint() const override { return {330, 441}; }
    double heightForWidth(double) const override { return sizeHint().height; }
protected:
    void layout() override
    {
        const double w = geometry().width;
        m_preview->setGeometry({(w - 96) / 2, 0, 96, 96});
        m_title->setGeometry({0, 97, w, 25});
        double x = (w - 37 * (m_swatches.size() + 1)) / 2;
        for (auto* swatch : m_swatches) {
            swatch->setGeometry({x, 128, 34, 33});
            x += 37;
        }
        m_plus->setGeometry({x, 131, 30, 27});
        double y = 171;
        if (m_custom->isVisible()) {
            m_custom->setGeometry({2, y, w - 81, 27});
            m_apply->setGeometry({w - 75, y, 73, 27});
            y += 33;
        }
        m_search->setGeometry({2, y, w - 4, 28});
        m_scroll->setGeometry({0, y + 34, w, 373 - y - 34});
        const double cell = w / 3;
        m_clear->setGeometry({2, 378, cell - 6, 27});
        m_emoji->setGeometry({cell + 2, 378, cell - 6, 27});
        m_done->setGeometry({cell * 2 + 2, 378, cell - 6, 27});
        m_error->setGeometry({2, 408, w - 4, 31});
    }
private:
    void filter()
    {
        m_grid->update(m_search->text(), m_useEmoji);
        m_grid->setSelected(m_editor.appearance().symbol);
    }
    void updated(bool success)
    {
        const auto look = m_editor.appearance();
        m_preview->setAppearance(look);
        m_grid->setSelected(look.symbol);
        for (size_t i = 0; i < m_swatches.size(); ++i)
            m_swatches[i]->setSelected(look.color == m_colors[i]);
        m_clear->setEnabled(!look.symbol.empty());
        m_error->setText(success ? std::string() : tr("Could not change the folder appearance. Check permissions and filesystem support."));
    }

    static constexpr std::array<std::optional<uint32_t>, 7> m_colors {
        std::nullopt, 0xf31d39, 0xff9d26, 0xfac529, 0x29a953, 0xc727ee, 0x727681};
    static constexpr std::array m_colorNames {trNoop("Default Color"), trNoop("Red"), trNoop("Orange"),
        trNoop("Yellow"), trNoop("Green"), trNoop("Purple"), trNoop("Gray")};
    folderappearance::Editor m_editor;
    FolderThemeIcon* m_preview;
    ui::Label *m_title, *m_error;
    ui::DialogButton *m_plus, *m_apply, *m_clear, *m_emoji, *m_done;
    ui::TextField *m_custom, *m_search;
    ui::ScrollView* m_scroll;
    SymbolGrid* m_grid;
    std::vector<Swatch*> m_swatches;
    bool m_useEmoji = false;
};

} // namespace

std::unique_ptr<FramelessDialog> showFolderAppearance(ui::Window* parent, std::string path, std::string name)
{
    auto dialog = std::make_unique<FramelessDialog>(tr("Customize Folder"), true);
    dialog->setParentWindow(parent);
    dialog->setHeader(29, 9);
    auto* target = dialog.get();
    dialog->onCloseRequested = [target] { target->hide(); };
    dialog->setContentFactory([target, path = std::move(path), name = std::move(name)] {
        return std::make_unique<AppearancePanel>(path, name, [target] { target->hide(); });
    }, 360, 15);
    dialog->show();
    return dialog;
}

} // namespace fm
