#pragma once
#include <FeatureFlags.h>
#if ENABLE_USB_MASS_STORAGE

class UsbSerialProtocol {
 public:
  void loop();   // call each iteration while in Active state
  void reset();  // call on session enter/exit to clear parser state
};

#endif
