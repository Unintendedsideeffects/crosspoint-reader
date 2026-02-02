#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MarkdownAST.h"

class MarkdownParser {
 public:
  static constexpr size_t MAX_INPUT_SIZE = 512 * 1024;  // 512KB
  static constexpr size_t MAX_AST_NODES = 10000;
  static constexpr size_t MAX_NESTING_DEPTH = 50;

  // Parse markdown text and return AST root (Document node)
  // Returns nullptr on parse failure
  std::unique_ptr<MdNode> parse(const std::string& markdown);

  // Parse with Obsidian preprocessing (frontmatter, comments, callouts, wikilinks)
  std::unique_ptr<MdNode> parseWithPreprocessing(const std::string& markdown);

 private:
  // Parser state during md4c callbacks
  std::unique_ptr<MdNode> root;
  std::vector<MdNode*> nodeStack;  // Path from root to current node
  size_t nodeCount = 0;
  bool limitExceeded = false;

  // md4c callback trampolines (static to match C callback signature)
  static int enterBlockCallback(unsigned type, void* detail, void* userdata);
  static int leaveBlockCallback(unsigned type, void* detail, void* userdata);
  static int enterSpanCallback(unsigned type, void* detail, void* userdata);
  static int leaveSpanCallback(unsigned type, void* detail, void* userdata);
  static int textCallback(unsigned type, const char* text, unsigned size, void* userdata);

  // Instance methods called by trampolines
  int onEnterBlock(unsigned type, void* detail);
  int onLeaveBlock(unsigned type, void* detail);
  int onEnterSpan(unsigned type, void* detail);
  int onLeaveSpan(unsigned type, void* detail);
  int onText(unsigned type, const char* text, unsigned size);

  // Helper to get current node (top of stack)
  MdNode* currentNode();

  // Push a new node onto stack as child of current, return pointer to it
  MdNode* pushNode(std::unique_ptr<MdNode> node);

  // Pop current node from stack
  void popNode();

  // Limits helpers
  void setLimitExceeded(const char* reason);
  bool checkNodeLimit();
  bool checkDepthLimit(size_t nextDepth);
  bool appendChildNode(MdNode* parent, std::unique_ptr<MdNode> node);
  bool appendTextNode(MdNode* parent, std::string text);

  // Preprocessing helpers (shared with Markdown.cpp but reimplemented here)
  static std::string preprocessMarkdown(const std::string& input);
  static std::string stripFrontmatter(const std::string& content);
  static std::string stripComments(const std::string& content);
  static std::string processLine(const std::string& line);
  static std::string processInline(const std::string& line);
  static std::string stripBlockId(const std::string& line);
  static std::string formatCalloutLine(const std::string& line);
  static bool isFenceStart(const std::string& line, std::string& fence);
  static bool isFenceEnd(const std::string& line, const std::string& fence);
};
