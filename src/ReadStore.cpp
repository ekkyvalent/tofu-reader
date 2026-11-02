#include "ReadStore.h"

#include <HalStorage.h>
#include <LibraryFormat.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr char READ_PATH[] = "/.crosspoint/read.bin";
constexpr char NEW_PATH[] = "/.crosspoint/read.new";
constexpr char BACKUP_PATH[] = "/.crosspoint/read.bak";
constexpr char CACHE_DIR[] = "/.crosspoint";
constexpr char MAGIC[4] = {'R', 'D', 'B', '1'};
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_BYTES = 16;
constexpr size_t MAX_ENTRIES = 4096;
// Read the hash section in small chunks: a bounded stack buffer beats 4096
// single-hash reads without holding a second copy of the whole store.
constexpr size_t HASH_CHUNK = 64;

uint64_t hashPath(const std::string& path) { return library::clixPathHash(path.data(), path.size()); }

uint16_t decodeCount(const uint8_t* header) {
  return static_cast<uint16_t>(header[5]) | static_cast<uint16_t>(static_cast<uint16_t>(header[6]) << 8);
}

void encodeHash(const uint64_t hash, uint8_t out[8]) {
  for (size_t i = 0; i < 8; i++) {
    out[i] = static_cast<uint8_t>(hash >> (8 * i));
  }
}

uint64_t decodeHash(const uint8_t* in) {
  uint64_t hash = 0;
  for (size_t i = 0; i < 8; i++) {
    hash |= static_cast<uint64_t>(in[i]) << (8 * i);
  }
  return hash;
}

}  // namespace

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = 5;
  return path.size() > n && path.compare(0, n, "/read") == 0 && path[n] == '/';
}

ReadStore& ReadStore::getInstance() {
  static ReadStore instance;
  return instance;
}

#ifdef CROSSPOINT_UNIT_TESTS
void ReadStore::resetForTests() {
  hashes.clear();
  loaded = false;
}
#endif

void ReadStore::ensureLoaded() {
  if (loaded) return;
  loaded = true;
  load();
}

void ReadStore::load() {
  HalFile file;
  if (!Storage.openFileForRead("READ", READ_PATH, file)) {
    return;  // No file yet: empty store.
  }

  uint8_t header[HEADER_BYTES];
  const uint64_t fileSize = file.fileSize64();
  bool valid = file.read(header, sizeof(header)) == static_cast<int>(sizeof(header)) &&
               std::memcmp(header, MAGIC, sizeof(MAGIC)) == 0 && header[4] == VERSION;
  const uint16_t count = valid ? decodeCount(header) : 0;
  if (valid) {
    valid = count <= MAX_ENTRIES && fileSize == HEADER_BYTES + static_cast<uint64_t>(count) * sizeof(uint64_t);
  }
  if (!valid) {
    LOG_ERR("READ", "corrupt read state, treating %s as empty", READ_PATH);
    file.close();
    return;
  }

  hashes.clear();
  hashes.reserve(count);
  uint8_t chunk[HASH_CHUNK * sizeof(uint64_t)];
  size_t read = 0;
  while (read < count) {
    const size_t n = std::min(HASH_CHUNK, static_cast<size_t>(count - read));
    if (file.read(chunk, n * sizeof(uint64_t)) != static_cast<int>(n * sizeof(uint64_t))) {
      LOG_ERR("READ", "short read from %s, treating it as empty", READ_PATH);
      hashes.clear();
      file.close();
      return;
    }
    for (size_t i = 0; i < n; i++) {
      hashes.push_back(decodeHash(chunk + i * sizeof(uint64_t)));
    }
    read += n;
  }
  file.close();

  // The writer keeps these invariants; re-establish them for a hand-edited or
  // partially damaged file so binary search below stays sound.
  std::sort(hashes.begin(), hashes.end());
  hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
}

bool ReadStore::contains(const uint64_t hash) const {
  return std::binary_search(hashes.begin(), hashes.end(), hash);
}

bool ReadStore::insert(const uint64_t hash) {
  const auto it = std::lower_bound(hashes.begin(), hashes.end(), hash);
  if (it != hashes.end() && *it == hash) return true;
  if (hashes.size() >= MAX_ENTRIES) return false;
  hashes.insert(it, hash);
  return true;
}

void ReadStore::removeHash(const uint64_t hash) {
  const auto it = std::lower_bound(hashes.begin(), hashes.end(), hash);
  if (it != hashes.end() && *it == hash) hashes.erase(it);
}

bool ReadStore::isRead(const std::string& path) {
  if (isInReadFolder(path)) return true;
  ensureLoaded();
  return contains(hashPath(path));
}

bool ReadStore::markRead(const std::string& path, const bool read) {
  // Folder membership wins; an unread tombstone for a /read/ path is not
  // representable, so the request cannot be honoured.
  if (isInReadFolder(path)) return read;
  ensureLoaded();

  const uint64_t hash = hashPath(path);
  const bool stored = contains(hash);
  if (read == stored) return true;

  if (read) {
    if (!insert(hash)) {
      LOG_ERR("READ", "read state full (%u entries), refusing to add", static_cast<unsigned>(MAX_ENTRIES));
      return false;
    }
  } else {
    removeHash(hash);
  }
  return save();
}

