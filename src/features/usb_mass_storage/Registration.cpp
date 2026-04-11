#include "features/usb_mass_storage/Registration.h"

namespace features::usb_mass_storage {

void registerFeature() {
  // Intentionally empty: USB MSC behavior is owned by the serial protocol and
  // settings surfaces, so there are no feature-local registries to mount here.
}

}  // namespace features::usb_mass_storage
