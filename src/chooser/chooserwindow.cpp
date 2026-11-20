// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "chooser/chooserwindow.h"
#include "chooser/filechooser.h"
#include "dialogs/unlockdialog.h"
#include "model/pathbarmodel.h"
#include "model/places.h"
#include "views/colors.h"
#include "views/completionpopup.h"
#include "views/listview.h"
#include "views/pathbar.h"
#include "views/style.h"

#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/url.h"
#include "ui/application.h"
#include "ui/check_box.h"
#include "ui/color_provider.h"
#include "ui/combo_box.h"
#include "ui/dialog_window.h"
#include "ui/header_button.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/progress_bar.h"
#include "ui/cell_layout.h"
#include "ui/scroll_view.h"
#include "ui/text_field.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <cmath>

namespace fm {

using ui::rounding::centeredStart;
using cutefish::i18n::format;
using cutefish::i18n::tr;

static constexpr double kWidth = 860;
static constexpr double kHeight = 520;
static constexpr double kCompactWidth = 390;
static constexpr double kCompactHeight = 318;
static constexpr double kSideBarWidth = 180;
static constexpr double kSmallSpacing = 6;
static constexpr double kLargeSpacing = ui::design::dialogInset;
static constexpr double kFieldHeight = ui::design::compactControlHeight;

// The chooser's places: favourites, disks and local mounts; remote places
// cannot be handed to the portal as local paths.
class ChooserPlaces : public ui::View
{
public:
    ChooserPlaces(FileChooser& chooser, PlacesModel& places)
        : m_chooser(chooser)
        , m_places(places)
    {
        rebuild();
    }

    bool acceptsPointer() const override { return true; }

    double heightForWidth(double) const override
    {
        double height = kLargeSpacing + 32;
        for (const Row& row : m_rows)
            height += (row.section ? 36 : 0) + (row.usable ? rowHeight() + 3 : 0);
        return height;
    }

    void rebuild()
    {
        for (const auto& child : children())
            child->setVisible(false);
        m_rows.clear();
        std::vector<std::shared_ptr<View>> gone;
        while (!children().empty())
            gone.push_back(takeChild(children().front().get()));
        base::EventLoop::current()->post([gone] {});

        m_favorites = addChild<ui::Label>(tr("Favorites"));
        m_favorites->setWeight(700);
        m_favorites->setFontSize(9 * 4.0 / 3);
        std::string category = "Favorites";
        for (int i = 0; i < m_places.count(); ++i) {
            const PlacesModel::Place& place = m_places.at(i);
            Row row;
            row.index = i;
            if (place.category != category) {
                category = place.category;
                if (!category.empty()) {
                    row.section = addChild<ui::Label>(tr(category.c_str()));
                    row.section->setWeight(700);
                    row.section->setFontSize(9 * 4.0 / 3);
                }
            }
            row.usable = place.url.starts_with("file:") || place.setupNeeded();
            row.icon = addChild<ui::ThemeIcon>(place.iconName, 20);
            row.icon->setThemeSize(20);
            row.label = addChild<ui::Label>(place.displayName());
            row.label->setElide(ui::Label::Elide::Right);
            row.icon->setVisible(row.usable);
            row.label->setVisible(row.usable);
            m_rows.push_back(row);
        }
        requestLayout();
        requestPaint();
    }

protected:
    double rowHeight() const { return std::max(double(ui::design::sidebarRowHeight), style::fontHeight() + 10); }

    void layout() override
    {
        const ui::Theme& theme = ui::Theme::current();
        const double width = geometry().width;
        const double inset = 8;
        double y = kLargeSpacing;
        m_favorites->setColor(theme.disabledText);
        m_favorites->setGeometry({inset + kSmallSpacing, y + centeredStart(32, m_favorites->sizeHint().height),
                                  width - inset - kSmallSpacing, m_favorites->sizeHint().height});
        y += 32;
        const std::string current = m_chooser.directory();
        for (Row& row : m_rows) {
            if (row.section) {
                row.section->setColor(theme.disabledText);
                const double height = row.section->sizeHint().height;
                row.section->setGeometry({inset + kSmallSpacing, y + centeredStart(36, height),
                                          width - inset - kSmallSpacing, height});
                y += 36;
            }
            const double height = row.usable ? rowHeight() : 0;
            row.rect = {0, y, width, height};
            const PlacesModel::Place& place = m_places.at(row.index);
            row.checked = place.path() == current;
            const ui::Color color = row.checked ? theme.highlight : theme.text;
            if (row.usable) {
                row.icon->setColor(color);
                row.icon->setGeometry({inset + kSmallSpacing, y + centeredStart(height, 20), 20, 20});
                row.label->setColor(color);
                const double labelX = inset + kSmallSpacing + 20 + kSmallSpacing;
                const double labelHeight = row.label->sizeHint().height;
                row.label->setGeometry({labelX, y + centeredStart(height, labelHeight),
                                        std::max(0.0, width - inset - kSmallSpacing - labelX), labelHeight});
            }
            if (row.usable)
                y += height + 3;
        }
    }

