#include "nxloader/package_scan.hpp"

#include <cassert>
#include <iostream>
#include <map>

using nxloader::Entry;
using nxloader::EntryKind;
using nxloader::Root;
using nxloader::ScanOptions;
using enum EntryKind;

struct Tree {
    std::map<std::string, std::vector<Entry>> entries;
    std::map<std::string, std::size_t> reads;
    std::map<std::string, std::string> errors;

    template <class Emit>
    std::string operator()(const std::string& path, Emit&& emit) {
        ++reads[path];
        if (const auto found = entries.find(path); found != entries.end()) {
            for (const auto& entry : found->second) {
                if (!emit(entry)) {
                    break;
                }
            }
        }
        if (const auto error = errors.find(path); error != errors.end()) {
            return error->second;
        }
        return {};
    }
};

const auto not_cancelled = [] { return false; };

void recursive_selection() {
    Tree tree{{
        {"ums0:/Games", {{"Second", Directory}, {"First", Directory}, {"README.txt", File},
                         {"._resource.NSP", File}, {".Spotlight-V100", Directory},
                         {"cycle", Other}, {".", Directory}, {"..", Directory}}},
        {"ums0:/Games/First", {{"base.NsP", File}, {"DLC", Directory}}},
        {"ums0:/Games/First/DLC", {{"content.NSZ", File}}},
        {"ums0:/Games/Second", {{"patch.nsz", File}, {"manual.pdf", File}}},
    }, {}, {}};
    const auto result = nxloader::Scan({{"ums0:////Games/", Directory}}, tree, not_cancelled);
    assert(result.error.empty() && !result.cancelled);
    assert(result.directories == 4);
    assert((result.files == std::vector<std::string>{
        "ums0:/Games/First/DLC/content.NSZ", "ums0:/Games/First/base.NsP", "ums0:/Games/Second/patch.nsz"}));
    assert(!tree.reads.contains("ums0:/Games/.Spotlight-V100"));
    assert(!tree.reads.contains("ums0:/Games/cycle"));
}

void overlapping_roots() {
    Tree tree{{
        {"/Games", {{"A", Directory}, {"root.nsp", File}}},
        {"/Games/A", {{"base.nsp", File}, {"base.nsp", File}}},
    }, {}, {}};
    const auto result = nxloader::Scan({
        {"/Games/", Directory}, {"/Games//A/", Directory}, {"/Games/A/base.nsp", File},
        {"/Games/A/../A/./", Directory}, {"/Games", Directory}}, tree, not_cancelled);
    assert(result.error.empty() && !result.cancelled);
    assert((result.files == std::vector<std::string>{"/Games/A/base.nsp", "/Games/root.nsp"}));
    assert(tree.reads["/Games"] == 1 && tree.reads["/Games/A"] == 1);

    Tree relative{{{".", {{"base.nsp", File}}}}, {}, {}};
    const auto relative_result = nxloader::Scan({{"./", Directory}, {"./base.nsp", File}}, relative, not_cancelled);
    assert(relative_result.error.empty());
    assert(relative_result.files == std::vector<std::string>{"base.nsp"});
}

void file_types_and_empty_selection() {
    Tree tree;
    auto result = nxloader::Scan({{"/z.NSP", File}, {"/a.xci", File}, {"/b.XCZ", File},
        {"/c.MsP", File}, {"/d.nsz", File}, {"/e.nsp.bak", File}, {"/f.nsp", Other},
        {"/._g.nsp", File}}, tree, not_cancelled);
    assert(result.files.size() == 5 && result.directories == 0 && result.error.empty());
    assert(result.files.front() == "/a.xci" && result.files.back() == "/z.NSP");
    result = nxloader::Scan({}, tree, not_cancelled);
    assert(result.files.empty() && result.error.empty() && !result.cancelled);

    Tree unicode{{{"ums0:/遊戲", {{"Café [日本].NSZ", File}}}}, {}, {}};
    result = nxloader::Scan({{"ums0:/遊戲", Directory}}, unicode, not_cancelled);
    assert(result.error.empty());
    assert(result.files == std::vector<std::string>{"ums0:/遊戲/Café [日本].NSZ"});
}

