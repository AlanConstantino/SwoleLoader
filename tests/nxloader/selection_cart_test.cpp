#include "nxloader/selection_cart.hpp"
#include "nxloader/install_review.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace sphaira::nxloader_ui;
using nxloader::EntryKind;
using namespace std::chrono_literals;

struct CartFs final : fs::SizeFs {
    std::map<std::string, std::vector<nxloader::Entry>> dirs;
    std::map<std::string, nxloader::PackageSize> packages;
    std::string failed;
    std::string ReadDirectory(const std::string& path, const Emit& emit) override {
        if (path == failed) return "drive read failed";
        for (const auto& entry : dirs[path]) if (!emit(entry)) break;
        return {};
    }
    nxloader::PackageSize ReadPackage(const std::string& path) override { return packages.at(path); }
    Result FileGetSizeAndTimestamp(const fs::FsPath&, FsTimeStampRaw*, s64*) override { return 1; }
};

void Finish(SelectionCart& cart) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (cart.Pending() && std::chrono::steady_clock::now() < deadline) {
        cart.Poll(); std::this_thread::sleep_for(1ms);
    }
    assert(!cart.Pending());
    for (auto& worker : cart.Pause()) worker->WaitIdle();
    cart.Poll();
}

void selection_rules() {
    nxloader::SelectionRules rules;
    rules.Set({"/Games/A/DLC/one.nsp", EntryKind::File}, true);
    rules.Set({"/Games/A-other/base.nsp", EntryKind::File}, true);
    rules.Set({"/Games/A", EntryKind::Directory}, true);
    assert(rules.Count() == 2 && rules.Selected("/Games/A/DLC/two.nsp"));
    assert(!rules.Selected("/Games/A-other/no.nsp"));
    rules.Set({"/Games/A/DLC", EntryKind::Directory}, false);
    assert(!rules.Selected("/Games/A/DLC/one.nsp"));
    assert(rules.Partial("/Games/A") && rules.Partial("/Games"));
    rules.Set({"/Games/A/DLC/one.nsp", EntryKind::File}, true);
    assert(rules.Selected("/Games/A/DLC/one.nsp") && !rules.Selected("/Games/A/DLC/two.nsp"));
    assert(rules.Count() == 3);
    rules.Set({"/Games/A/DLC", EntryKind::Directory}, true);
    assert(rules.Count() == 2 && !rules.Partial("/Games/A"));
    assert(rules.Selected("/Games/A-other/base.nsp"));
    rules.Set({"/Games/A/../A", EntryKind::Directory}, false);
    assert(!rules.Selected("/Games/A/base.nsp") && rules.Count() == 1);
    rules.Set({"/", EntryKind::Directory}, true);
    assert(rules.Count() == 1 && rules.Selected("/Games/A/base.nsp"));
    rules.Set({"/", EntryKind::Directory}, false);
    assert(!rules.Count());
}

void retained_selection_removal_undo_and_multiple_sources() {
    auto fs = std::make_shared<CartFs>();
    fs->dirs["/A"] = {{"base.nsz", EntryKind::File}, {"DLC", EntryKind::Directory}};
    fs->dirs["/A/DLC"] = {{"one.nsp", EntryKind::File}, {"two.nsp", EntryKind::File}};
    fs->packages["/A/base.nsz"] = {10, 100, 1, true, {}};
    fs->packages["/A/DLC/one.nsp"] = {2, 20, 1, true, {}};
    fs->packages["/A/DLC/two.nsp"] = {3, 30, 1, true, {}};
    fs->packages["/B/patch.nsp"] = {4, 40, 1, true, {}};
    SelectionCart cart;
    cart.Set("usb", fs, {"/A", EntryKind::Directory}, true);
    cart.Set("usb", fs, {"/B/patch.nsp", EntryKind::File}, true);
    Finish(cart);
    assert(cart.Count() == 2 && cart.Totals().installed_bytes == 190);
    assert(cart.Selected("usb", "/A/base.nsz") && cart.Selected("usb", "/B/patch.nsp"));
    assert(cart.Size("usb", "/A/DLC/one.nsp")->installed_bytes == 20);
    cart.Set("usb", fs, {"/A/base.nsz", EntryKind::File}, true); // overlap cannot double count
    cart.Resume(); Finish(cart);
    assert(cart.Count() == 2 && cart.Totals().installed_bytes == 190);
    cart.Set("usb", fs, {"/A/DLC", EntryKind::Directory}, false);
    cart.Set("usb", fs, {"/A/DLC/one.nsp", EntryKind::File}, true);
    cart.Resume(); Finish(cart);
    assert(cart.Count() == 3 && cart.Totals().installed_bytes == 160);
    assert(cart.Partial("usb", "/A") && !cart.Selected("usb", "/A/DLC/two.nsp"));
    const auto before = cart.Snapshot();
    cart.Set("usb", fs, {"/B/patch.nsp", EntryKind::File}, false);
    assert(!cart.Selected("usb", "/B/patch.nsp"));
    cart.Set("usb", fs, {"/B/patch.nsp", EntryKind::File}, true);
    cart.Set("sd", fs, {"/B/patch.nsp", EntryKind::File}, true); // same path on another device
    cart.Resume(); Finish(cart);
    assert(cart.Totals().installed_bytes == 200);
    assert(cart.Snapshot().size() == before.size() + 1);
    cart.Clear();
    assert(cart.Count() == 0 && cart.Totals().installed_bytes == 0 && cart.Totals().unknown_count == 0);
}