    void paint(ui::Painter& painter) override
    {
        const double inset = 8;
        for (const Row& row : m_rows) {
            if (!row.usable)
                continue;
            ui::Color fill {0, 0, 0, 0};
            if (&row - m_rows.data() == m_pressed)
                fill = ui::color(ui::ColorId::ItemPressed);
            else if (row.checked)
                fill = ui::Theme::current().highlight.withAlpha(.12);
            if (fill.a)
                painter.drawRoundedRect({inset, row.rect.y, row.rect.width - inset * 2, row.rect.height},
                                        {ui::design::menuItemRadius, 0, ui::themeCorners()}, fill);
        }
    }

    void pointerPress(const ui::PointerEvent& event) override
    {
        m_pressed = rowAt(event.position);
        requestPaint();
    }

    void pointerRelease(const ui::PointerEvent& event) override
    {
        const int pressed = m_pressed;
        m_pressed = -1;
        requestPaint();
        if (pressed < 0 || rowAt(event.position) != pressed)
            return;
        const int index = m_rows[size_t(pressed)].index;
        const PlacesModel::Place& place = m_places.at(index);
        if (place.setupNeeded())
            m_places.requestSetup(index);
        else
            m_chooser.navigate(place.path());
    }

private:
    struct Row
    {
        int index = 0;
        bool usable = false;
        bool checked = false;
        ui::Label* section = nullptr;
        ui::ThemeIcon* icon = nullptr;
        ui::Label* label = nullptr;
        ui::RectF rect;
    };

    int rowAt(ui::PointF point) const
    {
        for (size_t i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].usable && m_rows[i].rect.contains(point))
                return int(i);
        }
        return -1;
    }

    FileChooser& m_chooser;
    PlacesModel& m_places;
    ui::Label* m_favorites = nullptr;
    std::vector<Row> m_rows;
    int m_pressed = -1;
};

// The location field: closes when it loses the keys or Escape is pressed.
class LocationField : public ui::TextField
{
public:
    std::function<bool(const ui::KeyEvent&)> onKey;
    std::function<void()> onFocusLost;

protected:
    bool keyPress(const ui::KeyEvent& event) override
    {
        if (onKey && onKey(event))
            return true;
        return TextField::keyPress(event);
    }

    void focusOut() override
    {
        TextField::focusOut();
        if (onFocusLost)
            onFocusLost();
    }
};

// The file manager's breadcrumbs, or the location field in their place;
// dragging them moves the window, which has no title bar.
class ChooserPathBar : public ui::View
{
public:
    explicit ChooserPathBar(FileChooser& chooser)
        : m_chooser(chooser)
    {
        crumbs = addChild<Breadcrumbs>(false);
        crumbs->onItemClicked = [this](const std::string& path) { m_chooser.navigate(path); };
        crumbs->onEmptyClicked = [this] { openEditor(); };
        crumbs->onDragStarted = [this] {
            if (window())
                window()->startMove();
        };
        editor = addChild<LocationField>();
        editor->setName("locationField");
        editor->setSelectByMouse(true);
        editor->setInputPurpose(ui::TextPurpose::Url);
        editor->setVisible(false);
        editor->onAccepted = [this] { m_chooser.navigate(editor->text()); };
        editor->onFocusLost = [this] {
            completion->hide();
            closeEditor();
        };
        editor->onKey = [this](const ui::KeyEvent& event) {
            if (completion->handleKey(event, true))
                return true;
            if (event.key == ui::key::Escape) {
                closeEditor();
                return true;
            }
            return false;
        };
        editor->onTextEdited = [this] { completion->edited(true); };
        editor->onTextChanged = [this] { completion->textChanged(); };
        completion = std::make_unique<CompletionPopup>(editor);
        completion->onActivated = [this](const std::string& path) { m_chooser.navigate(path); };
    }

    void setUrl(const std::string& url)
    {
        std::string resolved;
        crumbs->setCrumbs(pathCrumbs(url, &resolved));
        m_url = resolved;
    }

