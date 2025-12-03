#pragma once
#include <string>
#include <utility>

#include "Screen.h"

class FullScreenMessageScreen final : public Screen {
  std::string text;
  bool bold;
  bool italic;
  bool invert;

 public:
  explicit FullScreenMessageScreen(EpdRenderer* renderer, std::string text, const bool bold = false,
                                   const bool italic = false, const bool invert = false)
      : Screen(renderer), text(std::move(text)), bold(bold), italic(italic), invert(invert) {}
  void onEnter() override;
};
