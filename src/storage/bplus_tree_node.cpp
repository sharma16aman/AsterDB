#include "asterdb/storage/bplus_tree_node.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace asterdb {

namespace {

constexpr std::size_t TYPE_OFFSET = 0;
constexpr std::size_t KEY_COUNT_OFFSET = 1;
constexpr std::size_t PARENT_PAGE_ID_OFFSET = 8;
constexpr std::size_t NEXT_LEAF_PAGE_ID_OFFSET = 16;
constexpr std::size_t ENTRIES_OFFSET =
    BPlusTreeNode::NODE_HEADER_SIZE;

void encode_uint16_le(
    std::byte* destination,
    std::uint16_t value
) {
    destination[0] =
        static_cast<std::byte>(value & 0xFFU);

    destination[1] =
        static_cast<std::byte>((value >> 8) & 0xFFU);
}

std::uint16_t decode_uint16_le(
    const std::byte* source
) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(source[0]) |
        (static_cast<std::uint16_t>(source[1]) << 8)
    );
}

void encode_uint64_le(
    std::byte* destination,
    std::uint64_t value
) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        destination[i] =
            static_cast<std::byte>(
                (value >> (i * 8)) & 0xFFU
            );
    }
}

std::uint64_t decode_uint64_le(
    const std::byte* source
) {
    std::uint64_t value = 0;

    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |=
            static_cast<std::uint64_t>(source[i]) << (i * 8);
    }

    return value;
}

}  // namespace

BPlusTreeNode::BPlusTreeNode(
    PageId page_id,
    BPlusTreeNodeType type
)
    : page_id_(page_id),
      type_(type),
      parent_page_id_(INVALID_PAGE_ID),
      next_leaf_page_id_(INVALID_PAGE_ID),
      leaf_entries_(),
      keys_(),
      children_() {
    if (type_ == BPlusTreeNodeType::Leaf) {
        leaf_entries_.reserve(MAX_LEAF_ENTRIES);
    } else if (type_ == BPlusTreeNodeType::Internal) {
        keys_.reserve(MAX_INTERNAL_KEYS);
        children_.reserve(MAX_INTERNAL_CHILDREN);
    } else {
        throw std::invalid_argument(
            "Invalid B+ tree node type"
        );
    }
}

PageId BPlusTreeNode::page_id() const {
    return page_id_;
}

BPlusTreeNodeType BPlusTreeNode::type() const {
    return type_;
}

bool BPlusTreeNode::is_leaf() const {
    return type_ == BPlusTreeNodeType::Leaf;
}

bool BPlusTreeNode::is_internal() const {
    return type_ == BPlusTreeNodeType::Internal;
}

PageId BPlusTreeNode::parent_page_id() const {
    return parent_page_id_;
}

void BPlusTreeNode::set_parent_page_id(PageId page_id) {
    parent_page_id_ = page_id;
}

PageId BPlusTreeNode::next_leaf_page_id() const {
    return next_leaf_page_id_;
}

void BPlusTreeNode::set_next_leaf_page_id(PageId page_id) {
    next_leaf_page_id_ = page_id;
}

const std::vector<LeafEntry>&
BPlusTreeNode::leaf_entries() const {
    return leaf_entries_;
}

std::vector<LeafEntry>&
BPlusTreeNode::leaf_entries() {
    return leaf_entries_;
}

const std::vector<Key>&
BPlusTreeNode::keys() const {
    return keys_;
}

std::vector<Key>&
BPlusTreeNode::keys() {
    return keys_;
}

const std::vector<PageId>&
BPlusTreeNode::children() const {
    return children_;
}

std::vector<PageId>&
BPlusTreeNode::children() {
    return children_;
}

void BPlusTreeNode::add_leaf_entry(
    Key key,
    Value value
) {
    if (!is_leaf()) {
        throw std::logic_error(
            "Cannot add leaf entry to internal node"
        );
    }

    if (leaf_entries_.size() >= MAX_LEAF_ENTRIES) {
        throw std::overflow_error(
            "Leaf node is full"
        );
    }

    leaf_entries_.push_back({key, value});
}

void BPlusTreeNode::add_internal_key(Key key) {
    if (!is_internal()) {
        throw std::logic_error(
            "Cannot add internal key to leaf node"
        );
    }

    if (keys_.size() >= MAX_INTERNAL_KEYS) {
        throw std::overflow_error(
            "Internal node is full"
        );
    }

    if (children_.empty()) {
        throw std::logic_error(
            "Internal key requires an existing child"
        );
    }

    if (children_.size() != keys_.size() + 1) {
        throw std::logic_error(
            "Internal node invariant violated"
        );
    }

    keys_.push_back(key);
}

void BPlusTreeNode::add_internal_child(
    PageId child_page_id
) {
    if (!is_internal()) {
        throw std::logic_error(
            "Cannot add child to leaf node"
        );
    }

    if (children_.size() >= MAX_INTERNAL_CHILDREN) {
        throw std::overflow_error(
            "Internal node has maximum children"
        );
    }

    if (children_.size() != keys_.size() &&
        children_.size() != keys_.size() + 1) {
        throw std::logic_error(
            "Internal node invariant violated"
        );
    }

    children_.push_back(child_page_id);
}

