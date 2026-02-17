#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lib/FsHelpers/FsHelpers.h"
#include "lib/Markdown/MarkdownParser.h"
#include "src/activities/todo/TodoPlannerStorage.h"
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
  assert(TodoPlannerStorage::dailyPath("2026-02-17", true, true, false) == "/daily/2026-02-17.md");
  assert(TodoPlannerStorage::dailyPath("2026-02-17", false, false, true) == "/daily/2026-02-17.txt");
  assert(TodoPlannerStorage::dailyPath("2026-02-17", false, true, false) == "/daily/2026-02-17.md");
  assert(TodoPlannerStorage::dailyPath("2026-02-17", true, false, false) == "/daily/2026-02-17.md");
  assert(TodoPlannerStorage::dailyPath("2026-02-17", false, false, false) == "/daily/2026-02-17.txt");
  assert(TodoPlannerStorage::formatEntry("Task", false) == "- [ ] Task");
  assert(TodoPlannerStorage::formatEntry("Agenda item", true) == "Agenda item");
  std::cout << "TODO Planner storage selection tests passed!" << std::endl;
}

int main() {
  testPathNormalisation();
  testMarkdownLimits();
  testTodoPlannerStorageSelection();
  std::cout << "All Host Tests Passed!" << std::endl;
  return 0;
}
