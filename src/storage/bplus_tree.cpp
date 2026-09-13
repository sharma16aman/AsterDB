#include "asterdb/storage/bplus_tree.h"

#include <algorithm>
#include <stdexcept>

namespace asterdb {

BPlusTree::BPlusTree(Database& database)
    : database_(database),
      root_page_id_(database.metadata().root_page_id) {}

BPlusTreeNode BPlusTree::load_node(PageId page_id) const {
    return BPlusTreeNode::from_page(
        database_.read_page(page_id)
    );
}

void BPlusTree::save_node(const BPlusTreeNode& node) {
    database_.write_page(node.to_page());
}

PageId BPlusTree::find_leaf_page(Key key) const {
    PageId current_page_id = root_page_id_;

    while (true) {
        const BPlusTreeNode node =
            load_node(current_page_id);

        if (node.is_leaf()) {
            return current_page_id;
        }

        const auto& keys = node.keys();
        const auto& children = node.children();

        if (children.size() != keys.size() + 1) {
            throw std::logic_error(
                "Internal node invariant violated"
            );
        }

        const auto it =
            std::upper_bound(
                keys.begin(),
                keys.end(),
                key
            );

        const std::size_t child_index =
            static_cast<std::size_t>(
                it - keys.begin()
            );

        current_page_id = children[child_index];
    }
}

std::optional<Value> BPlusTree::get(Key key) const {
    const PageId leaf_page_id =
        find_leaf_page(key);

    const BPlusTreeNode leaf =
        load_node(leaf_page_id);

    const auto& entries = leaf.leaf_entries();

    const auto it = std::lower_bound(
        entries.begin(),
        entries.end(),
        key,
        [](const LeafEntry& entry, Key search_key) {
            return entry.key < search_key;
        }
    );

    if (it == entries.end() || it->key != key) {
        return std::nullopt;
    }

    return it->value;
}

std::vector<std::pair<Key, Value>> BPlusTree::scan(
    Key start_key,
    Key end_key
) const {
    std::vector<std::pair<Key, Value>> results;

    if (start_key > end_key) {
        return results;
    }

    PageId leaf_page_id =
        find_leaf_page(start_key);

    while (leaf_page_id != INVALID_PAGE_ID) {
        const BPlusTreeNode leaf =
            load_node(leaf_page_id);

        if (!leaf.is_leaf()) {
            throw std::logic_error(
                "Expected leaf node during range scan"
            );
        }

        for (const LeafEntry& entry :
             leaf.leaf_entries()) {
            if (entry.key < start_key) {
                continue;
            }

            if (entry.key > end_key) {
                return results;
            }

            results.emplace_back(
                entry.key,
                entry.value
            );
        }

        leaf_page_id =
            leaf.next_leaf_page_id();
    }

    return results;
}

void BPlusTree::split_leaf_and_insert(
    BPlusTreeNode& leaf,
    Key key,
    Value value
) {
    auto& entries = leaf.leaf_entries();

    const auto it = std::lower_bound(
        entries.begin(),
        entries.end(),
        key,
        [](const LeafEntry& entry, Key search_key) {
            return entry.key < search_key;
        }
    );

    if (it != entries.end() && it->key == key) {
        it->value = value;
        save_node(leaf);
        return;
    }

    entries.insert(
        it,
        LeafEntry{key, value}
    );

    const PageId right_page_id =
        database_.allocate_page();

    BPlusTreeNode right(
        right_page_id,
        BPlusTreeNodeType::Leaf
    );

    right.set_parent_page_id(
        leaf.parent_page_id()
    );

    right.set_next_leaf_page_id(
        leaf.next_leaf_page_id()
    );

    const std::size_t split_index =
        entries.size() / 2;

    right.leaf_entries().assign(
        entries.begin() + split_index,
        entries.end()
    );

    entries.erase(
        entries.begin() + split_index,
        entries.end()
    );

    leaf.set_next_leaf_page_id(
        right_page_id
    );

    const Key separator_key =
        right.leaf_entries().front().key;

    save_node(leaf);
    save_node(right);

    insert_into_parent(
        leaf,
        right,
        separator_key
    );
}

void BPlusTree::insert_into_parent(
    BPlusTreeNode& left,
    BPlusTreeNode& right,
    Key separator_key
) {
    if (left.parent_page_id() == INVALID_PAGE_ID) {
        create_new_root(
            left,
            right,
            separator_key
        );
        return;
    }

    BPlusTreeNode parent =
        load_node(left.parent_page_id());

    if (!parent.is_internal()) {
        throw std::logic_error(
            "B+ tree parent must be an internal node"
        );
    }

    auto& children = parent.children();

    if (children.size() != parent.keys().size() + 1) {
        throw std::logic_error(
            "Internal node invariant violated"
        );
    }

    const auto child_it =
        std::find(
            children.begin(),
            children.end(),
            left.page_id()
        );

    if (child_it == children.end()) {
        throw std::logic_error(
            "Left child is not present in parent"
        );
    }

    const std::size_t child_index =
        static_cast<std::size_t>(
            child_it - children.begin()
        );

    if (parent.keys().size() <
        BPlusTreeNode::MAX_INTERNAL_KEYS) {
        parent.insert_internal_entry(
            child_index,
            separator_key,
            right.page_id()
        );

        right.set_parent_page_id(
            parent.page_id()
        );

        save_node(parent);
        save_node(right);

        return;
    }

    split_internal_and_insert(
        parent,
        child_index,
        separator_key,
        right.page_id()
    );

    return;
}

void BPlusTree::split_internal_and_insert(
    BPlusTreeNode& internal,
    std::size_t child_index,
    Key separator_key,
    PageId right_child_page_id
) {
    if (!internal.is_internal()) {
        throw std::logic_error(
            "Cannot split a non-internal node"
        );
    }

    auto& keys = internal.keys();
    auto& children = internal.children();

    if (children.size() != keys.size() + 1) {
        throw std::logic_error(
            "Internal node invariant violated"
        );
    }

    if (keys.size() !=
        BPlusTreeNode::MAX_INTERNAL_KEYS) {
        throw std::logic_error(
            "Internal node must be full before splitting"
        );
    }

    if (child_index >= children.size()) {
        throw std::out_of_range(
            "Internal child index is outside the node"
        );
    }

    internal.insert_internal_entry(
        child_index,
        separator_key,
        right_child_page_id
    );

    const std::size_t middle =
        keys.size() / 2;

    const Key promoted_key =
        keys[middle];

    const PageId right_page_id =
        database_.allocate_page();

    BPlusTreeNode right(
        right_page_id,
        BPlusTreeNodeType::Internal
    );

    right.set_parent_page_id(
        internal.parent_page_id()
    );

    right.keys().assign(
        keys.begin() +
            static_cast<std::ptrdiff_t>(middle + 1),
        keys.end()
    );

    right.children().assign(
        children.begin() +
            static_cast<std::ptrdiff_t>(middle + 1),
        children.end()
    );

    // Remove the promoted key and everything belonging
    // to the new right node from the original node.
    keys.erase(
        keys.begin() +
            static_cast<std::ptrdiff_t>(middle),
        keys.end()
    );

    children.erase(
        children.begin() +
            static_cast<std::ptrdiff_t>(middle + 1),
        children.end()
    );

    // Children moved into the new right internal node
    // must now point to that node as their parent.
    for (const PageId child_page_id : right.children()) {
        BPlusTreeNode child =
            load_node(child_page_id);

        child.set_parent_page_id(
            right.page_id()
        );

        save_node(child);
    }

    save_node(internal);
    save_node(right);

    insert_into_parent(
        internal,
        right,
        promoted_key
    );
}

void BPlusTree::create_new_root(
    BPlusTreeNode& left,
    BPlusTreeNode& right,
    Key separator_key
) {
    const PageId new_root_page_id =
        database_.allocate_page();

    BPlusTreeNode new_root(
        new_root_page_id,
        BPlusTreeNodeType::Internal
    );

    new_root.add_internal_child(
        left.page_id()
    );

    new_root.add_internal_key(
        separator_key
    );

    new_root.add_internal_child(
        right.page_id()
    );

    left.set_parent_page_id(
        new_root_page_id
    );

    right.set_parent_page_id(
        new_root_page_id
    );

    save_node(left);
    save_node(right);
    save_node(new_root);

    root_page_id_ = new_root_page_id;

    database_.set_root_page_id(
        new_root_page_id
    );
}

void BPlusTree::put(Key key, Value value) {
    const PageId leaf_page_id =
        find_leaf_page(key);

    BPlusTreeNode leaf =
        load_node(leaf_page_id);

    auto& entries = leaf.leaf_entries();

    const auto it = std::lower_bound(
        entries.begin(),
        entries.end(),
        key,
        [](const LeafEntry& entry, Key search_key) {
            return entry.key < search_key;
        }
    );

    if (it != entries.end() && it->key == key) {
        it->value = value;
        save_node(leaf);
        return;
    }

    if (entries.size() <
        BPlusTreeNode::MAX_LEAF_ENTRIES) {
        entries.insert(
            it,
            LeafEntry{key, value}
        );

        save_node(leaf);
        return;
    }

    split_leaf_and_insert(
        leaf,
        key,
        value
    );
}

bool BPlusTree::remove(Key key) {
    const PageId leaf_page_id =
        find_leaf_page(key);

    BPlusTreeNode leaf =
        load_node(leaf_page_id);

    auto& entries = leaf.leaf_entries();

    const auto it = std::lower_bound(
        entries.begin(),
        entries.end(),
        key,
        [](const LeafEntry& entry, Key search_key) {
            return entry.key < search_key;
        }
    );

    if (it == entries.end() || it->key != key) {
        return false;
    }

    entries.erase(it);

    if (leaf.page_id() == root_page_id_) {
        save_node(leaf);
        return true;
    }

    if (entries.size() >=
    (BPlusTreeNode::MAX_LEAF_ENTRIES + 1) / 2) {
    save_node(leaf);

    if (!entries.empty()) {
        BPlusTreeNode parent =
            load_node(leaf.parent_page_id());

        const auto child_it =
            std::find(
                parent.children().begin(),
                parent.children().end(),
                leaf.page_id()
            );

        if (child_it == parent.children().end()) {
            throw std::logic_error(
                "Leaf is not present in parent"
            );
        }

        const std::size_t child_index =
            static_cast<std::size_t>(
                child_it - parent.children().begin()
            );

        if (child_index > 0) {
            parent.keys()[child_index - 1] =
                entries.front().key;

            save_node(parent);
        }
    }

    return true;
}

    save_node(leaf);

    rebalance_leaf(leaf);

    return true;
}

void BPlusTree::rebalance_leaf(
    BPlusTreeNode& leaf
) {
    if (leaf.parent_page_id() == INVALID_PAGE_ID) {
        return;
    }

    BPlusTreeNode parent =
        load_node(leaf.parent_page_id());

    if (!parent.is_internal()) {
        throw std::logic_error(
            "Leaf parent must be an internal node"
        );
    }

    auto& children = parent.children();

    const auto child_it =
        std::find(
            children.begin(),
            children.end(),
            leaf.page_id()
        );

    if (child_it == children.end()) {
        throw std::logic_error(
            "Leaf is not present in parent"
        );
    }

    const std::size_t child_index =
        static_cast<std::size_t>(
            child_it - children.begin()
        );

    constexpr std::size_t MIN_LEAF_ENTRIES =
        (BPlusTreeNode::MAX_LEAF_ENTRIES + 1) / 2;

    // Try borrowing from the left sibling.
    if (child_index > 0) {
        BPlusTreeNode left =
            load_node(children[child_index - 1]);

        if (left.leaf_entries().size() >
            MIN_LEAF_ENTRIES) {
            const LeafEntry borrowed =
                left.leaf_entries().back();

            left.leaf_entries().pop_back();

            leaf.leaf_entries().insert(
                leaf.leaf_entries().begin(),
                borrowed
            );

            parent.keys()[child_index - 1] =
                leaf.leaf_entries().front().key;

            save_node(left);
            save_node(leaf);
            save_node(parent);

            return;
        }
    }

    // Try borrowing from the right sibling.
    if (child_index + 1 < children.size()) {
        BPlusTreeNode right =
            load_node(children[child_index + 1]);

        if (right.leaf_entries().size() >
            MIN_LEAF_ENTRIES) {
            const LeafEntry borrowed =
                right.leaf_entries().front();

            right.leaf_entries().erase(
                right.leaf_entries().begin()
            );

            leaf.leaf_entries().push_back(borrowed);

            parent.keys()[child_index] =
                right.leaf_entries().front().key;

            save_node(right);
            save_node(leaf);
            save_node(parent);

            return;
        }
    }

    // No sibling can spare an entry.
    // Prefer merging with the right sibling.
    if (child_index + 1 < children.size()) {
        BPlusTreeNode right =
            load_node(children[child_index + 1]);

        leaf.leaf_entries().insert(
            leaf.leaf_entries().end(),
            right.leaf_entries().begin(),
            right.leaf_entries().end()
        );

        leaf.set_next_leaf_page_id(
            right.next_leaf_page_id()
        );

        save_node(leaf);

        remove_child_from_parent(
            parent,
            child_index + 1
        );

        return;
    }

    // Otherwise merge this leaf into the left sibling.
    if (child_index > 0) {
        BPlusTreeNode left =
            load_node(children[child_index - 1]);

        left.leaf_entries().insert(
            left.leaf_entries().end(),
            leaf.leaf_entries().begin(),
            leaf.leaf_entries().end()
        );

        left.set_next_leaf_page_id(
            leaf.next_leaf_page_id()
        );

        save_node(left);

        remove_child_from_parent(
            parent,
            child_index
        );

        return;
    }

    throw std::logic_error(
        "Leaf has no sibling to rebalance with"
    );
}

void BPlusTree::remove_child_from_parent(
    BPlusTreeNode& parent,
    std::size_t child_index
) {
    if (!parent.is_internal()) {
        throw std::logic_error(
            "Parent must be an internal node"
        );
    }

    if (parent.children().size() !=
        parent.keys().size() + 1) {
        throw std::logic_error(
            "Internal node invariant violated"
        );
    }

    if (child_index >= parent.children().size()) {
        throw std::out_of_range(
            "Child index is outside parent"
        );
    }

    parent.children().erase(
        parent.children().begin() +
            static_cast<std::ptrdiff_t>(child_index)
    );

    if (child_index == 0) {
        parent.keys().erase(
            parent.keys().begin()
        );
    } else {
        parent.keys().erase(
            parent.keys().begin() +
                static_cast<std::ptrdiff_t>(child_index - 1)
        );
    }

    save_node(parent);

    if (parent.page_id() == root_page_id_) {
        shrink_root_if_needed();
        return;
    }

    constexpr std::size_t MIN_INTERNAL_KEYS =
        BPlusTreeNode::MAX_INTERNAL_KEYS / 2;

    if (parent.keys().size() < MIN_INTERNAL_KEYS) {
        rebalance_internal(parent);
    }
}

void BPlusTree::rebalance_internal(
    BPlusTreeNode& internal
) {
    if (internal.parent_page_id() == INVALID_PAGE_ID) {
        shrink_root_if_needed();
        return;
    }

    BPlusTreeNode parent =
        load_node(internal.parent_page_id());

    if (!parent.is_internal()) {
        throw std::logic_error(
            "Internal node parent must be internal"
        );
    }

    auto& siblings = parent.children();

    const auto child_it =
        std::find(
            siblings.begin(),
            siblings.end(),
            internal.page_id()
        );

    if (child_it == siblings.end()) {
        throw std::logic_error(
            "Internal node is not present in parent"
        );
    }

    const std::size_t child_index =
        static_cast<std::size_t>(
            child_it - siblings.begin()
        );

    constexpr std::size_t MIN_INTERNAL_KEYS =
        BPlusTreeNode::MAX_INTERNAL_KEYS / 2;

    // Borrow from left sibling.
    if (child_index > 0) {
        BPlusTreeNode left =
            load_node(siblings[child_index - 1]);

        if (left.keys().size() >
            MIN_INTERNAL_KEYS) {
            const Key parent_separator =
                parent.keys()[child_index - 1];

            const PageId borrowed_child =
                left.children().back();

            const Key new_parent_separator =
                left.keys().back();

            left.children().pop_back();
            left.keys().pop_back();

            internal.children().insert(
                internal.children().begin(),
                borrowed_child
            );

            internal.keys().insert(
                internal.keys().begin(),
                parent_separator
            );

            parent.keys()[child_index - 1] =
                new_parent_separator;

            BPlusTreeNode borrowed_node =
                load_node(borrowed_child);

            borrowed_node.set_parent_page_id(
                internal.page_id()
            );

            save_node(left);
            save_node(internal);
            save_node(borrowed_node);
            save_node(parent);

            return;
        }
    }

    // Borrow from right sibling.
    if (child_index + 1 < siblings.size()) {
        BPlusTreeNode right =
            load_node(siblings[child_index + 1]);

        if (right.keys().size() >
            MIN_INTERNAL_KEYS) {
            const Key parent_separator =
                parent.keys()[child_index];

            const PageId borrowed_child =
                right.children().front();

            const Key new_parent_separator =
                right.keys().front();

            right.children().erase(
                right.children().begin()
            );

            right.keys().erase(
                right.keys().begin()
            );

            internal.keys().push_back(
                parent_separator
            );

            internal.children().push_back(
                borrowed_child
            );

            parent.keys()[child_index] =
                new_parent_separator;

            BPlusTreeNode borrowed_node =
                load_node(borrowed_child);

            borrowed_node.set_parent_page_id(
                internal.page_id()
            );

            save_node(right);
            save_node(internal);
            save_node(borrowed_node);
            save_node(parent);

            return;
        }
    }

    // Merge with right sibling when possible.
    if (child_index + 1 < siblings.size()) {
        BPlusTreeNode right =
            load_node(siblings[child_index + 1]);

        const Key parent_separator =
            parent.keys()[child_index];

        internal.keys().push_back(
            parent_separator
        );

        internal.keys().insert(
            internal.keys().end(),
            right.keys().begin(),
            right.keys().end()
        );

        internal.children().insert(
            internal.children().end(),
            right.children().begin(),
            right.children().end()
        );

        for (const PageId child_page_id :
             right.children()) {
            BPlusTreeNode child =
                load_node(child_page_id);

            child.set_parent_page_id(
                internal.page_id()
            );

            save_node(child);
        }

        save_node(internal);

        remove_child_from_parent(
            parent,
            child_index + 1
        );

        return;
    }

    // Otherwise merge this node into its left sibling.
    if (child_index > 0) {
        BPlusTreeNode left =
            load_node(siblings[child_index - 1]);

        const Key parent_separator =
            parent.keys()[child_index - 1];

        left.keys().push_back(
            parent_separator
        );

        left.keys().insert(
            left.keys().end(),
            internal.keys().begin(),
            internal.keys().end()
        );

        left.children().insert(
            left.children().end(),
            internal.children().begin(),
            internal.children().end()
        );

        for (const PageId child_page_id :
             internal.children()) {
            BPlusTreeNode child =
                load_node(child_page_id);

            child.set_parent_page_id(
                left.page_id()
            );

            save_node(child);
        }

        save_node(left);

        remove_child_from_parent(
            parent,
            child_index
        );

        return;
    }

    throw std::logic_error(
        "Internal node has no sibling to rebalance with"
    );
}

void BPlusTree::shrink_root_if_needed() {
    BPlusTreeNode root =
        load_node(root_page_id_);

    if (!root.is_internal()) {
        return;
    }

    if (!root.keys().empty()) {
        return;
    }

    if (root.children().size() != 1) {
        throw std::logic_error(
            "Empty internal root must have exactly one child"
        );
    }

    const PageId new_root_page_id =
        root.children().front();

    BPlusTreeNode new_root =
        load_node(new_root_page_id);

    new_root.set_parent_page_id(
        INVALID_PAGE_ID
    );

    save_node(new_root);

    root_page_id_ = new_root_page_id;

    database_.set_root_page_id(
        new_root_page_id
    );
}

PageId BPlusTree::root_page_id() const {
    return root_page_id_;
}

}  // namespace asterdb
