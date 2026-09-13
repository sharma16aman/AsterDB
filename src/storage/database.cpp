#include "asterdb/storage/database.h"
#include "asterdb/storage/bplus_tree_node.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace asterdb {

namespace {

constexpr std::size_t MAGIC_OFFSET = 0;
constexpr std::size_t VERSION_OFFSET = 8;
constexpr std::size_t PAGE_SIZE_OFFSET = 12;
constexpr std::size_t ROOT_PAGE_ID_OFFSET = 16;

void encode_uint32_le(
    std::byte* destination,
    std::uint32_t value
) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        destination[i] =
            static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
    }
}

std::uint32_t decode_uint32_le(const std::byte* source) {
    std::uint32_t value = 0;

    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |=
            static_cast<std::uint32_t>(source[i]) << (i * 8);
    }

    return value;
}

void encode_uint64_le(
    std::byte* destination,
    std::uint64_t value
) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        destination[i] =
            static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
    }
}

std::uint64_t decode_uint64_le(const std::byte* source) {
    std::uint64_t value = 0;

    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |=
            static_cast<std::uint64_t>(source[i]) << (i * 8);
    }

    return value;
}

}  // namespace

Database::Database(const std::filesystem::path& path)
    : disk_(std::make_unique<DiskManager>(path)),
      metadata_{} {
    if (disk_->page_count() == 0) {
        initialize_new_database();
    } else {
        load_existing_database();
    }
}

void Database::initialize_new_database() {
    metadata_.magic = ASTERDB_MAGIC;
    metadata_.format_version = ASTERDB_FORMAT_VERSION;
    metadata_.page_size = static_cast<std::uint32_t>(PAGE_SIZE);

    const PageId metadata_page_id = disk_->allocate_page();

    if (metadata_page_id != 0) {
        throw std::runtime_error(
            "Metadata page was not allocated as page 0"
        );
    }

    const PageId root_page_id = disk_->allocate_page();
    metadata_.root_page_id = root_page_id;

    write_metadata();

    BPlusTreeNode root_node(
    root_page_id,
    BPlusTreeNodeType::Leaf
    );

    disk_->write_page(root_node.to_page());

}

void Database::load_existing_database() {
    metadata_ = read_metadata();

    if (metadata_.magic != ASTERDB_MAGIC) {
        throw std::runtime_error("Invalid AsterDB magic number");
    }

    if (metadata_.format_version != ASTERDB_FORMAT_VERSION) {
        throw std::runtime_error("Unsupported AsterDB format version");
    }

    if (metadata_.page_size != PAGE_SIZE) {
        throw std::runtime_error("Database page size does not match AsterDB");
    }

    if (metadata_.root_page_id >= disk_->page_count()) {
        throw std::runtime_error("Metadata contains invalid root page ID");
    }

    const Page root_page = disk_->read_page(metadata_.root_page_id);

    if (root_page.id() != metadata_.root_page_id) {
        throw std::runtime_error("Root page ID does not match metadata");
    }
}

void Database::write_metadata() {
    Page metadata_page(0, PageType::Metadata);

    auto* data = metadata_page.data();

    encode_uint64_le(data + MAGIC_OFFSET, metadata_.magic);
    encode_uint32_le(data + VERSION_OFFSET, metadata_.format_version);
    encode_uint32_le(data + PAGE_SIZE_OFFSET, metadata_.page_size);
    encode_uint64_le(data + ROOT_PAGE_ID_OFFSET, metadata_.root_page_id);

    disk_->write_page(metadata_page);
}

DatabaseMetadata Database::read_metadata() const {
    const Page page = disk_->read_page(0);

    if (page.type() != PageType::Metadata) {
        throw std::runtime_error(
            "Page 0 is not an AsterDB metadata page"
        );
    }

    const auto* data = page.data();

    DatabaseMetadata metadata{};
    metadata.magic = decode_uint64_le(data + MAGIC_OFFSET);
    metadata.format_version = decode_uint32_le(data + VERSION_OFFSET);
    metadata.page_size = decode_uint32_le(data + PAGE_SIZE_OFFSET);
    metadata.root_page_id = decode_uint64_le(data + ROOT_PAGE_ID_OFFSET);

    return metadata;
}

const DatabaseMetadata& Database::metadata() const {
    return metadata_;
}

void Database::set_root_page_id(PageId page_id) {
    if (page_id >= disk_->page_count()) {
        throw std::out_of_range(
            "Root page ID is outside the database"
        );
    }

    metadata_.root_page_id = page_id;
    write_metadata();
}

PageId Database::allocate_page() {
    return disk_->allocate_page();
}

Page Database::read_page(PageId page_id) const {
    return disk_->read_page(page_id);
}

void Database::write_page(const Page& page) {
    disk_->write_page(page);
}

const std::filesystem::path& Database::path() const {
    return disk_->path();
}

}  // namespace asterdb
