#pragma once

#include "asterdb/storage/disk_manager.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace asterdb {

inline constexpr std::uint64_t ASTERDB_MAGIC = 0x4153544552444231ULL;
inline constexpr std::uint32_t ASTERDB_FORMAT_VERSION = 1;

using RootPageId = PageId;

struct DatabaseMetadata {
    std::uint64_t magic;
    std::uint32_t format_version;
    std::uint32_t page_size;
    RootPageId root_page_id;
};

class Database {
public:
    explicit Database(const std::filesystem::path& path);

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    const DatabaseMetadata& metadata() const;
    void set_root_page_id(PageId page_id);

    PageId allocate_page();
    Page read_page(PageId page_id) const;
    void write_page(const Page& page);

    const std::filesystem::path& path() const;

private:
    void initialize_new_database();
    void load_existing_database();
    void write_metadata();
    DatabaseMetadata read_metadata() const;

    std::unique_ptr<DiskManager> disk_;
    DatabaseMetadata metadata_;
};

}  // namespace asterdb
