#include "AnkiStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace util {

AnkiStore& AnkiStore::getInstance() {
  static AnkiStore instance;
  return instance;
}

bool AnkiStore::load() {
  if (!Storage.exists(kFilePath)) {
    cards.clear();
    return true;
  }

  String json = Storage.readFile(kFilePath);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("ANKI", "Failed to parse cards JSON: %s", error.c_str());
    return false;
  }

  cards.clear();
  JsonArray arr = doc.as<JsonArray>();
  cards.reserve(arr.size());
  for (JsonObject obj : arr) {
    AnkiCard card;
    card.front = obj["f"] | "";
    card.back = obj["b"] | "";
    card.context = obj["c"] | "";
    card.timestamp = obj["t"] | 0;
    cards.push_back(std::move(card));
  }

  LOG_INF("ANKI", "Loaded %zu cards", cards.size());
  return true;
}

bool AnkiStore::save() const {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& card : cards) {
    JsonObject obj = arr.add<JsonObject>();
    obj["f"] = card.front;
    obj["b"] = card.back;
    obj["c"] = card.context;
    obj["t"] = card.timestamp;
  }

  String json;
  serializeJson(doc, json);

  Storage.mkdir("/.crosspoint");
  if (Storage.writeFile(kFilePath, json)) {
    LOG_INF("ANKI", "Saved %zu cards", cards.size());
    return true;
  }

  LOG_ERR("ANKI", "Failed to save cards to %s", kFilePath);
  return false;
}

void AnkiStore::addCard(const AnkiCard& card) {
  cards.push_back(card);
  save();
}

void AnkiStore::removeCard(size_t index) {
  if (index < cards.size()) {
    cards.erase(cards.begin() + index);
    save();
  }
}

void AnkiStore::clear() {
  cards.clear();
  save();
}

}  // namespace util
