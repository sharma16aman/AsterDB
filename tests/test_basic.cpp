#include "asterdb/storage/database.h"
#include "asterdb/storage/disk_manager.h"
#include "asterdb/storage/page.h"
#include "asterdb/storage/bplus_tree_node.h"
#include "asterdb/storage/bplus_tree.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

void validate_bplus_tree_node(
    const asterdb::Database& database,
    asterdb::PageId page_id,
    asterdb::PageId expected_parent,
    std::vector<asterdb::PageId>& leaf_pages
) {
    const auto node =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(page_id)
        );

    ASSERT_EQ(node.parent_page_id(), expected_parent);

    if (node.is_leaf()) {
        const auto& entries = node.leaf_entries();

        for (std::size_t i = 1; i < entries.size(); ++i) {
            ASSERT_LT(
                entries[i - 1].key,
                entries[i].key
            );
        }

        leaf_pages.push_back(page_id);
        return;
    }

    ASSERT_TRUE(node.is_internal());
    ASSERT_EQ(
        node.children().size(),
        node.keys().size() + 1
    );

    for (std::size_t i = 1; i < node.keys().size(); ++i) {
        ASSERT_LT(
            node.keys()[i - 1],
            node.keys()[i]
        );
    }

    for (const auto child_page_id : node.children()) {
        validate_bplus_tree_node(
            database,
            child_page_id,
            node.page_id(),
            leaf_pages
        );
    }
}

namespace asterdb {

TEST(PageTest, DefaultPageHasExpectedProperties) {
    Page page;

    EXPECT_EQ(page.id(), 0);
    EXPECT_EQ(page.type(), PageType::Invalid);
    EXPECT_EQ(page.size(), PAGE_DATA_SIZE);
}

TEST(PageTest, ConstructedPageStoresMetadata) {
    Page page(42, PageType::Data);

    EXPECT_EQ(page.id(), 42);
    EXPECT_EQ(page.type(), PageType::Data);
}

TEST(PageTest, PageDataIsZeroInitialized) {
    Page page(1, PageType::Data);

    for (std::size_t i = 0; i < page.size(); ++i) {
        EXPECT_EQ(page.data()[i], std::byte{0});
    }
}

TEST(PageTest, PageDataCanBeModified) {
    Page page(1, PageType::Data);

    page.data()[0] = std::byte{0xAB};
    page.data()[PAGE_DATA_SIZE - 1] = std::byte{0xCD};

    EXPECT_EQ(page.data()[0], std::byte{0xAB});
    EXPECT_EQ(page.data()[PAGE_DATA_SIZE - 1], std::byte{0xCD});
}

TEST(PageTest, SerializationRoundTripPreservesPage) {
    Page original(42, PageType::Metadata);

    original.data()[0] = std::byte{0x12};
    original.data()[100] = std::byte{0x34};
    original.data()[PAGE_DATA_SIZE - 1] = std::byte{0x56};

    const auto bytes = original.serialize();
    const Page restored = Page::deserialize(bytes);

    EXPECT_EQ(restored.id(), original.id());
    EXPECT_EQ(restored.type(), original.type());
    EXPECT_EQ(restored.size(), original.size());

    for (std::size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ(restored.data()[i], original.data()[i]);
    }
}

TEST(PageTest, SerializationAlwaysProducesFixedSize) {
    Page page(7, PageType::Data);

    const auto bytes = page.serialize();

    EXPECT_EQ(bytes.size(), PAGE_SIZE);
}

TEST(PageTest, SerializationUsesLittleEndianPageId) {
    Page page(0x0102030405060708ULL, PageType::Data);

    const auto bytes = page.serialize();

    EXPECT_EQ(bytes[0], std::byte{0x08});
    EXPECT_EQ(bytes[1], std::byte{0x07});
    EXPECT_EQ(bytes[2], std::byte{0x06});
    EXPECT_EQ(bytes[3], std::byte{0x05});
    EXPECT_EQ(bytes[4], std::byte{0x04});
    EXPECT_EQ(bytes[5], std::byte{0x03});
    EXPECT_EQ(bytes[6], std::byte{0x02});
    EXPECT_EQ(bytes[7], std::byte{0x01});
    EXPECT_EQ(bytes[8], std::byte{0x02});
}

class DiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        database_path_ =
            std::filesystem::temp_directory_path() / "asterdb_test.db";

        std::filesystem::remove(database_path_);
    }

    void TearDown() override {
        std::filesystem::remove(database_path_);
    }

    std::filesystem::path database_path_;
};

TEST_F(DiskManagerTest, CreatesEmptyDatabase) {
    DiskManager disk(database_path_);

    EXPECT_TRUE(std::filesystem::exists(database_path_));
    EXPECT_EQ(disk.page_count(), 0);
}

TEST_F(DiskManagerTest, AllocatesPagesSequentially) {
    DiskManager disk(database_path_);

    const PageId first = disk.allocate_page();
    const PageId second = disk.allocate_page();
    const PageId third = disk.allocate_page();

    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(third, 2);
    EXPECT_EQ(disk.page_count(), 3);
}

TEST_F(DiskManagerTest, WritesAndReadsPage) {
    DiskManager disk(database_path_);

    Page page(0, PageType::Data);
    page.data()[0] = std::byte{0xAA};
    page.data()[123] = std::byte{0xBB};
    page.data()[PAGE_DATA_SIZE - 1] = std::byte{0xCC};

    disk.write_page(page);

    const Page restored = disk.read_page(0);

    EXPECT_EQ(restored.id(), 0);
    EXPECT_EQ(restored.type(), PageType::Data);
    EXPECT_EQ(restored.data()[0], std::byte{0xAA});
    EXPECT_EQ(restored.data()[123], std::byte{0xBB});
    EXPECT_EQ(
        restored.data()[PAGE_DATA_SIZE - 1],
        std::byte{0xCC}
    );
}

