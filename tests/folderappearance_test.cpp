// SPDX-License-Identifier: BSD-3-Clause
#include "core/filetags.h"
#include "core/folderappearance.h"
#include "dialogs/folderappearancedialog.h"
#include "testutil.h"

#include <filesystem>
#include <gtest/gtest.h>

namespace appearance = fm::folderappearance;

TEST(FolderAppearanceTest, StoresColorSymbolAndTagsIndependently)
{
    fmtest::TemporaryDir dir;
    const auto path = dir.filePath("Project");
    ASSERT_TRUE(std::filesystem::create_directory(path));
    ASSERT_TRUE(fm::filetags::set(path, 4, true));
    std::string changed;
    auto connection = appearance::changed().connect([&](const std::string& path) { changed = path; });
    {
        appearance::Editor edit(path);
        ASSERT_TRUE(edit.valid());
        EXPECT_TRUE(edit.setColor(0xf31d39));
        EXPECT_TRUE(edit.setSymbol("emoji:car"));
        EXPECT_FALSE(edit.setColor(0x1000000));
        EXPECT_FALSE(edit.setSymbol("emoji:unknown"));
    }
    EXPECT_EQ(changed, path);
    EXPECT_EQ(appearance::read(path), (appearance::Appearance{0xf31d39, "emoji:car"}));
    appearance::Editor reopened(path);
    ASSERT_TRUE(reopened.setColor(std::nullopt));
    EXPECT_EQ(reopened.appearance().symbol, "emoji:car");
    ASSERT_TRUE(reopened.setSymbol({}));
    EXPECT_EQ(appearance::read(path), appearance::Appearance{});
    EXPECT_EQ(fm::filetags::read(path), 1 << 4);
}

TEST(FolderAppearanceTest, DoesNotApplyToAReplacementOrFollowALink)
{
    fmtest::TemporaryDir dir;
    const auto path = dir.filePath("Project");
    const auto moved = dir.filePath("Moved");
    const auto link = dir.filePath("Link");
    ASSERT_TRUE(std::filesystem::create_directory(path));
    appearance::Editor edit(path);
    ASSERT_TRUE(edit.valid());
    std::filesystem::rename(path, moved);
    ASSERT_TRUE(std::filesystem::create_directory(path));
    std::filesystem::create_directory_symlink(moved, link);
    EXPECT_FALSE(appearance::Editor(link).valid());
    EXPECT_TRUE(edit.setColor(0x349bac));
    EXPECT_FALSE(appearance::read(path).color.has_value());
    EXPECT_EQ(appearance::read(moved).color, 0x349bac);
}

TEST(FolderAppearanceTest, DialogRendersAndCloses)
{
    fmtest::TemporaryDir dir;
    cutefish::ui::Window parent;
    parent.show();
    auto dialog = fm::showFolderAppearance(&parent, dir.path(), "Project");
    ASSERT_TRUE(dialog->isShown());
    EXPECT_FALSE(dialog->window()->grab().isNull());
    dialog->onCloseRequested();
    EXPECT_FALSE(dialog->isShown());
}
