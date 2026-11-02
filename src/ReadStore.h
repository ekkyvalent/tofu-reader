#pragma once

#include <cstdint>
#include <string>
#include <vector>

// True when the path is under the legacy /read/ folder. Folder membership is
// an implicit "read" state and wins over anything stored.
bool isInReadFolder(const std::string& path);

// Persisted read / not-read state for books, keyed by
// library::clixPathHash(complete path). Loaded lazily on the first query.
class ReadStore {
 public:
  static ReadStore& getInstance();
  static const char* getFilePath() { return "/.crosspoint/read.bin"; }

  // true when the path is under /read/ (legacy override) or its hash is stored.
  bool isRead(const std::string& path);
  // No-op and returns true when the state already matches (no SD write).
  bool markRead(const std::string& path, bool read);
  bool rekey(const std::string& oldPath, const std::string& newPath);
  bool erase(const std::string& path);

#ifdef CROSSPOINT_UNIT_TESTS
  // Test-only: drop the in-memory copy so the next query re-reads the file.
  void resetForTests();
#endif

 private:
  ReadStore() = default;

  void ensureLoaded();
  void load();
  // Whole save in one function so a later reader task can wrap it.
  bool save();
  bool validateNewFile() const;
  bool contains(uint64_t hash) const;
  bool insert(uint64_t hash);
  void removeHash(uint64_t hash);

  std::vector<uint64_t> hashes;
  bool loaded = false;
};

#define READ_STORE ReadStore::getInstance()