    void openEditor()
    {
        completion->reset();
        editor->setText(m_url);
        editor->setVisible(true);
        crumbs->setVisible(false);
        editor->setFocus();
        editor->selectAll();
    }

    void closeEditor()
    {
        if (!editor->isVisible())
            return;
        editor->setVisible(false);
        crumbs->setVisible(true);
        if (onClosed)
            onClosed();
    }

    bool isEditing() const { return editor->isVisible(); }

    Breadcrumbs* crumbs;
    LocationField* editor;
    std::unique_ptr<CompletionPopup> completion;
    std::function<void()> onClosed;

protected:
    void layout() override
    {
        crumbs->setGeometry(localRect());
        editor->setGeometry(localRect());
    }

private:
    FileChooser& m_chooser;
    std::string m_url;
};

class ChooserRoot : public ui::View
{
public:
    ChooserRoot(ChooserWindow* window, FileChooser& chooser)
        : m_owner(window)
        , m_chooser(chooser)
    {
        m_places = std::make_unique<PlacesModel>();
        placesScroll = addChild<ui::ScrollView>();
        placesScroll->setScrollDirections(false, true);
        placesScroll->setCornerRadius(12);
        places = placesScroll->setContent<ChooserPlaces>(chooser, *m_places);
        title = addChild<ui::Label>(chooser.title());
        title->setWeight(600);
        title->setElide(ui::Label::Elide::Right);
        title->setVerticallyCentered(true);
        saveIcon = addChild<ui::Icon>("folder", 22);
        saveHint = addChild<ui::Label>(tr("Choose a name and location for the file."));
        saveHint->setWrap(true);

        back = addChild<ui::HeaderButton>(std::string(), 30);
        back->setGlyph("chevron-left", 16);
        back->onClicked = [this] { m_chooser.back(); };
        forward = addChild<ui::HeaderButton>(std::string(), 30);
        forward->setGlyph("chevron-right", 16);
        forward->onClicked = [this] { m_chooser.forward(); };
        for (ui::HeaderButton* button : {back, forward})
            button->setPlateAlphas({0.17, 0.26}, {0.21, 0.15});
        pathBar = addChild<ChooserPathBar>(chooser);
        pathBar->onClosed = [this] {
            if (m_expanded)
                list->contentView()->setFocus();
            else
                nameField->setFocus();
        };

        list = addChild<FolderListView>(chooser.folder());
        list->setName("fileList");
        list->setChooserMode(chooser.multiple());
        list->setBackdropBase(ui::ColorId::Background);
        list->onActivated = [this](int row) { m_chooser.activate(row); };
        list->onKeyPressed = [this](const ui::KeyEvent& event) { return listKey(event); };
        busy = addChild<ui::BusyIndicator>();
        busy->setVisible(false);

        saveAsLabel = addChild<ui::Label>(tr("Save As:"));
        whereLabel = addChild<ui::Label>(tr("Location:"));
        nameField = addChild<ui::TextField>(chooser.fileName());
        nameField->setName("fileNameField");
        nameField->setSelectByMouse(true);
        nameField->onTextEdited = [this] { m_chooser.setFileName(nameField->text()); };
        nameField->onAccepted = [this] { m_chooser.accept(); };
        filterLabel = addChild<ui::Label>(tr("File Type:"));
        std::vector<std::string> filterNames;
        for (const base::json::Value& filter : chooser.filters())
            filterNames.push_back(filter["name"].toString());
        filterCombo = addChild<ui::ComboBox>(filterNames);
        filterCombo->setName("filterCombo");
        filterCombo->setCurrentIndex(chooser.filterIndex());
        filterCombo->onActivated = [this](int index) { m_chooser.setFilterIndex(index); };

        for (const base::json::Value& choice : chooser.choices()) {
            ChoiceRow row;
            const std::string id = choice["id"].toString();
            const base::json::Value::Array& options = choice["options"].toArray();
            if (options.empty()) {
                row.check = addChild<ui::CheckBox>(choice["label"].toString());
                row.check->setChecked(choice["selected"].toString() == "true");
                row.check->onToggled = [this, id](bool checked) { m_chooser.setChoice(id, checked ? "true" : "false"); };
            } else {
                row.label = addChild<ui::Label>(choice["label"].toString());
                std::vector<std::string> labels;
                std::vector<std::string> ids;
                int selected = 0;
                for (const base::json::Value& option : options) {
                    if (option["id"].toString() == choice["selected"].toString())
                        selected = int(labels.size());
                    labels.push_back(option["label"].toString());
                    ids.push_back(option["id"].toString());
                }
                row.combo = addChild<ui::ComboBox>(labels);
                row.combo->setCurrentIndex(selected);
                row.combo->onActivated = [this, id, ids](int index) {
                    if (index >= 0 && index < int(ids.size()))
                        m_chooser.setChoice(id, ids[size_t(index)]);
                };
            }
            m_choices.push_back(row);
        }

        error = addChild<ui::Label>();
        error->setWrap(true);
        error->setVisible(false);

        newFolder = addChild<ui::DialogButton>(tr("New Folder"));
        newFolder->setVisible(canCreateFolder());
        newFolder->onClicked = [this] { m_owner->beginNewFolder(); };
        summary = addChild<ui::Label>();
        summary->setElide(ui::Label::Elide::Middle);
        summary->setVisible(chooser.multiple());
        expand = addChild<ui::DialogButton>();
        expand->setName("toggleFileBrowser");
        expand->onClicked = [this] { setExpanded(!m_expanded); };
        cancel = addChild<ui::DialogButton>(tr("Cancel"));
        cancel->setName("cancelButton");
        cancel->onClicked = [this] { m_chooser.cancel(); };
        accept = addChild<ui::DialogButton>(chooser.acceptLabel(), true);
        accept->setName("acceptButton");
        accept->onClicked = [this] { m_chooser.accept(); };

        m_subscriptions.push_back(chooser.locationChanged.connect([this] { locationChanged(); }));
        m_subscriptions.push_back(chooser.fileNameChanged.connect([this] {
            if (nameField->text() != m_chooser.fileName())
                nameField->setText(m_chooser.fileName());
        }));
        m_subscriptions.push_back(chooser.selectionChanged.connect([this] { summary->setText(m_chooser.selectionSummary()); }));
        m_subscriptions.push_back(chooser.errorChanged.connect([this] {
            error->setText(m_chooser.error());
            error->setVisible(!m_chooser.error().empty());
            requestLayout();
        }));
        m_subscriptions.push_back(chooser.folder().statusChanged.connect([this] { updateBusy(); }));
        m_subscriptions.push_back(m_places->changed.connect([this] {
            places->rebuild();
            placesScroll->requestLayout();
        }));
        m_subscriptions.push_back(m_places->deviceSetupDone.connect([this](const std::string& path) { m_chooser.navigate(path); }));
        m_subscriptions.push_back(m_places->passphraseRequested.connect([this](std::string udi, std::string name, bool retry) {
            m_owner->m_unlock = std::make_unique<UnlockVolumeDialog>(this->window(), *m_places, std::move(udi),
                                                                     std::move(name), retry);
            m_owner->m_unlock->show();
        }));
        m_subscriptions.push_back(m_places->deviceOperationFailed.connect([this](const std::string& message) {
            m_owner->m_deviceError = std::make_unique<ui::ConfirmDialog>(this->window(), std::string(), message);
            m_owner->deviceErrorTexts();
            m_owner->m_deviceError->setCancelVisible(false);
            m_owner->m_deviceError->show();
        }));
        locationChanged();
        summary->setText(chooser.selectionSummary());
        nameField->setVisible(chooser.saveMode());
        saveAsLabel->setVisible(chooser.saveMode());
        expand->setVisible(canCollapse());
    }

