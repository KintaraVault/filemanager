// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "dialogs/framelessdialog.h"

namespace fm {

class FolderPage;

class ViewOptionsDialog : public FramelessDialog
{
public:
    explicit ViewOptionsDialog(ui::Window* parent);
    void sync(FolderPage* page);

private:
    class Panel;
    FolderPage* m_page = nullptr;
    Panel* m_panel = nullptr;
    int m_mode = -1;
    bool m_trash = false;
    std::string m_title;
};

} // namespace fm
