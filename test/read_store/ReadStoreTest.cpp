#include <HalStorage.h>
#include <LibraryFormat.h>
#include <ReadStore.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr const char* READ_PATH = "/.crosspoint/read.bin";
constexpr const char* NEW_PATH = "/.crosspoint/read.new";
constexpr const char* BACKUP_PATH = "/.crosspoint/read.bak";

struct Header {
  std::string magic;
  uint8_t version = 0;
  uint16_t count = 0;
  uint64_t size = 0;
};

uint64_t hashOf(const std::string& path) { return library::clixPathHash(path.data(), path.size()); }

std::vector<uint8_t> encodeStore(const uint16_t count, const std::vector<uint64_t>& hashes) {
  std::vector<uint8_t> bytes(16 + hashes.size() * 8, 0);
  bytes[0] = 'R';
  bytes[1] = 'D';
  bytes[2] = 'B';
  bytes[3] = '1';
  bytes[4] = 1;
  bytes[5] = static_cast<uint8_t>(count & 0xFF);
  bytes[6] = static_cast<uint8_t>((count >> 8) & 0xFF);
  for (size_t i = 0; i < hashes.size(); i++) {
    for (size_t b = 0; b < 8; b++) {
      bytes[16 + i * 8 + b] = static_cast<uint8_t>(hashes[i] >> (8 * b));
    }
  }
  return bytes;
}

void installStore(const std::vector<uint8_t>& bytes) {
  fake::add(READ_PATH, std::string(bytes.begin(), bytes.end()));
}

void writeStoreFile(const uint16_t count, const std::vector<uint64_t>& hashes) {
  installStore(encodeStore(count, hashes));
}

Header readStoreHeader() {
  Header header;
  const auto found = fake::files.find(READ_PATH);
  if (found == fake::files.end()) return header;
  const auto& bytes = found->second->bytes;
  if (bytes.size() >= 4) header.magic.assign(reinterpret_cast<const char*>(bytes.data()), 4);
  if (bytes.size() >= 5) header.version = bytes[4];
  if (bytes.size() >= 7) header.count = static_cast<uint16_t>(bytes[5] | (bytes[6] << 8));
  header.size = bytes.size();
  return header;
}

std::vector<uint64_t> storedHashes() {
  std::vector<uint64_t> hashes;
  const auto found = fake::files.find(READ_PATH);
  if (found == fake::files.end()) return hashes;
  const auto& bytes = found->second->bytes;
  for (size_t offset = 16; offset + 8 <= bytes.size(); offset += 8) {
    uint64_t hash = 0;
    for (size_t b = 0; b < 8; b++) {
      hash |= static_cast<uint64_t>(bytes[offset + b]) << (8 * b);
    }
    hashes.push_back(hash);
  }
  return hashes;
}

std::vector<uint8_t> fileBytes(const char* path) {
  const auto found = fake::files.find(path);
  if (found == fake::files.end()) return {};
  return found->second->bytes;
}

bool hasFile(const char* path) { return fake::files.count(path) != 0; }

class ReadStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    ReadStore::getInstance().resetForTests();
  }

  void TearDown() override { ReadStore::getInstance().resetForTests(); }
};

}  // namespace

TEST_F(ReadStoreTest, EmptyStoreReportsUnreadAndDoesNotWrite) {
  EXPECT_FALSE(ReadStore::getInstance().isRead("/books/a.epub"));
  EXPECT_TRUE(fake::writesByPath.empty());
  EXPECT_FALSE(hasFile(READ_PATH));
  EXPECT_FALSE(hasFile(NEW_PATH));
}

TEST_F(ReadStoreTest, MarkReadTrueCreatesTheFileAndReloads) {
  auto& store = ReadStore::getInstance();
  const std::string path = "/books/a.epub";
  ASSERT_TRUE(store.markRead(path, true));

  const Header header = readStoreHeader();
  EXPECT_EQ(header.magic, "RDB1");
  EXPECT_EQ(header.version, 1);
  EXPECT_EQ(header.count, 1);
  EXPECT_EQ(header.size, 16u + 8u);
  const auto hashes = storedHashes();
  ASSERT_EQ(hashes.size(), 1u);
  EXPECT_EQ(hashes[0], hashOf(path));

  store.resetForTests();
  EXPECT_TRUE(store.isRead(path));
}