    bool canCreateFolder() const { return m_chooser.saveMode() || m_chooser.directoryMode(); }
    bool canCollapse() const { return m_chooser.saveMode() && m_choices.empty(); }
    bool acceptsPointer() const override { return true; }

    void setExpanded(bool expanded)
    {
        m_expanded = expanded || !canCollapse();
        const ui::SizeF size = m_expanded ? ui::SizeF{kWidth, kHeight} : ui::SizeF{kCompactWidth, kCompactHeight};
        window()->setMinimumSize({});
        window()->setMaximumSize(size);
        window()->setMinimumSize(size);
        window()->resize(size);
        placesScroll->setVisible(m_expanded);
        back->setVisible(m_expanded);
        forward->setVisible(m_expanded);
        list->setVisible(m_expanded);
        newFolder->setVisible(m_expanded && canCreateFolder());
        whereLabel->setVisible(!m_expanded);
        saveIcon->setVisible(!m_expanded);
        saveHint->setVisible(!m_expanded);
        expand->setText(m_expanded ? tr("Hide Folders") : tr("Browse…"));
        updateBusy();
        requestLayout();
    }

    // The location, the list's selection, the name typed and the filter stay.
    void retranslate()
    {
        saveAsLabel->setText(tr("Save As:"));
        filterLabel->setText(tr("File Type:"));
        whereLabel->setText(tr("Location:"));
        title->setText(m_chooser.title());
        saveHint->setText(tr("Choose a name and location for the file."));
        expand->setText(m_expanded ? tr("Hide Folders") : tr("Browse…"));
        newFolder->setText(tr("New Folder"));
        cancel->setText(tr("Cancel"));
        accept->setText(m_chooser.acceptLabel());
        summary->setText(m_chooser.selectionSummary());
        m_chooser.folder().retranslate();
        list->retranslate();
        m_places->retranslate();
        pathBar->setUrl(m_chooser.folder().url());
        requestLayout();
    }

