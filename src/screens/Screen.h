#pragma once
#include <InputManager.h>

class EpdRenderer;

class Screen {
 protected:
  EpdRenderer& renderer;
  InputManager& inputManager;

 public:
  explicit Screen(EpdRenderer& renderer, InputManager& inputManager) : renderer(renderer), inputManager(inputManager) {}
  virtual ~Screen() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void handleInput() {}
};
