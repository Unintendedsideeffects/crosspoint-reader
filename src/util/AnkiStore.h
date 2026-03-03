#pragma once

#include <string>
#include <vector>

namespace util {

struct AnkiCard {
  std::string front;
  std::string back;
  std::string context;  // e.g. the book title or surrounding text
  uint32_t timestamp;   // when it was added
};

class AnkiStore {
 public:
  static AnkiStore& getInstance();

  bool load();
  bool save() const;

  const std::vector<AnkiCard>& getCards() const { return cards; }
  void addCard(const AnkiCard& card);
  void removeCard(size_t index);
  void clear();

  size_t count() const { return cards.size(); }

 private:
  AnkiStore() = default;
  std::vector<AnkiCard> cards;
  static constexpr const char* kFilePath = "/.crosspoint/anki_cards.json";
};

}  // namespace util