bool ReadStore::rekey(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) return true;
  ensureLoaded();

  const uint64_t oldHash = hashPath(oldPath);
  const uint64_t newHash = hashPath(newPath);
  if (oldHash == newHash) return true;

  const bool oldStored = contains(oldHash);
  const bool newStored = contains(newHash);
  // Keep the user-visible state: a legacy /read/ book leaves the folder as read.
  const bool wasRead = oldStored || isInReadFolder(oldPath);
  const bool wantNewStored = wasRead && !isInReadFolder(newPath);

  // Check the cap before dropping the old entry: a refused mutation must leave
  // the store untouched.
  if (wantNewStored && !newStored && !oldStored && hashes.size() >= MAX_ENTRIES) {
    LOG_ERR("READ", "read state full (%u entries), refusing rekey", static_cast<unsigned>(MAX_ENTRIES));
    return false;
  }

  bool changed = false;
  if (oldStored) {
    removeHash(oldHash);
    changed = true;
  }
  if (wantNewStored) {
    if (!newStored) {
      insert(newHash);
      changed = true;
    }
  } else if (newStored) {
    removeHash(newHash);
    changed = true;
  }

  return changed ? save() : true;
}

bool ReadStore::erase(const std::string& path) {
  ensureLoaded();

  const uint64_t hash = hashPath(path);
  if (!contains(hash)) return true;
  removeHash(hash);
  return save();
}

bool ReadStore::validateNewFile() const {
  HalFile file;
  if (!Storage.openFileForRead("READ", NEW_PATH, file)) return false;

  uint8_t header[HEADER_BYTES];
  const uint64_t fileSize = file.fileSize64();
  const bool ok = file.read(header, sizeof(header)) == static_cast<int>(sizeof(header));
  file.close();
  if (!ok) return false;

  const uint16_t count = decodeCount(header);
  return std::memcmp(header, MAGIC, sizeof(MAGIC)) == 0 && header[4] == VERSION &&
         fileSize == HEADER_BYTES + static_cast<uint64_t>(count) * sizeof(uint64_t);
}

bool ReadStore::save() {
  if (!Storage.exists(CACHE_DIR)) Storage.mkdir(CACHE_DIR);

  HalFile out;
  if (!Storage.openFileForWrite("READ", NEW_PATH, out)) {
    LOG_ERR("READ", "cannot open %s for write", NEW_PATH);
    return false;
  }

  const uint16_t count = static_cast<uint16_t>(hashes.size());
  uint8_t header[HEADER_BYTES] = {};
  std::memcpy(header, MAGIC, sizeof(MAGIC));
  header[4] = VERSION;
  header[5] = static_cast<uint8_t>(count & 0xFF);
  header[6] = static_cast<uint8_t>((count >> 8) & 0xFF);

  bool ok = out.write(header, sizeof(header)) == sizeof(header);
  uint8_t encoded[8];
  for (size_t i = 0; ok && i < hashes.size(); i++) {
    encodeHash(hashes[i], encoded);
    ok = out.write(encoded, sizeof(encoded)) == sizeof(encoded);
  }
  if (!out.close()) ok = false;

  if (!ok) {
    LOG_ERR("READ", "write to %s failed", NEW_PATH);
    Storage.remove(NEW_PATH);
    return false;
  }

  // Reopen what we just wrote before it can replace the live state.
  if (!validateNewFile()) {
    LOG_ERR("READ", "written %s failed validation", NEW_PATH);
    Storage.remove(NEW_PATH);
    return false;
  }

  // FatFs rename does not overwrite, so a stale backup from an interrupted save
  // must be out of the way. The new file is complete at this point, so dropping
  // the old backup cannot lose the only copy.
  if (Storage.exists(BACKUP_PATH) && !Storage.remove(BACKUP_PATH)) {
    LOG_ERR("READ", "cannot remove stale backup %s", BACKUP_PATH);
    Storage.remove(NEW_PATH);
    return false;
  }

  const bool hadPrevious = Storage.exists(READ_PATH);
  if (hadPrevious && !Storage.rename(READ_PATH, BACKUP_PATH)) {
    LOG_ERR("READ", "cannot stage %s for replacement", READ_PATH);
    Storage.remove(NEW_PATH);
    return false;
  }

  if (!Storage.rename(NEW_PATH, READ_PATH)) {
    LOG_ERR("READ", "rename %s -> %s failed", NEW_PATH, READ_PATH);
    if (hadPrevious && !Storage.rename(BACKUP_PATH, READ_PATH)) {
      // Keep the backup: it is the only complete state left.
      LOG_ERR("READ", "rollback failed, previous state retained at %s", BACKUP_PATH);
    }
    Storage.remove(NEW_PATH);
    return false;
  }

  if (hadPrevious && !Storage.remove(BACKUP_PATH)) {
    // The live file is complete; a stale backup is removed before the next save.
    LOG_ERR("READ", "new state installed but backup cleanup failed: %s", BACKUP_PATH);
  }
  return true;
}
