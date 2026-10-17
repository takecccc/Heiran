#include "heiran/ui/SessionMutationController.hpp"

#include "heiran/app/SessionPathResolver.hpp"
#include "heiran/ui/SessionLayoutController.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <optional>

namespace heiran::ui {

namespace {

QString toDroppedImageSetName(const QString &localPath) {
  const QFileInfo fileInfo(localPath);
  return fileInfo.isDir() ? fileInfo.fileName() : fileInfo.completeBaseName();
}

QString toDroppedImageSetPath(const heiran::domain::SessionModel &session,
                              const QString &localPath) {
  const QFileInfo fileInfo(localPath);
  const QString absolutePath =
      fileInfo.isDir()
          ? QDir(fileInfo.absoluteFilePath()).filePath("*")
          : QDir(fileInfo.absolutePath()).filePath(QString("*.%1").arg(fileInfo.suffix()));
  if (session.sessionDirectory.isEmpty()) {
    return absolutePath;
  }
  return QDir(session.sessionDirectory).relativeFilePath(absolutePath);
}

struct DroppedImageSetInfo {
  QString name;
  QString path;
  QVector<QString> resolvedPaths;
  std::optional<int> droppedPageIndex;
};

DroppedImageSetInfo buildDroppedImageSetInfo(const heiran::domain::SessionModel &session,
                                             const QString &localPath) {
  DroppedImageSetInfo info;
  info.name = toDroppedImageSetName(localPath);
  info.path = toDroppedImageSetPath(session, localPath);
  info.resolvedPaths =
      heiran::app::SessionPathResolver::resolveGlobbedPaths(session.sessionDirectory, info.path);

  const QFileInfo fileInfo(localPath);
  if (!fileInfo.isDir()) {
    const QString absoluteFilePath = fileInfo.absoluteFilePath();
    const auto it =
        std::find(info.resolvedPaths.begin(), info.resolvedPaths.end(), absoluteFilePath);
    if (it != info.resolvedPaths.end()) {
      info.droppedPageIndex = static_cast<int>(std::distance(info.resolvedPaths.begin(), it));
    }
  }

  return info;
}

void normalizeImageSetPageOffsets(heiran::domain::SessionModel *session, int *currentPage) {
  std::optional<int> minOffset;
  for (const auto &imageSet : session->images) {
    if (imageSet.resolvedPaths.isEmpty()) {
      continue;
    }
    minOffset = minOffset ? std::min(*minOffset, imageSet.pageOffset) : imageSet.pageOffset;
  }

  if (!minOffset || *minOffset == 0) {
    return;
  }

  for (auto &imageSet : session->images) {
    imageSet.pageOffset -= *minOffset;
  }
  *currentPage -= *minOffset;
}

}  // namespace

ApplyDroppedItemsResult SessionMutationController::applyDroppedItems(
    heiran::domain::SessionModel *session, QVector<heiran::domain::CellConfig> *cellLayoutCache,
    int targetCellIndex, const QStringList &paths, int targetDisplayPage) {
  ApplyDroppedItemsResult result;
  int cellCount = session->viewer.layout.rows * session->viewer.layout.cols;
  if (targetCellIndex < 0 || targetCellIndex >= cellCount || paths.isEmpty()) {
    return result;
  }

  // 複数の画像をドロップしたときは、それぞれを別のセルへ読み込む。セルが足りなければ
  // 格子を広げる。広げないと、1 枚だけ開いた直後 (1 セル) に何枚ドロップしても
  // 1 枚しか入らない。画像を直接開く経路が枚数に合う格子を選ぶのと揃える。
  const int neededCells =
      std::min(targetCellIndex + static_cast<int>(paths.size()), kMaxCellsGrownByDrop);
  if (neededCells > cellCount) {
    const auto grownLayout = SessionLayoutController::layoutForImageCount(neededCells);
    if (grownLayout.rows * grownLayout.cols > cellCount) {
      session->viewer.layout.rows = grownLayout.rows;
      session->viewer.layout.cols = grownLayout.cols;
      cellCount = grownLayout.rows * grownLayout.cols;
      result.layoutChanged = true;
    }
  }

  if (session->sessionDirectory.isEmpty()) {
    session->sessionDirectory = QDir::currentPath();
  }
  if (cellLayoutCache->size() < cellCount) {
    cellLayoutCache->resize(cellCount);
  }

  const int assignCount = std::min(static_cast<int>(paths.size()), cellCount - targetCellIndex);
  for (int index = 0; index < assignCount; ++index) {
    const QString &localPath = paths.at(index);
    const int cellIndex = targetCellIndex + index;
    auto &cellConfig = (*cellLayoutCache)[cellIndex];
    const DroppedImageSetInfo droppedInfo = buildDroppedImageSetInfo(*session, localPath);

    int imageIndex = cellConfig.imageIndex;
    if (imageIndex < 0 || imageIndex >= session->images.size()) {
      imageIndex = session->images.size();
      session->images.push_back({.name = droppedInfo.name,
                                 .label = droppedInfo.name,
                                 .path = droppedInfo.path,
                                 .scale = 1.0,
                                 .pageOffset = 0,
                                 .labelVisible = false,
                                 .honorExifOrientation = true});
      cellConfig.imageIndex = imageIndex;
    }

    auto &imageSet = session->images[imageIndex];
    // ラベルも名前に合わせる。既存の画像セットへ割り当て直した場合、
    // 更新しないと前の画像の名前が残る。
    imageSet.label = droppedInfo.name;
    imageSet.name = droppedInfo.name;
    imageSet.path = droppedInfo.path;
    imageSet.resolvedPaths = droppedInfo.resolvedPaths;
    if (droppedInfo.droppedPageIndex) {
      imageSet.pageOffset = targetDisplayPage - *droppedInfo.droppedPageIndex;
    }
    cellConfig.scale = imageSet.scale;
  }

  session->cells = *cellLayoutCache;
  heiran::app::SessionPathResolver::resolveImagePaths(session);
  normalizeImageSetPageOffsets(session, &targetDisplayPage);

  result.changed = true;
  result.preserveView = !result.layoutChanged;
  result.targetPage = targetDisplayPage;
  if (paths.size() > assignCount) {
    result.statusMessage =
        QCoreApplication::translate("SessionMutationController",
                                    "Applied %1 dropped item(s). Ignored %2 item(s) because there "
                                    "were not enough visible cells.")
            .arg(assignCount)
            .arg(paths.size() - assignCount);
  } else if (result.layoutChanged) {
    result.statusMessage =
        QCoreApplication::translate("SessionMutationController",
                                    "Applied %1 dropped item(s). The layout is now %2x%3.")
            .arg(assignCount)
            .arg(session->viewer.layout.rows)
            .arg(session->viewer.layout.cols);
  } else {
    result.statusMessage =
        QCoreApplication::translate("SessionMutationController", "Applied %1 dropped item(s).")
            .arg(assignCount);
  }
  return result;
}

bool SessionMutationController::clearAllCells(
    heiran::domain::SessionModel *session, QVector<heiran::domain::CellConfig> *cellLayoutCache) {
  bool changed = false;
  for (auto &cell : session->cells) {
    if (cell.imageIndex >= 0) {
      cell.imageIndex = -1;
      changed = true;
    }
  }
  for (auto &cell : *cellLayoutCache) {
    cell.imageIndex = -1;
  }
  return changed;
}

bool SessionMutationController::applyLabelVisibility(heiran::domain::SessionModel *session,
                                                     int cellIndex, bool visible) {
  if (cellIndex < 0 || cellIndex >= session->cells.size()) {
    return false;
  }

  const int imageIndex = session->cells.at(cellIndex).imageIndex;
  if (imageIndex < 0 || imageIndex >= session->images.size()) {
    return false;
  }

  if (session->images[imageIndex].labelVisible == visible) {
    return false;
  }

  session->images[imageIndex].labelVisible = visible;
  return true;
}

bool SessionMutationController::applyLabelText(heiran::domain::SessionModel *session, int cellIndex,
                                               const QString &label) {
  if (cellIndex < 0 || cellIndex >= session->cells.size()) {
    return false;
  }

  const int imageIndex = session->cells.at(cellIndex).imageIndex;
  if (imageIndex < 0 || imageIndex >= session->images.size()) {
    return false;
  }

  if (session->images[imageIndex].label == label) {
    return false;
  }

  session->images[imageIndex].label = label;
  return true;
}

bool SessionMutationController::assignImageIndexToCell(
    heiran::domain::SessionModel *session, QVector<heiran::domain::CellConfig> *cellLayoutCache,
    int cellIndex, int imageIndex) {
  if (cellIndex < 0 || cellIndex >= session->cells.size()) {
    return false;
  }
  if (imageIndex >= session->images.size()) {
    return false;
  }
  if (session->cells[cellIndex].imageIndex == imageIndex) {
    return false;
  }

  session->cells[cellIndex].imageIndex = imageIndex;
  if (imageIndex >= 0) {
    session->cells[cellIndex].scale = session->images.at(imageIndex).scale;
  }
  if (cellLayoutCache->size() < session->cells.size()) {
    cellLayoutCache->resize(session->cells.size());
  }
  (*cellLayoutCache)[cellIndex] = session->cells[cellIndex];
  return true;
}

void SessionMutationController::applyImageSetEdit(
    heiran::domain::SessionModel *session, const QVector<heiran::domain::ImageSet> &images,
    const QVector<heiran::domain::CellConfig> &cells) {
  session->images = images;
  session->cells = cells;
  heiran::app::SessionPathResolver::resolveImagePaths(session);
}

}  // namespace heiran::ui
