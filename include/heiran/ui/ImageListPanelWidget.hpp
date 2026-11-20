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
class QCheckBox;
class QComboBox;
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
  void setDetailsVisible(bool details);
  bool detailsVisible() const;
  void setFavoriteStates(const QVector<bool> &states);
  void setDisplayedFiles(const QStringList &cellPaths);
  void setExcludedDisplayedFiles(const QStringList &cellPaths);
  QStringList selectedFilePaths() const;

Q_SIGNALS:
  void pageSelected(int index);
  void favoritesOnlyRequested(bool enabled);
  void detailsVisibleChanged(bool details);
  void compareRequested(const QStringList &paths);
  void favoritesRequested(const QStringList &paths, bool favorite);

private:
  void startLoading();
  void startDetailsLoading();
  void updateViewMode();
  QVector<int> selectedRows(bool details) const;
  void syncSelection(bool fromDetails);
  void updateSelectionActions();
  void updateItemDescription(int index);

  QListWidget *list_;
  QLabel *count_;
  QLabel *empty_;
  QGroupBox *excludedGroup_;
  QLabel *excludedFiles_;
  QCheckBox *favoritesOnly_;
  QComboBox *viewMode_;
  QStackedWidget *views_;
  QTreeWidget *details_;
  QPushButton *compare_;
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