TEST_F(DiskManagerTest, WritesAndReadsMultiplePages) {
    DiskManager disk(database_path_);

    for (PageId id = 0; id < 5; ++id) {
        Page page(id, PageType::Data);
        page.data()[0] = static_cast<std::byte>(id);
        disk.write_page(page);
    }

    EXPECT_EQ(disk.page_count(), 5);

    for (PageId id = 0; id < 5; ++id) {
        const Page page = disk.read_page(id);

        EXPECT_EQ(page.id(), id);
        EXPECT_EQ(page.data()[0], static_cast<std::byte>(id));
    }
}

TEST_F(DiskManagerTest, ReopenPreservesPages) {
    {
        DiskManager disk(database_path_);

        Page page(0, PageType::Metadata);
        page.data()[0] = std::byte{0x11};
        page.data()[1] = std::byte{0x22};
        page.data()[2] = std::byte{0x33};

        disk.write_page(page);
        EXPECT_EQ(disk.page_count(), 1);
    }

    {
        DiskManager disk(database_path_);

        EXPECT_EQ(disk.page_count(), 1);

        const Page page = disk.read_page(0);

        EXPECT_EQ(page.id(), 0);
        EXPECT_EQ(page.type(), PageType::Metadata);
        EXPECT_EQ(page.data()[0], std::byte{0x11});
        EXPECT_EQ(page.data()[1], std::byte{0x22});
        EXPECT_EQ(page.data()[2], std::byte{0x33});
    }
}

TEST_F(DiskManagerTest, AllocatePageCreatesPhysicalPage) {
    DiskManager disk(database_path_);

    const PageId id = disk.allocate_page();

    EXPECT_EQ(id, 0);
    EXPECT_EQ(disk.page_count(), 1);
    EXPECT_EQ(
        std::filesystem::file_size(database_path_),
        PAGE_SIZE
    );

    const Page page = disk.read_page(0);

    EXPECT_EQ(page.id(), 0);
    EXPECT_EQ(page.type(), PageType::Data);
}

TEST_F(DiskManagerTest, RejectsInvalidPageReads) {
    DiskManager disk(database_path_);

    EXPECT_THROW(disk.read_page(0), std::out_of_range);
}

TEST_F(DiskManagerTest, RejectsWritingPastNextPage) {
    DiskManager disk(database_path_);

    Page page(5, PageType::Data);

    EXPECT_THROW(disk.write_page(page), std::out_of_range);
}

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        database_path_ =
            std::filesystem::temp_directory_path() / "asterdb_database_test.db";

        std::filesystem::remove(database_path_);
    }

    void TearDown() override {
        std::filesystem::remove(database_path_);
    }

    std::filesystem::path database_path_;
};

TEST_F(DatabaseTest, CreatesInitializedDatabase) {
    Database database(database_path_);

    const auto& metadata = database.metadata();

    EXPECT_EQ(metadata.magic, ASTERDB_MAGIC);
    EXPECT_EQ(metadata.format_version, ASTERDB_FORMAT_VERSION);
    EXPECT_EQ(metadata.page_size, PAGE_SIZE);
    EXPECT_EQ(metadata.root_page_id, 1);

    EXPECT_TRUE(std::filesystem::exists(database_path_));
    EXPECT_EQ(std::filesystem::file_size(database_path_), 2 * PAGE_SIZE);
}

TEST_F(DatabaseTest, MetadataPageHasCorrectType) {
    Database database(database_path_);

    const Page metadata_page = database.read_page(0);

    EXPECT_EQ(metadata_page.id(), 0);
    EXPECT_EQ(metadata_page.type(), PageType::Metadata);
}

TEST_F(DatabaseTest, RootPageExists) {
    Database database(database_path_);

    const Page root_page =
        database.read_page(database.metadata().root_page_id);

    EXPECT_EQ(root_page.id(), database.metadata().root_page_id);
    EXPECT_EQ(root_page.type(), PageType::Data);
}

TEST_F(DatabaseTest, ReopenPreservesMetadata) {
    DatabaseMetadata original_metadata{};

    {
        Database database(database_path_);
        original_metadata = database.metadata();
    }

    {
        Database database(database_path_);

        const auto& metadata = database.metadata();

        EXPECT_EQ(metadata.magic, original_metadata.magic);
        EXPECT_EQ(
            metadata.format_version,
            original_metadata.format_version
        );
        EXPECT_EQ(metadata.page_size, original_metadata.page_size);
        EXPECT_EQ(metadata.root_page_id, original_metadata.root_page_id);
    }
}

TEST_F(DatabaseTest, ReopenPreservesRootPage) {
    {
        Database database(database_path_);

        Page root = database.read_page(database.metadata().root_page_id);
        root.data()[0] = std::byte{0xDE};
        root.data()[1] = std::byte{0xAD};

        database.write_page(root);
    }

    {
        Database database(database_path_);

        const Page root =
            database.read_page(database.metadata().root_page_id);

        EXPECT_EQ(root.data()[0], std::byte{0xDE});
        EXPECT_EQ(root.data()[1], std::byte{0xAD});
    }
}

TEST_F(DatabaseTest, RejectsInvalidDatabaseMagic) {
    {
        Database database(database_path_);

        Page metadata_page = database.read_page(0);
        metadata_page.data()[0] = std::byte{0xFF};

        database.write_page(metadata_page);
    }

    EXPECT_THROW(
        Database database(database_path_),
        std::runtime_error
    );
}

TEST_F(DatabaseTest, RejectsInvalidPageSize) {
    {
        Database database(database_path_);

        Page metadata_page = database.read_page(0);

        metadata_page.data()[12] = std::byte{0x00};
        metadata_page.data()[13] = std::byte{0x20};
        metadata_page.data()[14] = std::byte{0x00};
        metadata_page.data()[15] = std::byte{0x00};

        database.write_page(metadata_page);
    }

    EXPECT_THROW(
        Database database(database_path_),
        std::runtime_error
    );
}

