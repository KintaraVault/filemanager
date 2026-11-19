// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "model/foldercontents.h"
#include "core/lister.h"
#include "core/filetags.h"
#include "core/launcher.h"
#include "core/preferences.h"
#include "core/standardfolders.h"

#include "base/collator.h"
#include "base/i18n.h"
#include "base/locale.h"

#include <algorithm>
#include <numeric>
#include <ctime>
#include <unicode/unistr.h>
#include <unicode/uchar.h>

namespace fm {

namespace {

using cutefish::i18n::tr;
using Section = std::pair<int, std::string>;

static Section sectionFor(const FileItem& item, int group, std::map<std::string, std::string>& applications)
{
    const auto& mime = item.mimeType;
    switch (group) {
    case Preferences::TagsGroup: {
        const auto tags = item.isLocalFile() ? filetags::read(item.path) : 0;
        for (size_t i = 0; i < filetags::names.size(); ++i)
            if (tags & (1u << i))
                return {int(i), tr(filetags::names[i])};
        return {int(filetags::names.size()), tr("Untagged")};
    }
    case Preferences::KindGroup:
        if (item.isDir) return {0, tr("Folders")};
        if (item.isBundle || item.isDesktopFile() || mime == "application/x-executable") return {1, tr("Applications")};
        if (mime.starts_with("text/") || mime == "application/pdf" || mime.starts_with("application/vnd.oasis.opendocument")
            || mime.starts_with("application/vnd.openxmlformats-officedocument")) return {2, tr("Documents")};
        if (mime.starts_with("image/")) return {3, tr("Images")};
        if (mime.starts_with("audio/")) return {4, tr("Audio")};
        if (mime.starts_with("video/")) return {5, tr("Movies")};
        if (mime == "application/zip" || mime == "application/x-7z-compressed" || mime == "application/x-tar"
            || mime == "application/gzip" || mime == "application/x-rar") return {6, tr("Archives")};
        return {7, tr("Other")};
    case Preferences::NameGroup: {
        const auto name = icu::UnicodeString::fromUTF8(item.text());
        const UChar32 first = name.isEmpty() ? 0 : name.char32At(0);
        if (!u_isalpha(first))
            return {1, "#"};
        std::string letter;
        icu::UnicodeString(u_toupper(first)).toUTF8String(letter);
        return {0, letter};
    }
    case Preferences::ModifiedGroup:
    case Preferences::CreatedGroup: {
        const int64_t value = group == Preferences::CreatedGroup ? item.created : item.modified;
        if (value <= 0)
            return {0, tr("No Date")};
        const auto seconds = std::time_t(value / 1000);
        const auto now = std::time(nullptr);
        std::tm today {}, date {};
        localtime_r(&now, &today);
        localtime_r(&seconds, &date);
        if (today.tm_year == date.tm_year && today.tm_yday == date.tm_yday)
            return {-1000000, tr("Today")};
        const double age = std::difftime(now, seconds);
        if (age >= 0 && age < 7 * 86400)
            return {-999999, tr("Previous 7 Days")};
        if (age >= 0 && age < 30 * 86400)
            return {-999998, tr("Previous 30 Days")};
        const auto title = base::Locale::system().toString(base::DateTime::fromSecondsSinceEpoch(seconds), "MMMM yyyy");
        return {-(date.tm_year + 1900) * 12 - date.tm_mon, title};
    }
    case Preferences::ApplicationGroup: {
        if (item.isDir)
            return {0, tr("Folders")};
        auto [it, inserted] = applications.try_emplace(mime);
        if (inserted) {
            const auto desktop = launcher::defaultApplication(mime);
            if (!desktop.empty()) {
                it->second = fileName(desktop);
                if (it->second.ends_with(".desktop"))
                    it->second.resize(it->second.size() - 8);
            }
        }
        return it->second.empty() ? Section{2, tr("Other")} : Section{1, it->second};
    }
    case Preferences::SizeGroup:
        if (item.isDir) return {0, tr("Folders")};
        if (item.size == 0) return {1, tr("Empty")};
        if (item.size < 1000000) return {2, tr("Under 1 MB")};
        if (item.size < 100000000) return {3, tr("1–100 MB")};
        if (item.size < 1000000000) return {4, tr("100 MB–1 GB")};
        return {5, tr("1 GB and Larger")};
    default:
        return {};
    }
}

// What the sort compares, read once per item instead of once per comparison.
struct SortEntry
{
    const std::string* nameKey = nullptr;
    std::string mimeType;
    std::string originKey;
    int64_t modified = 0;
    int64_t deleted = 0;
    uint64_t size = 0;
    bool dir = false;
};

struct Cancelled
{
};

} // namespace

struct FolderContents::Prepared
{
    FileItemList listed;
    uint64_t listingGeneration = 0;
    FileItemList ordered;
    std::unordered_map<std::string, int> rows;
    ContentsChange change;
    std::vector<Section> sections;
};

struct FolderContents::Branch
{
    DirectoryLister lister;
    FolderContents contents;
    base::Subscription changed;
    base::Subscription completed;
};

FolderContents::FolderContents()
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(std::chrono::milliseconds(0));
    m_timer.onTimeout = [this] {
        prepare();
    };
}

