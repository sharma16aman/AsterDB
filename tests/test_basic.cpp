#include "asterdb/storage/database.h"
#include "asterdb/storage/disk_manager.h"
#include "asterdb/storage/page.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

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
