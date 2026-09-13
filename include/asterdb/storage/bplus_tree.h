#pragma once

#include "asterdb/storage/database.h"
#include "asterdb/storage/bplus_tree_node.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include <cstddef>

namespace asterdb {

class BPlusTree {
public:
    explicit BPlusTree(Database& database);

    std::optional<Value> get(Key key) const;
    std::vector<std::pair<Key, Value>> scan(
        Key start_key,
        Key end_key
    ) const;
    void put(Key key, Value value);
    bool remove(Key key);

    PageId root_page_id() const;

private:
    BPlusTreeNode load_node(PageId page_id) const;
    void save_node(const BPlusTreeNode& node);

    PageId find_leaf_page(Key key) const;

    void split_leaf_and_insert(
        BPlusTreeNode& leaf,
        Key key,
        Value value
    );

    void insert_into_parent(
        BPlusTreeNode& left,
        BPlusTreeNode& right,
        Key separator_key
    );

    void split_internal_and_insert(
        BPlusTreeNode& internal,
        std::size_t child_index,
        Key separator_key,
        PageId right_child_page_id
    );

    void create_new_root(
        BPlusTreeNode& left,
        BPlusTreeNode& right,
        Key separator_key
    );

    void rebalance_leaf(
        BPlusTreeNode& leaf
    );

    void remove_child_from_parent(
        BPlusTreeNode& parent,
        std::size_t child_index
    );

    void rebalance_internal(
        BPlusTreeNode& internal
    );

    void shrink_root_if_needed();

    Database& database_;
    PageId root_page_id_;
};

}  // namespace asterdb