FolderContents::~FolderContents()
{
    cancelPreparation();
}

FileItemPtr FolderContents::item(int row) const
{
    return row >= 0 && row < count() ? m_items[size_t(row)] : nullptr;
}

int FolderContents::rowForUrl(const std::string& url) const
{
    const auto found = m_rows.find(url);
    return found != m_rows.end() ? found->second : -1;
}

int FolderContents::rowForLocalPath(const std::string& path) const
{
    for (size_t row = 0; row < m_items.size(); ++row) {
        if (m_items[row]->path == path)
            return int(row);
    }
    return -1;
}

std::string FolderContents::originalLocation(const FileItem& item)
{
    if (item.originalPath.empty())
        return {};

    const std::string folder = parentPath(item.originalPath);
    const std::string home = homePath();
    if (folder == home)
        return "~";
    if (home != "/" && folder.starts_with(home + '/'))
        return '~' + folder.substr(home.size());
    return folder;
}

void FolderContents::setLister(DirectoryLister* lister)
{
    if (m_lister == lister)
        return;

    m_itemsChanged.reset();
    clear();
    m_lister = lister;
    if (!lister)
        return;

    m_itemsChanged = lister->itemsChanged.connect([this] {
        ++m_listingGeneration;
        schedulePreparation();
    });
    if (!lister->items().empty())
        schedulePreparation();
}

void FolderContents::setQuery(const SortQuery& query)
{
    m_query = query;
    for (const auto& [url, branch] : m_expanded)
        branch->contents.setQuery({query.column, query.descending, query.directoriesFirst});
    schedulePreparation();
}

void FolderContents::retakeSnapshots()
{
    if (!m_lister)
        return;
    m_listed.clear();
    ++m_listingGeneration;
    schedulePreparation();
}

void FolderContents::cancelPreparation()
{
    if (m_cancelled)
        m_cancelled->store(true);
    m_timer.stop();
    m_job.cancel();
}

void FolderContents::clear()
{
    cancelPreparation();
    m_items.clear();
    m_sections.clear();
    m_baseItems.clear();
    m_baseSections.clear();
    m_expanded.clear();
    m_depths.clear();
    m_rows.clear();
    m_renames.clear();
    m_listed.clear();
    ++m_listingGeneration;
    reset.emit();
}

void FolderContents::expectRename(const std::string& from, const std::string& to)
{
    m_renames[from] = to;
}

void FolderContents::forgetRename(const std::string& from, const std::string& to)
{
    if (const auto found = m_renames.find(from); found != m_renames.end() && found->second == to)
        m_renames.erase(found);
}

bool FolderContents::isSettled() const
{
    return !m_lister->isListing() || m_lister->isSearching();
}

void FolderContents::schedulePreparation()
{
    cancelPreparation();
    if (m_lister && isSettled())
        m_timer.start();
}

void FolderContents::setFilter(Filter filter)
{
    m_filter = std::move(filter);
    schedulePreparation();
}

