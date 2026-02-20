#include "FsHelpers.h"

#include <vector>

std::string FsHelpers::normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;
  auto flushComponent = [&components](const std::string& part) {
    if (part.empty() || part == ".") {
      return;
    }
    if (part == "..") {
      if (!components.empty()) {
        components.pop_back();
      }
      return;
    }
    components.push_back(part);
  };

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        flushComponent(component);
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    flushComponent(component);
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) {
      result += "/";
    }
    result += c;
  }

  return result;
}
