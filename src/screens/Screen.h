#pragma once
#include "Input.h"

class EpdRenderer;

class Screen {
 protected:
  EpdRenderer* renderer;

 public:
  explicit Screen(EpdRenderer* renderer) : renderer(renderer) {}
  virtual ~Screen() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void handleInput(Input input) {}
};