void scan_failure_discards_partial_queue() {
    Tree tree{{{"/games", {{"base.nsp", File}, {"nested", Directory}}}}, {}, {{"/games/nested", "drive removed"}}};
    auto result = nxloader::Scan({{"/games", Directory}}, tree, not_cancelled);
    assert(result.files.empty() && !result.cancelled);
    assert(result.error.find("drive removed") != std::string::npos);
    tree.errors = {{"/games", "read failed after entries"}};
    result = nxloader::Scan({{"/games", Directory}}, tree, not_cancelled);
    assert(result.files.empty() && result.error.find("read failed after entries") != std::string::npos);
}

void cancellation_discards_partial_queue() {
    Tree tree{{{"/games", {{"base.nsp", File}, {"patch.nsz", File}}}}, {}, {}};
    auto result = nxloader::Scan({{"/games", Directory}}, tree, [] { return true; });
    assert(result.cancelled && result.files.empty() && tree.reads.empty());
    std::size_t checks{};
    result = nxloader::Scan({{"/games", Directory}}, tree, [&] { return ++checks >= 4; });
    assert(result.cancelled && result.files.empty() && result.error.empty());
    assert(tree.reads["/games"] == 1);
}

void limits_discard_partial_queue() {
    Tree tree{{{"/g", {{"a.nsp", File}, {"b.nsz", File}, {"dir", Directory}}}}, {}, {}};
    ScanOptions options;
    options.max_files = 1;
    auto result = nxloader::Scan({{"/g", Directory}}, tree, not_cancelled, options);
    assert(result.files.empty() && result.error.find("Package limit") != std::string::npos);
    options.max_files = 2;
    options.max_directories = 1;
    result = nxloader::Scan({{"/g", Directory}}, tree, not_cancelled, options);
    assert(result.files.empty() && result.error.find("Directory limit") != std::string::npos);
    options.max_directories = 2;
    options.max_path_length = 7;
    result = nxloader::Scan({{"/g", Directory}}, tree, not_cancelled, options);
    assert(result.files.empty() && result.error.find("Path exceeds") != std::string::npos);
    options.max_path_length = 8;
    result = nxloader::Scan({{"/g", Directory}}, tree, not_cancelled, options);
    assert(result.error.empty() && result.files.size() == 2);
    options.max_files = 0;
    result = nxloader::Scan({{"/g/a.nsp", File}}, tree, not_cancelled, options);
    assert(!result.error.empty() && result.files.empty());
}

void invalid_names_and_roots() {
    for (const auto& name : std::vector<std::string>{"", "nested/file.nsp", std::string("bad\0.nsp", 8)}) {
        Tree tree{{{"/g", {{name, File}}}}, {}, {}};
        const auto result = nxloader::Scan({{"/g", Directory}}, tree, not_cancelled);
        assert(result.files.empty() && result.error.find("Invalid directory entry") != std::string::npos);
    }
    Tree tree;
    auto result = nxloader::Scan({{"", Directory}}, tree, not_cancelled);
    assert(!result.error.empty());
    result = nxloader::Scan({{std::string("/g\0/h", 5), Directory}}, tree, not_cancelled);
    assert(!result.error.empty());
}

void mount_root_and_deep_tree() {
    Tree tree{{{"ums0:/", {{"a.nsp", File}}}}, {}, {}};
    auto result = nxloader::Scan({{"ums0:////", Directory}}, tree, not_cancelled);
    assert(result.error.empty() && result.files == std::vector<std::string>{"ums0:/a.nsp"});

    std::string path = "/deep";
    for (std::size_t i = 0; i < 1000; ++i) {
        tree.entries[path] = {{"d", Directory}};
        path += "/d";
    }
    tree.entries[path] = {{"end.nsp", File}};
    ScanOptions options;
    options.max_path_length = 4096;
    result = nxloader::Scan({{"/deep", Directory}}, tree, not_cancelled, options);
    assert(result.error.empty() && result.directories == 1001);
    assert(result.files == std::vector<std::string>{path + "/end.nsp"});
}

int main() {
    recursive_selection();
    overlapping_roots();
    file_types_and_empty_selection();
    scan_failure_discards_partial_queue();
    cancellation_discards_partial_queue();
    limits_discard_partial_queue();
    invalid_names_and_roots();
    mount_root_and_deep_tree();
    std::cout << "All package scan tests passed.\n";
}
