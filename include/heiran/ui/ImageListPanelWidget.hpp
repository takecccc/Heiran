#pragma once

#include "heiran/infra/ImageFileDetails.hpp"

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <QWidget>

class QLabel;
class QListWidget;
class QToolButton;
class QSlider;
class QStackedWidget;
class QTreeWidget;
class QPushButton;
class QAction;
class QGroupBox;

namespace heiran::ui {

class ImageListPanelWidget final : public QWidget {
  Q_OBJECT

public:
  explicit ImageListPanelWidget(QWidget *parent = nullptr);
  ~ImageListPanelWidget() override;
  void setPages(const QStringList &pagePaths, int currentPage);
  void setCurrentPage(int page);
  void cancelLoading();
  void setFavoritesOnly(bool enabled);
  void setComparisonActive(bool active);
  void setDetailsVisible(bool details);
  bool detailsVisible() const;
  void setThumbnailWidth(int width);
  int thumbnailWidth() const { return thumbnailWidth_; }
  void setFavoriteStates(const QVector<bool> &states);
  void setDisplayedFiles(const QStringList &cellPaths);
  void setExcludedDisplayedFiles(const QStringList &cellPaths);
  QStringList selectedFilePaths() const;

Q_SIGNALS:
  void pageSelected(int index);
  void favoritesOnlyRequested(bool enabled);
  void detailsVisibleChanged(bool details);
  void thumbnailWidthChanged(int width);
  void compareRequested(const QStringList &paths);
  void finishComparisonRequested();
  void favoritesRequested(const QStringList &paths, bool favorite);

private:
  void startLoading();
  void startDetailsLoading();
  void updateViewMode();
  void updateThumbnailGeometry();
  QSize thumbnailSize() const;
  QVector<int> selectedRows(bool details) const;
  void syncSelection(bool fromDetails);
  void updateSelectionActions();
  void updateItemDescription(int index);

  QListWidget *list_;
  QLabel *count_;
  QLabel *empty_;
  QGroupBox *excludedGroup_;
  QLabel *excludedFiles_;
  QToolButton *favoritesOnly_;
  QAction *thumbnailsAction_;
  QAction *detailsAction_;
  QWidget *thumbnailSizeControls_;
  QSlider *thumbnailSizeSlider_;
  QLabel *thumbnailSizeLabel_;
  int thumbnailWidth_ = 160;
  QStackedWidget *views_;
  QTreeWidget *details_;
  QPushButton *compare_;
  QPushButton *finishComparison_;
  QPushButton *addFavorites_;
  QPushButton *removeFavorites_;
  QAction *addFavoritesAction_;
  QAction *removeFavoritesAction_;
  QStringList paths_;
  quint64 generation_ = 0;
  QThreadPool loaderPool_;
  QPointer<QFutureWatcher<QImage>> watcher_;
  QThreadPool detailsPool_;
  QPointer<QFutureWatcher<heiran::infra::ImageFileDetails>> detailsWatcher_;
};

}  // namespace heiran::ui
