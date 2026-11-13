#pragma once

#include "fs.hpp"
#include "nxloader/package_scan.hpp"
#include <memory>

namespace sphaira::nxloader_ui {

class SelectionCart;
struct InstallReviewData;
void ScanAndInstall(std::shared_ptr<SelectionCart> cart);
void RunReviewedBatch(std::shared_ptr<InstallReviewData> review, bool continue_on_error);
void RetryReviewSizes(std::shared_ptr<InstallReviewData> review, std::vector<std::size_t> indices);

}
