#include "Markdown.h"

#include <SDCardManager.h>
#include <Serialization.h>
#include <ctype.h>

#include <algorithm>
#include <functional>
#include <string>

#include "MarkdownParser.h"

extern "C" {
#include <md4c-html.h>
}

namespace {
constexpr uint32_t META_MAGIC = 0x4D44544D;  // "MDTM"
constexpr uint8_t META_VERSION = 2;

uint32_t hashFileContents(const std::string& path) {
  FsFile file;
  if (!SdMan.openFileForRead("MD ", path, file)) {
    return 0;
  }

  uint32_t hash = 2166136261u;  // FNV-1a
  uint8_t buffer[512];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    for (size_t i = 0; i < readSize; i++) {
      hash ^= buffer[i];
      hash *= 16777619u;
    }
  }
  file.close();
  return hash;
}

struct HtmlOutput {
  FsFile* file;
  bool ok;
};

void writeHtmlChunk(const MD_CHAR* data, MD_SIZE size, void* userdata) {
  auto* out = static_cast<HtmlOutput*>(userdata);
  if (!out || !out->ok || !out->file) {
    return;
  }
  if (out->file->write(reinterpret_cast<const uint8_t*>(data), size) != size) {
    out->ok = false;
  }
}

bool hasImageExtension(const std::string& target) {
  const auto dot = target.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = target.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "gif" || ext == "webp";
}

bool isFenceStart(const std::string& line, std::string& fence) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    i++;
  }
  if (i + 2 >= line.size()) {
    return false;
  }
  const char ch = line[i];
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

bool isFenceEnd(const std::string& line, const std::string& fence) {
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
}  // namespace

Markdown::Markdown(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/md_" + std::to_string(hash);
}

bool Markdown::load() {
  if (loaded) {
    return true;
  }

  if (!SdMan.exists(filepath.c_str())) {
    Serial.printf("[%lu] [MD ] File does not exist: %s\n", millis(), filepath.c_str());
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    Serial.printf("[%lu] [MD ] Failed to open file: %s\n", millis(), filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  Serial.printf("[%lu] [MD ] Loaded markdown file: %s (%zu bytes)\n", millis(), filepath.c_str(), fileSize);
  return true;
}

std::string Markdown::getTitle() const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  if (filename.length() >= 3 && filename.substr(filename.length() - 3) == ".md") {
    filename = filename.substr(0, filename.length() - 3);
  }

  return filename;
}

void Markdown::setupCacheDir() const {
  if (!SdMan.exists(cacheBasePath.c_str())) {
    SdMan.mkdir(cacheBasePath.c_str());
  }
  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }
}

std::string Markdown::getHtmlPath() const { return cachePath + "/content.xhtml"; }

std::string Markdown::getContentBasePath() const {
  const auto lastSlash = filepath.find_last_of('/');
  if (lastSlash == std::string::npos) {
    return "/";
  }
  return filepath.substr(0, lastSlash + 1);
}

bool Markdown::ensureHtml() {
  if (!loaded) {
    return false;
  }

  setupCacheDir();

  const std::string htmlPath = getHtmlPath();
  const std::string metaPath = cachePath + "/meta.bin";

  bool needsRender = true;
  if (SdMan.exists(htmlPath.c_str()) && SdMan.exists(metaPath.c_str())) {
    FsFile metaFile;
    if (SdMan.openFileForRead("MD ", metaPath, metaFile)) {
      uint32_t magic = 0;
      uint8_t version = 0;
      uint32_t cachedSize = 0;
      uint32_t cachedHash = 0;
      serialization::readPod(metaFile, magic);
      serialization::readPod(metaFile, version);
      serialization::readPod(metaFile, cachedSize);
      serialization::readPod(metaFile, cachedHash);
      metaFile.close();
      if (magic == META_MAGIC && version == META_VERSION && cachedSize == fileSize && cachedHash != 0) {
        const uint32_t currentHash = hashFileContents(filepath);
        if (currentHash != 0 && currentHash == cachedHash) {
          needsRender = false;
        }
      }
    }
  }

  if (!needsRender) {
    return true;
  }

  if (!renderToHtmlFile(htmlPath)) {
    SdMan.remove(htmlPath.c_str());
    return false;
  }

  FsFile metaFile;
  if (SdMan.openFileForWrite("MD ", metaPath, metaFile)) {
    serialization::writePod(metaFile, META_MAGIC);
    serialization::writePod(metaFile, META_VERSION);
    serialization::writePod(metaFile, static_cast<uint32_t>(fileSize));
    serialization::writePod(metaFile, hashFileContents(filepath));
    metaFile.close();
  }

  return true;
}

