// Run on the host with: sh tests/run_navigation.sh
#include <cstddef>
#include "../navigation.h"
#include <cassert>

int main() {
  for (unsigned mask = 0; mask < 4; ++mask) {
    Dash d = {};
    d.valid = true;
    d.nLimits = mask & 1 ? 3 : 0;
    d.nCodexLimits = mask & 2 ? 2 : 0;
    d.claudeNote[0] = 'x'; d.codexNote[0] = 'x'; // Notes do not enable detail pages.
    const unsigned expected = 5 + !!(d.nLimits || d.nCodexLimits);
    assert(pageTotal(d) == expected);
    Page p = PAGE_DASH;
    for (unsigned i = 1; i <= expected; ++i) {
      assert(pageVisible(d, p));
      assert(pageNumber(d, p) == i);
      if (i == expected) assert(p == PAGE_DEVICE);
      Page next = pageStep(d, p, true);
      assert(pageStep(d, next, false) == p);
      p = next;
    }
    assert(p == PAGE_DASH);
    assert(pageStep(d, PAGE_DASH, false) == PAGE_DEVICE);
    assert(pageNormalize(d, PAGE_AI) == ((d.nLimits || d.nCodexLimits) ? PAGE_AI : PAGE_DASH));
    assert(pageStep(d, PAGE_DASH, true) == ((d.nLimits || d.nCodexLimits) ? PAGE_AI : PAGE_SERVERS));
    // Simulate data disappearing while a detail page is open.
    d.nLimits = 0; d.nCodexLimits = 0;
    assert(pageNormalize(d, PAGE_AI) == PAGE_DASH);
    d.valid = false; d.nLimits = 3; d.nCodexLimits = 2;
    assert(pageTotal(d) == 5);
  }
}