void FolderContents::prepare()
{
    if (!m_lister || !isSettled())
        return;

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancelled = cancelled;

    std::vector<std::string> offered;
    for (const auto& [from, to] : m_renames)
        offered.push_back(from);

    // A new sort or filter of an unchanged listing reuses its snapshots.
    const bool relist = m_listedGeneration != m_listingGeneration;
    std::vector<files::FileItem> listed;
    if (relist)
        listed = m_lister->items();
    m_job = base::ThreadPool::global().run(
            [listed = std::move(listed), relist, kept = m_listed, generation = m_listingGeneration, previous = m_baseItems,
             query = m_query, renames = m_renames, filter = m_filter, cancelled]() -> std::shared_ptr<Prepared> {
                auto result = std::make_shared<Prepared>();
                result->listingGeneration = generation;
                const auto checkpoint = [&] {
                    if (cancelled->load())
                        throw Cancelled();
                };

                try {
                    if (relist) {
                        // The previous snapshot of a file whose stat is unchanged is kept.
                        std::unordered_map<std::string, const FileItemPtr*> known;
                        known.reserve(kept.size());
                        for (const FileItemPtr& item : kept)
                            known[item->url] = &item;
                        // Numbers in names compare by value: "img2" before "img10".
                        base::Collator collator;
                        collator.setNumericMode(true);
                        const auto standardNames = standardFolderNames();
                        result->listed.reserve(listed.size());
                        for (const files::FileItem& file : listed) {
                            checkpoint();
                            FileItem snapshot = DirectoryLister::snapshot(file);
                            const auto found = known.find(snapshot.url);
                            if (found != known.end() && (*found->second)->sameMetadata(snapshot)) {
                                result->listed.push_back(*found->second);
                                continue;
                            }
                            // Standard folders sort by the name they are shown under.
                            std::string name = snapshot.name;
                            if (snapshot.isDir && snapshot.isLocalFile()) {
                                if (const auto standard = standardNames->find(snapshot.path); standard != standardNames->end())
                                    name = standard->second;
                            }
                            snapshot.sortKey = collator.sortKey(name);
                            result->listed.push_back(std::make_shared<const FileItem>(std::move(snapshot)));
                        }
                    } else {
                        result->listed = kept;
                    }
                    FileItemList incoming;
                    incoming.reserve(result->listed.size());
                    for (const FileItemPtr& item : result->listed) {
                        checkpoint();
                        if ((!filter || filter(*item)) && (query.tag < 0 || (item->isLocalFile() && (filetags::read(item->path) & (1u << query.tag)))))
                            incoming.push_back(item);
                    }

                    // Rows by URL, in listing order until the sort renumbers them.
                    std::unordered_map<std::string, int> listedRows;
                    listedRows.reserve(incoming.size());
                    for (size_t row = 0; row < incoming.size(); ++row)
                        listedRows[incoming[row]->url] = int(row);

                    std::vector<size_t> positions(incoming.size());
                    std::iota(positions.begin(), positions.end(), 0);

                    if (query.column >= 0) {
                        base::Collator collator;
                        collator.setNumericMode(true);

                        std::vector<SortEntry> entries(incoming.size());
                        for (size_t i = 0; i < incoming.size(); ++i) {
                            checkpoint();
                            const FileItem& item = *incoming[i];
                            SortEntry& entry = entries[i];
                            entry.dir = item.isDir;
                            entry.nameKey = &item.sortKey;
                            if (query.column == Preferences::Size)
                                entry.size = item.size;
                            else if (query.column == Preferences::CreatedTime)
                                entry.modified = item.created;
                            else if (query.column == Preferences::AccessedTime)
                                entry.modified = item.accessed;
                            else if (query.column == Preferences::ModifiedTime)
                                entry.modified = item.modified;
                            else if (query.column == Preferences::Type)
                                entry.mimeType = item.mimeType;
                            else if (query.column == Preferences::OriginalPath)
                                entry.originKey = collator.sortKey(originalLocation(item));
                            else if (query.column == Preferences::DeletionTime)
                                entry.deleted = item.deletionTime;
                        }

                        const auto less = [&](size_t a, size_t b) {
                            const SortEntry& left = entries[a];
                            const SortEntry& right = entries[b];

                            if (query.directoriesFirst && left.dir != right.dir)
                                return left.dir;

                            int order = 0;
                            if (query.column == Preferences::Size)
                                order = left.size < right.size ? -1 : left.size > right.size;
                            else if (query.column == Preferences::ModifiedTime || query.column == Preferences::CreatedTime
                                     || query.column == Preferences::AccessedTime)
                                order = left.modified < right.modified ? -1 : left.modified > right.modified;
                            else if (query.column == Preferences::Type)
                                order = left.mimeType.compare(right.mimeType);
                            else if (query.column == Preferences::OriginalPath)
                                order = left.originKey.compare(right.originKey);
                            else if (query.column == Preferences::DeletionTime)
                                order = left.deleted < right.deleted ? -1 : left.deleted > right.deleted;
                            if (order == 0)
                                order = left.nameKey->compare(*right.nameKey);
                            if (order == 0)
                                order = incoming[a]->url.compare(incoming[b]->url);
                            return query.descending ? order > 0 : order < 0;
                        };

                        checkpoint();
                        std::stable_sort(positions.begin(), positions.end(), less);
                    }

                    // Which listed files a previous row keeps, and those that are new.
                    std::vector<bool> retained(incoming.size(), false);
                    std::vector<int> previousRows(previous.size(), -1);
                    for (size_t row = 0; row < previous.size(); ++row) {
                        checkpoint();
                        const FileItem& item = *previous[row];
                        int found = -1;
                        bool renamedHere = false;
                        if (const auto it = listedRows.find(item.url); it != listedRows.end())
                            found = it->second;
                        if (found < 0) {
                            // A rename we made ourselves: the file went away under its old
                            // name and is back under the new one; the row stays.
                            if (const auto renamed = renames.find(item.url); renamed != renames.end()) {
                                if (const auto it = listedRows.find(renamed->second); it != listedRows.end()) {
                                    found = it->second;
                                    renamedHere = true;
                                }
                            }
                        }
                        // A rename onto an existing name keeps one row, not two.
                        if (found >= 0 && retained[size_t(found)])
                            found = -1;
                        if (found < 0) {
                            result->change.removed.push_back(item.url);
                            continue;
                        }
                        retained[size_t(found)] = true;
                        previousRows[row] = found;
                        if (renamedHere)
                            result->change.renamed.emplace_back(item.url, incoming[size_t(found)]->url);
                    }

                    if (query.column < 0) {
                        // Unsorted views keep their rows in place; new files come last.
                        positions.clear();
                        for (size_t row = 0; row < previous.size(); ++row) {
                            if (previousRows[row] >= 0)
                                positions.push_back(size_t(previousRows[row]));
                        }
                        for (size_t i = 0; i < incoming.size(); ++i) {
                            if (!retained[i])
                                positions.push_back(i);
                        }
                    }

                    if (query.group != Preferences::NoGroup) {
                        std::map<std::string, std::string> applications;
                        std::vector<Section> keys;
                        keys.reserve(incoming.size());
                        for (const auto& item : incoming) {
                            checkpoint();
                            keys.push_back(sectionFor(*item, query.group, applications));
                        }
                        std::stable_sort(positions.begin(), positions.end(), [&](size_t a, size_t b) { return keys[a] < keys[b]; });
                        for (size_t row = 0; row < positions.size(); ++row) {
                            const auto& key = keys[positions[row]];
                            if (!row || key != keys[positions[row - 1]])
                                result->sections.emplace_back(int(row), key.second);
                        }
                    }
                    result->ordered.reserve(positions.size());
                    std::unordered_map<std::string, int> oldRows;
                    for (size_t row = 0; row < previous.size(); ++row) {
                        if (previousRows[row] >= 0)
                            oldRows[incoming[size_t(previousRows[row])]->url] = int(row);
                    }
                    for (size_t row = 0; row < positions.size(); ++row) {
                        const FileItemPtr& item = incoming[positions[row]];
                        result->ordered.push_back(item);
                        result->rows[item->url] = int(row);
                        if (!retained[positions[row]]) {
                            result->change.added.push_back(item->url);
                            continue;
                        }
                        const int old = oldRows[item->url];
                        if (old != int(row))
                            result->change.reordered = true;
                        if (!previous[size_t(old)]->sameMetadata(*item) || previous[size_t(old)]->url != item->url)
                            result->change.changedRows.push_back(int(row));
                    }
                    if (!result->change.removed.empty() || !result->change.added.empty())
                        result->change.reordered = true;
                } catch (const Cancelled&) {
                    return nullptr;
                }
                return result;
            },
            [this, cancelled, offered](std::shared_ptr<Prepared> prepared) {
                if (!prepared || cancelled->load())
                    return;
                // A rename this listing did not match had its chance; only ones
                // expected since it started stay.
                for (const std::string& from : offered)
                    m_renames.erase(from);
                publish(std::move(prepared));
            });

    preparationStarted.emit();
}