TEST_F(DatabaseTest, RejectsInvalidRootPageId) {
    {
        Database database(database_path_);

        Page metadata_page = database.read_page(0);

        for (std::size_t i = 16; i < 24; ++i) {
            metadata_page.data()[i] = std::byte{0xFF};
        }

        database.write_page(metadata_page);
    }

    EXPECT_THROW(
        Database database(database_path_),
        std::runtime_error
    );
}


TEST(PageTest, RejectsInvalidPageType) {
    Page page(1, PageType::Data);

    auto bytes = page.serialize();
    bytes[8] = std::byte{0xFF};

    EXPECT_THROW(
        Page::deserialize(bytes),
        std::runtime_error
    );
}

TEST_F(DiskManagerTest, RejectsUnalignedDatabaseFile) {
    {
        std::ofstream file(database_path_, std::ios::binary);
        ASSERT_TRUE(file.is_open());

        const char invalid_data = 'X';
        file.write(&invalid_data, 1);
    }

    EXPECT_THROW(
        DiskManager disk(database_path_),
        std::runtime_error
    );
}

TEST_F(DatabaseTest, PreservesMultiplePagesAcrossReopen) {
    {
        Database database(database_path_);

        const PageId root_id = database.metadata().root_page_id;

        Page root = database.read_page(root_id);
        root.data()[0] = std::byte{0x10};

        database.write_page(root);

        Page extra(2, PageType::Data);
        extra.data()[0] = std::byte{0x20};
        extra.data()[100] = std::byte{0x30};

        database.write_page(extra);
    }

    {
        Database database(database_path_);

        const Page root =
            database.read_page(database.metadata().root_page_id);

        const Page extra = database.read_page(2);

        EXPECT_EQ(root.data()[0], std::byte{0x10});
        EXPECT_EQ(extra.id(), 2);
        EXPECT_EQ(extra.data()[0], std::byte{0x20});
        EXPECT_EQ(extra.data()[100], std::byte{0x30});
    }
}

}  // namespace asterdb

TEST(BPlusTreeNodeTest, LeafNodeStoresMetadataAndEntries) {
    asterdb::BPlusTreeNode node(
        10,
        asterdb::BPlusTreeNodeType::Leaf
    );

    node.set_parent_page_id(3);
    node.set_next_leaf_page_id(11);

    node.add_leaf_entry(10, 100);
    node.add_leaf_entry(20, 200);
    node.add_leaf_entry(30, 300);

    EXPECT_EQ(node.page_id(), 10);
    EXPECT_TRUE(node.is_leaf());
    EXPECT_FALSE(node.is_internal());

    EXPECT_EQ(node.parent_page_id(), 3);
    EXPECT_EQ(node.next_leaf_page_id(), 11);

    ASSERT_EQ(node.leaf_entries().size(), 3);

    EXPECT_EQ(node.leaf_entries()[0].key, 10);
    EXPECT_EQ(node.leaf_entries()[0].value, 100);

    EXPECT_EQ(node.leaf_entries()[1].key, 20);
    EXPECT_EQ(node.leaf_entries()[1].value, 200);

    EXPECT_EQ(node.leaf_entries()[2].key, 30);
    EXPECT_EQ(node.leaf_entries()[2].value, 300);
}

TEST(BPlusTreeNodeTest, InternalNodeStoresKeysAndChildren) {
    asterdb::BPlusTreeNode node(
        20,
        asterdb::BPlusTreeNodeType::Internal
    );

    node.set_parent_page_id(5);

    node.add_internal_child(10);
    node.add_internal_key(30);
    node.add_internal_child(11);
    node.add_internal_key(60);
    node.add_internal_child(12);

    EXPECT_EQ(node.page_id(), 20);
    EXPECT_TRUE(node.is_internal());
    EXPECT_FALSE(node.is_leaf());

    EXPECT_EQ(node.parent_page_id(), 5);

    ASSERT_EQ(node.keys().size(), 2);
    ASSERT_EQ(node.children().size(), 3);

    EXPECT_EQ(node.keys()[0], 30);
    EXPECT_EQ(node.keys()[1], 60);

    EXPECT_EQ(node.children()[0], 10);
    EXPECT_EQ(node.children()[1], 11);
    EXPECT_EQ(node.children()[2], 12);

    EXPECT_EQ(
        node.children().size(),
        node.keys().size() + 1
    );
}

TEST(BPlusTreeNodeTest, LeafNodeRoundTripsThroughPage) {
    asterdb::BPlusTreeNode original(
        7,
        asterdb::BPlusTreeNodeType::Leaf
    );

    original.set_parent_page_id(2);
    original.set_next_leaf_page_id(8);

    original.add_leaf_entry(100, 1000);
    original.add_leaf_entry(200, 2000);
    original.add_leaf_entry(300, 3000);

    const asterdb::Page page = original.to_page();
    const asterdb::BPlusTreeNode restored =
        asterdb::BPlusTreeNode::from_page(page);

    EXPECT_EQ(restored.page_id(), 7);
    EXPECT_TRUE(restored.is_leaf());
    EXPECT_EQ(restored.parent_page_id(), 2);
    EXPECT_EQ(restored.next_leaf_page_id(), 8);

    ASSERT_EQ(restored.leaf_entries().size(), 3);

    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(
            restored.leaf_entries()[i].key,
            original.leaf_entries()[i].key
        );

        EXPECT_EQ(
            restored.leaf_entries()[i].value,
            original.leaf_entries()[i].value
        );
    }
}

TEST(BPlusTreeNodeTest, InternalNodeRoundTripsThroughPage) {
    asterdb::BPlusTreeNode original(
        20,
        asterdb::BPlusTreeNodeType::Internal
    );

    original.set_parent_page_id(2);

    original.add_internal_child(30);
    original.add_internal_key(100);
    original.add_internal_child(31);
    original.add_internal_key(200);
    original.add_internal_child(32);

    const asterdb::Page page = original.to_page();
    const asterdb::BPlusTreeNode restored =
        asterdb::BPlusTreeNode::from_page(page);

    EXPECT_EQ(restored.page_id(), 20);
    EXPECT_TRUE(restored.is_internal());
    EXPECT_EQ(restored.parent_page_id(), 2);

    EXPECT_EQ(restored.keys(), original.keys());
    EXPECT_EQ(restored.children(), original.children());
}

