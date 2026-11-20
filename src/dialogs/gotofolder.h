// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cutefish::ui { class DialogWindow; class Window; }

namespace fm {

// Local folder lookup; no command expansion, globbing, or network protocol.
std::string folderPath(const std::string& text, const std::string& current, const std::string& home);
std::vector<std::string> folderSuggestions(const std::string& text, const std::string& current,
                                           const std::string& home);

class GoToFolderDialog
{
public:
    GoToFolderDialog(cutefish::ui::Window* parent, std::string current);
    ~GoToFolderDialog();
    void show();
    cutefish::ui::Window* window() const;

    std::function<void(const std::string&)> onFolder;
    std::function<void()> onDone;

private:
    class Content;
    std::unique_ptr<cutefish::ui::DialogWindow> m_dialog;
    Content* m_content = nullptr;
};

} // namespace fm
