#pragma once
#include <iosfwd>
#include <string>

class CrossPointState {
  void serialize(std::ostream& os) const;
  static CrossPointState* deserialize(std::istream& is);

 public:
  std::string openEpubPath;
  ~CrossPointState() = default;
  void saveToFile() const;
  static CrossPointState* loadFromFile();
};