TEST(BPlusTreeNodeTest, NodeCapacityMatchesPageLayout) {
    EXPECT_EQ(
        asterdb::BPlusTreeNode::MAX_LEAF_ENTRIES,
        253
    );

    EXPECT_EQ(
        asterdb::BPlusTreeNode::MAX_INTERNAL_KEYS,
        252
    );

    EXPECT_EQ(
        asterdb::BPlusTreeNode::MAX_INTERNAL_CHILDREN,
        253
    );
}

TEST(BPlusTreeNodeTest, InternalNodeRejectsInvalidKeyOrderingOfOperations) {
    asterdb::BPlusTreeNode node(
        20,
        asterdb::BPlusTreeNodeType::Internal
    );

    EXPECT_THROW(
        node.add_internal_key(100),
        std::logic_error
    );

    node.add_internal_child(10);

    node.add_internal_key(100);

    EXPECT_EQ(node.keys().size(), 1);
    EXPECT_EQ(node.children().size(), 1);

    // Adding another child restores the invariant.
    node.add_internal_child(11);

    EXPECT_EQ(
        node.children().size(),
        node.keys().size() + 1
    );
}

TEST(BPlusTreeNodeTest, LeafCannotReceiveInternalEntries) {
    asterdb::BPlusTreeNode node(
        1,
        asterdb::BPlusTreeNodeType::Leaf
    );

    EXPECT_THROW(
        node.add_internal_key(100),
        std::logic_error
    );

    EXPECT_THROW(
        node.add_internal_child(10),
        std::logic_error
    );
}

TEST(BPlusTreeNodeTest, InternalCannotReceiveLeafEntries) {
    asterdb::BPlusTreeNode node(
        1,
        asterdb::BPlusTreeNodeType::Internal
    );

    EXPECT_THROW(
        node.add_leaf_entry(100, 1000),
        std::logic_error
    );
}

TEST(BPlusTreeNodeTest, InsertInternalEntry) {
    asterdb::BPlusTreeNode node(
    10,
    asterdb::BPlusTreeNodeType::Internal
    );

    node.add_internal_child(100);
    node.add_internal_key(30);
    node.add_internal_child(200);
    node.add_internal_key(60);
    node.add_internal_child(300);

    node.insert_internal_entry(
        1,
        45,
        250
    );

    ASSERT_EQ(node.keys().size(), 3);
    ASSERT_EQ(node.children().size(), 4);

    EXPECT_EQ(node.keys()[0], 30);
    EXPECT_EQ(node.keys()[1], 45);
    EXPECT_EQ(node.keys()[2], 60);

    EXPECT_EQ(node.children()[0], 100);
    EXPECT_EQ(node.children()[1], 200);
    EXPECT_EQ(node.children()[2], 250);
    EXPECT_EQ(node.children()[3], 300);
}

