// SPDX-License-Identifier: BSD-3-Clause
#include "dialogs/gotofolder.h"

#include "base/i18n.h"
#include "base/paths.h"
#include "base/url.h"
#include "ui/color_provider.h"
#include "ui/cell_layout.h"
#include "ui/design_metrics.h"
#include "ui/dialog_window.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/list_view.h"
#include "ui/painter.h"
#include "ui/text_field.h"
#include "ui/window.h"

#include <algorithm>
#include <filesystem>

namespace fm {
namespace ui = cutefish::ui;
namespace base = cutefish::base;
namespace fs = std::filesystem;
using cutefish::i18n::tr;

std::string folderPath(const std::string& text, const std::string& current, const std::string& home)
{
    if (text.empty() || text.find('\0') != std::string::npos)
        return {};
    if (text.starts_with("file:")) {
        auto path = base::url::toLocalFile(text);
        return path && path->find('\0') == std::string::npos ? *path : std::string();
    }
    if (text.find("://") != std::string::npos)
        return {};
    if (text == "~")
        return home;
    if (text.starts_with("~/"))
        return home + text.substr(1);
    if (text.starts_with('~'))
        return {};
    const fs::path path(text);
    if (path.is_absolute())
        return path.string();
    const auto directory = base::url::toLocalFile(current);
    return fs::path(directory.value_or(home)).append(text).string();
}

std::vector<std::string> folderSuggestions(const std::string& text, const std::string& current,
                                           const std::string& home)
{
    if (text.empty())
        return {};
    const fs::path path(folderPath(text, current, home));
    if (path.empty())
        return {};
    std::error_code ec;
    std::vector<std::string> result;
    const auto needle = path.filename().string();
    if (!needle.empty() && needle != "." && needle != ".." && fs::is_directory(path, ec))
        result.push_back(path.string());
    fs::directory_iterator entries(path.parent_path(), fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && entries != end; entries.increment(ec)) {
        const auto name = entries->path().filename().string();
        if (!name.starts_with(needle) || (name.starts_with('.') && !needle.starts_with('.')))
            continue;
        auto candidate = entries->path().string();
        std::error_code status;
        if (candidate != path && entries->is_directory(status))
            result.push_back(std::move(candidate));
        if (result.size() == 65)
            break;
    }
    const bool exact = !result.empty() && result.front() == path;
    std::sort(result.begin() + (exact ? 1 : 0), result.end());
    if (result.size() > 6)
        result.resize(6);
    return result;
}

class GoToFolderDialog::Content : public ui::Column
{
public:
    Content(GoToFolderDialog& dialog, std::string current)
        : m_dialog(dialog)
        , m_current(std::move(current))
        , m_home(base::paths::home())
    {
        setSpacing(8);
        m_field = addChild<ui::TextField>();
        m_field->setName("folderPath");
        m_field->setPlaceholder(tr("Enter a folder path"));
        m_field->setLayoutHints({.fillWidth = true});
        m_field->setText("~/");
        m_field->onTextChanged = [this] { refresh(); };
        m_field->onAccepted = [this] { accept(); };
        m_message = addChild<ui::Label>(tr("Go to:"));
        m_message->setName("folderStatus");
        m_message->setColor(ui::ColorId::DisabledText);
        m_message->setLayoutHints({.fillWidth = true});
        m_list = addChild<ui::ListView>();
        m_list->setName("folderSuggestions");
        m_list->setLayoutHints({.fillWidth = true, .preferredHeight = 128});
        m_list->setRowHeight(ui::design::menuRowHeight);
        m_list->setSpacing(2);
        m_list->createItem = [this] { return std::make_unique<Row>(*this); };
        m_list->bindItem = [this](ui::View* view, size_t i) {
            static_cast<Row*>(view)->bind(i, m_folders[i], i == m_selected);
        };
        auto* actions = addChild<ui::RowLayout>();
        actions->setSpacing(8);
        actions->setLayoutHints({.horizontalAlignment = ui::Alignment::End});
        actions->addChild<ui::DialogButton>(tr("Cancel"))->onClicked = [this] { m_dialog.m_dialog->close(); };
        actions->addChild<ui::DialogButton>(tr("Go"), true)->onClicked = [this] { accept(); };
        refresh();
    }

