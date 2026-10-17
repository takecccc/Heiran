#include "heiran/ui/SessionLayoutController.hpp"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

heiran::domain::ViewerLayout SessionLayoutController::layoutForImageCount(int imageCount) {
  heiran::domain::ViewerLayout layout;
  if (imageCount <= 1) {
    return layout;
  }

  // 正方形に近い格子のうち、列を先に埋める形を選ぶ。
  const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(imageCount))));
  layout.cols = cols;
  layout.rows = (imageCount + cols - 1) / cols;
  return layout;
}

QVector<heiran::domain::ViewerLayout> SessionLayoutController::layoutsForCellCount(int cellCount) {
  QVector<heiran::domain::ViewerLayout> layouts;
  if (cellCount < 1 || cellCount > kMaxCellCount) {
    return layouts;
  }
  for (int rows = 1; rows <= cellCount; ++rows) {
    if (cellCount % rows != 0) {
      continue;
    }
    heiran::domain::ViewerLayout layout;
    layout.rows = rows;
    layout.cols = cellCount / rows;
    layouts.push_back(layout);
  }
  return layouts;
}

bool SessionLayoutController::growLayout(heiran::domain::ViewerLayout *layout) {
  const auto fits = [](int rows, int cols) { return rows * cols <= kMaxCellCount; };
  const int rows = layout->rows;
  const int cols = layout->cols;
  if (cols < 4 && fits(rows, cols + 1)) {
    ++layout->cols;
  } else if (rows < 4 && cols <= 4 && fits(rows + 1, cols)) {
    ++layout->rows;
  } else if (fits(rows, cols + 1)) {
    ++layout->cols;
  } else if (fits(rows + 1, cols)) {
    ++layout->rows;
  } else {
    return false;
  }
  return true;
}

int SessionLayoutController::visibleCellCount(const heiran::domain::SessionModel &session) {
  return session.viewer.layout.rows * session.viewer.layout.cols;
}

void SessionLayoutController::ensureVisibleCellCapacity(
    QVector<heiran::domain::CellConfig> *cellLayoutCache, int visibleCellCount) {
  if (cellLayoutCache->size() < visibleCellCount) {
    cellLayoutCache->resize(visibleCellCount);
  }
}

bool SessionLayoutController::applyLayout(heiran::domain::SessionModel *session,
                                          QVector<heiran::domain::CellConfig> *cellLayoutCache,
                                          int rows, int cols) {
  if (rows <= 0 || cols <= 0) {
    return false;
  }

  session->viewer.layout.rows = rows;
  session->viewer.layout.cols = cols;
  ensureVisibleCellCapacity(cellLayoutCache, rows * cols);
  session->cells = *cellLayoutCache;
  return true;
}

bool SessionLayoutController::applyLayoutOrder(heiran::domain::SessionModel *session,
                                               const QString &order) {
  if (order.isEmpty() || session->viewer.layout.order == order) {
    return false;
  }

  session->viewer.layout.order = order;
  return true;
}

bool SessionLayoutController::applyCustomImageOrder(
    const heiran::domain::SessionModel &session,
    const QVector<heiran::domain::CellConfig> &cellLayoutCache, const QVector<int> &imageIndices,
    QVector<heiran::domain::CellConfig> *newCells, QString *errorMessage) {
  const int cellCount = visibleCellCount(session);
  if (cellCount <= 0 || imageIndices.size() != cellCount) {
    return false;
  }

  QVector<heiran::domain::CellConfig> cells = cellLayoutCache;
  ensureVisibleCellCapacity(&cells, cellCount);

  QVector<bool> used(session.images.size(), false);
  for (int cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
    const int imageIndex = imageIndices.at(cellIndex);
    if (imageIndex >= 0) {
      if (used[imageIndex]) {
        if (errorMessage != nullptr) {
          *errorMessage = QCoreApplication::translate(
                              "SessionLayoutController",
                              "Image %1 is assigned more than once. Choose a unique image for "
                              "each cell or use Empty.")
                              .arg(imageIndex + 1);
        }
        return false;
      }
      used[imageIndex] = true;
    }
    cells[cellIndex].imageIndex = imageIndex;
  }

  *newCells = cells;
  return true;
}

bool SessionLayoutController::reorderImagesAscending(
    const heiran::domain::SessionModel &session,
    QVector<heiran::domain::CellConfig> *cellLayoutCache) {
  const int cellCount = visibleCellCount(session);
  if (cellCount <= 0) {
    return false;
  }

  ensureVisibleCellCapacity(cellLayoutCache, cellCount);
  std::sort(cellLayoutCache->begin(), cellLayoutCache->begin() + cellCount,
            [](const heiran::domain::CellConfig &lhs, const heiran::domain::CellConfig &rhs) {
              if (lhs.imageIndex < 0) {
                return false;
              }
              if (rhs.imageIndex < 0) {
                return true;
              }
              return lhs.imageIndex < rhs.imageIndex;
            });
  return true;
}

bool SessionLayoutController::reorderImagesReverse(
    const heiran::domain::SessionModel &session,
    QVector<heiran::domain::CellConfig> *cellLayoutCache) {
  const int cellCount = visibleCellCount(session);
  if (cellCount <= 0) {
    return false;
  }

  ensureVisibleCellCapacity(cellLayoutCache, cellCount);
  std::reverse(cellLayoutCache->begin(), cellLayoutCache->begin() + cellCount);
  return true;
}

bool SessionLayoutController::reorderImagesRotateLeft(
    const heiran::domain::SessionModel &session,
    QVector<heiran::domain::CellConfig> *cellLayoutCache) {
  const int cellCount = visibleCellCount(session);
  if (cellCount <= 1) {
    return false;
  }

  ensureVisibleCellCapacity(cellLayoutCache, cellCount);
  std::rotate(cellLayoutCache->begin(), cellLayoutCache->begin() + 1,
              cellLayoutCache->begin() + cellCount);
  return true;
}

bool SessionLayoutController::reorderImagesRotateRight(
    const heiran::domain::SessionModel &session,
    QVector<heiran::domain::CellConfig> *cellLayoutCache) {
  const int cellCount = visibleCellCount(session);
  if (cellCount <= 1) {
    return false;
  }

  ensureVisibleCellCapacity(cellLayoutCache, cellCount);
  std::rotate(cellLayoutCache->begin(), cellLayoutCache->begin() + cellCount - 1,
              cellLayoutCache->begin() + cellCount);
  return true;
}

}  // namespace heiran::ui
