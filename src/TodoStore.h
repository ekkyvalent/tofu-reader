#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct TodoItem {
  std::string text;
  bool done = false;
};

/**
 * Singleton list of short todo items, persisted to the SD card as JSON.
 *
 * The bounds are deliberate: the sleep screen widget copies items into fixed
 * stack buffers, so an unbounded item count or text length would be a stack
 * problem. addItem() enforces both; fromJson() re-applies them so a
 * hand-edited file cannot violate the invariants.
 */
class TodoStore : public PersistableStore<TodoStore> {
 public:
  static constexpr size_t kMaxItems = 16;
  static constexpr size_t kMaxTextBytes = 64;

 private:
  std::vector<TodoItem> items;

  TodoStore() = default;
  friend class PersistableStore<TodoStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/todos.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Rejects blank text and a full list. Text longer than kMaxTextBytes is
  // truncated on a UTF-8 boundary rather than rejected.
  bool addItem(const std::string& text);
  bool updateItem(size_t index, const std::string& text);
  bool setDone(size_t index, bool done);
  bool removeItem(size_t index);

  const std::vector<TodoItem>& getItems() const { return items; }
  size_t undoneCount() const;
};

#define TODO_STORE TodoStore::getInstance()