bool Markdown::renderToHtmlFile(const std::string& htmlPath) const {
  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    return false;
  }

  std::string content;
  content.reserve(fileSize + 1);
  uint8_t buffer[1024];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    content.append(reinterpret_cast<const char*>(buffer), readSize);
  }
  file.close();

  std::string processed = stripFrontmatter(content);
  processed = stripComments(processed);

  std::string output;
  output.reserve(processed.size() + 64);

  bool inFence = false;
  std::string fence;

  size_t start = 0;
  while (start <= processed.size()) {
    const size_t end = processed.find('\n', start);
    const bool hasNewline = end != std::string::npos;
    const size_t lineLen = hasNewline ? (end - start) : (processed.size() - start);
    std::string line = processed.substr(start, lineLen);

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

  FsFile htmlFile;
  if (!SdMan.openFileForWrite("MD ", htmlPath, htmlFile)) {
    return false;
  }

  HtmlOutput htmlOut{&htmlFile, true};
  constexpr char kHtmlOpen[] = "<html><body>\n";
  constexpr char kHtmlClose[] = "\n</body></html>";
  htmlFile.write(reinterpret_cast<const uint8_t*>(kHtmlOpen), sizeof(kHtmlOpen) - 1);

  const unsigned parserFlags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_WIKILINKS;
  const unsigned rendererFlags = MD_HTML_FLAG_XHTML | MD_HTML_FLAG_SKIP_UTF8_BOM;
  const int result = md_html(output.c_str(), static_cast<MD_SIZE>(output.size()), writeHtmlChunk, &htmlOut, parserFlags,
                             rendererFlags);

  htmlFile.write(reinterpret_cast<const uint8_t*>(kHtmlClose), sizeof(kHtmlClose) - 1);
  htmlFile.close();

  return result == 0 && htmlOut.ok;
}

std::string Markdown::stripFrontmatter(const std::string& content) {
  size_t pos = 0;
  size_t lineEnd = content.find('\n');
  if (lineEnd == std::string::npos) {
    return content;
  }

  std::string firstLine = content.substr(0, lineEnd);
  if (firstLine != "---" && firstLine != "---\r") {
    return content;
  }

  pos = lineEnd + 1;
  while (pos < content.size()) {
    lineEnd = content.find('\n', pos);
    const size_t len = (lineEnd == std::string::npos) ? content.size() - pos : lineEnd - pos;
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

std::string Markdown::stripComments(const std::string& content) {
  std::string output;
  output.reserve(content.size());
  size_t i = 0;
  while (i < content.size()) {
    if (i + 1 < content.size() && content[i] == '%' && content[i + 1] == '%') {
      const size_t end = content.find("%%", i + 2);
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

std::string Markdown::processLine(const std::string& line) {
  std::string trimmed = line;
  if (!trimmed.empty() && trimmed.back() == '\r') {
    trimmed.pop_back();
  }
  std::string formatted = formatCalloutLine(trimmed);
  formatted = processInline(formatted);
  formatted = stripBlockId(formatted);
  return formatted;
}

std::string Markdown::formatCalloutLine(const std::string& line) {
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

std::string Markdown::processInline(const std::string& line) {
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
          out.append(target);
          out.append(")");
        } else {
          const std::string& label = alias.empty() ? target : alias;
          out.append("[");
          out.append(label);
          out.append("](");
          out.append(target);
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
        out.append(target);
        out.append(")");
        i = end + 2;
        continue;
      }
    }

    if (i + 1 < line.size() && line[i] == '=' && line[i + 1] == '=') {
      const size_t end = line.find("==", i + 2);
      if (end != std::string::npos && end > i + 2) {
        std::string inner = line.substr(i + 2, end - (i + 2));
        out.append("<mark>");
        out.append(inner);
        out.append("</mark>");
        i = end + 2;
        continue;
      }
    }

    if (i + 1 < line.size() && line[i] == '~' && line[i + 1] == '~') {
      out.append("~~");
      i += 2;
      continue;
    }

    if (line[i] == '~' && (i + 1 < line.size()) && line[i + 1] != '~') {
      const size_t end = line.find('~', i + 1);
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

    if (line[i] == '^') {
      const size_t end = line.find('^', i + 1);
      if (end != std::string::npos && end > i + 1) {
        std::string inner = line.substr(i + 1, end - (i + 1));
        // Superscript requires non-empty content (subscript already gated by end > i + 1).
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

std::string Markdown::stripBlockId(const std::string& line) {
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

std::string Markdown::getContent() const {
  if (!loaded) {
    return "";
  }

  FsFile file;
  if (!SdMan.openFileForRead("MD ", filepath, file)) {
    return "";
  }

  std::string content;
  content.reserve(fileSize + 1);
  uint8_t buffer[1024];
  while (file.available()) {
    const size_t readSize = file.read(buffer, sizeof(buffer));
    if (readSize == 0) {
      break;
    }
    content.append(reinterpret_cast<const char*>(buffer), readSize);
  }
  file.close();

  return content;
}

bool Markdown::parseToAst() {
  if (!loaded) {
    return false;
  }

  std::string content = getContent();
  if (content.empty()) {
    return false;
  }

  MarkdownParser parser;
  ast = parser.parseWithPreprocessing(content);

  if (!ast) {
    Serial.printf("[%lu] [MD ] Failed to parse markdown to AST\n", millis());
    return false;
  }

  // Build navigation data from AST
  navigation = md_detail::make_unique<MarkdownNavigation>(*ast);

  Serial.printf("[%lu] [MD ] Parsed to AST: %zu TOC entries, %zu links\n", millis(), navigation->getTotalHeadings(),
                navigation->getTotalLinks());

  return true;
}
