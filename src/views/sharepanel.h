// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "core/fileitem.h"
#include "ui/view.h"

#include <functional>

namespace fm {

// The selected objects are captured on opening, so the header and the
// clipboard refer to the same objects even if the model is refreshed.
std::unique_ptr<ui::View> makeSharePanel(FileItemList items, std::string name, std::function<void()> close);

} // namespace fm
