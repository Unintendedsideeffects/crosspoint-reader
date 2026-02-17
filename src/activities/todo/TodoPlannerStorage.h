#pragma once

#include <string>

namespace TodoPlannerStorage {

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

inline std::string formatEntry(const std::string& text, const bool agendaEntry) {
  return agendaEntry ? text : "- [ ] " + text;
}

}  // namespace TodoPlannerStorage
