#pragma once

#include <QVector>
#include <optional>

namespace heiran::ui {

class PageNavigationFilter {
public:
  void setPages(std::optional<QVector<int>> pages);
  int count(int total) const;
  int indexOf(int page, int total) const;
  int pageAt(int index, int total) const;

private:
  std::optional<QVector<int>> pages_;
};

}  // namespace heiran::ui
