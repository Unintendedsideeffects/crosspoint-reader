# Full Markdown AST Processing Implementation Plan

## Current State

The markdown pipeline currently uses **streaming mode**:
```
Markdown text → (Obsidian preprocessing) → (md4c-html) → HTML → (ChapterHtmlSlimParser) → Pages
```

The AST is never materialized - md4c fires callbacks that directly emit HTML chunks. This is memory-efficient but loses all semantic structure after conversion.

## Goal

Implement full AST processing to enable:
1. **Table of Contents** - Generate from heading hierarchy
2. **Semantic Navigation** - Jump between headings, links, code blocks
3. **Custom Rendering** - Transform specific elements (callouts, task lists, diagrams)
4. **Link/Reference Tracking** - Navigate internal links and footnotes

## Architecture Decision

**New pipeline**:
```
Markdown text → (Obsidian preprocessing) → (md4c + AST builder) → MarkdownAST
                                                                      ↓
                                           ┌──────────────────────────┴───────────────────────────┐
                                           ↓                                                       ↓
                                    (AST traversal)                                        (AST renderer)
                                           ↓                                                       ↓
                                  TOC, Links, Navigation                                     Page objects
```

This replaces the HTML intermediate with a proper AST, giving full control over both metadata extraction and rendering.

---

## Phase 1: AST Data Structures

**Files**: `lib/Markdown/MarkdownAST.h`

Define lightweight AST nodes optimized for ESP32 memory constraints:

```cpp
enum class MdNodeType : uint8_t {
  // Block nodes
  Document, Heading, Paragraph, CodeBlock, List, ListItem,
  Blockquote, Table, TableRow, TableCell, HorizontalRule,
  // Inline nodes
  Text, Emphasis, Strong, Link, Image, Code, Strikethrough,
  // Extensions
  TaskListItem, Callout, WikiLink, Highlight
};

struct MdNode {
  MdNodeType type;
  std::vector<std::unique_ptr<MdNode>> children;

  // Union-like storage for node-specific data
  union {
    struct { uint8_t level; } heading;           // H1-H6
    struct { std::string* info; } codeBlock;     // Language info
    struct { bool ordered; int start; } list;    // OL vs UL
    struct { bool checked; } taskItem;           // [ ] vs [x]
    struct { std::string* href; std::string* title; } link;
    struct { std::string* src; std::string* alt; } image;
  } data;

  std::string text;  // For text nodes and inline content
};
```

**Memory optimization**: Use a pool allocator for nodes, or serialize AST to disk for large documents.

---

## Phase 2: AST Builder (Parser)

**Files**: `lib/Markdown/MarkdownParser.h`, `lib/Markdown/MarkdownParser.cpp`

Implement md4c callbacks that build the AST:

```cpp
class MarkdownParser {
public:
  std::unique_ptr<MdNode> parse(const std::string& markdown);

private:
  std::vector<MdNode*> nodeStack;  // Current path from root to active node

  // md4c callbacks
  static int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata);
  static int leaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata);
  static int enterSpan(MD_SPANTYPE type, void* detail, void* userdata);
  static int leaveSpan(MD_SPANTYPE type, void* detail, void* userdata);
  static int onText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata);
};
```

Obsidian preprocessing (callouts, wikilinks, highlights, frontmatter stripping) happens **before** parsing, same as current implementation.

---

## Phase 3: AST Renderer

**Files**: `lib/Markdown/MarkdownRenderer.h`, `lib/Markdown/MarkdownRenderer.cpp`

Traverse AST and produce `Page` objects compatible with existing display system:

```cpp
class MarkdownRenderer {
public:
  MarkdownRenderer(GfxRenderer& gfx, int fontId, /* settings */);

  // Render entire document to pages
  std::vector<std::unique_ptr<Page>> render(const MdNode& root);

  // Render to section file (cached pages)
  bool renderToSectionFile(const MdNode& root, const std::string& cachePath);

private:
  void renderNode(const MdNode& node);
  void renderHeading(const MdNode& node);
  void renderParagraph(const MdNode& node);
  void renderCodeBlock(const MdNode& node);
  void renderList(const MdNode& node);
  void renderBlockquote(const MdNode& node);
  void renderTable(const MdNode& node);  // Full table support!

  // Use existing ParsedText/TextBlock for layout
  std::unique_ptr<ParsedText> currentText;
};
```

**Key advantage**: Can now render tables properly (current HTML parser shows "[Table omitted]").

---

## Phase 4: Navigation & Metadata Extraction

**Files**: `lib/Markdown/MarkdownNavigation.h`, `lib/Markdown/MarkdownNavigation.cpp`

