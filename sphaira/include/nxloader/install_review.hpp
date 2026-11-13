#pragma once

#include "nxloader/selection_cart.hpp"
#include <functional>

namespace sphaira::nxloader_ui {

struct ReviewPackage {
    std::string source_key;
    std::shared_ptr<fs::Fs> source;
    nxloader::Root root;
    nxloader::PackageSize size;
    bool included{true};
    bool completed{};
    std::string issue;
};

struct ReviewGroup {
    std::string folder;
    std::string source_key;
    std::vector<std::size_t> packages;
};

struct ReviewRow { bool heading; std::size_t index; };

struct InstallReviewData {
    std::vector<ReviewPackage> packages;
    std::vector<ReviewGroup> groups;
    std::vector<ReviewRow> rows;
    nxloader::StorageSpace storage;
    std::shared_ptr<SelectionCart> cart;
    bool sd{};
    std::uint64_t revision{};

    bool SetIncluded(std::size_t index, bool included) {
        auto& package = packages.at(index);
        if (package.completed || package.included == included) return false;
        package.included = included;
        cart->Set(package.source_key, package.source, package.root, included);
        ++revision;
        return true;
    }

    void MarkCompleted(std::size_t index) {
        SetIncluded(index, false);
        packages.at(index).completed = true;
        packages.at(index).issue.clear();
        ++revision;
    }

    nxloader::CapacityEstimate Totals() const {
        nxloader::CapacityEstimate total;
        for (const auto& package : packages) if (package.included && !package.completed) total.Add(package.size);
        return total;
    }
};

void ShowInstallReview(std::shared_ptr<InstallReviewData> data);

}
