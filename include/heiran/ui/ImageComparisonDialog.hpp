#pragma once

#include "heiran/app/FolderComparison.hpp"
#include "heiran/ui/StartupPreparationController.hpp"

#include <QDialog>
#include <QFutureWatcher>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

namespace heiran::ui {

class ViewerGridWidget;

class ImageComparisonDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ImageComparisonDialog(QWidget *parent = nullptr);
  ~ImageComparisonDialog() override;
  void openImages(const QStringList &paths);
  void openPreparedImages(PreparedImageSession prepared,
                          std::shared_ptr<DecodeScheduler> scheduler = {});
  void openFolders(const QString &left, const QString &right);

private:
  void reloadFolders();
  void populateEntries();
  void updateFilter();
  void stepFiles(int direction);
  void showCurrentEntry();
  void showMessage(const QString &message);
  void openExternalViewer();
  QString chooseExternalViewer();
  void applyDisplayMode();
  void applyDiffSettings();
  void updateComparisonControls();
  heiran::domain::DiffComparisonBasis comparisonBasis() const;
  void updatePixelStatus();
  void checkPixels();
  QString fileStatus(heiran::app::FileComparisonStatus status) const;
  const heiran::app::FolderComparisonEntry *currentEntry() const;

  ViewerGridWidget *grid_;
  QStackedWidget *preview_;
  QLabel *previewMessage_;
  QLabel *paths_;
  QWidget *folderControls_;
  QLabel *folderStatus_;
  QTreeWidget *entries_;
  QComboBox *filter_;
  QPushButton *previousFile_;
  QPushButton *nextFile_;
  QPushButton *externalViewer_;
  QComboBox *display_;
  QComboBox *calculation_;
  QComboBox *comparisonBasis_;
  QComboBox *sizeMatch_;
  QDoubleSpinBox *threshold_;
  QCheckBox *ignoreTransparentRgb_;
  QPushButton *previousDifference_;
  QPushButton *nextDifference_;
  QPushButton *fit_;
  QLabel *differences_;
  QLabel *pixelStatus_;
  QString leftRoot_;
  QString rightRoot_;
  heiran::app::FolderComparisonResult folderResult_;
  QFutureWatcher<heiran::app::FolderComparisonResult> folderTask_;
  QFutureWatcher<QString> pixelTask_;
  bool comparingImages_ = false;
  bool pixelsStarted_ = false;
};

}  // namespace heiran::ui
