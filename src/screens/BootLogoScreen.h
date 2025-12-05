#pragma once
#include "Screen.h"

class BootLogoScreen final : public Screen {
 public:
  explicit BootLogoScreen(EpdRenderer* renderer) : Screen(renderer) {}
  void onEnter() override;
};
