// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/signal.h"
#include "core/launcher.h"
#include "dialogs/framelessdialog.h"

#include <functional>
#include <string>
#include <vector>

namespace cutefish::ui {
class CheckBox;
class Label;
class ListView;
}

namespace fm {

// The applications for a file, the default
// first, to open it with once or from now on.
class OpenWithDialog : public FramelessDialog
{
public:
    OpenWithDialog(std::string path, std::string mimeType);
    void show();

    // The application chosen and Set as default stay.
    void retranslate() override;

    std::function<void()> onDone;

private:
    std::unique_ptr<ui::View> createContent();
    void openApplication();
    void done();

    std::string m_path;
    std::string m_mimeType;
    std::vector<launcher::Application> m_applications;
    int m_current = 0;
    bool m_hasShortcuts = false;
    cutefish::base::Subscription m_registryChanged;
    ui::ListView* m_list = nullptr;
    ui::CheckBox* m_default = nullptr;
    ui::Label* m_status = nullptr;
};

} // namespace fm
