#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lib/FsHelpers/FsHelpers.h"
#include "lib/Markdown/MarkdownParser.h"
#include "src/activities/todo/TodoPlannerStorage.h"
#include "src/util/InputValidation.h"
#include "test/mock/Arduino.h"

// Mock ESP implementation
MockESP ESP;

void testPathNormalisation() {
  std::cout << "Testing Path Normalisation..." << std::endl;
  assert(FsHelpers::normalisePath("/a/b/c") == "a/b/c");
  assert(FsHelpers::normalisePath("a/b/../c") == "a/c");
  assert(FsHelpers::normalisePath("a/b/../../c") == "c");
  assert(FsHelpers::normalisePath("///a//b/") == "a/b");
  assert(FsHelpers::normalisePath("test/../test/dir") == "test/dir");
  assert(FsHelpers::normalisePath("a/b/.") == "a/b");
  assert(FsHelpers::normalisePath("a/b/..") == "a");
  assert(FsHelpers::normalisePath("a/./b/../c/.") == "a/c");
  std::cout << "Path Normalisation tests passed!" << std::endl;
}

void testMarkdownLimits() {
  std::cout << "Testing Markdown Parser Limits..." << std::endl;
  MarkdownParser parser;

  // Test input size limit
  std::cout << "  Testing input size limit..." << std::endl;
  std::string largeInput(MarkdownParser::MAX_INPUT_SIZE + 1, 'a');
  auto resultLarge = parser.parse(largeInput);
  assert(resultLarge == nullptr);

  // Test nesting depth limit
  std::cout << "  Testing nesting depth limit..." << std::endl;
  std::string deepNesting = "";
  // Each "> " increases depth. MarkdownParser::MAX_NESTING_DEPTH = 50.
  for (int i = 0; i < 60; ++i) {
    deepNesting += "> ";
  }
  deepNesting += "Deep";
  auto resultDeep = parser.parse(deepNesting);
  assert(resultDeep == nullptr);

  // Test node count limit
  std::cout << "  Testing node count limit..." << std::endl;
  std::string manyNodes = "";
  // Each "p\n\n" creates a paragraph and a text node (at least).
  // MarkdownParser::MAX_AST_NODES = 10000.
  for (int i = 0; i < 6000; ++i) {
    manyNodes += "p\n\n";
  }
  auto resultMany = parser.parse(manyNodes);
  assert(resultMany == nullptr);

  std::cout << "Markdown Parser Limits tests passed!" << std::endl;
}

void testTodoPlannerStorageSelection() {
  std::cout << "Testing TODO Planner storage selection..." << std::endl;
  const std::string isoDate = "2026-02-17";
  const std::string alternateDate = "17.02.2026";
  assert(TodoPlannerStorage::dailyPath(isoDate, true, true, false) == "/daily/2026-02-17.md");
  assert(TodoPlannerStorage::dailyPath(isoDate, false, false, true) == "/daily/2026-02-17.txt");
  assert(TodoPlannerStorage::dailyPath(isoDate, false, true, false) == "/daily/2026-02-17.md");
  assert(TodoPlannerStorage::dailyPath(isoDate, true, false, false) == "/daily/2026-02-17.md");
  assert(TodoPlannerStorage::dailyPath(isoDate, false, false, false) == "/daily/2026-02-17.txt");
  assert(TodoPlannerStorage::dailyPath(alternateDate, false, false, false) == "/daily/17.02.2026.txt");
  assert(TodoPlannerStorage::formatEntry("Task", false) == "- [ ] Task");
  assert(TodoPlannerStorage::formatEntry("Agenda item", true) == "Agenda item");
  std::cout << "TODO Planner storage selection tests passed!" << std::endl;
}

void testInputValidation() {
  std::cout << "Testing input validation hardening..." << std::endl;

  size_t index = 0;
  assert(!InputValidation::findAsciiControlChar("/ok/path", 8, index));

  const std::string hasNewline = "/bad\npath";
  assert(InputValidation::findAsciiControlChar(hasNewline.c_str(), hasNewline.size(), index));
  assert(index == 4);

  const std::string hasDel = std::string("abc") + static_cast<char>(0x7F) + "def";
  assert(InputValidation::findAsciiControlChar(hasDel.c_str(), hasDel.size(), index));
  assert(index == 3);

  const std::string hasNull = std::string("ab\0cd", 5);
  assert(InputValidation::findAsciiControlChar(hasNull.c_str(), hasNull.size(), index));
  assert(index == 2);

  size_t parsed = 0;
  assert(InputValidation::parseStrictPositiveSize("1", 1, 100, parsed) && parsed == 1);
  assert(InputValidation::parseStrictPositiveSize("512", 3, 512, parsed) && parsed == 512);
  assert(InputValidation::parseStrictPositiveSize("0010", 4, 100, parsed) && parsed == 10);
  assert(InputValidation::parseStrictPositiveSize("536870912", 9, 536870912ULL, parsed) && parsed == 536870912ULL);

  assert(!InputValidation::parseStrictPositiveSize("", 0, 100, parsed));
  assert(!InputValidation::parseStrictPositiveSize("0", 1, 100, parsed));
  assert(!InputValidation::parseStrictPositiveSize("101", 3, 100, parsed));
  assert(!InputValidation::parseStrictPositiveSize("536870913", 9, 536870912ULL, parsed));
  assert(!InputValidation::parseStrictPositiveSize("12a", 3, 100, parsed));
  assert(!InputValidation::parseStrictPositiveSize(nullptr, 0, 100, parsed));

  const std::string huge = "184467440737095516161844674407370955161";
  assert(!InputValidation::parseStrictPositiveSize(huge.c_str(), huge.size(), static_cast<size_t>(-1), parsed));

  std::cout << "Input validation hardening tests passed!" << std::endl;
}

int main() {
  testPathNormalisation();
  testMarkdownLimits();
  testTodoPlannerStorageSelection();
  testInputValidation();
  std::cout << "All Host Tests Passed!" << std::endl;
  return 0;
}
