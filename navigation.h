#pragma once
#include "model.h"

// Keep the established order, with device information always last.
enum Page : uint8_t { PAGE_DASH = 0, PAGE_AI, PAGE_SERVERS, PAGE_WEATHER, PAGE_CALENDAR, PAGE_DEVICE, PAGE_COUNT };

inline bool pageVisible(const Dash& d, Page page) {
  if (page == PAGE_AI) return d.valid && (d.nLimits > 0 || d.nCodexLimits > 0);
  return page < PAGE_COUNT;
}
inline Page pageNormalize(const Dash& d, Page page) {
  return pageVisible(d, page) ? page : PAGE_DASH;
}
inline Page pageStep(const Dash& d, Page page, bool forward) {
  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    page = (Page)((page + (forward ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT);
    if (pageVisible(d, page)) return page;
  }
  return PAGE_DASH;
}
inline uint8_t pageTotal(const Dash& d) {
  uint8_t count = 0;
  for (uint8_t p = 0; p < PAGE_COUNT; ++p) if (pageVisible(d, (Page)p)) ++count;
  return count;
}
inline uint8_t pageNumber(const Dash& d, Page page) {
  page = pageNormalize(d, page);
  uint8_t number = 0;
  for (uint8_t p = 0; p <= page; ++p) if (pageVisible(d, (Page)p)) ++number;
  return number;
}