TEST_F(ReadStoreTest, MarkReadNoOpDoesNotWrite) {
  auto& store = ReadStore::getInstance();
  const std::string path = "/books/a.epub";
  ASSERT_TRUE(store.markRead(path, true));
  const unsigned writes = fake::writesByPath[NEW_PATH];
  EXPECT_TRUE(store.markRead(path, true));
  EXPECT_EQ(fake::writesByPath[NEW_PATH], writes);
}

TEST_F(ReadStoreTest, MarkReadFalseRemoves) {
  auto& store = ReadStore::getInstance();
  const std::string path = "/books/a.epub";
  ASSERT_TRUE(store.markRead(path, true));
  ASSERT_TRUE(store.markRead(path, false));

  EXPECT_FALSE(store.isRead(path));
  const Header header = readStoreHeader();
  EXPECT_EQ(header.count, 0);
  EXPECT_EQ(header.size, 16u);
}

TEST_F(ReadStoreTest, CorruptFilesAreTreatedAsEmptyAndLeftUntouched) {
  const std::string path = "/books/a.epub";
  const std::vector<uint64_t> one = {hashOf(path)};

  std::vector<std::vector<uint8_t>> variants;
  {
    auto bytes = encodeStore(1, one);
    bytes[0] = 'X';
    variants.push_back(bytes);
  }
  {
    auto bytes = encodeStore(1, one);
    bytes[4] = 2;
    variants.push_back(bytes);
  }
  variants.push_back(std::vector<uint8_t>(8, 0));
  variants.push_back(encodeStore(2, one));
  variants.push_back(encodeStore(4097, std::vector<uint64_t>(4097, 1)));

  for (const auto& bytes : variants) {
    fake::reset();
    ReadStore::getInstance().resetForTests();
    installStore(bytes);
    const auto original = fileBytes(READ_PATH);

    EXPECT_FALSE(ReadStore::getInstance().isRead(path));
    EXPECT_EQ(fileBytes(READ_PATH), original);
    EXPECT_FALSE(hasFile(NEW_PATH));
    EXPECT_TRUE(fake::writesByPath.empty());
  }
}

TEST_F(ReadStoreTest, DamageIsNormalisedOnLoad) {
  const std::string a = "/books/a.epub";
  const std::string b = "/books/b.epub";
  const std::string c = "/books/c.epub";
  const std::string d = "/books/d.epub";
  writeStoreFile(4, {hashOf(b), hashOf(a), hashOf(a), hashOf(c)});

  auto& store = ReadStore::getInstance();
  EXPECT_TRUE(store.isRead(a));
  EXPECT_TRUE(store.isRead(b));
  EXPECT_TRUE(store.isRead(c));

  ASSERT_TRUE(store.markRead(d, true));
  const auto hashes = storedHashes();
  ASSERT_EQ(hashes.size(), 4u);
  EXPECT_TRUE(std::is_sorted(hashes.begin(), hashes.end()));
  EXPECT_EQ(std::adjacent_find(hashes.begin(), hashes.end()), hashes.end());
  EXPECT_EQ(readStoreHeader().count, 4);
}

TEST_F(ReadStoreTest, ReadFolderWinsOverStore) {
  auto& store = ReadStore::getInstance();

  EXPECT_TRUE(store.isRead("/read/x.epub"));
  EXPECT_FALSE(store.markRead("/read/x.epub", false));
  EXPECT_TRUE(store.markRead("/read/x.epub", true));
  EXPECT_TRUE(fake::writesByPath.empty());
  EXPECT_FALSE(hasFile(READ_PATH));

  EXPECT_FALSE(isInReadFolder("/read"));
  EXPECT_TRUE(isInReadFolder("/read/"));
  EXPECT_FALSE(isInReadFolder("/readme/a"));
  EXPECT_TRUE(isInReadFolder("/read/a"));
}

TEST_F(ReadStoreTest, RekeyCarriesReadState) {
  auto& store = ReadStore::getInstance();
  const std::string oldPath = "/books/old.epub";
  const std::string newPath = "/books/new.epub";
  ASSERT_TRUE(store.markRead(oldPath, true));

  ASSERT_TRUE(store.rekey(oldPath, newPath));
  EXPECT_FALSE(store.isRead(oldPath));
  EXPECT_TRUE(store.isRead(newPath));
  EXPECT_EQ(readStoreHeader().count, 1);
}

TEST_F(ReadStoreTest, RekeyOfUnreadPathsIsNoOp) {
  EXPECT_TRUE(ReadStore::getInstance().rekey("/books/x.epub", "/books/y.epub"));
  EXPECT_TRUE(fake::writesByPath.empty());
  EXPECT_FALSE(hasFile(READ_PATH));
}