void FolderContents::publish(std::shared_ptr<Prepared> prepared)
{
    // The old lists are freed on a worker: tens of thousands of entries
    // take milliseconds the UI thread does not have.
    auto old = std::make_unique<std::pair<FileItemList, FileItemList>>(std::move(m_listed), std::move(m_baseItems));
    m_listed = std::move(prepared->listed);
    m_listedGeneration = prepared->listingGeneration;
    m_baseItems = std::move(prepared->ordered);
    m_baseSections = std::move(prepared->sections);
    if (m_expanded.empty() && m_depths.empty()) {
        m_items = m_baseItems;
        m_sections = m_baseSections;
        m_rows = std::move(prepared->rows);
        rowsChanged.emit(prepared->change);
    } else {
        publishTree(prepared->change);
    }
    preparationFinished.emit();
    base::ThreadPool::global().post([old = std::shared_ptr(std::move(old)), prepared = std::move(prepared)]() mutable {
        old.reset();
        prepared.reset();
    });
}

void FolderContents::toggleExpansion(const std::string& url, bool showHidden)
{
    if (m_expanded.erase(url)) {
        const auto prefix = url + '/';
        std::erase_if(m_expanded, [&](const auto& entry) { return entry.first.starts_with(prefix); });
        publishTree();
        return;
    }
    const auto file = item(rowForUrl(url));
    if (!file || !file->isDir || file->isLink || !file->isLocalFile() || depth(rowForUrl(url)) >= 32)
        return;
    auto branch = std::make_unique<Branch>();
    branch->lister.setShowHiddenFiles(showHidden);
    branch->lister.setAutoUpdate(true);
    branch->contents.setQuery({m_query.column, m_query.descending, m_query.directoriesFirst});
    branch->contents.setLister(&branch->lister);
    auto* raw = branch.get();
    branch->changed = raw->contents.rowsChanged.connect([this](const ContentsChange&) { publishTree(); });
    branch->completed = raw->lister.completed.connect([raw] { raw->contents.schedulePreparation(); });
    m_expanded.emplace(url, std::move(branch));
    raw->lister.openUrl(url);
    publishTree();
}