TEST(BPlusTreeTest, StartsWithDatabaseRoot) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_root_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    EXPECT_EQ(
        tree.root_page_id(),
        database.metadata().root_page_id
    );

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, GetReturnsNothingForMissingKey) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_missing_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    EXPECT_FALSE(tree.get(42).has_value());

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, PutAndGetSingleKey) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_single_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(42, 4200);

    const auto value = tree.get(42);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 4200);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, PutMaintainsSortedLeafEntries) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_sorted_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(50, 500);
    tree.put(10, 100);
    tree.put(30, 300);
    tree.put(20, 200);
    tree.put(40, 400);

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root.is_leaf());

    ASSERT_EQ(root.leaf_entries().size(), 5U);

    EXPECT_EQ(root.leaf_entries()[0].key, 10);
    EXPECT_EQ(root.leaf_entries()[1].key, 20);
    EXPECT_EQ(root.leaf_entries()[2].key, 30);
    EXPECT_EQ(root.leaf_entries()[3].key, 40);
    EXPECT_EQ(root.leaf_entries()[4].key, 50);

    EXPECT_EQ(tree.get(10).value(), 100);
    EXPECT_EQ(tree.get(20).value(), 200);
    EXPECT_EQ(tree.get(30).value(), 300);
    EXPECT_EQ(tree.get(40).value(), 400);
    EXPECT_EQ(tree.get(50).value(), 500);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, PutUpdatesExistingKey) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_update_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(42, 100);
    tree.put(42, 200);

    const auto value = tree.get(42);

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 200);

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    EXPECT_EQ(root.leaf_entries().size(), 1U);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, SplitsFullRootLeaf) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_split_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count =
        asterdb::BPlusTreeNode::MAX_LEAF_ENTRIES + 1;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    EXPECT_NE(
        tree.root_page_id(),
        1U
    );

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root.is_internal());
    ASSERT_EQ(root.keys().size(), 1U);
    ASSERT_EQ(root.children().size(), 2U);

    const auto left =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[0])
        );

    const auto right =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[1])
        );

    ASSERT_TRUE(left.is_leaf());
    ASSERT_TRUE(right.is_leaf());

    EXPECT_EQ(
        left.next_leaf_page_id(),
        right.page_id()
    );

    EXPECT_EQ(
        right.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        left.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        left.leaf_entries().size() +
            right.leaf_entries().size(),
        key_count
    );

    EXPECT_LT(
        left.leaf_entries().back().key,
        right.leaf_entries().front().key
    );

    EXPECT_EQ(
        root.keys()[0],
        right.leaf_entries().front().key
    );

    for (std::size_t i = 0; i < key_count; ++i) {
        const auto value =
            tree.get(static_cast<asterdb::Key>(i));

        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(
            *value,
            static_cast<asterdb::Value>(i * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, SplitRootPersistsAcrossReopen) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_split_reopen_test.db";

    std::filesystem::remove(path);

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        constexpr std::size_t key_count =
            asterdb::BPlusTreeNode::MAX_LEAF_ENTRIES + 1;

        for (std::size_t i = 0; i < key_count; ++i) {
            tree.put(
                static_cast<asterdb::Key>(i),
                static_cast<asterdb::Value>(i + 1000)
            );
        }
    }

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        EXPECT_EQ(
            tree.root_page_id(),
            database.metadata().root_page_id
        );

        EXPECT_FALSE(
            tree.get(0) == std::nullopt
        );

        EXPECT_EQ(
            tree.get(0).value(),
            1000
        );

        EXPECT_EQ(
            tree.get(
                static_cast<asterdb::Key>(
                    asterdb::BPlusTreeNode::MAX_LEAF_ENTRIES
                )
            ).value(),
            1000 +
                asterdb::BPlusTreeNode::MAX_LEAF_ENTRIES
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, SplitsNonRootLeafAndUpdatesParent) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_non_root_split_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count =
        2 * asterdb::BPlusTreeNode::MAX_LEAF_ENTRIES;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root.is_internal());

    // Two leaf splits should leave the root with
    // three leaf children.
    ASSERT_EQ(root.children().size(), 3U);
    ASSERT_EQ(root.keys().size(), 2U);

    const auto left =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[0])
        );

    const auto middle =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[1])
        );

    const auto right =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[2])
        );

    ASSERT_TRUE(left.is_leaf());
    ASSERT_TRUE(middle.is_leaf());
    ASSERT_TRUE(right.is_leaf());

    EXPECT_EQ(
        left.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        middle.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        right.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        left.next_leaf_page_id(),
        middle.page_id()
    );

    EXPECT_EQ(
        middle.next_leaf_page_id(),
        right.page_id()
    );

    EXPECT_EQ(
        right.next_leaf_page_id(),
        asterdb::INVALID_PAGE_ID
    );

    EXPECT_LT(
        left.leaf_entries().back().key,
        middle.leaf_entries().front().key
    );

    EXPECT_LT(
        middle.leaf_entries().back().key,
        right.leaf_entries().front().key
    );

    EXPECT_EQ(
        root.keys()[0],
        middle.leaf_entries().front().key
    );

    EXPECT_EQ(
        root.keys()[1],
        right.leaf_entries().front().key
    );

    // Verify every key is still searchable after
    // splitting a non-root leaf.
    for (std::size_t i = 0; i < key_count; ++i) {
        const auto value =
            tree.get(static_cast<asterdb::Key>(i));

        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(
            *value,
            static_cast<asterdb::Value>(i * 10)
        );
    }

    std::filesystem::remove(path);
}