Extract navigation structures from AST:

```cpp
struct TocEntry {
  uint8_t level;        // 1-6
  std::string title;
  size_t pageIndex;     // Which page this heading is on
  size_t nodeOffset;    // Position in AST for direct jump
};

struct LinkEntry {
  std::string text;
  std::string href;
  bool isInternal;      // Wikilink or relative path
  size_t pageIndex;
};

class MarkdownNavigation {
public:
  explicit MarkdownNavigation(const MdNode& root);

  const std::vector<TocEntry>& getToc() const;
  const std::vector<LinkEntry>& getLinks() const;

  // Navigation helpers
  std::optional<size_t> findNextHeading(size_t currentPage) const;
  std::optional<size_t> findPrevHeading(size_t currentPage) const;
  std::optional<size_t> resolveLink(const std::string& href) const;

private:
  void extractFromAst(const MdNode& node, int depth = 0);
  std::vector<TocEntry> toc;
  std::vector<LinkEntry> links;
};
```

---

## Phase 5: Integration with Reader Activity

**Files**: Modify `src/activities/reader/MarkdownReaderActivity.h/cpp`

Update the reader to use AST-based pipeline:

```cpp
class MarkdownReaderActivity {
  std::unique_ptr<Markdown> markdown;
  std::unique_ptr<MdNode> ast;              // NEW: parsed AST
  std::unique_ptr<MarkdownNavigation> nav;  // NEW: navigation data
  std::unique_ptr<HtmlSection> section;     // Keep for page caching

  void onEnter() override {
    ast = markdownParser.parse(markdown->getContent());
    nav = std::make_unique<MarkdownNavigation>(*ast);
    // Render AST to section file...
  }

  // NEW: TOC dialog
  void showTableOfContents();

  // NEW: Heading navigation
  void jumpToNextHeading();
  void jumpToPrevHeading();
};
```

Add new input mappings for navigation:
- Long-press left/right: Jump to prev/next heading
- New button combo: Show TOC overlay

---

## Phase 6: TOC Dialog Activity

**Files**: `src/activities/reader/TocActivity.h`, `src/activities/reader/TocActivity.cpp`

Modal dialog showing table of contents with selection:

```cpp
class TocActivity : public Activity {
  const std::vector<TocEntry>& toc;
  size_t selectedIndex = 0;
  std::function<void(size_t)> onSelect;

  void renderTocList();
  void loop() override;  // Handle up/down/select
};
```

---

## File Changes Summary

### New Files
- `lib/Markdown/MarkdownAST.h` - AST node definitions
- `lib/Markdown/MarkdownParser.h/cpp` - md4c → AST builder
- `lib/Markdown/MarkdownRenderer.h/cpp` - AST → Pages renderer
- `lib/Markdown/MarkdownNavigation.h/cpp` - TOC/links extraction
- `src/activities/reader/TocActivity.h/cpp` - TOC dialog

### Modified Files
- `lib/Markdown/Markdown.h/cpp` - Add AST generation methods
- `src/activities/reader/MarkdownReaderActivity.h/cpp` - Use AST pipeline
- `platformio.ini` - Ensure md4c.h is included (not just md4c-html.h)

### Removed/Deprecated
- HTML intermediate step no longer needed for markdown
- `Markdown::ensureHtml()` can be kept for backwards compat or removed

---

## Memory Considerations (ESP32)

1. **Streaming parse for large files**: If document > threshold (e.g., 100KB), use a two-pass approach:
   - First pass: Build lightweight index (headings, links only)
   - Second pass: Render page-by-page without full AST in memory

2. **AST serialization**: For very large documents, serialize AST to SD card and load portions on demand.

3. **Page caching**: Continue using section file caching (existing `HtmlSection` approach) to avoid re-rendering.

---

## Implementation Order

1. **Phase 1** (AST structures) - Foundation
2. **Phase 2** (Parser) - Can test with debug output
3. **Phase 3** (Renderer) - Produces visible output, testable
4. **Phase 4** (Navigation) - TOC/links extraction
5. **Phase 5** (Integration) - Wire into reader
6. **Phase 6** (TOC dialog) - User-facing navigation

Estimated: ~6 phases, can be done incrementally with each phase independently testable.

---

## Questions Resolved

- **Custom rendering**: Handled in Phase 3 via node-specific render methods
- **Semantic navigation**: Handled in Phase 4 with heading/link tracking
- **Table of contents**: Handled in Phase 4 extraction + Phase 6 dialog
- **Link tracking**: Handled in Phase 4 with LinkEntry structures
