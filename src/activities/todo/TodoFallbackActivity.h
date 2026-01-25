#pragma once

#include <functional>
#include <string>

#include "../Activity.h"

class TodoFallbackActivity final : public Activity {
  std::string dateText;
  const std::function<void()> onBack;

  void render() const;

 public:
  explicit TodoFallbackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string dateText,
                                const std::function<void()>& onBack)
      : Activity("TodoFallback", renderer, mappedInput), dateText(std::move(dateText)), onBack(onBack) {}

  void onEnter() override;
  void loop() override;
};