    void focus() { m_field->setFocus(); m_field->selectAll(); }

    void move(int step)
    {
        if (m_folders.empty())
            return;
        m_selected = m_picked ? std::clamp(int(m_selected) + step, 0, int(m_folders.size()) - 1)
                              : (step < 0 ? m_folders.size() - 1 : 0);
        m_picked = true;
        m_list->reload();
        m_list->scrollToRow(m_selected);
    }

private:
    class Row final : public ui::View
    {
    public:
        explicit Row(Content& owner) : m_owner(owner)
        {
            m_label = addChild<ui::Label>();
            m_label->setElide(ui::Label::Elide::Middle);
            m_label->setVerticallyCentered(true);
        }
        void bind(size_t i, const std::string& path, bool selected)
        {
            m_index = i;
            m_selected = selected;
            m_label->setText(path);
            m_label->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::Text);
            requestPaint();
        }
    protected:
        bool acceptsPointer() const override { return true; }
        void layout() override { m_label->setGeometry(localRect().adjusted(8, 0, -8, 0)); }
        void paint(ui::Painter& painter) override
        {
            if (m_selected)
                painter.drawSmoothRectangle(localRect(), ui::design::menuItemRadius, ui::color(ui::ColorId::MenuHighlight));
        }
        void pointerPress(const ui::PointerEvent& event) override
        {
            if (event.button != ui::MouseButton::Left)
                return;
            m_owner.m_selected = m_index;
            m_owner.m_picked = true;
            m_owner.m_list->reload();
            if (event.clickCount == 2)
                m_owner.accept();
        }
    private:
        Content& m_owner;
        ui::Label* m_label;
        size_t m_index = 0;
        bool m_selected = false;
    };

    void refresh()
    {
        m_folders = folderSuggestions(m_field->text(), m_current, m_home);
        m_selected = 0;
        m_picked = false;
        m_message->setText(tr("Go to:"));
        m_message->setColor(ui::ColorId::DisabledText);
        m_list->setRowCount(m_folders.size());
        m_list->reload();
    }

    void accept()
    {
        auto path = folderPath(m_field->text(), m_current, m_home);
        std::error_code ec;
        if (!m_folders.empty() && (m_picked || !fs::is_directory(path, ec)))
            path = m_folders[m_selected];
        if (path.empty() || !fs::is_directory(path, ec)) {
            m_message->setText(tr("Folder not found."));
            m_message->setColor(ui::ColorId::Text);
            m_field->setFocus();
            return;
        }
        m_dialog.m_dialog->close();
        if (m_dialog.onFolder)
            m_dialog.onFolder(base::url::fromLocalFile(path));
    }

    GoToFolderDialog& m_dialog;
    const std::string m_current;
    const std::string m_home;
    ui::TextField* m_field;
    ui::Label* m_message;
    ui::ListView* m_list;
    std::vector<std::string> m_folders;
    size_t m_selected = 0;
    bool m_picked = false;
};

GoToFolderDialog::GoToFolderDialog(ui::Window* parent, std::string current)
    : m_dialog(std::make_unique<ui::DialogWindow>(parent, tr("Go to Folder"), 440))
{
    m_dialog->setCloseVisible(false);
    m_content = m_dialog->setContent<Content>(*this, std::move(current));
    m_dialog->onClosed = [this] { if (onDone) onDone(); };
    window()->addShortcut(ui::NoModifier, ui::key::Up, [this] { m_content->move(-1); });
    window()->addShortcut(ui::NoModifier, ui::key::Down, [this] { m_content->move(1); });
}

GoToFolderDialog::~GoToFolderDialog() = default;
ui::Window* GoToFolderDialog::window() const { return m_dialog->window(); }
void GoToFolderDialog::show() { m_dialog->show(); m_content->focus(); }

} // namespace fm
