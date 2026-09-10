#pragma once

#include "asterdb/storage/page.h"

#include <cstddef>
#include <filesystem>
#include <fstream>

namespace asterdb {

class DiskManager {
public:
    explicit DiskManager(const std::filesystem::path& path);
    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    PageId allocate_page();
    void write_page(const Page& page);
    Page read_page(PageId page_id) const;

    std::size_t page_count() const;
    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
    mutable std::fstream file_;
    std::size_t page_count_;
};

}  // namespace asterdb