    void focusFirst()
    {
        // Saving starts in the name with its stem selected, so typing renames and Enter saves.
        if (m_chooser.saveMode()) {
            nameField->setFocus();
            const std::string& text = nameField->text();
            const size_t dot = text.rfind('.');
            nameField->select(0, dot != std::string::npos && dot > 0 ? dot : text.size());
        } else {
            list->contentView()->setFocus();
        }
    }

    ChooserPlaces* places;
    ui::ScrollView* placesScroll;
    ui::Label* title;
    ui::Icon* saveIcon;
    ui::Label* saveHint;
    ui::HeaderButton* back;
    ui::HeaderButton* forward;
    ChooserPathBar* pathBar;
    FolderListView* list;
    ui::BusyIndicator* busy;
    ui::Label* saveAsLabel;
    ui::Label* whereLabel;
    ui::TextField* nameField;
    ui::Label* filterLabel;
    ui::ComboBox* filterCombo;
    ui::Label* error;
    ui::DialogButton* newFolder;
    ui::DialogButton* expand;
    ui::Label* summary;
    ui::DialogButton* cancel;
    ui::DialogButton* accept;

protected:
    void layout() override
    {
        const ui::Theme& theme = ui::Theme::current();
        const ui::RectF rect = localRect();
        const double titleHeight = 28;
        const double left = m_expanded ? kSideBarWidth : 0;
        const double width = rect.width - left;
        const double inner = width - 2 * kLargeSpacing;
        const double edge = left + kLargeSpacing;
        const double gap = 10;
        const double titleX = m_expanded ? edge : edge + 100;
        title->setFontSize(theme.fontSize() + (m_expanded ? 0 : 4));
        title->setGeometry({titleX, kLargeSpacing - 5, edge + inner - titleX, titleHeight});
        placesScroll->setGeometry({8, 20, kSideBarWidth - 8, rect.height - 40});
        if (!m_expanded) {
            saveIcon->setColor(theme.highlight);
            saveIcon->setGeometry({edge + 20, kLargeSpacing + 13, 22, 22});
            saveHint->setColor(theme.disabledText);
            const double hintWidth = edge + inner - titleX - 20;
            saveHint->setGeometry({titleX, title->geometry().bottom() + 2, hintWidth, saveHint->heightForWidth(hintWidth)});
        }

        double y = title->geometry().bottom() + gap;
        if (!m_expanded)
            y = std::max(108.0, saveHint->geometry().bottom() + gap);
        const bool save = m_chooser.saveMode();
        const bool filter = !m_chooser.filters().empty();
        filterLabel->setVisible(filter);
        filterCombo->setVisible(filter);
        double labelWidth = std::max(saveAsLabel->sizeHint().width, filterLabel->sizeHint().width);
        if (!m_expanded)
            labelWidth = std::max({labelWidth, whereLabel->sizeHint().width, 90.0});
        labelWidth = std::min(labelWidth, inner * .45);
        const double fieldX = edge + labelWidth + gap;
        const double fieldWidth = inner - labelWidth - gap - (m_expanded ? 0 : 20);
        const auto placeField = [&](ui::Label* label, ui::View* field, double rowY) {
            placeLabel(label, edge, rowY, labelWidth);
            field->setGeometry({fieldX, rowY, fieldWidth, kFieldHeight});
        };
        if (save) {
            placeField(saveAsLabel, nameField, y);
            y += kFieldHeight + gap;
        }

        // The header row.
        if (m_expanded) {
            back->setEnabled(m_chooser.folder().canGoBack());
            forward->setEnabled(m_chooser.folder().canGoForward());
            back->setGeometry({edge, y, 30, 30});
            forward->setGeometry({edge + 33, y, 30, 30});
            pathBar->setGeometry({edge + 69, y, inner - 69, 30});
            y += 30 + gap;
        } else {
            if (filter) {
                placeField(filterLabel, filterCombo, y);
                y += kFieldHeight + gap;
            }
            placeField(whereLabel, pathBar, y);
            y += kFieldHeight + gap;
        }

        // From the bottom: the buttons, the error, the fields.
        double bottom = rect.height - kLargeSpacing;
        const double buttonHeight = std::max({cancel->sizeHint().height, accept->sizeHint().height,
                                             newFolder->isVisible() ? newFolder->sizeHint().height : 0.0,
                                             expand->isVisible() ? expand->sizeHint().height : 0.0});
        const double buttonsY = bottom - buttonHeight;
        double right = left + width - kLargeSpacing;
        const int buttons = 2 + int(newFolder->isVisible()) + int(expand->isVisible());
        const double maxButtonWidth = (inner - (buttons - 1) * kSmallSpacing) / buttons;
        const auto buttonSize = [maxButtonWidth](const ui::DialogButton* button) {
            auto size = button->sizeHint();
            size.width = std::clamp(size.width, std::min(76.0, maxButtonWidth), maxButtonWidth);
            return size;
        };
        const ui::SizeF acceptSize = buttonSize(accept);
        accept->setGeometry({right - acceptSize.width, buttonsY + centeredStart(buttonHeight, acceptSize.height),
                             acceptSize.width, acceptSize.height});
        right -= acceptSize.width + kSmallSpacing;
        const ui::SizeF cancelSize = buttonSize(cancel);
        cancel->setGeometry({right - cancelSize.width, buttonsY + centeredStart(buttonHeight, cancelSize.height),
                             cancelSize.width, cancelSize.height});
        right -= cancelSize.width + kSmallSpacing;
        double buttonX = edge;
        if (newFolder->isVisible()) {
            const ui::SizeF size = buttonSize(newFolder);
            newFolder->setGeometry({buttonX, buttonsY + centeredStart(buttonHeight, size.height), size.width,
                                    size.height});
            buttonX += size.width + kSmallSpacing;
        }
        if (expand->isVisible()) {
            const ui::SizeF size = buttonSize(expand);
            expand->setGeometry({buttonX, buttonsY + centeredStart(buttonHeight, size.height), size.width, size.height});
            buttonX += size.width + kSmallSpacing;
        }
        if (summary->isVisible()) {
            summary->setColor(theme.disabledText);
            const double summaryX = buttonX + kSmallSpacing;
            const double height = summary->sizeHint().height;
            summary->setGeometry({summaryX, buttonsY + centeredStart(buttonHeight, height),
                                  std::max(0.0, std::min(summary->sizeHint().width, right - summaryX)), height});
        }
        m_footerY = buttonsY - 10;
        bottom = m_footerY - 10;

        if (error->isVisible()) {
            error->setColor(ui::color(ui::ColorId::DestructiveAction));
            const double height = error->heightForWidth(inner);
            error->setGeometry({edge, bottom - height, inner, height});
            bottom -= height + gap;
        }

        if (m_expanded && (filter || !m_choices.empty())) {
            double height = 0;
            auto addRow = [&](double rowHeight) {
                if (height > 0)
                    height += kSmallSpacing;
                height += rowHeight;
            };
            if (filter)
                addRow(kFieldHeight);
            for (const ChoiceRow& row : m_choices)
                addRow(row.check ? row.check->sizeHint().height : kFieldHeight);
            double rowY = bottom - height;
            bottom = rowY - gap;
            if (filter) {
                placeField(filterLabel, filterCombo, rowY);
                rowY += kFieldHeight + kSmallSpacing;
            }
            for (ChoiceRow& row : m_choices) {
                if (row.check) {
                    const ui::SizeF size = row.check->sizeHint();
                    row.check->setGeometry({left + kLargeSpacing, rowY, size.width, size.height});
                    rowY += size.height + kSmallSpacing;
                } else {
                    const double labelWidthHere = std::min(row.label->sizeHint().width, inner * .45);
                    placeLabel(row.label, left + kLargeSpacing, rowY, labelWidthHere);
                    const double comboX = left + kLargeSpacing + labelWidthHere + kSmallSpacing;
                    row.combo->setGeometry({comboX, rowY, left + width - kLargeSpacing - comboX, kFieldHeight});
                    rowY += kFieldHeight + kSmallSpacing;
                }
            }
        }

        const ui::RectF listRect {edge, y, inner, std::max(0.0, bottom - y)};
        list->setGeometry(listRect);
        busy->setGeometry({listRect.x + centeredStart(listRect.width, 22), listRect.y + centeredStart(listRect.height, 22),
                           22, 22});
    }

