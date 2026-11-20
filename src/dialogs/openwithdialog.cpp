// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "dialogs/openwithdialog.h"

#include "base/application_registry.h"
#include "base/i18n.h"
#include "core/standardfolders.h"
#include "ui/check_box.h"
#include "ui/color_provider.h"
#include "ui/design_metrics.h"
#include "ui/dialog_window.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/list_view.h"
#include "ui/painter.h"
#include "ui/cell_layout.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>

namespace fm {

using ui::rounding::centeredStart;
using cutefish::i18n::tr;

// A compact row with the application's real desktop icon.
class ApplicationRow : public ui::View
{
public:
    explicit ApplicationRow(std::function<void(size_t, int)> pressed)
        : m_pressed(std::move(pressed))
    {
        m_icon = addChild<ui::ThemeIcon>(std::string(), 22);
        m_name = addChild<ui::Label>();
        m_name->setElide(ui::Label::Elide::Middle);
    }

    void bind(size_t row, const launcher::Application& application, bool selected)
    {
        m_row = row;
        m_selected = selected;
        m_icon->setIconName(application.icon);
        m_name->setText(application.name);
        m_name->setColor(selected ? ui::ColorId::HighlightedText : ui::ColorId::Text);
        requestLayout();
        requestPaint();
    }

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override
    {
        const ui::RectF rect = localRect().adjusted(8, 0, -8, 0);
        m_icon->setGeometry({rect.x, rect.y + centeredStart(rect.height, 22), 22, 22});
        const ui::SizeF hint = m_name->sizeHint();
        m_name->setGeometry({rect.x + 32, rect.y + centeredStart(rect.height, hint.height),
                              std::max(0.0, rect.width - 32), hint.height});
    }

    void paint(ui::Painter& painter) override
    {
        if (m_selected)
            painter.drawSmoothRectangle(localRect(), ui::design::menuItemRadius, ui::color(ui::ColorId::MenuHighlight));
    }

    void pointerPress(const ui::PointerEvent& event) override
    {
        if (event.button == ui::MouseButton::Left)
            m_pressed(m_row, event.clickCount);
    }

private:
    std::function<void(size_t, int)> m_pressed;
    ui::ThemeIcon* m_icon;
    ui::Label* m_name;
    size_t m_row = 0;
    bool m_selected = false;
};

OpenWithDialog::OpenWithDialog(std::string path, std::string mimeType)
    : FramelessDialog(tr("Open With"), false)
    , m_path(std::move(path))
    , m_mimeType(std::move(mimeType))
{
    m_applications = launcher::applicationsFor(m_path, m_mimeType);
    m_registryChanged = cutefish::base::ApplicationRegistry::instance().changed.connect([this] { retranslate(); });
    setContentFactory([this] { return createContent(); }, 390, ui::design::dialogInset);
    onCloseRequested = [this] { done(); };
}

void OpenWithDialog::show()
{
    if (!isShown())
        m_applications = launcher::applicationsFor(m_path, m_mimeType);
    FramelessDialog::show();
    if (m_hasShortcuts)
        return;
    m_hasShortcuts = true;
    auto move = [this](int step) {
        if (m_applications.empty())
            return;
        m_current = std::clamp(m_current + step, 0, int(m_applications.size()) - 1);
        m_list->reload();
        m_list->scrollToRow(size_t(m_current));
    };
    window()->addShortcut(ui::NoModifier, ui::key::Up, [move] { move(-1); });
    window()->addShortcut(ui::NoModifier, ui::key::Down, [move] { move(1); });
    window()->addShortcut(ui::NoModifier, ui::key::Return, [this] { openApplication(); });
}

void OpenWithDialog::retranslate()
{
    setTitle(tr("Open With"));
    if (!isShown())
        return;
    const std::string current = m_current >= 0 && m_current < int(m_applications.size())
            ? m_applications[size_t(m_current)].desktopFile
            : std::string();
    const bool isDefault = m_default && m_default->isChecked();
    m_applications = launcher::applicationsFor(m_path, m_mimeType);
    const auto found = std::find_if(m_applications.begin(), m_applications.end(),
                                    [&current](const launcher::Application& application) {
                                        return application.desktopFile == current;
                                    });
    m_current = found != m_applications.end() ? int(found - m_applications.begin()) : 0;
    rebuildContent();
    m_default->setChecked(isDefault);
}

void OpenWithDialog::done()
{
    hide();
    m_hasShortcuts = false;
    if (onDone)
        onDone();
}

void OpenWithDialog::openApplication()
{
    if (m_current < 0 || m_current >= int(m_applications.size()))
        return;
    const launcher::Application& application = m_applications[size_t(m_current)];
    if (!launcher::launchApplication(application.desktopFile, m_path)) {
        m_status->setText(tr("Could not open the file with this application."));
        return;
    }
    if (m_default && m_default->isChecked())
        launcher::setDefaultApplication(m_mimeType, application.desktopFile);
    done();
}

std::unique_ptr<ui::View> OpenWithDialog::createContent()
{
    auto layout = std::make_unique<ui::ColumnLayout>();
    layout->setSpacing(10);

    auto* title = layout->addChild<ui::Label>(tr("Open With"));
    title->setFontSize(14);
    title->setWeight(600);
    title->setColor(ui::ColorId::Text);
    title->setLayoutHints({.fillWidth = true});
    auto* subtitle = layout->addChild<ui::Label>(
            cutefish::i18n::format(tr("Choose an application to open “{name}”."), {{"name", fileName(m_path)}}));
    subtitle->setElide(ui::Label::Elide::Middle);
    subtitle->setColor(ui::ColorId::DisabledText);
    subtitle->setLayoutHints({.fillWidth = true});
    m_status = subtitle;

    m_list = layout->addChild<ui::ListView>();
    m_list->setName("openWithApplications");
    m_list->setLayoutHints({.fillWidth = true, .preferredHeight = 184});
    m_list->setRowHeight(ui::design::sidebarRowHeight);
    m_list->setSpacing(2);
    m_list->createItem = [this] {
        return std::make_unique<ApplicationRow>([this](size_t row, int clicks) {
            m_current = int(row);
            m_list->reload();
            if (clicks == 2)
                openApplication();
        });
    };
    m_list->bindItem = [this](ui::View* item, size_t row) {
        static_cast<ApplicationRow*>(item)->bind(row, m_applications[row], int(row) == m_current);
    };
    m_list->setRowCount(m_applications.size());
    m_list->setVisible(!m_applications.empty());
    auto* empty = layout->addChild<ui::Label>(cutefish::base::ApplicationRegistry::instance().isLoading()
                                            ? tr("Looking for applications…") : tr("No applications"));
    empty->setVisible(m_applications.empty());

    m_default = layout->addChild<ui::CheckBox>(tr("Set as default"));
    m_default->setPadding({});
    m_default->setEnabled(!m_applications.empty());

    auto* actions = layout->addChild<ui::RowLayout>();
    actions->setSpacing(8);
    actions->setLayoutHints({.horizontalAlignment = ui::Alignment::End});
    auto* cancel = actions->addChild<ui::DialogButton>(tr("Cancel"));
    cancel->onClicked = [this] { done(); };
    auto* open = actions->addChild<ui::DialogButton>(tr("Open"), true);
    open->setEnabled(!m_applications.empty());
    open->onClicked = [this] { openApplication(); };
    return layout;
}

} // namespace fm
