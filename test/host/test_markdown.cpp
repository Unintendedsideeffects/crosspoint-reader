#include "doctest/doctest.h"
#include "lib/Markdown/MarkdownParser.h"
#include <string>

TEST_CASE("testMarkdownLimits") {
  MarkdownParser parser;
  std::string largeInput(MarkdownParser::MAX_INPUT_SIZE + 1, 'a');
  auto resultLarge = parser.parse(largeInput);
  CHECK(resultLarge == nullptr);
  std::string deepNesting = "";
  for (int i = 0; i < 60; ++i) {
    deepNesting += "> ";
  }
  deepNesting += "Deep";
  auto resultDeep = parser.parse(deepNesting);
  CHECK(resultDeep == nullptr);
  std::string manyNodes = "";
  for (int i = 0; i < 6000; ++i) {
    manyNodes += "p\n\n";
  }
  auto resultMany = parser.parse(manyNodes);
  CHECK(resultMany == nullptr);
}