    void paint(ui::Painter& painter) override
    {
        const ui::RectF rect = localRect();
        const auto separator = ui::color(ui::ColorId::Separator);
        const double left = m_expanded ? kSideBarWidth : 0;
        if (m_expanded) {
            painter.fillRoundedRect({8, 8, left - 8, rect.height - 16}, 18,
                                    ui::color(FmColor::ChooserSidebarBackground), ui::themeCorners());
            painter.strokeRoundedRect(list->geometry().adjusted(-1, -1, 1, 1), 8, separator);
        } else {
            const auto& theme = ui::Theme::current();
            const ui::RectF icon = saveIcon->geometry().adjusted(-13, -13, 13, 13);
            painter.fillRoundedRect(icon, 12, theme.highlight.withAlpha(.14), ui::themeCorners());
            if (!pathBar->isEditing())
                painter.fillRoundedRect(pathBar->geometry(), 9, theme.alternateBackground, ui::themeCorners());
        }
        painter.drawHorizontalLine(left + kLargeSpacing, m_footerY, rect.width - left - 2 * kLargeSpacing, separator);
    }

    // The top strip stands in for the title bar: a drag there moves the window.
    void pointerPress(const ui::PointerEvent& event) override
    {
        m_press = event.position;
        m_pressActive = event.button == ui::MouseButton::Left && event.position.y < kLargeSpacing * 2;
    }

