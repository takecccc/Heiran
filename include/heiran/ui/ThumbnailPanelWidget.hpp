#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <QWidget>

class QLabel;
class QListWidget;

namespace heiran::ui {

class ThumbnailPanelWidget final : public QWidget {
  Q_OBJECT

public:
  explicit ThumbnailPanelWidget(QWidget *parent = nullptr);
  ~ThumbnailPanelWidget() override;
  void setPages(const QStringList &pagePaths, int currentPage);
  void setCurrentPage(int page);
  void cancelLoading();

Q_SIGNALS:
  void pageSelected(int index);

private:
  void startLoading();

  QListWidget *list_;
  QLabel *count_;
  QLabel *empty_;
  QStringList paths_;
  quint64 generation_ = 0;
  QThreadPool loaderPool_;
  QPointer<QFutureWatcher<QImage>> watcher_;
};

}  // namespace heiran::ui
