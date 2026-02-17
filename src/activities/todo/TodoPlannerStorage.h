#pragma once

#include <string>

namespace TodoPlannerStorage {

// Select daily TODO file path with precedence:
// 1) Existing .md, 2) existing .txt, 3) extension from markdownEnabled fallback.
inline std::string dailyPath(const std::string& date, const bool markdownEnabled, const bool markdownExists,
                             const bool textExists) {
  if (markdownExists) {
    return "/daily/" + date + ".md";
  }
  if (textExists) {
    return "/daily/" + date + ".txt";
  }
  return "/daily/" + date + (markdownEnabled ? ".md" : ".txt");
}

// Agenda entries are stored as plain text; TODO entries use markdown checkbox format.
inline std::string formatEntry(const std::string& text, const bool agendaEntry) {
  return agendaEntry ? text : "- [ ] " + text;
}

}  // namespace TodoPlannerStorage
