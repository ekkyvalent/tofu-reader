#include "TodoStore.h"

#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

// Truncates to at most maxBytes without splitting a UTF-8 sequence: if the
// first dropped byte is a continuation byte, walk back to the sequence's lead
// byte and drop the whole character.
std::string truncateToBytes(const std::string& text, size_t maxBytes) {
  if (text.size() <= maxBytes) return text;
  size_t end = maxBytes;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
    --end;
  }
  return text.substr(0, end);
}

bool isBlank(const std::string& text) {
  return std::all_of(text.begin(), text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

}  // namespace

void TodoStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["items"].to<JsonArray>();
  for (const auto& item : items) {
    JsonObject obj = arr.add<JsonObject>();
    obj["text"] = item.text;
    obj["done"] = item.done;
  }
}

bool TodoStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'items' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  items.clear();
  JsonArrayConst arr = doc["items"].as<JsonArrayConst>();
  items.reserve(std::min(arr.size(), kMaxItems));
  for (JsonObjectConst obj : arr) {
    if (items.size() >= kMaxItems) break;
    TodoItem item;
    item.text = truncateToBytes(obj["text"] | "", kMaxTextBytes);
    if (isBlank(item.text)) continue;
    item.done = obj["done"] | false;
    items.push_back(std::move(item));
  }

  LOG_DBG("TODO", "Loaded %zu todo items", items.size());
  return true;
}

bool TodoStore::addItem(const std::string& text) {
  if (items.size() >= kMaxItems) {
    LOG_DBG("TODO", "Cannot add todo item, limit of %zu reached", kMaxItems);
    return false;
  }

  std::string bounded = truncateToBytes(text, kMaxTextBytes);
  if (isBlank(bounded)) {
    LOG_DBG("TODO", "Rejected blank todo item");
    return false;
  }

  items.push_back(TodoItem{std::move(bounded), false});
  LOG_DBG("TODO", "Added todo item: %s", items.back().text.c_str());
  return saveToFile();
}

bool TodoStore::updateItem(size_t index, const std::string& text) {
  if (index >= items.size()) {
    return false;
  }

  std::string bounded = truncateToBytes(text, kMaxTextBytes);
  if (isBlank(bounded)) {
    LOG_DBG("TODO", "Rejected blank todo item");
    return false;
  }
  if (items[index].text == bounded) return true;  // unchanged: skip the SD write

  items[index].text = std::move(bounded);
  LOG_DBG("TODO", "Updated todo item: %s", items[index].text.c_str());
  return saveToFile();
}

bool TodoStore::setDone(size_t index, bool done) {
  if (index >= items.size()) {
    return false;
  }
  if (items[index].done == done) return true;  // unchanged: skip the SD write

  items[index].done = done;
  return saveToFile();
}

bool TodoStore::removeItem(size_t index) {
  if (index >= items.size()) {
    return false;
  }

  LOG_DBG("TODO", "Removed todo item: %s", items[index].text.c_str());
  items.erase(items.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

bool TodoStore::moveItem(size_t from, size_t to) {
  if (from >= items.size() || to >= items.size()) {
    return false;
  }
  if (from == to) return true;  // unchanged: skip the SD write

  TodoItem moved = std::move(items[from]);
  items.erase(items.begin() + static_cast<ptrdiff_t>(from));
  items.insert(items.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
  LOG_DBG("TODO", "Moved todo item to position %zu: %s", to, items[to].text.c_str());
  return saveToFile();
}

size_t TodoStore::undoneCount() const {
  return static_cast<size_t>(
      std::count_if(items.begin(), items.end(), [](const TodoItem& item) { return !item.done; }));
}
