#pragma once
#include <iosfwd>
#include <string>

class CrossPointState {
 public:
  std::string openEpubPath;
  ~CrossPointState() = default;

  bool saveToFile() const;

  bool loadFromFile();
};
