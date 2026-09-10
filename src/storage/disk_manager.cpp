#include "asterdb/storage/disk_manager.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace asterdb {

namespace {

std::streamoff page_offset(PageId page_id) {
    return static_cast<std::streamoff>(page_id * PAGE_SIZE);
}

}  // namespace

DiskManager::DiskManager(const std::filesystem::path& path)
    : path_(path), file_(), page_count_(0) {
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);

    if (!file_.is_open()) {
        std::ofstream create_file(path_, std::ios::binary);
        if (!create_file) {
            throw std::runtime_error(
                "Failed to create database file: " + path_.string()
            );
        }
        create_file.close();

        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!file_.is_open()) {
        throw std::runtime_error(
            "Failed to open database file: " + path_.string()
        );
    }

    file_.seekg(0, std::ios::end);
    const auto file_size = file_.tellg();

    if (file_size < 0) {
        throw std::runtime_error("Failed to determine database file size");
    }

    if (file_size % static_cast<std::streamoff>(PAGE_SIZE) != 0) {
        throw std::runtime_error(
            "Database file size is not aligned to page size"
        );
    }

    page_count_ = static_cast<std::size_t>(
        file_size / static_cast<std::streamoff>(PAGE_SIZE)
    );
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

PageId DiskManager::allocate_page() {
    const PageId id = static_cast<PageId>(page_count_);

    Page page(id, PageType::Data);
    write_page(page);

    return id;
}

void DiskManager::write_page(const Page& page) {
    const bool extending_file = page.id() == page_count_;

    if (page.id() > page_count_) {
        throw std::out_of_range("Cannot write page beyond next allocated page");
    }

    const auto bytes = page.serialize();

    file_.clear();
    file_.seekp(page_offset(page.id()), std::ios::beg);

    if (!file_) {
        throw std::runtime_error("Failed to seek to page position");
    }

    file_.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!file_) {
        throw std::runtime_error("Failed to write page");
    }

    file_.flush();

    if (!file_) {
        throw std::runtime_error("Failed to flush page");
    }

    if (extending_file) {
        ++page_count_;
    }
}

Page DiskManager::read_page(PageId page_id) const {
    if (page_id >= page_count_) {
        throw std::out_of_range("Page ID is outside the database");
    }

    std::array<std::byte, PAGE_SIZE> bytes{};

    file_.clear();
    file_.seekg(page_offset(page_id), std::ios::beg);

    if (!file_) {
        throw std::runtime_error("Failed to seek to page position");
    }

    file_.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (file_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        throw std::runtime_error("Failed to read complete page");
    }

    return Page::deserialize(bytes);
}

std::size_t DiskManager::page_count() const {
    return page_count_;
}

const std::filesystem::path& DiskManager::path() const {
    return path_;
}

}  // namespace asterdb