void BPlusTreeNode::insert_internal_entry(
    std::size_t child_index,
    Key separator_key,
    PageId right_child_page_id
) {
    if (!is_internal()) {
        throw std::logic_error(
            "Cannot insert an internal entry into a leaf"
        );
    }

    if (children_.size() != keys_.size() + 1) {
        throw std::logic_error(
            "Internal node invariant violated"
        );
    }

    if (child_index >= children_.size()) {
        throw std::out_of_range(
            "Internal child index is outside the node"
        );
    }

    if (keys_.size() >= MAX_INTERNAL_KEYS + 1) {
        throw std::overflow_error(
            "Internal node has too many keys"
        );
    }

    keys_.insert(
        keys_.begin() + static_cast<std::ptrdiff_t>(child_index),
        separator_key
    );

    children_.insert(
        children_.begin() +
            static_cast<std::ptrdiff_t>(child_index + 1),
        right_child_page_id
    );
}

Page BPlusTreeNode::to_page() const {
    if (is_internal() &&
        children_.size() != keys_.size() + 1) {
        throw std::logic_error(
            "Internal node must have one more child than key"
        );
    }

    Page page(page_id_, PageType::Data);
    auto* data = page.data();

    data[TYPE_OFFSET] =
        static_cast<std::byte>(
            static_cast<std::uint8_t>(type_)
        );

    const std::size_t entry_count =
        is_leaf()
            ? leaf_entries_.size()
            : keys_.size();

    if (entry_count >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max()
        )) {
        throw std::overflow_error(
            "Too many B+ tree node entries"
        );
    }

    if (is_leaf() &&
        entry_count > MAX_LEAF_ENTRIES) {
        throw std::overflow_error(
            "Leaf node exceeds maximum capacity"
        );
    }

if (is_internal() &&
    entry_count > MAX_INTERNAL_KEYS) {
    throw std::overflow_error(
        "Internal node exceeds maximum capacity"
    );
}

    encode_uint16_le(
        data + KEY_COUNT_OFFSET,
        static_cast<std::uint16_t>(entry_count)
    );

    encode_uint64_le(
        data + PARENT_PAGE_ID_OFFSET,
        parent_page_id_
    );

    encode_uint64_le(
        data + NEXT_LEAF_PAGE_ID_OFFSET,
        next_leaf_page_id_
    );

    std::size_t offset = ENTRIES_OFFSET;

    if (is_leaf()) {
        for (const auto& entry : leaf_entries_) {
            encode_uint64_le(
                data + offset,
                entry.key
            );
            offset += sizeof(Key);

            encode_uint64_le(
                data + offset,
                entry.value
            );
            offset += sizeof(Value);
        }
    } else {
        // Serialize:
        //
        // child0, key0, child1, key1, ..., keyN-1, childN
        //
        // This is the on-page representation of an
        // internal B+ tree node.

        encode_uint64_le(
            data + offset,
            children_[0]
        );
        offset += sizeof(PageId);

        for (std::size_t i = 0; i < keys_.size(); ++i) {
            encode_uint64_le(
                data + offset,
                keys_[i]
            );
            offset += sizeof(Key);

            encode_uint64_le(
                data + offset,
                children_[i + 1]
            );
            offset += sizeof(PageId);
        }
    }

    return page;
}

BPlusTreeNode BPlusTreeNode::from_page(
    const Page& page
) {
    if (page.type() != PageType::Data) {
        throw std::invalid_argument(
            "B+ tree node must be stored in a data page"
        );
    }

    const auto* data = page.data();

    const auto type =
        static_cast<BPlusTreeNodeType>(
            static_cast<std::uint8_t>(
                data[TYPE_OFFSET]
            )
        );

    if (type != BPlusTreeNodeType::Leaf &&
        type != BPlusTreeNodeType::Internal) {
        throw std::runtime_error(
            "Invalid B+ tree node type"
        );
    }

    const std::uint16_t key_count =
        decode_uint16_le(
            data + KEY_COUNT_OFFSET
        );

    BPlusTreeNode node(page.id(), type);

    node.parent_page_id_ =
        decode_uint64_le(
            data + PARENT_PAGE_ID_OFFSET
        );

    node.next_leaf_page_id_ =
        decode_uint64_le(
            data + NEXT_LEAF_PAGE_ID_OFFSET
        );

    if (type == BPlusTreeNodeType::Leaf) {
        if (key_count >
            BPlusTreeNode::MAX_LEAF_ENTRIES) {
            throw std::runtime_error(
                "Leaf node contains too many entries"
            );
        }

        for (std::size_t i = 0; i < key_count; ++i) {
            const std::size_t offset =
                ENTRIES_OFFSET +
                i * BPlusTreeNode::LEAF_ENTRY_SIZE;

            const Key key =
                decode_uint64_le(
                    data + offset
                );

            const Value value =
                decode_uint64_le(
                    data + offset + sizeof(Key)
                );

            node.leaf_entries_.push_back({
                key,
                value
            });
        }
    } else {
        if (key_count >
            BPlusTreeNode::MAX_INTERNAL_KEYS) {
            throw std::runtime_error(
                "Internal node contains too many keys"
            );
        }

        const std::size_t child_count =
            static_cast<std::size_t>(key_count) + 1;

        if (child_count >
            BPlusTreeNode::MAX_INTERNAL_CHILDREN) {
            throw std::runtime_error(
                "Internal node contains too many children"
            );
        }

        std::size_t offset = ENTRIES_OFFSET;

        node.children_.push_back(
            decode_uint64_le(data + offset)
        );
        offset += sizeof(PageId);

        for (std::size_t i = 0; i < key_count; ++i) {
            node.keys_.push_back(
                decode_uint64_le(data + offset)
            );
            offset += sizeof(Key);

            node.children_.push_back(
                decode_uint64_le(data + offset)
            );
            offset += sizeof(PageId);
        }
    }

    return node;
}

}  // namespace asterdb