TEST(BPlusTreeTest, SplitsFullInternalRoot) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_internal_split_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count =
        32'258;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root.is_internal());

    // The original internal root should have split,
    // creating a new internal root.
    ASSERT_EQ(root.keys().size(), 1U);
    ASSERT_EQ(root.children().size(), 2U);

    const auto left =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[0])
        );

    const auto right =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(root.children()[1])
        );

    ASSERT_TRUE(left.is_internal());
    ASSERT_TRUE(right.is_internal());

    EXPECT_EQ(
        left.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        right.parent_page_id(),
        root.page_id()
    );

    EXPECT_EQ(
        left.children().size(),
        left.keys().size() + 1
    );

    EXPECT_EQ(
        right.children().size(),
        right.keys().size() + 1
    );

    // The promoted separator belongs only to the new root.
    EXPECT_LT(
        left.keys().back(),
        root.keys()[0]
    );

    EXPECT_LT(
        root.keys()[0],
        right.keys().front()
    );

    // Verify lookups on both sides of the new root.
    EXPECT_EQ(
        tree.get(0).value(),
        0
    );

    EXPECT_EQ(
        tree.get(
            static_cast<asterdb::Key>(key_count - 1)
        ).value(),
        static_cast<asterdb::Value>(
            (key_count - 1) * 10
        )
    );

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveExistingKey) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(10, 100);
    tree.put(20, 200);
    tree.put(30, 300);

    EXPECT_TRUE(tree.remove(20));

    EXPECT_FALSE(tree.get(20).has_value());
    ASSERT_TRUE(tree.get(10).has_value());
    EXPECT_EQ(tree.get(10).value(), 100);
    ASSERT_TRUE(tree.get(30).has_value());
    EXPECT_EQ(tree.get(30).value(), 300);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveMissingKeyReturnsFalse) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_missing_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(10, 100);

    EXPECT_FALSE(tree.remove(20));
    ASSERT_TRUE(tree.get(10).has_value());
    EXPECT_EQ(tree.get(10).value(), 100);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveFromRootLeaf) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_root_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    for (asterdb::Key key = 0; key < 10; ++key) {
        tree.put(key, key * 10);
    }

    EXPECT_TRUE(tree.remove(0));
    EXPECT_TRUE(tree.remove(5));
    EXPECT_TRUE(tree.remove(9));

    EXPECT_FALSE(tree.get(0).has_value());
    EXPECT_FALSE(tree.get(5).has_value());
    EXPECT_FALSE(tree.get(9).has_value());

    EXPECT_EQ(tree.get(1).value(), 10);
    EXPECT_EQ(tree.get(8).value(), 80);

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    EXPECT_TRUE(root.is_leaf());
    EXPECT_EQ(root.leaf_entries().size(), 7U);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveTriggersLeafBorrowFromLeft) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_borrow_left_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 255;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    EXPECT_TRUE(tree.remove(254));

    EXPECT_FALSE(tree.get(254).has_value());
    EXPECT_EQ(tree.get(253).value(), 2530);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveTriggersLeafBorrowFromRight) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_borrow_right_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 255;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    EXPECT_TRUE(tree.remove(0));

    EXPECT_FALSE(tree.get(0).has_value());
    EXPECT_EQ(tree.get(1).value(), 10);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveTriggersLeafMerge) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_merge_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 382;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    const auto root_before =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root_before.is_internal());
    ASSERT_EQ(root_before.children().size(), 3U);

    EXPECT_TRUE(tree.remove(0));

    const auto root_after =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root_after.is_internal());
    EXPECT_EQ(root_after.children().size(), 2U);

    EXPECT_FALSE(tree.get(0).has_value());
    EXPECT_EQ(tree.get(1).value(), 10);
    EXPECT_EQ(
        tree.get(static_cast<asterdb::Key>(key_count - 1)).value(),
        static_cast<asterdb::Value>((key_count - 1) * 10)
    );

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveCanShrinkRoot) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_shrink_root_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 255;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    const auto old_root_page_id = tree.root_page_id();

    EXPECT_TRUE(tree.remove(0));

    EXPECT_EQ(tree.root_page_id(), old_root_page_id);

    for (asterdb::Key key = 1; key < key_count; ++key) {
        EXPECT_EQ(
            tree.get(key).value(),
            static_cast<asterdb::Value>(key * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveTriggersInternalBorrowFromLeft) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_internal_borrow_left_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 32'258;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    // Remove keys from the far-right leaf until it underflows.
    //
    // The exact structural result is checked rather than assuming
    // a particular physical page ID.
    for (std::size_t key = key_count - 1;
         key >= key_count - 64;
         --key) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(key))
        );
    }

    EXPECT_FALSE(
        tree.get(static_cast<asterdb::Key>(key_count - 1))
            .has_value()
    );

    EXPECT_EQ(
        tree.get(static_cast<asterdb::Key>(key_count - 65)).value(),
        static_cast<asterdb::Value>((key_count - 65) * 10)
    );

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveTriggersInternalBorrowFromRight) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_internal_borrow_right_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 32'258;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    // Remove keys from the far-left leaf until it underflows.
    for (std::size_t key = 0; key < 64; ++key) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(key))
        );
    }

    EXPECT_FALSE(tree.get(0).has_value());

    EXPECT_EQ(
        tree.get(64).value(),
        static_cast<asterdb::Value>(64 * 10)
    );

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemovePreservesTreeAfterLargeDeletion) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_large_delete_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 2'000;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    for (std::size_t i = 0; i < key_count; i += 2) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(i))
        );
    }

    for (std::size_t i = 0; i < key_count; ++i) {
        const auto result =
            tree.get(static_cast<asterdb::Key>(i));

        if (i % 2 == 0) {
            EXPECT_FALSE(result.has_value());
        } else {
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(
                result.value(),
                static_cast<asterdb::Value>(i * 10)
            );
        }
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemovePersistsAfterReopen) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_delete_reopen_test.db";

    std::filesystem::remove(path);

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        constexpr std::size_t key_count = 1'000;

        for (std::size_t i = 0; i < key_count; ++i) {
            tree.put(
                static_cast<asterdb::Key>(i),
                static_cast<asterdb::Value>(i * 10)
            );
        }

        for (std::size_t i = 0; i < key_count; i += 3) {
            ASSERT_TRUE(
                tree.remove(static_cast<asterdb::Key>(i))
            );
        }
    }

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        constexpr std::size_t key_count = 1'000;

        for (std::size_t i = 0; i < key_count; ++i) {
            const auto result =
                tree.get(static_cast<asterdb::Key>(i));

            if (i % 3 == 0) {
                EXPECT_FALSE(result.has_value());
            } else {
                ASSERT_TRUE(result.has_value());
                EXPECT_EQ(
                    result.value(),
                    static_cast<asterdb::Value>(i * 10)
                );
            }
        }
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemovePreservesEntireTreeStructure) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_delete_structure_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 2'000;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    // Create substantial redistribution and merging.
    for (std::size_t i = 0; i < key_count; i += 2) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(i))
        );
    }

    std::vector<asterdb::PageId> leaf_pages;

    validate_bplus_tree_node(
        database,
        tree.root_page_id(),
        asterdb::INVALID_PAGE_ID,
        leaf_pages
    );

    ASSERT_FALSE(leaf_pages.empty());

    // Verify the leaf linked list is ordered.
    for (std::size_t i = 1; i < leaf_pages.size(); ++i) {
        const auto previous =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(leaf_pages[i - 1])
            );

        const auto current =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(leaf_pages[i])
            );

        ASSERT_EQ(
            previous.next_leaf_page_id(),
            current.page_id()
        );

        ASSERT_FALSE(previous.leaf_entries().empty());
        ASSERT_FALSE(current.leaf_entries().empty());

        ASSERT_LT(
            previous.leaf_entries().back().key,
            current.leaf_entries().front().key
        );
    }

    // Verify every remaining key is still retrievable.
    for (std::size_t i = 1; i < key_count; i += 2) {
        const auto result =
            tree.get(static_cast<asterdb::Key>(i));

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(
            result.value(),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanSingleLeafRange) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_single_leaf_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    for (asterdb::Key key = 0; key < 10; ++key) {
        tree.put(key, key * 10);
    }

    const auto results = tree.scan(3, 7);

    ASSERT_EQ(results.size(), 5U);

    for (std::size_t i = 0; i < results.size(); ++i) {
        EXPECT_EQ(
            results[i].first,
            static_cast<asterdb::Key>(i + 3)
        );

        EXPECT_EQ(
            results[i].second,
            static_cast<asterdb::Value>((i + 3) * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanAcrossMultipleLeaves) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_multi_leaf_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 1'000;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    const auto results = tree.scan(100, 900);

    ASSERT_EQ(results.size(), 801U);

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto expected_key =
            static_cast<asterdb::Key>(i + 100);

        EXPECT_EQ(results[i].first, expected_key);
        EXPECT_EQ(
            results[i].second,
            static_cast<asterdb::Value>(expected_key * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanIncludesBothBoundaries) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_boundaries_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    for (asterdb::Key key = 10; key <= 20; ++key) {
        tree.put(key, key * 10);
    }

    const auto results = tree.scan(10, 20);

    ASSERT_EQ(results.size(), 11U);
    EXPECT_EQ(results.front().first, 10U);
    EXPECT_EQ(results.back().first, 20U);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanHandlesMissingBoundaryKeys) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_missing_boundary_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(10, 100);
    tree.put(20, 200);
    tree.put(30, 300);
    tree.put(40, 400);

    const auto results = tree.scan(15, 35);

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].first, 20U);
    EXPECT_EQ(results[0].second, 200U);
    EXPECT_EQ(results[1].first, 30U);
    EXPECT_EQ(results[1].second, 300U);

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanReturnsEmptyForReversedRange) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_reversed_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    for (asterdb::Key key = 0; key < 10; ++key) {
        tree.put(key, key * 10);
    }

    const auto results = tree.scan(8, 3);

    EXPECT_TRUE(results.empty());

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanReturnsEmptyWhenNoKeysMatch) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_empty_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    tree.put(10, 100);
    tree.put(20, 200);
    tree.put(30, 300);

    const auto results = tree.scan(40, 50);

    EXPECT_TRUE(results.empty());

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanWorksAfterDeletionAndMerging) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_after_delete_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 1'000;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    for (std::size_t i = 0; i < key_count; i += 2) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(i))
        );
    }

    const auto results = tree.scan(100, 200);

    ASSERT_EQ(results.size(), 50U);

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto expected_key =
            static_cast<asterdb::Key>(101 + i * 2);

        EXPECT_EQ(results[i].first, expected_key);
        EXPECT_EQ(
            results[i].second,
            static_cast<asterdb::Value>(expected_key * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, ScanPersistsAfterReopen) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_scan_reopen_test.db";

    std::filesystem::remove(path);

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        for (asterdb::Key key = 0; key < 1'000; ++key) {
            tree.put(key, key * 10);
        }

        for (asterdb::Key key = 0; key < 1'000; key += 3) {
            ASSERT_TRUE(tree.remove(key));
        }
    }

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        const auto results = tree.scan(100, 200);

        for (const auto& [key, value] : results) {
            EXPECT_GE(key, 100U);
            EXPECT_LE(key, 200U);
            EXPECT_EQ(value, key * 10);
            EXPECT_NE(key % 3, 0U);
        }

        ASSERT_FALSE(results.empty());

        for (std::size_t i = 1; i < results.size(); ++i) {
            EXPECT_LT(
                results[i - 1].first,
                results[i].first
            );
        }
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveAllKeysCollapsesTreeToEmptyLeafRoot) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_remove_all_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 1'000;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    for (std::size_t i = 0; i < key_count; ++i) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(i))
        );
    }

    EXPECT_TRUE(tree.scan(0, key_count).empty());

    const auto root =
        asterdb::BPlusTreeNode::from_page(
            database.read_page(tree.root_page_id())
        );

    ASSERT_TRUE(root.is_leaf());
    EXPECT_TRUE(root.leaf_entries().empty());
    EXPECT_EQ(
        root.parent_page_id(),
        asterdb::INVALID_PAGE_ID
    );

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveAllKeysThenReinsertWorks) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_reinsert_after_delete_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 1'000;

    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    for (std::size_t i = 0; i < key_count; ++i) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(i))
        );
    }

    // The tree should still be a valid empty tree.
    EXPECT_TRUE(tree.scan(0, key_count).empty());

    // Reinsert after complete collapse.
    constexpr std::size_t new_key_count = 500;

    for (std::size_t i = 0; i < new_key_count; ++i) {
        const auto key =
            static_cast<asterdb::Key>(10'000 + i);

        tree.put(
            key,
            static_cast<asterdb::Value>(key * 10)
        );
    }

    const auto results =
        tree.scan(10'000, 10'499);

    ASSERT_EQ(results.size(), new_key_count);

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto expected_key =
            static_cast<asterdb::Key>(10'000 + i);

        EXPECT_EQ(results[i].first, expected_key);
        EXPECT_EQ(
            results[i].second,
            static_cast<asterdb::Value>(expected_key * 10)
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, RemoveAllKeysPersistsEmptyTreeAfterReopen) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_empty_reopen_test.db";

    std::filesystem::remove(path);

    asterdb::PageId root_page_id;

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        constexpr std::size_t key_count = 1'000;

        for (std::size_t i = 0; i < key_count; ++i) {
            tree.put(
                static_cast<asterdb::Key>(i),
                static_cast<asterdb::Value>(i * 10)
            );
        }

        for (std::size_t i = 0; i < key_count; ++i) {
            ASSERT_TRUE(
                tree.remove(static_cast<asterdb::Key>(i))
            );
        }

        root_page_id = tree.root_page_id();

        const auto root =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(root_page_id)
            );

        ASSERT_TRUE(root.is_leaf());
        EXPECT_TRUE(root.leaf_entries().empty());
    }

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        EXPECT_EQ(tree.root_page_id(), root_page_id);
        EXPECT_TRUE(tree.scan(0, 1'000).empty());

        const auto root =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(tree.root_page_id())
            );

        ASSERT_TRUE(root.is_leaf());
        EXPECT_TRUE(root.leaf_entries().empty());

        // The reopened empty tree must remain usable.
        tree.put(42, 420);

        ASSERT_TRUE(tree.get(42).has_value());
        EXPECT_EQ(tree.get(42).value(), 420);
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, MixedWorkloadPreservesCorrectness) {
    const auto path =
        std::filesystem::temp_directory_path() /
        "asterdb_bplus_tree_mixed_workload_test.db";

    std::filesystem::remove(path);

    asterdb::Database database(path);
    asterdb::BPlusTree tree(database);

    constexpr std::size_t key_count = 2'000;

    // Initial population.
    for (std::size_t i = 0; i < key_count; ++i) {
        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 10)
        );
    }

    // Delete every third key.
    for (std::size_t i = 0; i < key_count; i += 3) {
        ASSERT_TRUE(
            tree.remove(static_cast<asterdb::Key>(i))
        );
    }

    // Update every fifth remaining key.
    for (std::size_t i = 1; i < key_count; i += 5) {
        if (i % 3 == 0) {
            continue;
        }

        tree.put(
            static_cast<asterdb::Key>(i),
            static_cast<asterdb::Value>(i * 100)
        );
    }

    // Insert a completely new key range.
    for (std::size_t i = 0; i < 500; ++i) {
        const auto key =
            static_cast<asterdb::Key>(10'000 + i);

        tree.put(
            key,
            static_cast<asterdb::Value>(key * 10)
        );
    }

    // Delete part of the newly inserted range.
    for (std::size_t i = 0; i < 500; i += 4) {
        ASSERT_TRUE(
            tree.remove(
                static_cast<asterdb::Key>(10'000 + i)
            )
        );
    }

    // Verify the original key range.
    for (std::size_t i = 0; i < key_count; ++i) {
        const auto result =
            tree.get(static_cast<asterdb::Key>(i));

        if (i % 3 == 0) {
            EXPECT_FALSE(result.has_value());
            continue;
        }

        ASSERT_TRUE(result.has_value());

        if (i % 5 == 1) {
            EXPECT_EQ(
                result.value(),
                static_cast<asterdb::Value>(i * 100)
            );
        } else {
            EXPECT_EQ(
                result.value(),
                static_cast<asterdb::Value>(i * 10)
            );
        }
    }

    // Verify the new key range through SCAN.
    const auto results =
        tree.scan(10'000, 10'499);

    ASSERT_EQ(results.size(), 375U);

    for (const auto& [key, value] : results) {
        EXPECT_EQ(
            value,
            static_cast<asterdb::Value>(key * 10)
        );

        EXPECT_NE(
            (key - 10'000) % 4,
            0U
        );
    }

    // Verify the complete structural invariants again.
    std::vector<asterdb::PageId> leaf_pages;

    validate_bplus_tree_node(
        database,
        tree.root_page_id(),
        asterdb::INVALID_PAGE_ID,
        leaf_pages
    );

    ASSERT_FALSE(leaf_pages.empty());

    for (std::size_t i = 1; i < leaf_pages.size(); ++i) {
        const auto previous =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(leaf_pages[i - 1])
            );

        EXPECT_EQ(
            previous.next_leaf_page_id(),
            leaf_pages[i]
        );

        ASSERT_FALSE(previous.leaf_entries().empty());

        const auto current =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(leaf_pages[i])
            );

        ASSERT_FALSE(current.leaf_entries().empty());

        EXPECT_LT(
            previous.leaf_entries().back().key,
            current.leaf_entries().front().key
        );
    }

    std::filesystem::remove(path);
}

