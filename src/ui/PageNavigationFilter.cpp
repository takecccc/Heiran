#include "heiran/ui/PageNavigationFilter.hpp"

#include <algorithm>

namespace heiran::ui {

void PageNavigationFilter::setPages(std::optional<QVector<int>> pages) {
  if (pages) {
    pages->removeIf([](int page) { return page < 0; });
    std::sort(pages->begin(), pages->end());
    pages->erase(std::unique(pages->begin(), pages->end()), pages->end());
  }
  pages_ = std::move(pages);
}

int PageNavigationFilter::count(int total) const {
  return pages_ ? static_cast<int>(std::lower_bound(pages_->begin(), pages_->end(), total) -
                                   pages_->begin())
                : total;
}

int PageNavigationFilter::indexOf(int page, int total) const {
  const int available = count(total);
  if (available <= 0) {
    return -1;
  }
  return pages_ ? std::min(static_cast<int>(std::lower_bound(pages_->begin(), pages_->end(), page) -
                                            pages_->begin()),
                           available - 1)
                : std::clamp(page, 0, available - 1);
}

int PageNavigationFilter::pageAt(int index, int total) const {
  const int available = count(total);
  if (available <= 0) {
    return -1;
  }
  index = std::clamp(index, 0, available - 1);
  return pages_ ? pages_->at(index) : index;
}

}  // namespace heiran::ui
