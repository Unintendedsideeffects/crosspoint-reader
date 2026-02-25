#include "util/ForkDriftNavigation.h"

#include <algorithm>

namespace ForkDriftNavigation {

CoverNavResult navigateCoverGrid(int selectedIndex, int bookCount, int cols, int /*rows*/, bool left, bool right,
                                 bool up, bool down) {
  if (bookCount <= 0) return {0, false};

  // Clamp incoming index to valid range
  selectedIndex = std::max(0, std::min(selectedIndex, bookCount - 1));

  if (left) {
    return {(selectedIndex + bookCount - 1) % bookCount, false};
  }
  if (right) {
    return {(selectedIndex + 1) % bookCount, false};
  }

  const int row = selectedIndex / cols;
  const int col = selectedIndex % cols;
  const int lastBookRow = (bookCount - 1) / cols;

  if (up) {
    if (row == 0) {
      // Nothing above the top row: wrap down to the button grid
      return {selectedIndex, true};
    }
    const int newIndex = std::min((row - 1) * cols + col, bookCount - 1);
    return {std::max(0, newIndex), false};
  }

  if (down) {
    if (row >= lastBookRow) {
      // At or below the last row that contains books: enter button grid
      return {selectedIndex, true};
    }
    const int newIndex = std::min((row + 1) * cols + col, bookCount - 1);
    return {std::max(0, newIndex), false};
  }

  return {selectedIndex, false};
}

}  // namespace ForkDriftNavigation
