// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "core/common.h"

#include <memory>
#include <functional>
#include <string>

namespace cutefish::ui {
class Menu;
}

namespace fm {

class FolderModel;

// What a model action of FolderModel::action() does.
void triggerAction(FolderModel& model, const std::string& name);

// The folder's menu without a selection, the
// selection's with one. The model's actions are prepared first.
using CustomizeFolder = std::function<void(std::string path, std::string name)>;
std::unique_ptr<ui::Menu> createFolderMenu(FolderModel& model, CustomizeFolder customizeFolder = {});

} // namespace fm