TEST_F(ReadStoreTest, RekeyFromReadFolderCarriesReadState) {
  auto& store = ReadStore::getInstance();
  const std::string oldPath = "/read/legacy.epub";
  const std::string newPath = "/books/legacy.epub";

  ASSERT_TRUE(store.rekey(oldPath, newPath));
  EXPECT_TRUE(store.isRead(newPath));
  const auto hashes = storedHashes();
  EXPECT_NE(std::find(hashes.begin(), hashes.end(), hashOf(newPath)), hashes.end());
  EXPECT_EQ(std::find(hashes.begin(), hashes.end(), hashOf(oldPath)), hashes.end());
}

TEST_F(ReadStoreTest, RekeyToReadFolderStoresNothing) {
  auto& store = ReadStore::getInstance();
  const std::string oldPath = "/books/legacy.epub";
  const std::string newPath = "/read/legacy.epub";
  ASSERT_TRUE(store.markRead(oldPath, true));

  ASSERT_TRUE(store.rekey(oldPath, newPath));
  EXPECT_FALSE(store.isRead(oldPath));
  EXPECT_TRUE(store.isRead(newPath));
  const auto hashes = storedHashes();
  EXPECT_EQ(hashes.end(), std::find(hashes.begin(), hashes.end(), hashOf(newPath)));
}

TEST_F(ReadStoreTest, EraseRemovesEntry) {
  auto& store = ReadStore::getInstance();
  const std::string path = "/books/a.epub";
  ASSERT_TRUE(store.markRead(path, true));

  ASSERT_TRUE(store.erase(path));
  EXPECT_FALSE(store.isRead(path));
  EXPECT_EQ(readStoreHeader().count, 0);
}

TEST_F(ReadStoreTest, EraseUnknownPathIsNoOp) {
  EXPECT_TRUE(ReadStore::getInstance().erase("/books/none.epub"));
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST_F(ReadStoreTest, FailedSecondRenameKeepsPreviousState) {
  auto& store = ReadStore::getInstance();
  const std::string first = "/books/a.epub";
  const std::string second = "/books/b.epub";
  ASSERT_TRUE(store.markRead(first, true));
  const auto previous = fileBytes(READ_PATH);

  fake::failRename = 1;
  EXPECT_FALSE(store.markRead(second, true));
  EXPECT_EQ(fileBytes(READ_PATH), previous);
  EXPECT_FALSE(hasFile(NEW_PATH));
  EXPECT_FALSE(hasFile(BACKUP_PATH));
}

TEST_F(ReadStoreTest, BodyWriteFailureKeepsPreviousState) {
  auto& store = ReadStore::getInstance();
  const std::string first = "/books/a.epub";
  const std::string second = "/books/b.epub";
  ASSERT_TRUE(store.markRead(first, true));
  const auto previous = fileBytes(READ_PATH);

  fake::shortWrite = 8;
  EXPECT_FALSE(store.markRead(second, true));
  EXPECT_EQ(fileBytes(READ_PATH), previous);
  EXPECT_FALSE(hasFile(NEW_PATH));

  store.resetForTests();
  fake::failWrite = 1;
  EXPECT_FALSE(store.markRead(second, true));
  EXPECT_EQ(fileBytes(READ_PATH), previous);
  EXPECT_FALSE(hasFile(NEW_PATH));
}

TEST_F(ReadStoreTest, FullStoreRefusesNewEntryWithoutCorruptingFile) {
  std::vector<uint64_t> hashes;
  hashes.reserve(4096);
  for (int i = 0; i < 4096; i++) hashes.push_back(hashOf("/books/" + std::to_string(i)));
  std::sort(hashes.begin(), hashes.end());
  writeStoreFile(4096, hashes);
  const auto before = fileBytes(READ_PATH);

  EXPECT_FALSE(ReadStore::getInstance().markRead("/books/new.epub", true));
  EXPECT_EQ(fileBytes(READ_PATH), before);
  const Header header = readStoreHeader();
  EXPECT_EQ(header.count, 4096);
  EXPECT_EQ(header.size, 16u + 4096u * 8u);
}

TEST_F(ReadStoreTest, LoadIsLazy) {
  writeStoreFile(1, {hashOf("/books/a.epub")});
  EXPECT_EQ(fake::reads, 0u);

  EXPECT_TRUE(ReadStore::getInstance().isRead("/books/a.epub"));
  EXPECT_GT(fake::reads, 0u);
}
