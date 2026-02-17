#include "MarkdownParser.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>

// Helper to extract string from MD_ATTRIBUTE
static std::string attributeToString(const MD_ATTRIBUTE& attr) {
  if (attr.text == nullptr || attr.size == 0) {
    return "";
  }
  return std::string(attr.text, attr.size);
}

// Convert md4c alignment to our enum
static MdAlign convertAlign(MD_ALIGN align) {
  switch (align) {
    case MD_ALIGN_LEFT:
      return MdAlign::Left;
    case MD_ALIGN_CENTER:
      return MdAlign::Center;
    case MD_ALIGN_RIGHT:
      return MdAlign::Right;
    default:
      return MdAlign::Default;
  }
}

static bool hasImageExtension(const std::string& target) {
  const auto dot = target.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = target.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "gif" || ext == "webp";
}

static std::string formatLinkTarget(const std::string& target) {
  if (target.find(' ') != std::string::npos) {
    return "<" + target + ">";
  }
  return target;
}

static std::string encodeUtf8Codepoint(uint32_t cp) {
  std::string encoded;
  if (cp <= 0x7F) {
    encoded.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    encoded.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    encoded.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    encoded.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    encoded.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    encoded.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    encoded.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    encoded.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    encoded.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    encoded.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return encoded;
}

std::unique_ptr<MdNode> MarkdownParser::parse(const std::string& markdown) {
  if (markdown.size() > MAX_INPUT_SIZE) {
    LOG_ERR("MD", "Parse failed: input size %zu exceeds limit %zu", markdown.size(), MAX_INPUT_SIZE);
    return nullptr;
  }

  const size_t freeHeap = ESP.getFreeHeap();
  constexpr size_t kHeapHeadroomBytes = 64 * 1024;
  if (freeHeap < markdown.size() + kHeapHeadroomBytes) {
    LOG_ERR("MD", "Parse failed: low heap (%zu free, need >= %zu)", freeHeap, markdown.size() + kHeapHeadroomBytes);
    return nullptr;
  }

  root = MdNode::createDocument();
  nodeStack.clear();
  nodeStack.push_back(root.get());
  nodeCount = 1;
  limitExceeded = false;

  MD_PARSER parser = {};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS;
  parser.enter_block = enterBlockCallback;
  parser.leave_block = leaveBlockCallback;
  parser.enter_span = enterSpanCallback;
  parser.leave_span = leaveSpanCallback;
  parser.text = textCallback;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  int result = md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, this);

  nodeStack.clear();

  if (result != 0 || limitExceeded) {
    root.reset();
    return nullptr;
  }

  return std::move(root);
}

std::unique_ptr<MdNode> MarkdownParser::parseWithPreprocessing(const std::string& markdown) {
  if (markdown.size() > MAX_INPUT_SIZE) {
    LOG_ERR("MD", "Preprocess failed: input size %zu exceeds limit %zu", markdown.size(), MAX_INPUT_SIZE);
    return nullptr;
  }
  std::string processed = preprocessMarkdown(markdown);
  return parse(processed);
}

MdNode* MarkdownParser::currentNode() {
  if (nodeStack.empty()) {
    return nullptr;
  }
  return nodeStack.back();
}

MdNode* MarkdownParser::pushNode(std::unique_ptr<MdNode> node) {
  MdNode* ptr = node.get();
  MdNode* parent = currentNode();
  const size_t nextDepth = nodeStack.size() + 1;
  if (!checkDepthLimit(nextDepth)) {
    return nullptr;
  }
  if (parent) {
    if (!appendChildNode(parent, std::move(node))) {
      return nullptr;
    }
  }
  nodeStack.push_back(ptr);
  return ptr;
}

void MarkdownParser::popNode() {
  if (!nodeStack.empty() && nodeStack.size() > 1) {
    nodeStack.pop_back();
  }
}

// Static callback trampolines
int MarkdownParser::enterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onEnterBlock(type, detail);
}

int MarkdownParser::leaveBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onLeaveBlock(type, detail);
}

int MarkdownParser::enterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onEnterSpan(type, detail);
}

int MarkdownParser::leaveSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onLeaveSpan(type, detail);
}

