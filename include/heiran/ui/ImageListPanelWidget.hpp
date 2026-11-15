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
  QStringList selectedFilePaths() const;

Q_SIGNALS:
  void pageSelected(int index);
  void favoritesOnlyRequested(bool enabled);
  void detailsVisibleChanged(bool details);
  void compareRequested(const QStringList &paths);

private:
  void startLoading();
  void startDetailsLoading();
  void updateViewMode();
  QVector<int> selectedRows(bool details) const;
  void syncSelection(bool fromDetails);
  void updateSelectionActions();

  QListWidget *list_;
  QLabel *count_;
  QLabel *empty_;
  QCheckBox *favoritesOnly_;
  QComboBox *viewMode_;
  QStackedWidget *views_;
  QTreeWidget *details_;
  QPushButton *compare_;
  QStringList paths_;
  quint64 generation_ = 0;
  QThreadPool loaderPool_;
  QPointer<QFutureWatcher<QImage>> watcher_;
  QThreadPool detailsPool_;
  QPointer<QFutureWatcher<heiran::infra::ImageFileDetails>> detailsWatcher_;
};

}  // namespace heiran::ui
