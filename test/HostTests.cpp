#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lib/FsHelpers/FsHelpers.h"
#include "lib/Markdown/MarkdownParser.h"
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

int main() {
  testPathNormalisation();
  testMarkdownLimits();
  std::cout << "All Host Tests Passed!" << std::endl;
  return 0;
}