int MarkdownParser::textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
  return static_cast<MarkdownParser*>(userdata)->onText(type, text, size);
}

int MarkdownParser::onEnterBlock(MD_BLOCKTYPE type, void* detail) {
  if (limitExceeded) {
    return -1;
  }
  switch (type) {
    case MD_BLOCK_DOC:
      // Document already created as root
      break;

    case MD_BLOCK_QUOTE:
      if (!pushNode(MdNode::createBlockquote())) {
        return -1;
      }
      break;

    case MD_BLOCK_UL: {
      auto* d = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
      if (!pushNode(MdNode::createUnorderedList(d->is_tight != 0, d->mark))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_OL: {
      auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
      if (!pushNode(MdNode::createOrderedList(d->is_tight != 0, d->mark_delimiter, d->start))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_LI: {
      auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
      if (!pushNode(MdNode::createListItem(d->is_task != 0, d->task_mark == 'x' || d->task_mark == 'X'))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_HR:
      if (!pushNode(MdNode::createHorizontalRule())) {
        return -1;
      }
      break;

    case MD_BLOCK_H: {
      auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
      if (!pushNode(MdNode::createHeading(static_cast<uint8_t>(d->level)))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_CODE: {
      auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
      std::string lang = attributeToString(d->lang);
      if (!pushNode(MdNode::createCodeBlock(lang, d->fence_char))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_HTML:
      if (!pushNode(MdNode::createHtmlBlock(""))) {
        return -1;
      }
      break;

    case MD_BLOCK_P:
      if (!pushNode(MdNode::createParagraph())) {
        return -1;
      }
      break;

    case MD_BLOCK_TABLE: {
      auto* d = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
      if (!pushNode(MdNode::createTable(d->col_count, d->head_row_count, d->body_row_count))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_THEAD:
      if (!pushNode(MdNode::createTableHead())) {
        return -1;
      }
      break;

    case MD_BLOCK_TBODY:
      if (!pushNode(MdNode::createTableBody())) {
        return -1;
      }
      break;

    case MD_BLOCK_TR:
      if (!pushNode(MdNode::createTableRow())) {
        return -1;
      }
      break;

    case MD_BLOCK_TH: {
      auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
      if (!pushNode(MdNode::createTableCell(convertAlign(d->align), true))) {
        return -1;
      }
      break;
    }

    case MD_BLOCK_TD: {
      auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
      if (!pushNode(MdNode::createTableCell(convertAlign(d->align), false))) {
        return -1;
      }
      break;
    }

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onLeaveBlock(MD_BLOCKTYPE type, void* detail) {
  (void)detail;

  if (limitExceeded) {
    return -1;
  }

  switch (type) {
    case MD_BLOCK_DOC:
      // Don't pop root
      break;

    case MD_BLOCK_QUOTE:
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
    case MD_BLOCK_LI:
    case MD_BLOCK_HR:
    case MD_BLOCK_H:
    case MD_BLOCK_CODE:
    case MD_BLOCK_HTML:
    case MD_BLOCK_P:
    case MD_BLOCK_TABLE:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
    case MD_BLOCK_TR:
    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
      popNode();
      break;

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onEnterSpan(MD_SPANTYPE type, void* detail) {
  if (limitExceeded) {
    return -1;
  }
  switch (type) {
    case MD_SPAN_EM:
      if (!pushNode(MdNode::createEmphasis())) {
        return -1;
      }
      break;

    case MD_SPAN_STRONG:
      if (!pushNode(MdNode::createStrong())) {
        return -1;
      }
      break;

    case MD_SPAN_A: {
      auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
      if (!pushNode(MdNode::createLink(attributeToString(d->href), attributeToString(d->title), d->is_autolink != 0))) {
        return -1;
      }
      break;
    }

    case MD_SPAN_IMG: {
      auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
      if (!pushNode(MdNode::createImage(attributeToString(d->src), attributeToString(d->title)))) {
        return -1;
      }
      break;
    }

    case MD_SPAN_CODE:
      if (!pushNode(MdNode::createCode(""))) {
        return -1;
      }
      break;

    case MD_SPAN_DEL:
      if (!pushNode(MdNode::createStrikethrough())) {
        return -1;
      }
      break;

    case MD_SPAN_LATEXMATH:
      if (!pushNode(MdNode::createLatexMath("", false))) {
        return -1;
      }
      break;

    case MD_SPAN_LATEXMATH_DISPLAY:
      if (!pushNode(MdNode::createLatexMath("", true))) {
        return -1;
      }
      break;

    case MD_SPAN_WIKILINK: {
      auto* d = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail);
      std::string target = attributeToString(d->target);
      std::string alias;
      const size_t pipePos = target.find('|');
      if (pipePos != std::string::npos) {
        alias = target.substr(pipePos + 1);
        target = target.substr(0, pipePos);
      }
      if (!pushNode(MdNode::createWikiLink(target, alias))) {
        return -1;
      }
      break;
    }

    case MD_SPAN_U:
      // Treat underline as emphasis for now
      if (!pushNode(MdNode::createEmphasis())) {
        return -1;
      }
      break;

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onLeaveSpan(MD_SPANTYPE type, void* detail) {
  (void)detail;

  if (limitExceeded) {
    return -1;
  }

  switch (type) {
    case MD_SPAN_EM:
    case MD_SPAN_STRONG:
    case MD_SPAN_A:
    case MD_SPAN_IMG:
    case MD_SPAN_CODE:
    case MD_SPAN_DEL:
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
    case MD_SPAN_WIKILINK:
    case MD_SPAN_U:
      popNode();
      break;

    default:
      break;
  }
  return 0;
}

int MarkdownParser::onText(MD_TEXTTYPE type, const char* text, MD_SIZE size) {
  MdNode* current = currentNode();
  if (!current) {
    return 0;
  }
  if (limitExceeded) {
    return -1;
  }

  std::string content(text, size);

  if (type == MD_TEXT_HTML && (!current || current->type != MdNodeType::HtmlBlock)) {
    if (content == "<mark>") {
      if (!pushNode(MdNode::createHighlight())) {
        return -1;
      }
      return 0;
    }
    if (content == "</mark>") {
      if (current && current->type == MdNodeType::Highlight) {
        popNode();
      }
      return 0;
    }
    if (content == "<sub>") {
      if (!pushNode(MdNode::createSubscript())) {
        return -1;
      }
      return 0;
    }
    if (content == "</sub>") {
      if (current && current->type == MdNodeType::Subscript) {
        popNode();
      }
      return 0;
    }
    if (content == "<sup>") {
      if (!pushNode(MdNode::createSuperscript())) {
        return -1;
      }
      return 0;
    }
    if (content == "</sup>") {
      if (current && current->type == MdNodeType::Superscript) {
        popNode();
      }
      return 0;
    }
  }

  switch (type) {
    case MD_TEXT_NORMAL:
      if (!appendTextNode(current, std::move(content))) {
        return -1;
      }
      break;

    case MD_TEXT_NULLCHAR:
      if (!appendTextNode(current, "\xEF\xBF\xBD")) {  // U+FFFD
        return -1;
      }
      break;

    case MD_TEXT_BR:
      if (!appendChildNode(current, MdNode::createHardBreak())) {
        return -1;
      }
      break;

    case MD_TEXT_SOFTBR:
      if (!appendChildNode(current, MdNode::createSoftBreak())) {
        return -1;
      }
      break;

    case MD_TEXT_ENTITY: {
      std::string decodedContent;
      if (content == "&amp;") {
        decodedContent = "&";
      } else if (content == "&lt;") {
        decodedContent = "<";
      } else if (content == "&gt;") {
        decodedContent = ">";
      } else if (content == "&quot;") {
        decodedContent = "\"";
      } else if (content == "&apos;") {
        decodedContent = "'";
      } else if (content == "&nbsp;") {
        decodedContent = " ";
      } else if (content.size() > 3 && content[1] == '#') {
        try {
          int base = 10;
          int start = 2;
          if (content[2] == 'x' || content[2] == 'X') {
            base = 16;
            start = 3;
          }
          std::string numStr = content.substr(start, content.size() - start - 1);
          unsigned long num = std::stoul(numStr, nullptr, base);
          if (num <= 0x10FFFF) {
            decodedContent = encodeUtf8Codepoint(static_cast<uint32_t>(num));
          }
        } catch (const std::exception& e) {
          // Ignore malformed numeric entities
          LOG_WRN("MD", "Malformed numeric entity: %s", content.c_str());
        }
      }

      if (decodedContent.empty()) {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      } else {
        if (!appendTextNode(current, std::move(decodedContent))) {
          return -1;
        }
      }
      break;
    }

    case MD_TEXT_CODE:
      // For inline code spans, append to the Code node's text
      if (current->type == MdNodeType::Code || current->type == MdNodeType::CodeBlock) {
        current->text += content;
      } else {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      }
      break;

    case MD_TEXT_HTML:
      // Raw HTML - append to HtmlBlock node
      if (current->type == MdNodeType::HtmlBlock) {
        current->text += content;
      } else {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      }
      break;

    case MD_TEXT_LATEXMATH:
      // LaTeX math content
      if (current->type == MdNodeType::LatexMath || current->type == MdNodeType::LatexMathDisplay) {
        current->text += content;
      } else {
        if (!appendTextNode(current, std::move(content))) {
          return -1;
        }
      }
      break;

    default:
      if (!appendTextNode(current, std::move(content))) {
        return -1;
      }
      break;
  }

  return 0;
}

void MarkdownParser::setLimitExceeded(const char* reason) {
  if (limitExceeded) {
    return;
  }
  limitExceeded = true;
  LOG_ERR("MD", "Parse aborted: %s", reason);
}

bool MarkdownParser::checkNodeLimit() {
  if (nodeCount >= MAX_AST_NODES) {
    setLimitExceeded("AST node limit exceeded");
    return false;
  }
  return true;
}

bool MarkdownParser::checkDepthLimit(size_t nextDepth) {
  if (nextDepth > MAX_NESTING_DEPTH) {
    setLimitExceeded("AST nesting depth limit exceeded");
    return false;
  }
  return true;
}

bool MarkdownParser::appendChildNode(MdNode* parent, std::unique_ptr<MdNode> node) {
  if (!parent) {
    setLimitExceeded("Null parent while appending node");
    return false;
  }
  if (!checkNodeLimit()) {
    return false;
  }
  parent->appendChild(std::move(node));
  nodeCount++;
  return true;
}

bool MarkdownParser::appendTextNode(MdNode* parent, std::string text) {
  if (!parent) {
    setLimitExceeded("Null parent while appending text");
    return false;
  }
  if (text.empty()) {
    return true;
  }

  if (!parent->children.empty()) {
    MdNode* last = parent->children.back().get();
    if (last->type == MdNodeType::Text) {
      last->text += text;
      return true;
    }
  }

  return appendChildNode(parent, MdNode::createText(text));
}

// Preprocessing implementation
std::string MarkdownParser::preprocessMarkdown(const std::string& input) {
  std::string content = stripFrontmatter(input);
  content = stripComments(content);

  std::string output;
  output.reserve(content.size() + 64);

  bool inFence = false;
  std::string fence;

  size_t start = 0;
  while (start <= content.size()) {
    size_t end = content.find('\n', start);
    bool hasNewline = end != std::string::npos;
    size_t lineLen = hasNewline ? (end - start) : (content.size() - start);
    std::string line = content.substr(start, lineLen);

    if (!inFence) {
      std::string newFence;
      if (isFenceStart(line, newFence)) {
        inFence = true;
        fence = newFence;
        output.append(line);
      } else {
        std::string processedLine = processLine(line);
        output.append(processedLine);
      }
    } else {
      output.append(line);
      if (isFenceEnd(line, fence)) {
        inFence = false;
        fence.clear();
      }
    }

    if (hasNewline) {
      output.push_back('\n');
      start = end + 1;
    } else {
      break;
    }
  }

  return output;
}

std::string MarkdownParser::stripFrontmatter(const std::string& content) {
  size_t lineEnd = content.find('\n');
  if (lineEnd == std::string::npos) {
    return content;
  }

  std::string firstLine = content.substr(0, lineEnd);
  if (firstLine != "---" && firstLine != "---\r") {
    return content;
  }

  size_t pos = lineEnd + 1;
  while (pos < content.size()) {
    lineEnd = content.find('\n', pos);
    size_t len = (lineEnd == std::string::npos) ? content.size() - pos : lineEnd - pos;
    std::string line = content.substr(pos, len);
    if (line == "---" || line == "---\r") {
      if (lineEnd == std::string::npos) {
        return "";
      }
      return content.substr(lineEnd + 1);
    }
    if (lineEnd == std::string::npos) {
      break;
    }
    pos = lineEnd + 1;
  }

  return content;
}

std::string MarkdownParser::stripComments(const std::string& content) {
  std::string output;
  output.reserve(content.size());
  size_t i = 0;
  while (i < content.size()) {
    if (i + 1 < content.size() && content[i] == '%' && content[i + 1] == '%') {
      size_t end = content.find("%%", i + 2);
      if (end == std::string::npos) {
        break;
      }
      i = end + 2;
      continue;
    }
    output.push_back(content[i]);
    i++;
  }
  return output;
}

std::string MarkdownParser::processLine(const std::string& line) {
  std::string trimmed = line;
  if (!trimmed.empty() && trimmed.back() == '\r') {
    trimmed.pop_back();
  }
  std::string formatted = formatCalloutLine(trimmed);
  formatted = processInline(formatted);
  formatted = stripBlockId(formatted);
  return formatted;
}

std::string MarkdownParser::formatCalloutLine(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    i++;
  }
  if (i >= line.size() || line[i] != '>') {
    return line;
  }
  size_t j = i + 1;
  while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
    j++;
  }
  if (j + 2 >= line.size() || line[j] != '[' || line[j + 1] != '!') {
    return line;
  }
  size_t typeStart = j + 2;
  size_t typeEnd = line.find(']', typeStart);
  if (typeEnd == std::string::npos) {
    return line;
  }
  std::string type = line.substr(typeStart, typeEnd - typeStart);
  if (type.empty()) {
    return line;
  }
  size_t titleStart = typeEnd + 1;
  if (titleStart < line.size() && (line[titleStart] == '-' || line[titleStart] == '+')) {
    titleStart++;
  }
  while (titleStart < line.size() && (line[titleStart] == ' ' || line[titleStart] == '\t')) {
    titleStart++;
  }
  std::string title = line.substr(titleStart);

  std::string out;
  out.reserve(line.size() + 8);
  out.append(line.substr(0, i + 1));
  out.append(" **");
  out.append(type);
  out.append("**");
  if (!title.empty()) {
    out.push_back(' ');
    out.append(title);
  }
  return out;
}

std::string MarkdownParser::processInline(const std::string& line) {
  std::string out;
  out.reserve(line.size());
  bool inCode = false;
  size_t codeFence = 0;

  size_t i = 0;
  while (i < line.size()) {
    if (line[i] == '`') {
      size_t tickCount = 0;
      while (i + tickCount < line.size() && line[i + tickCount] == '`') {
        tickCount++;
      }
      if (!inCode) {
        inCode = true;
        codeFence = tickCount;
      } else if (tickCount == codeFence) {
        inCode = false;
        codeFence = 0;
      }
      out.append(line.substr(i, tickCount));
      i += tickCount;
      continue;
    }

    if (inCode) {
      out.push_back(line[i]);
      i++;
      continue;
    }

    if (i + 2 < line.size() && line[i] == '!' && line[i + 1] == '[' && line[i + 2] == '[') {
      const size_t end = line.find("]]", i + 3);
      if (end != std::string::npos) {
        std::string inner = line.substr(i + 3, end - (i + 3));
        std::string target = inner;
        std::string alias;
        const size_t pipePos = inner.find('|');
        if (pipePos != std::string::npos) {
          target = inner.substr(0, pipePos);
          alias = inner.substr(pipePos + 1);
        }
        if (hasImageExtension(target)) {
          out.append("![");
          out.append(alias);
          out.append("](");
          out.append(formatLinkTarget(target));
          out.append(")");
        } else {
          const std::string& label = alias.empty() ? target : alias;
          out.append("[");
          out.append(label);
          out.append("](");
          out.append(formatLinkTarget(target));
          out.append(")");
        }
        i = end + 2;
        continue;
      }
    }

    if (i + 1 < line.size() && line[i] == '[' && line[i + 1] == '[') {
      const size_t end = line.find("]]", i + 2);
      if (end != std::string::npos) {
        std::string inner = line.substr(i + 2, end - (i + 2));
        std::string target = inner;
        std::string alias;
        const size_t pipePos = inner.find('|');
        if (pipePos != std::string::npos) {
          target = inner.substr(0, pipePos);
          alias = inner.substr(pipePos + 1);
        }
        const std::string& label = alias.empty() ? target : alias;
        out.append("[");
        out.append(label);
        out.append("](");
        out.append(formatLinkTarget(target));
        out.append(")");
        i = end + 2;
        continue;
      }
    }

    // ==highlight== -> <mark>highlight</mark>
    if (i + 1 < line.size() && line[i] == '=' && line[i + 1] == '=') {
      size_t end = line.find("==", i + 2);
      if (end != std::string::npos && end > i + 2) {
        std::string inner = line.substr(i + 2, end - (i + 2));
        out.append("<mark>");
        out.append(inner);
        out.append("</mark>");
        i = end + 2;
        continue;
      }
    }

    // ~subscript~ -> <sub>subscript</sub>
    if (line[i] == '~' && (i + 1 < line.size()) && line[i + 1] != '~') {
      size_t end = line.find('~', i + 1);
      if (end != std::string::npos && end > i + 1) {
        std::string inner = line.substr(i + 1, end - (i + 1));
        if (inner.find(' ') == std::string::npos) {
          out.append("<sub>");
          out.append(inner);
          out.append("</sub>");
          i = end + 1;
          continue;
        }
      }
    }

    // ^superscript^ -> <sup>superscript</sup>
    if (line[i] == '^') {
      size_t end = line.find('^', i + 1);
      if (end != std::string::npos && end > i + 1) {
        std::string inner = line.substr(i + 1, end - (i + 1));
        if (!inner.empty() && inner.find(' ') == std::string::npos) {
          out.append("<sup>");
          out.append(inner);
          out.append("</sup>");
          i = end + 1;
          continue;
        }
      }
    }

    out.push_back(line[i]);
    i++;
  }

  return out;
}

std::string MarkdownParser::stripBlockId(const std::string& line) {
  if (line.empty()) {
    return line;
  }

  size_t caret = line.find_last_of('^');
  if (caret == std::string::npos || caret == 0) {
    return line;
  }

  if (!isspace(static_cast<unsigned char>(line[caret - 1]))) {
    return line;
  }

  size_t i = caret + 1;
  if (i >= line.size()) {
    return line;
  }

  while (i < line.size()) {
    char c = line[i];
    if (c == ' ' || c == '\t' || c == '\r') {
      break;
    }
    if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
      return line;
    }
    i++;
  }

  size_t end = i;
  while (end < line.size() && (line[end] == ' ' || line[end] == '\t' || line[end] == '\r')) {
    end++;
  }

  size_t trim = caret - 1;
  while (trim > 0 && line[trim - 1] == ' ') {
    trim--;
  }

  return line.substr(0, trim) + line.substr(end);
}

bool MarkdownParser::isFenceStart(const std::string& line, std::string& fence) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i + 2 >= line.size()) {
    return false;
  }
  char ch = line[i];
  if (ch != '`' && ch != '~') {
    return false;
  }
  size_t count = 0;
  while (i + count < line.size() && line[i + count] == ch) {
    count++;
  }
  if (count < 3) {
    return false;
  }
  fence.assign(count, ch);
  return true;
}

bool MarkdownParser::isFenceEnd(const std::string& line, const std::string& fence) {
  if (fence.empty()) {
    return false;
  }
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i + fence.size() > line.size()) {
    return false;
  }
  for (size_t j = 0; j < fence.size(); j++) {
    if (line[i + j] != fence[j]) {
      return false;
    }
  }
  return true;
}