TEST(BPlusTreeTest, DeleteFirstKeyFromNonLeftmostLeafUpdatesSeparator) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        "asterdb_delete_first_key_separator_test.db";

    std::filesystem::remove(path);

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        // Create the initial tree.
        for (std::uint64_t key = 1; key <= 253; ++key) {
            tree.put(key, key * 10);
        }

        // The first split creates:
        //
        //   left leaf  = 1..127
        //   right leaf = 128..253
        //
        // The right leaf is therefore non-leftmost and has 126 entries
        // after deleting 128, so we need one more key in this leaf.
        tree.put(254, 2540);

        const asterdb::PageId root_page_id =
            tree.root_page_id();

        const asterdb::BPlusTreeNode root =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(root_page_id)
            );

        ASSERT_TRUE(root.is_internal());
        ASSERT_EQ(root.children().size(), 2);
        ASSERT_EQ(root.keys().size(), 1);
        ASSERT_EQ(root.keys()[0], 128);

        const asterdb::PageId right_leaf_page_id =
            root.children()[1];

        const asterdb::BPlusTreeNode right_leaf =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(right_leaf_page_id)
            );

        ASSERT_TRUE(right_leaf.is_leaf());
        ASSERT_EQ(right_leaf.leaf_entries().front().key, 128);
        ASSERT_EQ(right_leaf.leaf_entries().size(), 127);

        // Deleting 128 leaves 126 entries, so this still underflows.
        // Therefore we need to add another key to the same leaf.
        //
        // Key 255 belongs to the rightmost leaf.
        tree.put(255, 2550);

        const asterdb::BPlusTreeNode populated_leaf =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(right_leaf_page_id)
            );

        ASSERT_EQ(populated_leaf.leaf_entries().front().key, 128);
        ASSERT_EQ(populated_leaf.leaf_entries().size(), 128);

        ASSERT_TRUE(tree.remove(128));

        const asterdb::BPlusTreeNode leaf_after =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(right_leaf_page_id)
            );

        ASSERT_EQ(
            leaf_after.leaf_entries().front().key,
            129
        );
        ASSERT_EQ(leaf_after.leaf_entries().size(), 127);

        const asterdb::BPlusTreeNode root_after =
            asterdb::BPlusTreeNode::from_page(
                database.read_page(root_page_id)
            );

        ASSERT_EQ(root_after.keys()[0], 129);

        EXPECT_FALSE(tree.get(128).has_value());
        EXPECT_EQ(
            tree.get(129),
            std::optional<asterdb::Value>(1290)
        );
        EXPECT_EQ(
            tree.get(255),
            std::optional<asterdb::Value>(2550)
        );
    }

    {
        asterdb::Database database(path);
        asterdb::BPlusTree tree(database);

        EXPECT_FALSE(tree.get(128).has_value());
        EXPECT_EQ(
            tree.get(129),
            std::optional<asterdb::Value>(1290)
        );
        EXPECT_EQ(
            tree.get(255),
            std::optional<asterdb::Value>(2550)
        );
    }

    std::filesystem::remove(path);
}