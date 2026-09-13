#pragma once

#include "asterdb/storage/page.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace asterdb {

using Key = std::uint64_t;
using Value = std::uint64_t;

enum class BPlusTreeNodeType : std::uint8_t {
    Leaf = 1,
    Internal = 2,
};

struct LeafEntry {
    Key key;
    Value value;
};

class BPlusTreeNode {
public:
    static constexpr std::size_t NODE_HEADER_SIZE = 32;

    // 4080-byte page payload - 32-byte node header.
    static constexpr std::size_t NODE_DATA_SIZE =
        PAGE_DATA_SIZE - NODE_HEADER_SIZE;

    // Leaf entry: key (8 bytes) + value (8 bytes).
    static constexpr std::size_t LEAF_ENTRY_SIZE =
        sizeof(Key) + sizeof(Value);

    // Internal child pointer: 8 bytes.
    static constexpr std::size_t CHILD_POINTER_SIZE =
        sizeof(PageId);

    // Internal separator key: 8 bytes.
    static constexpr std::size_t SEPARATOR_KEY_SIZE =
        sizeof(Key);

    // An internal node contains:
    //
    //   child0, key0, child1, key1, ..., keyN-1, childN
    //
    // Therefore:
    //
    //   children = keys + 1
    //
    // Each additional key requires one key + one child pointer.
    static constexpr std::size_t INTERNAL_KEY_CHILD_SIZE =
        SEPARATOR_KEY_SIZE + CHILD_POINTER_SIZE;

    static constexpr std::size_t MAX_LEAF_ENTRIES =
        NODE_DATA_SIZE / LEAF_ENTRY_SIZE;

    static constexpr std::size_t MAX_INTERNAL_KEYS =
        (NODE_DATA_SIZE - CHILD_POINTER_SIZE) /
        INTERNAL_KEY_CHILD_SIZE;

    static constexpr std::size_t MAX_INTERNAL_CHILDREN =
        MAX_INTERNAL_KEYS + 1;

    BPlusTreeNode(PageId page_id, BPlusTreeNodeType type);

    PageId page_id() const;
    BPlusTreeNodeType type() const;

    bool is_leaf() const;
    bool is_internal() const;

    PageId parent_page_id() const;
    void set_parent_page_id(PageId page_id);

    PageId next_leaf_page_id() const;
    void set_next_leaf_page_id(PageId page_id);

    const std::vector<LeafEntry>& leaf_entries() const;
    std::vector<LeafEntry>& leaf_entries();

    const std::vector<Key>& keys() const;
    std::vector<Key>& keys();

    const std::vector<PageId>& children() const;
    std::vector<PageId>& children();

    void add_leaf_entry(Key key, Value value);

    void add_internal_key(Key key);
    void add_internal_child(PageId child_page_id);

    void insert_internal_entry(
    std::size_t child_index,
    Key separator_key,
    PageId right_child_page_id
);

    Page to_page() const;

    static BPlusTreeNode from_page(const Page& page);

private:
    PageId page_id_;
    BPlusTreeNodeType type_;

    PageId parent_page_id_;
    PageId next_leaf_page_id_;

    std::vector<LeafEntry> leaf_entries_;

    std::vector<Key> keys_;
    std::vector<PageId> children_;
};

}  // namespace asterdb
