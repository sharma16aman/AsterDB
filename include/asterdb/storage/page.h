#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace asterdb {

using PageId = std::uint64_t;

inline constexpr PageId INVALID_PAGE_ID =
    std::numeric_limits<PageId>::max();

enum class PageType : std::uint8_t {
    Invalid = 0,
    Metadata = 1,
    Data = 2,
};

inline constexpr std::size_t PAGE_SIZE = 4096;
inline constexpr std::size_t PAGE_HEADER_SIZE = 16;
inline constexpr std::size_t PAGE_DATA_SIZE = PAGE_SIZE - PAGE_HEADER_SIZE;

class Page {
public:
    Page();
    explicit Page(PageId id, PageType type = PageType::Data);

    PageId id() const;
    PageType type() const;

    void set_id(PageId id);
    void set_type(PageType type);

    std::byte* data();
    const std::byte* data() const;

    std::size_t size() const;

    std::array<std::byte, PAGE_SIZE> serialize() const;
    static Page deserialize(const std::array<std::byte, PAGE_SIZE>& bytes);

private:
    PageId id_;
    PageType type_;
    std::array<std::byte, PAGE_DATA_SIZE> data_;
};

}  // namespace asterdb