    void pointerMove(const ui::PointerEvent& event) override
    {
        if (m_pressActive && (std::abs(event.position.x - m_press.x) > 4 || std::abs(event.position.y - m_press.y) > 4)) {
            m_pressActive = false;
            if (window())
                window()->startMove();
        }
    }

    bool keyPress(const ui::KeyEvent& event) override
    {
        const uint32_t modifiers = event.modifiers;
        if (event.key == ui::key::Escape && !pathBar->isEditing()) {
            m_chooser.cancel();
            return true;
        }
        if (modifiers == ui::AltModifier) {
            if (event.key == ui::key::Up) {
                m_chooser.up();
                return true;
            }
            if (event.key == ui::key::Left) {
                m_chooser.back();
                return true;
            }
            if (event.key == ui::key::Right) {
                m_chooser.forward();
                return true;
            }
        }
        if (modifiers == ui::ControlModifier && event.key == ui::key::letter('h')) {
            m_chooser.setShowHidden(!m_chooser.showHidden());
            return true;
        }
        if (modifiers == ui::ControlModifier && event.key == ui::key::letter('l')) {
            pathBar->openEditor();
            return true;
        }
        if (modifiers == (ui::ControlModifier | ui::ShiftModifier) && event.key == ui::key::letter('n')
            && canCreateFolder()) {
            if (!m_expanded)
                setExpanded(true);
            m_owner->beginNewFolder();
            return true;
        }
        return false;
    }

private:
    struct ChoiceRow
    {
        ui::Label* label = nullptr;
        ui::ComboBox* combo = nullptr;
        ui::CheckBox* check = nullptr;
    };

    static void placeLabel(ui::Label* label, double x, double y, double width)
    {
        label->setColor(ui::Theme::current().text);
        label->setElide(ui::Label::Elide::Right);
        const double height = label->sizeHint().height;
        label->setGeometry({x, y + centeredStart(kFieldHeight, height), width, height});
    }

    // Return opens the current row or accepts; Ctrl+A selects every file.
    bool listKey(const ui::KeyEvent& event)
    {
        if (event.key == ui::key::Return || event.key == ui::key::KeypadEnter) {
            const int row = list->currentRow();
            if (m_chooser.multiple() || row < 0)
                m_chooser.accept();
            else
                m_chooser.activate(row);
            return true;
        }
        if (event.key == ui::key::letter('a') && (event.modifiers & ui::ControlModifier)) {
            m_chooser.selectAll();
            return true;
        }
        return false;
    }

    void locationChanged()
    {
        list->setCurrentIndex(-1);
        pathBar->setUrl(m_chooser.folder().url());
        pathBar->closeEditor();
        places->requestLayout();
        places->requestPaint();
        updateBusy();
        requestLayout();
    }

    void updateBusy()
    {
        const bool listing = m_expanded && m_chooser.folder().status() == FolderModel::Status::Listing;
        busy->setVisible(listing);
        busy->setRunning(listing);
    }

    ChooserWindow* m_owner;
    FileChooser& m_chooser;
    std::unique_ptr<PlacesModel> m_places;
    std::vector<ChoiceRow> m_choices;
    std::vector<base::Subscription> m_subscriptions;
    ui::PointF m_press;
    bool m_pressActive = false;
    bool m_expanded = true;
    double m_footerY = 0;
};

