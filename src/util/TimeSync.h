#pragma once

namespace TimeSync {
// Attempts to sync time with NTP using minimal memory and a short timeout.
// Returns true if time is (or becomes) valid.
bool syncTimeWithNtpLowMemory();
}  // namespace TimeSync
