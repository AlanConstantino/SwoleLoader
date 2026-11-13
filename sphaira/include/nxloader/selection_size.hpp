#pragma once

#include "fs.hpp"
#include "nxloader/package_scan.hpp"
#include "nxloader/package_size.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::nxloader_ui {

class SelectionSizer {
public:
    struct Package { std::string path; nxloader::PackageSize size; };
    struct Summary {
        std::uint64_t source_bytes{};
        std::uint64_t installed_bytes{};
        std::uint64_t content_count{};
        std::uint64_t package_count{};
        std::uint64_t unknown_count{};
        bool complete{};
        std::string error;
        std::shared_ptr<const std::vector<Package>> packages;
        std::uint64_t source_unknown_count{};
    };

    struct Update {
        std::uint64_t id{};
        Summary summary;
    };

    explicit SelectionSizer(std::shared_ptr<fs::Fs> source);
    ~SelectionSizer();
    SelectionSizer(const SelectionSizer&) = delete;
    SelectionSizer& operator=(const SelectionSizer&) = delete;

    void SetSelected(std::uint64_t id, const nxloader::Root& root, bool selected,
        std::vector<std::string> excluded = {}, bool refresh = false);
    void Clear();
    void Cancel();
    void Resume();
    std::vector<Update> Poll();
    bool IsIdle() const;
    // Call from a worker after Cancel(), never from a UI event or drawing path.
    void WaitIdle();
    static void Retire(std::shared_ptr<SelectionSizer> sizer);
    static void PollRetired();
    // Off-UI handoff or application shutdown only.
    static void WaitRetired();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
