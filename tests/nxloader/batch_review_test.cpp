#include "nxloader/batch_review.hpp"

#include <cassert>
#include <iostream>
#include <set>

using nxloader::EntryKind;
using nxloader::GroupPackages;
using nxloader::Root;
const auto not_cancelled = [] { return false; };

void game_folders_keep_content_together() {
    const std::vector<std::string> files{
        "ums0:/Games/Alpha/Base/base.nsp",
        "ums0:/Games/Alpha/DLC/pack.nsz",
        "ums0:/Games/Alpha/Updates/v2/patch.nsp",
        "ums0:/Games/Beta/base.nsz",
    };
    const auto plan = GroupPackages(files, {{"ums0:/Games/Beta/", EntryKind::Directory},
        {"ums0:////Games/Alpha", EntryKind::Directory}}, not_cancelled);
    assert(!plan.cancelled && plan.groups.size() == 2);
    assert(plan.groups[0].folder == "ums0:/Games/Alpha");
    assert((plan.groups[0].packages == std::vector<std::size_t>{0, 1, 2}));
    assert(plan.groups[1].folder == "ums0:/Games/Beta");
    assert((plan.groups[1].packages == std::vector<std::size_t>{3}));
    assert(nxloader::detail::RelativeTo(files[2], plan.groups[0].folder) == "Updates/v2/patch.nsp");
}

void arbitrary_content_folders_do_not_split_a_selected_game() {
    const std::vector<std::string> files{
        "/Games/Alpha/NSP/base.nsp", "/Games/Alpha/NSZ/patch.nsz", "/Games/Alpha/v2/content.nsp"};
    const auto plan = GroupPackages(files, {{"/Games/Alpha", EntryKind::Directory}}, not_cancelled);
    assert(plan.groups.size() == 1);
    assert(plan.groups[0].folder == "/Games/Alpha");
    assert((plan.groups[0].packages == std::vector<std::size_t>{0, 1, 2}));
}

void overlapping_selection_does_not_duplicate_or_split() {
    const std::vector<std::string> files{"/Games/Alpha/Updates/patch.nsp", "/Games/Alpha/base.nsp"};
    const auto plan = GroupPackages(files, {{"/Games/Alpha/Updates", EntryKind::Directory},
        {"/Games/Alpha", EntryKind::Directory}, {"/Games/Alpha/", EntryKind::Directory},
        {files[0], EntryKind::File}}, not_cancelled);
    assert(plan.groups.size() == 1 && plan.groups[0].folder == "/Games/Alpha");
    assert((plan.groups[0].packages == std::vector<std::size_t>{0, 1}));
}

void individual_files_use_game_folder() {
    const std::vector<std::string> files{"sdmc:/Games/Alpha/DLC/extra.nsp", "sdmc:/Games/Alpha/base.nsz"};
    const auto plan = GroupPackages(files, {{files[0], EntryKind::File}, {files[1], EntryKind::File}}, not_cancelled);
    assert(plan.groups.size() == 1 && plan.groups[0].folder == "sdmc:/Games/Alpha");
    assert((plan.groups[0].packages == std::vector<std::size_t>{0, 1}));
}

void empty_folders_and_similar_names_stay_distinct() {
    const std::vector<std::string> files{"/Game/base.nsp", "/Game 2/base.nsz"};
    const auto plan = GroupPackages(files, {{"/Game", EntryKind::Directory},
        {"/Game 2", EntryKind::Directory}, {"/Empty", EntryKind::Directory}}, not_cancelled);
    assert(plan.groups.size() == 3);
    assert(plan.groups[0].folder == "/Empty" && plan.groups[0].packages.empty());
    assert(plan.groups[1].folder == "/Game" && plan.groups[1].packages.size() == 1);
    assert(plan.groups[2].folder == "/Game 2" && plan.groups[2].packages.size() == 1);
}

void roots_and_relative_paths() {
    for (const std::string prefix : {"/", "ums0:/", ""}) {
        const std::vector<std::string> files{prefix + "Alpha/base.nsp", prefix + "Beta/base.nsp"};
        const auto plan = GroupPackages(files, {{prefix.empty() ? "." : prefix, EntryKind::Directory}}, not_cancelled);
        assert(plan.groups.size() == 1);
        assert(plan.groups[0].folder == (prefix.empty() ? "." : prefix));
        assert((plan.groups[0].packages == std::vector<std::size_t>{0, 1}));
    }
}

void cancellation_never_returns_partial_review() {
    const std::vector<std::string> files{"/Games/Alpha/base.nsp", "/Games/Beta/base.nsp"};
    const std::vector<Root> roots{{"/Games", EntryKind::Directory}};
    std::size_t calls{};
    GroupPackages(files, roots, [&] { ++calls; return false; });
    for (std::size_t stop = 0; stop < calls; ++stop) {
        std::size_t current{};
        const auto plan = GroupPackages(files, roots, [&] { return current++ == stop; });
        assert(plan.cancelled && plan.groups.empty());
    }
}

void large_selection_lists_every_package_once() {
    std::vector<std::string> files;
    std::vector<Root> roots;
    constexpr std::size_t count = 10000;
    for (std::size_t i = 0; i < count; ++i) {
        const auto folder = "ums0:/Games/Game " + std::to_string(i);
        roots.push_back({folder, EntryKind::Directory});
        files.push_back(folder + "/base.nsp");
        files.push_back(folder + "/Updates/patch.nsz");
    }
    const auto plan = GroupPackages(files, roots, not_cancelled);
    assert(!plan.cancelled && plan.groups.size() == count);
    std::set<std::size_t> indices;
    for (const auto& group : plan.groups) {
        assert(group.packages.size() == 2);
        for (auto index : group.packages) assert(indices.insert(index).second);
    }
    assert(indices.size() == files.size());
}

int main() {
    game_folders_keep_content_together();
    arbitrary_content_folders_do_not_split_a_selected_game();
    overlapping_selection_does_not_duplicate_or_split();
    individual_files_use_game_folder();
    empty_folders_and_similar_names_stay_distinct();
    roots_and_relative_paths();
    cancellation_never_returns_partial_review();
    large_selection_lists_every_package_once();
    std::cout << "Game-folder review tests passed.\n";
}
