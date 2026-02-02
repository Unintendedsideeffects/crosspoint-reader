#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "MarkdownAST.h"

// Table of contents entry
struct TocEntry {
  uint8_t level;         // Heading level 1-6
  std::string title;     // Plain text of heading
  size_t nodeIndex;      // Index in flattened node list (for navigation)
  size_t estimatedPage;  // Estimated page number (set after rendering)
};

// Link entry for tracking document links
struct LinkEntry {
  std::string text;  // Display text
  std::string href;  // Link target
  bool isInternal;   // True for wikilinks and relative paths
  bool isImage;      // True if this is an image reference
  size_t nodeIndex;  // Index in flattened node list
};

// Heading reference for page-based navigation
struct HeadingRef {
  size_t nodeIndex;
  size_t pageNumber;
  uint8_t level;
};

class MarkdownNavigation {
 public:
  static constexpr size_t MAX_NAVIGATION_DEPTH = 50;

  // Build navigation data from AST
  explicit MarkdownNavigation(const MdNode& root);

  // Access extracted data
  const std::vector<TocEntry>& getToc() const { return toc; }
  const std::vector<LinkEntry>& getLinks() const { return links; }
  const std::vector<HeadingRef>& getHeadingRefs() const { return headingRefs; }

  // Update page numbers after rendering (call this after pages are generated)
  void updatePageNumbers(const std::vector<size_t>& nodeToPage);

  // Navigation helpers
  std::optional<size_t> findNextHeading(size_t currentPage) const;
  std::optional<size_t> findPrevHeading(size_t currentPage) const;
  std::optional<size_t> findHeadingPage(size_t tocIndex) const;

  // Link resolution
  std::optional<size_t> resolveInternalLink(const std::string& href) const;

  // Get heading at specific level or higher
  std::optional<size_t> findNextHeadingAtLevel(size_t currentPage, uint8_t maxLevel) const;
  std::optional<size_t> findPrevHeadingAtLevel(size_t currentPage, uint8_t maxLevel) const;

  // Statistics
  size_t getTotalHeadings() const { return toc.size(); }
  size_t getTotalLinks() const { return links.size(); }
  size_t getTotalInternalLinks() const;

 private:
  std::vector<TocEntry> toc;
  std::vector<LinkEntry> links;
  std::vector<HeadingRef> headingRefs;
  bool depthLimitExceeded = false;

  // Recursive extraction
  void extractFromNode(const MdNode& node, size_t& nodeIndex, size_t depth);
  void extractHeading(const MdNode& node, size_t nodeIndex);
  void extractLink(const MdNode& node, size_t nodeIndex);
  void extractWikiLink(const MdNode& node, size_t nodeIndex);
  void extractImage(const MdNode& node, size_t nodeIndex);

  // Helper to determine if a link is internal
  static bool isInternalLink(const std::string& href);
};
