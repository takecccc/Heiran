#pragma once

#include "heiran/app/FolderComparison.hpp"
#include "heiran/ui/PixelValueFormat.hpp"
#include "heiran/ui/StartupPreparationController.hpp"

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;

namespace heiran::ui {

class ViewerGridWidget;

class ImageComparisonWidget final : public QWidget {
  Q_OBJECT

public:
  explicit ImageComparisonWidget(QWidget *parent = nullptr);
  ~ImageComparisonWidget() override;
  void openImages(const QStringList &paths);
  void openPreparedImages(PreparedImageSession prepared,
                          std::shared_ptr<DecodeScheduler> scheduler = {});
  void openFolders(const QString &left, const QString &right);
  void setPixelValueFormat(PixelValueFormat format);

Q_SIGNALS:
  void endRequested();

protected:
  void keyPressEvent(QKeyEvent *event) override;

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
  void applyAlternationPlayback();
  void updateMapLegend();
  void updateHighlights();
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
  QButtonGroup *display_;
  QWidget *alternationControls_;
  QButtonGroup *alternationMode_;
  QWidget *alternationIntervalControls_;
  QSpinBox *alternationInterval_;
  QWidget *alternateFrameControls_;
  QButtonGroup *alternateFrame_;
  QCheckBox *highlightDifferences_;
  QButtonGroup *highlightMethod_;
  QCheckBox *showDifferenceMap_;
  QWidget *mapAppearance_;
  QComboBox *mapGain_;
  QLabel *mapLegend_;
  QComboBox *calculation_;
  QButtonGroup *comparisonBasis_;
  QComboBox *sizeMatch_;
  QDoubleSpinBox *threshold_;
  QSpinBox *blockSize_;
  QSpinBox *highlightOpacity_;
  QDoubleSpinBox *zoom_;
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
  QTimer highlightTimer_;
  bool highlightPhaseVisible_ = true;
  bool blinkPending_ = false;
  bool zoomEditing_ = false;
  bool comparingImages_ = false;
  bool pixelsStarted_ = false;
};

}  // namespace heiran::ui
