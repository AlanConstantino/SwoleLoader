#include "nxloader/install.hpp"
#include "nxloader/directory_reader.hpp"
#include "nxloader/batch_review.hpp"
#include "nxloader/install_review.hpp"
#include "nxloader/package_size_fs.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "yati/yati.hpp"
#include "utils/utils.hpp"

#include <cstdio>
#include <set>

namespace sphaira::nxloader_ui {
namespace {

std::string DescribeError(const std::string& path, Result rc) {
    char code[32];
    std::snprintf(code, sizeof(code), " (0x%08X)", rc);
    return path + code;
}

nxloader::StorageSpace ReadStorage(bool sd, Result* result = nullptr) {
    s64 free{}, total{};
    const auto rc = sd ? App::GetSdSize(&free, &total) : App::GetEmmcSize(&free, &total);
    if (result) *result = rc;
    if (R_FAILED(rc) || free < 0 || total <= 0 || free > total) return {};
    return {static_cast<u64>(free), static_cast<u64>(total), true};
}

struct Outcome { std::size_t index; Result result; };
// what the list view counts for each package: the bytes its install writes,
// or its file size when that isn't known.
s64 ProgressSize(const nxloader::PackageSize& size) {
    return static_cast<s64>(size.known ? size.installed_bytes : size.source_bytes);
}

struct Batch {
    std::vector<Outcome> outcomes;
    std::size_t completed{}, failed{};
    bool cancelled{};
    bool battery_stopped{};
    u64 elapsed_ns{};
    std::string stopped_reason;
    std::string report{APP_NAME " batch report\n"};
    nxloader::StorageSpace storage;
};

std::string GameFolder(const nxloader::Root& root) {
    auto folder = root.kind == nxloader::EntryKind::Directory ? root.path : nxloader::ParentPath(root.path);
    while (nxloader::detail::IsContentFolder(std::string_view(folder).substr(folder.find_last_of('/') + 1))) {
        const auto parent = nxloader::ParentPath(folder);
        if (parent == folder || parent.empty()) break;
        folder = parent;
    }
    return folder;
}

// quiet while the batch runs, as it is saved from the install thread.
void SaveBatchReport(const std::string& report, bool quiet = false) {
    fs::FsNativeSd sd;
    auto rc = sd.CreateDirectoryRecursively(APP_CONFIG_DIR);
    if (R_SUCCEEDED(rc)) rc = sd.write_entire_file(APP_CONFIG_DIR "/last-batch.txt",
        std::span<const u8>{reinterpret_cast<const u8*>(report.data()), report.size()});
    if (R_FAILED(rc)) {
        log_write("[batch] could not save the report: 0x%X\n", rc);
        if (!quiet) App::Notify("Could not save the batch report.");
    }
}

std::string DescribeBattery(const nxloader::Battery& battery) {
    if (!battery.known) return "unknown";
    return std::to_string(battery.percent) + "%" + (battery.charging ? " charging" : "");
}

std::string BatteryStopReason(const nxloader::Battery& battery) {
    return "The battery is at " + std::to_string(battery.percent) + "% and the console isn't charging, "
        "so the batch stopped before a game could be cut off. Charge or dock the console, then install the rest from Review.";
}

void StartReviewedBatch(std::shared_ptr<InstallReviewData> review, bool continue_on_error);

}

void RunReviewedBatch(std::shared_ptr<InstallReviewData> review, bool continue_on_error) {
    const auto battery = App::GetApp()->m_battery_guard.Get() ? App::GetBattery() : nxloader::Battery{};
    if (nxloader::CheckBattery(battery) != nxloader::BatteryAction::Continue) {
        App::Push<ui::OptionBox>("The battery is at " + std::to_string(battery.percent)
            + "%. Charge or dock the console to install.", "OK");
        return;
    }
    if (nxloader::BatteryNeedsWarning(battery)) {
        App::Push<ui::OptionBox>("The battery is at " + std::to_string(battery.percent) + "% and the console isn't charging. "
            "The batch will stop at " + std::to_string(nxloader::BatteryStopPercent) + "% and may not finish. Install anyway?",
            "Cancel", "Install", 0, [review, continue_on_error](auto op_index) {
                if (op_index && *op_index) StartReviewedBatch(review, continue_on_error);
            });
        return;
    }
    StartReviewedBatch(review, continue_on_error);
}

namespace {

void StartReviewedBatch(std::shared_ptr<InstallReviewData> review, bool continue_on_error) {
    struct Work { std::size_t index; ReviewPackage package; };
    auto work = std::make_shared<std::vector<Work>>();
    for (std::size_t i = 0; i < review->packages.size(); ++i) {
        const auto& package = review->packages[i];
        if (package.included && !package.completed) work->push_back({i, package});
    }
    if (work->empty()) { App::Notify("Select at least one package."); return; }
    auto sizers = review->cart->Pause();
    const auto batch = std::make_shared<Batch>();
    const bool sd = review->sd;
    batch->report += std::string("Destination: ") + (sd ? "microSD" : "system storage") + "\n";
    batch->report += std::string("emuMMC: ") + (App::IsFileBaseEmummc() ? "file-based" : App::IsEmummc() ? "partition-based" : "off") + "\n";
    batch->report += std::string("NCA hash verification: ") + (App::GetApp()->m_skip_nca_hash_verify.Get() ? "off" : "on") + "\n";
    batch->report += "Stage timings overlap; do not add them together. Input excludes container/ticket metadata.\n"
        "Output counts successful content-write calls, including partial work in failed/cancelled packages.\n"
        "COMPLETED means the installer returned success; reused/skipped content is reported separately.\n\n";
    const bool battery_guard = App::GetApp()->m_battery_guard.Get();
    auto box = std::make_unique<ui::ProgressBox>(0, "Installing", "", [work, batch, sd, continue_on_error, battery_guard, sizers = std::move(sizers)](auto pbox) -> Result {
        for (const auto& sizer : sizers) sizer->WaitIdle();
        SelectionSizer::WaitRetired();
        const auto start = armGetSystemTick();
        ON_SCOPE_EXIT(batch->elapsed_ns = armTicksToNs(armGetSystemTick() - start); batch->storage = ReadStorage(sd));
        const auto check_space = [&](const nxloader::CapacityEstimate& estimate) -> Result {
            Result storage_rc{};
            const auto plan = nxloader::EvaluateCapacity(ReadStorage(sd, &storage_rc), estimate);
            if (!plan.calculated) {
                batch->stopped_reason = "Destination space could not be read, or the selection is too large. Retry or remove items in Review.";
                return R_FAILED(storage_rc) ? storage_rc : Result(FsError_OutOfRange);
            }
            if (!plan.can_attempt) {
                batch->stopped_reason = "Not enough free space for the known content and reserved headroom. Remove items in Review.";
                return sd ? FsError_UsableSpaceNotEnoughSdCard : FsError_UsableSpaceNotEnoughMmcUser;
            }
            R_SUCCEED();
        };
        nxloader::CapacityEstimate all;
        for (const auto& item : *work) all.Add(item.package.size);
        log_write("[batch] start: %zu packages, %llu MiB source, destination %s, %s\n", work->size(),
            static_cast<unsigned long long>(all.source_bytes >> 20), sd ? "microSD" : "system storage",
            continue_on_error ? "continue after errors" : "stop on first error");
        R_TRY(check_space(all));
        s64 list_size{};
        for (const auto& item : *work) list_size += ProgressSize(item.package.size);
        pbox->StartBatch(list_size);
        log_write("[batch] battery %s\n", DescribeBattery(App::GetBattery()).c_str());
        // a cancel can come from the user or from the battery running out.
        const auto stopped = [&]() -> Result {
            if (pbox->StoppedForBattery()) {
                batch->battery_stopped = true;
                batch->stopped_reason = BatteryStopReason(App::GetBattery());
                return Result_BatteryLow;
            }
            batch->cancelled = true;
            return Result_TransferCancelled;
        };
        for (std::size_t i = 0; i < work->size(); ++i) {
            if (pbox->ShouldExit()) return stopped();
            if (battery_guard) {
                const auto battery = App::GetBattery();
                if (nxloader::CheckBattery(battery) != nxloader::BatteryAction::Continue) {
                    log_write("[batch] stopping before %zu/%zu, battery %s\n", i + 1, work->size(), DescribeBattery(battery).c_str());
                    batch->battery_stopped = true;
                    batch->stopped_reason = BatteryStopReason(battery);
                    return Result_BatteryLow;
                }
            }
            const auto& item = (*work)[i];
            const auto& package = item.package;
            nxloader::CapacityEstimate space;
            space.Add(package.size);
            R_TRY(check_space(space));
            pbox->SetActionName("Installing " + std::to_string(i + 1) + " / " + std::to_string(work->size()));
            pbox->NextBatchItem(ProgressSize(package.size));
            pbox->SetTitle(package.root.path).ResetTranfser();
            if (!package.size.known) batch->report += "SIZE WARNING " + package.root.path + ": " + package.size.error + "\n";
            nxloader::InstallMetrics metrics;
            yati::ConfigOverride options;
            options.metrics = &metrics;
            options.sd_card_install = sd;
            log_write("[batch] %zu/%zu installing %s (battery %s)\n", i + 1, work->size(), package.root.path.c_str(),
                DescribeBattery(App::GetBattery()).c_str());
            const auto package_start = armGetSystemTick();
            const auto rc = package.root.kind == nxloader::EntryKind::Directory ? Result(FsError_PathNotFound)
                : yati::InstallFromFile(pbox, package.source.get(), package.root.path, options);
            log_write("[batch] %zu/%zu %s after %.1f s: 0x%X %s\n", i + 1, work->size(),
                R_SUCCEEDED(rc) ? "completed" : rc == Result_TransferCancelled ? "cancelled" : "failed",
                armTicksToNs(armGetSystemTick() - package_start) / 1e9, rc, package.root.path.c_str());
            batch->report += "PACKAGE " + package.root.path + "\n" + nxloader::FormatInstallMetrics(metrics);
            if (rc == Result_TransferCancelled) return stopped();
            batch->outcomes.push_back({item.index, rc});
            if (R_FAILED(rc)) {
                ++batch->failed;
                batch->report += "FAILED " + DescribeError(package.root.path, rc) + "\n";
            } else {
                ++batch->completed;
                batch->report += "COMPLETED " + package.root.path + "\n";
            }
            // saved after every package, so a power cut doesn't lose what finished.
            SaveBatchReport(batch->report + "\nStill running: " + std::to_string(i + 1) + " of "
                + std::to_string(work->size()) + " packages attempted.\n", true);
            if (R_FAILED(rc)) {
                const bool no_space = rc == FsError_UsableSpaceNotEnoughSdCard || rc == FsError_UsableSpaceNotEnoughMmcUser;
                if (!continue_on_error || no_space) return rc;
            }
        }
        R_SUCCEED();
    }, [review, work, batch](Result rc) {
        for (const auto& outcome : batch->outcomes) {
            auto& package = review->packages[outcome.index];
            if (R_SUCCEEDED(outcome.result)) {
                review->MarkCompleted(outcome.index);
            } else {
                package.issue = DescribeError("Installation failed", outcome.result);
            }
        }
        review->storage = batch->storage;
        if (std::ranges::none_of(review->packages, [](const auto& package) { return package.included && !package.completed; }))
            review->cart->Clear();
        ++review->revision;
        const auto pending = work->size() - batch->completed - batch->failed;
        char elapsed[64];
        std::snprintf(elapsed, sizeof(elapsed), " %.1f seconds elapsed.", double(batch->elapsed_ns) / 1e9);
        const auto summary = std::to_string(batch->completed) + " completed, " + std::to_string(batch->failed)
            + " failed, " + std::to_string(pending) + " unfinished." + elapsed;
        batch->report += "\n" + summary + "\n" + batch->stopped_reason + "\n";
        log_write("[batch] done: %s%s%s (battery %s)\n", summary.c_str(), batch->stopped_reason.empty() ? "" : " Stopped: ",
            batch->stopped_reason.c_str(), DescribeBattery(App::GetBattery()).c_str());
        SaveBatchReport(batch->report);
        const std::string heading = batch->battery_stopped ? "Stopped, battery low: " : batch->cancelled ? "Cancelled: " : "Batch finished: ";
        App::Push<ui::OptionBox>(heading + summary
            + " Return to Review to remove or retry remaining items.", "Review");
        if (R_FAILED(rc) && !batch->cancelled) App::PushErrorBox(rc, batch->stopped_reason.empty()
            ? "Installation stopped. Failed and unfinished items remain in Review." : batch->stopped_reason);
    }, ui::ProgressBoxOption::ScreenToggle);
    if (work->size() > 1) box->EnableBatchView();
    if (battery_guard) box->EnableBatteryGuard();
    App::Push(std::move(box));
}

} // namespace

void RetryReviewSizes(std::shared_ptr<InstallReviewData> review, std::vector<std::size_t> indices) {
    struct Updates { std::vector<std::pair<std::size_t, nxloader::PackageSize>> sizes; nxloader::StorageSpace storage; };
    const auto updates = std::make_shared<Updates>();
    auto sizers = review->cart->Pause();
    App::Push<ui::ProgressBox>(0, "Retrying size calculation", "", [review, indices = std::move(indices), updates, sizers = std::move(sizers)](auto pbox) -> Result {
        for (const auto& sizer : sizers) sizer->WaitIdle();
        for (const auto index : indices) {
            R_TRY(pbox->ShouldExitResult());
            const auto& package = review->packages[index];
            if (package.completed || package.root.kind == nxloader::EntryKind::Directory) continue;
            pbox->SetTitle(package.root.path);
            auto size = ReadPackageSize(package.source.get(), package.root.path, [pbox] { return pbox->ShouldExit(); });
            R_TRY(pbox->ShouldExitResult());
            updates->sizes.emplace_back(index, std::move(size));
        }
        updates->storage = ReadStorage(review->sd);
        R_SUCCEED();
    }, [review, updates](Result rc) {
        for (auto& [index, size] : updates->sizes) {
            auto& package = review->packages[index];
            package.size = std::move(size);
            review->cart->Refresh(package.source_key, package.root.path);
        }
        if (updates->storage.known) review->storage = updates->storage;
        ++review->revision;
        if (R_FAILED(rc) && rc != Result_TransferCancelled) App::PushErrorBox(rc, "Could not refresh sizes.");
    });
}

void ScanAndInstall(std::shared_ptr<SelectionCart> cart) {
    if (!App::GetInstallEnable()) { App::ShowEnableInstallPrompt(); return; }
    const auto review = std::make_shared<InstallReviewData>();
    review->sd = App::GetInstallSdEnable();
    review->cart = cart;
    auto sizers = cart->Pause();
    App::PopToMenu();
    App::Push<ui::ProgressBox>(0, "Preparing installation review", "", [review, sizers = std::move(sizers)](auto pbox) -> Result {
        for (const auto& sizer : sizers) sizer->WaitIdle();
        SelectionSizer::WaitRetired();
        // The browser cannot mutate the cart while this modal worker owns focus.
        const auto roots = review->cart->Snapshot();
        std::map<std::pair<std::string, std::string>, std::size_t> groups;
        std::set<std::pair<std::string, std::string>> seen;
        const auto add = [&](const SelectedRoot& selection, nxloader::Root root, nxloader::PackageSize size, std::string issue = {}) {
            if (!seen.emplace(selection.source_key, root.path).second) return;
            const auto folder = GameFolder(selection.root);
            auto [it, inserted] = groups.try_emplace({selection.source_key, folder}, review->groups.size());
            if (inserted) review->groups.push_back({folder, selection.source_key, {}});
            review->groups[it->second].packages.push_back(review->packages.size());
            review->packages.push_back({selection.source_key, selection.source, std::move(root), std::move(size), true, false, std::move(issue)});
        };
        for (const auto& selection : roots) {
            R_TRY(pbox->ShouldExitResult());
            nxloader::ScanOptions options;
            options.max_path_length = sizeof(fs::FsPath) - 1;
            options.continue_on_read_error = true;
            auto scan = nxloader::Scan({selection.root}, [&](const std::string& path, auto emit) {
                pbox->SetTitle(path);
                if (nxloader::ExcludedPath(path, selection.excluded)) return std::string{};
                return ReadDirectory(selection.source.get(), path, emit);
            }, [pbox] { return pbox->ShouldExit(); }, options);
            if (scan.cancelled) return Result_TransferCancelled;
            if (!scan.error.empty()) add(selection, selection.root, {.error = scan.error, .source_known = false}, scan.error);
            for (const auto& issue : scan.issues) add(selection, {issue.path, nxloader::EntryKind::Directory},
                {.error = issue.error, .source_known = false}, "Folder could not be read: " + issue.error);
            for (const auto& path : scan.files) {
                R_TRY(pbox->ShouldExitResult());
                if (nxloader::ExcludedPath(path, selection.excluded)) continue;
                pbox->SetActionName("Reading package sizes");
                pbox->SetTitle(path);
                auto estimate = ReadPackageSize(selection.source.get(), path, [pbox] { return pbox->ShouldExit(); });
                R_TRY(pbox->ShouldExitResult());
                add(selection, {path, nxloader::EntryKind::File}, std::move(estimate));
            }
        }
        review->storage = ReadStorage(review->sd);
        for (std::size_t i = 0; i < review->groups.size(); ++i) {
            review->rows.push_back({true, i});
            for (const auto index : review->groups[i].packages) review->rows.push_back({false, index});
        }
        R_SUCCEED();
    }, [review](Result rc) {
        if (rc == Result_TransferCancelled) App::Notify("Scan cancelled. Your selection is preserved.");
        else if (R_FAILED(rc)) App::PushErrorBox(rc, "Could not prepare the installation review.");
        else if (review->packages.empty()) App::Push<ui::OptionBox>("No supported packages remain in this selection.", "OK");
        else ShowInstallReview(review);
    });
}

}
