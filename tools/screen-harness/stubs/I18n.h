#pragma once

enum StrId { STR_CROSSPOINT, STR_BOOTING };

inline const char* tr(StrId id) {
  switch (id) {
    case STR_CROSSPOINT:
      return "CrossPoint";
    case STR_BOOTING:
      return "Booting...";
    default:
      return "";
  }
}