void unknown_is_retryable_and_other_packages_survive_a_read_error() {
    auto fs = std::make_shared<CartFs>();
    fs->dirs["/Game"] = {{"base.nsz", EntryKind::File}, {"bad", EntryKind::Directory}};
    fs->packages["/Game/base.nsz"] = {7, 0, 0, false, "temporary read error"};
    fs->failed = "/Game/bad";
    SelectionCart cart;
    cart.Set("usb", fs, {"/Game", EntryKind::Directory}, true);
    Finish(cart);
    assert(cart.Totals().source_bytes == 7 && cart.Totals().unknown_count == 2);
    fs->packages["/Game/base.nsz"] = {7, 80, 1, true, {}};
    cart.Refresh("usb", "/Game/base.nsz");
    cart.Resume(); Finish(cart);
    assert(cart.Totals().installed_bytes == 80 && cart.Totals().unknown_count == 1);
    cart.Set("usb", fs, {"/Game/bad", EntryKind::Directory}, false);
    cart.Resume(); Finish(cart);
    assert(cart.Totals().installed_bytes == 80 && !cart.Totals().unknown_count);
}

void bulk_selection_survives_navigation_and_is_bounded() {
    auto fs = std::make_shared<CartFs>();
    std::vector<nxloader::Root> roots;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto path = "/library/" + std::to_string(i) + ".nsp";
        roots.push_back({path, EntryKind::File});
        fs->packages[path] = {1, 2, 1, true, {}};
    }
    SelectionCart cart;
    cart.Queue("usb", fs, std::move(roots));
    assert(cart.Count() == 10000 && cart.Queued() == 10000);
    cart.Poll();
    assert(cart.Queued() == 10000 - 32);
    // A browser changing directory has no ownership of this queued selection.
    assert(!cart.Selected("usb", "/different-folder/no.nsp"));
    Finish(cart);
    assert(cart.Count() == 10000 && cart.Totals().installed_bytes == 20000);
    assert(cart.Selected("usb", "/library/9999.nsp"));
    cart.Clear();
    assert(!cart.Queued() && !cart.Count());
}

void review_edits_and_completed_packages() {
    auto fs = std::make_shared<CartFs>();
    fs->packages["/Game/base.nsz"] = {10, 100, 1, true, {}};
    fs->packages["/Game/problem.nsz"] = {20, 0, 0, false, "size not recognized"};
    auto cart = std::make_shared<SelectionCart>();
    cart->Set("usb", fs, {"/Game/base.nsz", EntryKind::File}, true);
    cart->Set("usb", fs, {"/Game/problem.nsz", EntryKind::File}, true);
    Finish(*cart);
    InstallReviewData review;
    review.cart = cart;
    review.packages.push_back({"usb", fs, {"/Game/base.nsz", EntryKind::File}, fs->packages["/Game/base.nsz"], true, false, {}});
    review.packages.push_back({"usb", fs, {"/Game/problem.nsz", EntryKind::File}, fs->packages["/Game/problem.nsz"], true, false, {}});
    assert(review.Totals().unknown_count == 1);
    assert(review.SetIncluded(1, false));
    assert(!cart->Selected("usb", "/Game/problem.nsz") && !review.Totals().unknown_count);
    assert(review.SetIncluded(1, true));
    assert(cart->Selected("usb", "/Game/problem.nsz") && review.Totals().unknown_count == 1);
    review.MarkCompleted(0);
    assert(!review.SetIncluded(0, true)); // undo cannot reinstall a completed package
    assert(!cart->Selected("usb", "/Game/base.nsz") && review.Totals().source_bytes == 20);
    assert(review.packages[1].included); // failed or unfinished stays editable
}

int main() {
    selection_rules();
    review_edits_and_completed_packages();
    bulk_selection_survives_navigation_and_is_bounded();
    retained_selection_removal_undo_and_multiple_sources();
    unknown_is_retryable_and_other_packages_survive_a_read_error();
    SelectionSizer::WaitRetired();
    SelectionSizer::PollRetired();
    std::cout << "Selection cart tests passed: retained paths, overlap, exclusions, undo, multiple sources, retry and partial reads.\n";
}