void FolderContents::collapseAll()
{
    if (m_expanded.empty())
        return;
    m_expanded.clear();
    publishTree();
}

void FolderContents::setTreeHidden(bool show)
{
    for (const auto& [url, branch] : m_expanded)
        branch->lister.setShowHiddenFiles(show);
}

void FolderContents::publishTree(const ContentsChange& baseChange)
{
    auto oldRows = std::move(m_rows);
    auto oldItems = std::move(m_items);
    ContentsChange change;
    change.renamed = baseChange.renamed;
    m_rows.clear();
    m_items.clear();
    m_depths.clear();
    m_sections.clear();
    const auto append = [&](auto&& self, const FileItemPtr& file, int depth) -> void {
        const int row = int(m_items.size());
        if (!m_rows.emplace(file->url, row).second)
            return;
        m_items.push_back(file);
        if (!m_expanded.empty())
            m_depths.push_back(depth);
        if (const auto known = oldRows.find(file->url); known == oldRows.end()) {
            change.added.push_back(file->url);
            change.reordered = true;
        } else {
            change.reordered |= known->second != row;
            if (oldItems[size_t(known->second)] != file)
                change.changedRows.push_back(row);
            oldRows.erase(known);
        }
        if (const auto branch = m_expanded.find(file->url); branch != m_expanded.end()) {
            for (const auto& child : branch->second->contents.items())
                self(self, child, depth + 1);
        }
    };
    auto section = m_baseSections.begin();
    for (size_t row = 0; row < m_baseItems.size(); ++row) {
        if (section != m_baseSections.end() && section->first == int(row)) {
            m_sections.emplace_back(int(m_items.size()), section->second);
            ++section;
        }
        append(append, m_baseItems[row], 0);
    }
    for (const auto& [url, row] : oldRows)
        change.removed.push_back(url);
    change.reordered |= !change.removed.empty();
    rowsChanged.emit(change);
}

} // namespace fm
