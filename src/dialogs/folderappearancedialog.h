// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "dialogs/framelessdialog.h"

namespace fm {
std::unique_ptr<FramelessDialog> showFolderAppearance(ui::Window* parent, std::string path, std::string name);
}
