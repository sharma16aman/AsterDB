#include "asterdb/storage/page.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace asterdb {

namespace {

void encode_uint64_le(
    std::array<std::byte, PAGE_SIZE>& bytes,
    std::size_t offset,
    std::uint64_t value
) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
    }
}

std::uint64_t decode_uint64_le(
    const std::array<std::byte, PAGE_SIZE>& bytes,
    std::size_t offset
) {
    std::uint64_t value = 0;

    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |=
            static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    }

    return value;
}

}  // namespace

Page::Page() : Page(0, PageType::Invalid) {}

Page::Page(PageId id, PageType type) : id_(id), type_(type), data_{} {
    std::fill(data_.begin(), data_.end(), std::byte{0});
}

PageId Page::id() const {
    return id_;
}

PageType Page::type() const {
    return type_;
}

void Page::set_id(PageId id) {
    id_ = id;
}

void Page::set_type(PageType type) {
    type_ = type;
}

std::byte* Page::data() {
    return data_.data();
}

const std::byte* Page::data() const {
    return data_.data();
}

std::size_t Page::size() const {
    return data_.size();
}

std::array<std::byte, PAGE_SIZE> Page::serialize() const {
    std::array<std::byte, PAGE_SIZE> bytes{};

    encode_uint64_le(bytes, 0, id_);

    bytes[sizeof(id_)] =
        static_cast<std::byte>(static_cast<std::uint8_t>(type_));

    std::copy(
        data_.begin(),
        data_.end(),
        bytes.begin() + PAGE_HEADER_SIZE
    );

    return bytes;
}

Page Page::deserialize(const std::array<std::byte, PAGE_SIZE>& bytes) {
    const PageId id = decode_uint64_le(bytes, 0);

    const auto type_value =
        static_cast<std::uint8_t>(bytes[sizeof(id)]);

    if (type_value > static_cast<std::uint8_t>(PageType::Data)) {
        throw std::runtime_error("Invalid page type");
    }

    Page page(id, static_cast<PageType>(type_value));

    std::copy(
        bytes.begin() + PAGE_HEADER_SIZE,
        bytes.end(),
        page.data_.begin()
    );

    return page;
}

}  // namespace asterdb