ChooserWindow::ChooserWindow(FileChooser& chooser)
    : m_chooser(chooser)
{
    m_window = std::make_unique<ui::Window>();
    m_window->setTitle(chooser.title());
    // Not a file manager window: the dock does not count it, Quit and Hide leave it alone.
    m_window->setDialogAppearance();
    m_window->setBackgroundColor(ui::ColorId::PanelBackground);
    auto root = std::make_unique<ChooserRoot>(this, chooser);
    m_root = root.get();
    m_window->setContentView(std::move(root));
    m_root->setExpanded(!chooser.saveMode());
    m_window->onCloseRequested = [this] { m_chooser.cancel(); };

    const std::string parent = chooser.request()["parent_window"].toString();
    if (parent.starts_with("wayland:"))
        m_window->setTransientForForeign(parent, chooser.request()["modal"].toBool(true));

    m_subscriptions.push_back(ui::Application::instance()->languageChanged.connect([this] { retranslate(); }));
    m_subscriptions.push_back(chooser.confirmOverwrite.connect([this](const std::string& name) {
        m_overwrite = std::make_unique<ui::ConfirmDialog>(m_window.get(), std::string());
        m_overwriteName = name;
        overwriteTexts();
        m_overwrite->setDestructive(true);
        m_overwrite->onAccepted = [this] { m_chooser.accept(true); };
        m_overwrite->show();
    }));
}

ChooserWindow::~ChooserWindow() = default;

void ChooserWindow::retranslate()
{
    m_window->setTitle(m_chooser.title());
    m_root->retranslate();
    if (m_overwrite)
        overwriteTexts();
    if (m_deviceError)
        deviceErrorTexts();
    if (m_unlock)
        m_unlock->retranslate();
    if (m_newFolder && m_newFolder->window() && m_newFolderRetranslate)
        m_newFolderRetranslate();
}

void ChooserWindow::overwriteTexts()
{
    m_overwrite->setTitle(tr("Replace File?"));
    m_overwrite->setText(format(tr("“{name}” already exists. Replace it?"), {{"name", m_overwriteName}}));
    m_overwrite->setConfirmText(tr("Replace"));
}

void ChooserWindow::deviceErrorTexts()
{
    m_deviceError->setTitle(tr("Device unavailable"));
    m_deviceError->setConfirmText(tr("Close"));
}

void ChooserWindow::show()
{
    m_window->show();
    m_root->focusFirst();
    m_window->activate();
}

void ChooserWindow::beginNewFolder()
{
    m_newFolder = std::make_unique<ui::DialogWindow>(m_window.get(), tr("New Folder"), 340);
    // Cancel closes it.
    m_newFolder->setCloseVisible(false);
    if (ui::Window* window = m_newFolder->window())
        window->setBackgroundColor(ui::ColorId::PanelBackground);

    class Content : public ui::ColumnLayout
    {
    public:
        explicit Content(ChooserWindow* owner)
        {
            setSpacing(16);
            setPadding({0, 8, 0, 0});
            field = addChild<ui::TextField>();
            field->setSelectByMouse(true);
            field->setLayoutHints({.fillWidth = true});
            auto* actions = addChild<ui::RowLayout>();
            actions->setSpacing(8);
            actions->setLayoutHints({.horizontalAlignment = ui::Alignment::End});
            cancel = actions->addChild<ui::DialogButton>();
            create = actions->addChild<ui::DialogButton>(std::string(), true);
            retranslate();
            create->setEnabled(false);
            cancel->onClicked = [owner] { owner->m_newFolder->close(); };
            const auto accept = [this, owner] {
                if (field->text().empty())
                    return;
                const std::string name = field->text();
                owner->m_newFolder->close();
                owner->m_chooser.newFolder(name);
            };
            create->onClicked = accept;
            field->onAccepted = accept;
            field->onTextChanged = [this] { create->setEnabled(!field->text().empty()); };
        }

        void retranslate()
        {
            field->setPlaceholder(tr("Folder name"));
            cancel->setText(tr("Cancel"));
            create->setText(tr("Create"));
        }

        ui::TextField* field;
        ui::DialogButton* cancel;
        ui::DialogButton* create;
    };

    auto* content = m_newFolder->setContent<Content>(this);
    // The name typed stays.
    m_newFolderRetranslate = [this, content] {
        m_newFolder->setTitle(tr("New Folder"));
        content->retranslate();
    };
    if (ui::Window* window = m_newFolder->window())
        window->addShortcut(ui::NoModifier, ui::key::Escape, [this] { m_newFolder->close(); });
    m_newFolder->show();
    content->field->setFocus();
}

} // namespace fm
