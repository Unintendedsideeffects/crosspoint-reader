#include "MarkdownNavigation.h"

#include <algorithm>
#include <Arduino.h>

MarkdownNavigation::MarkdownNavigation(const MdNode& root) {
  size_t nodeIndex = 0;
  extractFromNode(root, nodeIndex, 0);
}

void MarkdownNavigation::extractFromNode(const MdNode& node, size_t& nodeIndex, size_t depth) {
  if (depth > MAX_NAVIGATION_DEPTH) {
    if (!depthLimitExceeded) {
      Serial.printf("[%lu] [MD ] Navigation depth limit exceeded (%zu)\n", millis(), MAX_NAVIGATION_DEPTH);
      depthLimitExceeded = true;
    }
    return;
  }

  // Process current node based on type
  switch (node.type) {
    case MdNodeType::Heading:
      extractHeading(node, nodeIndex);
      break;

    case MdNodeType::Link:
      extractLink(node, nodeIndex);
      break;

    case MdNodeType::WikiLink:
      extractWikiLink(node, nodeIndex);
      break;

    case MdNodeType::Image:
      extractImage(node, nodeIndex);
      break;

    default:
      break;
  }

  nodeIndex++;

  // Recurse into children
  for (const auto& child : node.children) {
    extractFromNode(*child, nodeIndex, depth + 1);
  }
}

void MarkdownNavigation::extractHeading(const MdNode& node, size_t nodeIndex) {
  if (!node.heading) {
    return;
  }

  TocEntry entry;
  entry.level = node.heading->level;
  entry.title = node.getPlainText();
  entry.nodeIndex = nodeIndex;
  entry.estimatedPage = 0;  // Updated later

  toc.push_back(std::move(entry));

  HeadingRef ref;
  ref.nodeIndex = nodeIndex;
  ref.pageNumber = 0;
  ref.level = node.heading->level;
  headingRefs.push_back(ref);
}

void MarkdownNavigation::extractLink(const MdNode& node, size_t nodeIndex) {
  if (!node.link) {
    return;
  }

  LinkEntry entry;
  entry.text = node.getPlainText();
  entry.href = node.link->href;
  entry.isInternal = isInternalLink(node.link->href);
  entry.isImage = false;
  entry.nodeIndex = nodeIndex;

  links.push_back(std::move(entry));
}

void MarkdownNavigation::extractWikiLink(const MdNode& node, size_t nodeIndex) {
  if (!node.wikiLink) {
    return;
  }

  LinkEntry entry;
  entry.text = node.wikiLink->alias.empty() ? node.wikiLink->target : node.wikiLink->alias;
  entry.href = node.wikiLink->target;
  entry.isInternal = true;  // Wikilinks are always internal
  entry.isImage = false;
  entry.nodeIndex = nodeIndex;

  links.push_back(std::move(entry));
}

void MarkdownNavigation::extractImage(const MdNode& node, size_t nodeIndex) {
  if (!node.image) {
    return;
  }

  LinkEntry entry;
  entry.text = node.getPlainText();  // Alt text
  entry.href = node.image->src;
  entry.isInternal = isInternalLink(node.image->src);
  entry.isImage = true;
  entry.nodeIndex = nodeIndex;

  links.push_back(std::move(entry));
}

bool MarkdownNavigation::isInternalLink(const std::string& href) {
  if (href.empty()) {
    return false;
  }

  // External links start with http://, https://, mailto:, etc.
  if (href.find("://") != std::string::npos) {
    return false;
  }
  if (href.compare(0, 7, "mailto:") == 0) {
    return false;
  }
  if (href.compare(0, 4, "tel:") == 0) {
    return false;
  }

  // Fragment-only links are internal
  if (href[0] == '#') {
    return true;
  }

  // Relative paths are internal
  return true;
}

void MarkdownNavigation::updatePageNumbers(const std::vector<size_t>& nodeToPage) {
  for (auto& entry : toc) {
    if (entry.nodeIndex < nodeToPage.size()) {
      entry.estimatedPage = nodeToPage[entry.nodeIndex];
    }
  }

  for (auto& ref : headingRefs) {
    if (ref.nodeIndex < nodeToPage.size()) {
      ref.pageNumber = nodeToPage[ref.nodeIndex];
    }
  }
}

std::optional<size_t> MarkdownNavigation::findNextHeading(size_t currentPage) const {
  for (const auto& ref : headingRefs) {
    if (ref.pageNumber > currentPage) {
      return ref.pageNumber;
    }
  }
  return std::nullopt;
}

std::optional<size_t> MarkdownNavigation::findPrevHeading(size_t currentPage) const {
  std::optional<size_t> result;
  for (const auto& ref : headingRefs) {
    if (ref.pageNumber < currentPage) {
      result = ref.pageNumber;
    } else {
      break;
    }
  }
  return result;
}

std::optional<size_t> MarkdownNavigation::findHeadingPage(size_t tocIndex) const {
  if (tocIndex >= toc.size()) {
    return std::nullopt;
  }
  return toc[tocIndex].estimatedPage;
}

std::optional<size_t> MarkdownNavigation::resolveInternalLink(const std::string& href) const {
  if (href.empty()) {
    return std::nullopt;
  }

  // Handle fragment links (#heading-slug)
  if (href[0] == '#') {
    std::string slug = href.substr(1);
    // Convert slug back to approximate heading match
    // Slugs are typically lowercase with hyphens for spaces
    for (size_t i = 0; i < toc.size(); i++) {
      std::string titleSlug;
      for (char c : toc[i].title) {
        if (c == ' ') {
          titleSlug += '-';
        } else if (isalnum(static_cast<unsigned char>(c))) {
          titleSlug += static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
      }
      if (titleSlug == slug) {
        return toc[i].estimatedPage;
      }
    }
  }

  // For file links, we can't resolve them within the same document
  // This would need external handling (opening another file)
  return std::nullopt;
}

std::optional<size_t> MarkdownNavigation::findNextHeadingAtLevel(size_t currentPage, uint8_t maxLevel) const {
  for (const auto& ref : headingRefs) {
    if (ref.pageNumber > currentPage && ref.level <= maxLevel) {
      return ref.pageNumber;
    }
  }
  return std::nullopt;
}

std::optional<size_t> MarkdownNavigation::findPrevHeadingAtLevel(size_t currentPage, uint8_t maxLevel) const {
  std::optional<size_t> result;
  for (const auto& ref : headingRefs) {
    if (ref.pageNumber >= currentPage) {
      break;
    }
    if (ref.level <= maxLevel) {
      result = ref.pageNumber;
    }
  }
  return result;
}

size_t MarkdownNavigation::getTotalInternalLinks() const {
  return std::count_if(links.begin(), links.end(), [](const LinkEntry& e) { return e.isInternal; });
}
