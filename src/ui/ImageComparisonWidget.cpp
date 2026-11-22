#include "heiran/ui/ImageComparisonWidget.hpp"

#include "heiran/ui/ActionIcons.hpp"
#include "heiran/ui/DiffRenderer.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPromise>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <initializer_list>
#include <utility>

namespace heiran::ui {
namespace {

using heiran::app::FileComparisonStatus;
using heiran::domain::DiffComparisonBasis;
using heiran::domain::DiffMode;
using heiran::domain::DiffSizeMatch;

constexpr int kDiffCell = 2;
enum Display { SideBySide, Blink };
enum AlternationMode { Automatic, Manual };
enum HighlightMethod { Steady, Blinking };
enum Filter { AllFiles, Changes, Modified, LeftOnly, RightOnly };
constexpr int kThresholdPrecision = 1000;

template <typename SpinBox> void syncSlider(QSlider *slider, const SpinBox *spin, int precision) {
  const QSignalBlocker blocker(slider);
  slider->setRange(qRound(double(spin->minimum()) * precision),
                   qRound(double(spin->maximum()) * precision));
  slider->setSingleStep(qRound(double(spin->singleStep()) * precision));
  slider->setPageStep(qMax(slider->singleStep(), (slider->maximum() - slider->minimum()) / 10));
  slider->setValue(qRound(double(spin->value()) * precision));
}

template <typename SpinBox>
QSlider *addSliderField(QFormLayout *form, const QString &label, SpinBox *spin,
                        const char *objectName, int precision = 1) {
  auto *field = new QWidget(spin->parentWidget());
  auto *row = new QHBoxLayout(field);
  row->setContentsMargins(0, 0, 0, 0);
  auto *slider = new QSlider(Qt::Horizontal, field);
  slider->setObjectName(objectName);
  slider->setAccessibleName(label);
  spin->setAccessibleName(label);
  syncSlider(slider, spin, precision);
  const auto reflectSlider = [spin, precision](int value) {
    spin->setValue(static_cast<decltype(spin->value())>(double(value) / precision));
  };
  QObject::connect(slider, &QSlider::valueChanged, spin, reflectSlider);
  QObject::connect(spin, &SpinBox::valueChanged, slider, [slider, precision](auto value) {
    const QSignalBlocker blocker(slider);
    slider->setValue(qRound(double(value) * precision));
  });
  row->addWidget(slider, 1);
  row->addWidget(spin);
  field->setFocusProxy(slider);
  form->addRow(label, field);
  return slider;
}

QLabel *messageLabel(QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setMinimumWidth(0);
  return label;
}

struct Choice {
  int id;
  QString text;
  QString description;
  const char *icon = nullptr;
};

QButtonGroup *addChoices(QFormLayout *form, QWidget *parent, const QString &label, const char *name,
                         int columns, std::initializer_list<Choice> choices) {
  auto *group = new QButtonGroup(parent);
  group->setObjectName(name);
  auto *field = new QWidget(parent);
  field->setAccessibleName(label);
  auto *buttons = new QGridLayout(field);
  buttons->setContentsMargins(0, 0, 0, 0);
  for (int column = 0; column < columns; ++column) {
    buttons->setColumnStretch(column, 1);
  }
  int index = 0;
  for (const auto &choice : choices) {
    auto *button = new QToolButton(field);
    button->setProperty("comparisonChoice", true);
    button->setText(choice.text);
    button->setToolTip(choice.description);
    button->setAccessibleDescription(choice.description);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    if (choice.icon) {
      button->setIcon(themedIcon(choice.icon));
    }
    button->setCheckable(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    group->addButton(button, choice.id);
    buttons->addWidget(button, index / columns, index % columns);
    ++index;
  }
  group->button(choices.begin()->id)->setChecked(true);
  if (!label.isEmpty()) {
    form->addRow(new QLabel(label, parent));
  }
  form->addRow(field);
  return group;
}

void setChoicesEnabled(QButtonGroup *group, bool enabled) {
  for (auto *button : group->buttons()) {
    button->setEnabled(enabled);
  }
}

QFormLayout *addSettingsGroup(QVBoxLayout *sidebar, const QString &title) {
  auto *group = new QGroupBox(title, sidebar->parentWidget());
  auto *form = new QFormLayout(group);
  form->setContentsMargins(0, 0, 0, 0);
  sidebar->addWidget(group);
  return form;
}

}  // namespace

ImageComparisonWidget::ImageComparisonWidget(QWidget *parent)
    : QWidget(parent), highlightTimer_(this) {
  setObjectName("imageComparison");
  setFocusPolicy(Qt::StrongFocus);
  setWindowTitle(tr("Image Difference - Heiran"));
  auto *root = new QVBoxLayout(this);
  auto *splitter = new QSplitter(this);
  splitter->setChildrenCollapsible(false);
  root->addWidget(splitter);
  auto *sidebarPane = new QWidget(splitter);
  auto *sidebarLayout = new QVBoxLayout(sidebarPane);
  sidebarLayout->setContentsMargins(0, 0, 8, 0);
  auto *sidebarScroll = new QScrollArea(sidebarPane);
  sidebarScroll->setFrameShape(QFrame::NoFrame);
  sidebarScroll->setWidgetResizable(true);
  sidebarLayout->addWidget(sidebarScroll, 1);
  auto *sidebar = new QWidget;
  sidebarScroll->setWidget(sidebar);
  auto *left = new QVBoxLayout(sidebar);
  left->setContentsMargins(0, 0, 0, 0);
  paths_ = messageLabel(sidebar);
  left->addWidget(paths_);

  folderControls_ = new QGroupBox(tr("Folder contents"), sidebar);
  auto *files = new QVBoxLayout(folderControls_);
  files->setContentsMargins(0, 0, 0, 0);
  filter_ = new QComboBox(folderControls_);
  filter_->setObjectName("folderComparisonFilter");
  filter_->addItems({tr("All files"), tr("Changes only"), tr("Modified files"), tr("Left only"),
                     tr("Right only")});
  filter_->setCurrentIndex(Changes);
  auto *reload = new QPushButton(themedIcon("refresh-cw"), tr("Refresh"), folderControls_);
  auto *filterRow = new QHBoxLayout;
  filterRow->addWidget(filter_, 1);
  filterRow->addWidget(reload);
  files->addLayout(filterRow);
  entries_ = new QTreeWidget(folderControls_);
  entries_->setObjectName("folderComparisonEntries");
  entries_->setHeaderLabels({tr("Relative path"), tr("File status")});
  entries_->setRootIsDecorated(false);
  entries_->setUniformRowHeights(true);
  entries_->header()->setStretchLastSection(false);
  entries_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  entries_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  files->addWidget(entries_, 1);
  folderStatus_ = messageLabel(folderControls_);
  files->addWidget(folderStatus_);
  previousFile_ =
      new QPushButton(themedIcon("arrow-left"), tr("Previous changed file"), folderControls_);
  previousFile_->setObjectName("previousChangedFile");
  nextFile_ = new QPushButton(themedIcon("arrow-right"), tr("Next changed file"), folderControls_);
  nextFile_->setObjectName("nextChangedFile");
  files->addWidget(previousFile_);
  files->addWidget(nextFile_);
  externalViewer_ = new QPushButton(themedIcon("external-link"),
                                    tr("Open in External Diff Viewer..."), folderControls_);
  externalViewer_->setObjectName("openExternalDiffViewer");
  externalViewer_->setEnabled(false);
  auto *chooseViewer = new QToolButton(folderControls_);
  chooseViewer->setIcon(themedIcon("settings"));
  chooseViewer->setToolTip(tr("Choose an External Diff Viewer"));
  chooseViewer->setAccessibleName(chooseViewer->toolTip());
  auto *viewerRow = new QHBoxLayout;
  viewerRow->addWidget(externalViewer_, 1);
  viewerRow->addWidget(chooseViewer);
  files->addLayout(viewerRow);
  left->addWidget(folderControls_, 1);
  folderControls_->hide();

  auto *viewForm = addSettingsGroup(left, tr("View"));
  auto *viewGroup = viewForm->parentWidget();
  display_ = addChoices(
      viewForm, viewGroup, tr("Display mode"), "imageComparisonDisplay", 2,
      {{SideBySide, tr("Side by side"), tr("Shows both images side by side."), "panel-left"},
       {Blink, tr("Alternate images"), tr("Switches between the two images in the same position."),
        "layers"}});
  auto *displayHint = messageLabel(viewGroup);
  displayHint->setObjectName("imageComparisonDisplayHint");
  displayHint->setText(display_->checkedButton()->toolTip());
  viewForm->addRow(displayHint);

  alternationControls_ = new QWidget(viewGroup);
  auto *alternationForm = new QFormLayout(alternationControls_);
  alternationForm->setContentsMargins(0, 0, 0, 0);
  alternationMode_ = addChoices(
      alternationForm, alternationControls_, tr("Image switching"),
      "imageComparisonAlternationMode", 2,
      {{Automatic, tr("Automatic blink"), tr("Switches images at a regular interval.")},
       {Manual, tr("Manual switching"), tr("Keeps the selected image visible until you switch.")}});
  alternationIntervalControls_ = new QWidget(alternationControls_);
  auto *intervalForm = new QFormLayout(alternationIntervalControls_);
  intervalForm->setContentsMargins(0, 0, 0, 0);
  alternationInterval_ = new QSpinBox(alternationIntervalControls_);
  alternationInterval_->setObjectName("imageComparisonAlternationInterval");
  alternationInterval_->setRange(100, 3000);
  alternationInterval_->setSingleStep(50);
  alternationInterval_->setSuffix(QStringLiteral(" ms"));
  alternationInterval_->setKeyboardTracking(false);
  alternationInterval_->setValue(500);
  addSliderField(intervalForm, tr("Switch interval"), alternationInterval_,
                 "imageComparisonAlternationIntervalSlider");
  alternationForm->addRow(alternationIntervalControls_);

  alternateFrameControls_ = new QWidget(alternationControls_);
  auto *frameForm = new QFormLayout(alternateFrameControls_);
  frameForm->setContentsMargins(0, 0, 0, 0);
  alternateFrame_ =
      addChoices(frameForm, alternateFrameControls_, {}, "imageComparisonAlternateFrame", 2,
                 {{0, tr("Left image"), tr("Show the left image (Alt+Left)."), "arrow-left"},
                  {1, tr("Right image"), tr("Show the right image (Alt+Right)."), "arrow-right"}});
  alternateFrame_->button(0)->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
  alternateFrame_->button(1)->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
  alternationForm->addRow(alternateFrameControls_);
  alternateFrameControls_->hide();
  viewForm->addRow(alternationControls_);
  alternationControls_->hide();

  showDifferenceMap_ = new QCheckBox(tr("Show difference map"), viewGroup);
  showDifferenceMap_->setIcon(themedIcon("layout-grid"));
  showDifferenceMap_->setObjectName("imageComparisonShowDifferenceMap");
  viewForm->addRow(showDifferenceMap_);
  mapAppearance_ = new QWidget(viewGroup);
  auto *mapForm = new QFormLayout(mapAppearance_);
  mapForm->setContentsMargins(0, 0, 0, 0);
  mapGain_ = new QComboBox(mapAppearance_);
  mapGain_->setObjectName("imageComparisonMapGain");
  for (const int gain : {1, 2, 4, 8, 16, 32}) {
    mapGain_->addItem(tr("%1×").arg(gain), gain);
  }
  mapGain_->setToolTip(tr("Brightens the map without changing which differences are detected."));
  mapForm->addRow(tr("Map brightness"), mapGain_);
  mapLegend_ = messageLabel(mapAppearance_);
  mapLegend_->setObjectName("imageComparisonMapLegend");
  mapForm->addRow(mapLegend_);
  viewForm->addRow(mapAppearance_);
  zoom_ = new QDoubleSpinBox(viewGroup);
  zoom_->setObjectName("imageComparisonZoom");
  zoom_->setRange(0.01, 1000000.0);
  zoom_->setDecimals(2);
  zoom_->setSuffix(QStringLiteral("%"));
  zoom_->setSingleStep(10.0);
  zoom_->setKeyboardTracking(false);
  zoom_->setValue(100.0);
  viewForm->addRow(tr("Zoom"), zoom_);

  auto *detectionForm = addSettingsGroup(left, tr("Difference detection"));
  auto *detectionGroup = detectionForm->parentWidget();
  calculation_ = new QComboBox(detectionGroup);
  calculation_->setObjectName("imageComparisonCalculation");
  calculation_->addItem(tr("RGB difference (maximum channel)"), static_cast<int>(DiffMode::RgbMax));
  calculation_->addItem(tr("Brightness"), static_cast<int>(DiffMode::Absolute));
  calculation_->addItem(tr("Color difference (ΔE00)"), static_cast<int>(DiffMode::DeltaE));
  calculation_->addItem(tr("Structural similarity (SSIM)"), static_cast<int>(DiffMode::Ssim));
  detectionForm->addRow(tr("Compare by"), calculation_);
  comparisonBasis_ =
      addChoices(detectionForm, detectionGroup, tr("Transparency comparison method"),
                 "imageComparisonBasis", 3,
                 {{static_cast<int>(DiffComparisonBasis::PixelValues), tr("Pixel values"),
                   tr("Compares pixel values and opacity directly.")},
                  {static_cast<int>(DiffComparisonBasis::WhiteBackground), tr("White"),
                   tr("Compares how the images look on a white background.")},
                  {static_cast<int>(DiffComparisonBasis::BlackBackground), tr("Black"),
                   tr("Compares how the images look on a black background.")}});
  ignoreTransparentRgb_ = new QCheckBox(tr("Ignore fully transparent color"), detectionGroup);
  ignoreTransparentRgb_->setObjectName("imageComparisonIgnoreTransparentRgb");
  ignoreTransparentRgb_->setToolTip(
      tr("Ignore RGB differences where both images are fully transparent."));
  ignoreTransparentRgb_->setChecked(true);
  detectionForm->addRow(ignoreTransparentRgb_);
  sizeMatch_ = new QComboBox(detectionGroup);
  sizeMatch_->setObjectName("imageComparisonSizeMatch");
  sizeMatch_->addItems(
      {tr("Align top left"), tr("Resize to left"), tr("Resize to right"), tr("Resize to larger")});
  detectionForm->addRow(tr("Image size"), sizeMatch_);
  const heiran::domain::DiffSettings defaultDiff;
  threshold_ = new QDoubleSpinBox(detectionGroup);
  threshold_->setObjectName("imageComparisonThreshold");
  threshold_->setDecimals(3);
  threshold_->setRange(0.0, 255.0);
  threshold_->setKeyboardTracking(false);
  threshold_->setValue(defaultDiff.threshold);
  auto *thresholdSlider = addSliderField(detectionForm, tr("Threshold"), threshold_,
                                         "imageComparisonThresholdSlider", kThresholdPrecision);
  auto *thresholdHint = messageLabel(detectionGroup);
  thresholdHint->setObjectName("imageComparisonThresholdHint");
  detectionForm->addRow(thresholdHint);
  auto *highlightForm = addSettingsGroup(left, tr("Difference highlighting"));
  auto *highlightGroup = highlightForm->parentWidget();
  highlightDifferences_ = new QCheckBox(tr("Highlight differences"), highlightGroup);
  highlightDifferences_->setObjectName("imageComparisonHighlightDifferences");
  highlightDifferences_->setIcon(themedIcon("scan-line"));
  highlightDifferences_->setToolTip(
      tr("Highlights differences on both images in side-by-side view."));
  highlightForm->addRow(highlightDifferences_);
  blockSize_ = new QSpinBox(highlightGroup);
  blockSize_->setObjectName("imageComparisonBlockSize");
  blockSize_->setRange(1, 128);
  blockSize_->setSuffix(QStringLiteral(" px"));
  blockSize_->setKeyboardTracking(false);
  blockSize_->setValue(defaultDiff.highlightBlockSize);
  addSliderField(highlightForm, tr("Block size"), blockSize_, "imageComparisonBlockSizeSlider");

  highlightMethod_ = addChoices(
      highlightForm, highlightGroup, {}, "imageComparisonHighlightMethod", 2,
      {{Steady, tr("Steady"), tr("Keeps the highlight color visible."), "scan-line"},
       {Blinking, tr("Blink highlight"), tr("Flashes only the highlight color."), "sun"}});
  highlightOpacity_ = new QSpinBox(highlightGroup);
  highlightOpacity_->setObjectName("imageComparisonHighlightOpacity");
  highlightOpacity_->setRange(0, 100);
  highlightOpacity_->setSuffix(QStringLiteral("%"));
  highlightOpacity_->setSingleStep(5);
  highlightOpacity_->setKeyboardTracking(false);
  highlightOpacity_->setValue(defaultDiff.highlightOpacity);
  addSliderField(highlightForm, tr("Highlight opacity"), highlightOpacity_,
                 "imageComparisonHighlightOpacitySlider");

  auto *resultsForm = addSettingsGroup(left, tr("Comparison results"));
  auto *resultsGroup = resultsForm->parentWidget();
  auto *navigation = new QHBoxLayout;
  previousDifference_ =
      new QPushButton(themedIcon("arrow-left"), tr("Previous difference"), resultsGroup);
  nextDifference_ = new QPushButton(themedIcon("arrow-right"), tr("Next difference"), resultsGroup);
  nextDifference_->setObjectName("nextComparisonDifference");
  const auto setShortcut = [](QPushButton *button, const QKeySequence &shortcut) {
    button->setShortcut(shortcut);
    button->setToolTip(button->text() + QStringLiteral(" (") +
                       shortcut.toString(QKeySequence::NativeText) + u')');
  };
  setShortcut(previousDifference_, QKeySequence(Qt::Key_F7));
  setShortcut(nextDifference_, QKeySequence(Qt::Key_F8));
  setShortcut(previousFile_, QKeySequence(Qt::CTRL | Qt::Key_F7));
  setShortcut(nextFile_, QKeySequence(Qt::CTRL | Qt::Key_F8));
  navigation->addWidget(previousDifference_);
  navigation->addWidget(nextDifference_);
  resultsForm->addRow(navigation);
  differences_ = messageLabel(resultsGroup);
  differences_->setObjectName("imageComparisonDetectionStatus");
  pixelStatus_ = messageLabel(resultsGroup);
  pixelStatus_->setObjectName("imageComparisonPixelStatus");
  resultsForm->addRow(differences_);
  resultsForm->addRow(pixelStatus_);
  left->addStretch();
  auto *footer = new QHBoxLayout;
  fit_ = new QPushButton(themedIcon("scan"), tr("Fit"), sidebarPane);
  auto *close = new QPushButton(themedIcon("x"), tr("End Comparison"), sidebarPane);
  close->setObjectName("endImageComparison");
  footer->addWidget(fit_);
  footer->addStretch();
  footer->addWidget(close);
  sidebarLayout->addLayout(footer);

  auto *comparisonPane = new QWidget(splitter);
  auto *comparisonLayout = new QVBoxLayout(comparisonPane);
  comparisonLayout->setContentsMargins(0, 0, 0, 0);
  preview_ = new QStackedWidget(comparisonPane);
  comparisonLayout->addWidget(preview_, 1);
  previewMessage_ = messageLabel(preview_);
  previewMessage_->setAlignment(Qt::AlignCenter);
  preview_->addWidget(previewMessage_);
  grid_ = new ViewerGridWidget(preview_);
  grid_->setComparisonPresentation();
  grid_->setAnimationPlaying(false);
  grid_->setFitOnPageChange(true);
  preview_->addWidget(grid_);
  auto *pixelReadout = new QLabel(comparisonPane);
  pixelReadout->setObjectName("imageComparisonHoverPixels");
  pixelReadout->setAccessibleName(QCoreApplication::translate("MainWindow", "Pixel Info"));
  pixelReadout->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  pixelReadout->setTextFormat(Qt::PlainText);
  pixelReadout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  pixelReadout->setMargin(6);
  pixelReadout->setFrameShape(QFrame::StyledPanel);
  pixelReadout->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  const QString hoverPrompt = tr("Hover over an image to inspect pixel values.");
  const QStringList heightSample(3, hoverPrompt);
  pixelReadout->setText(heightSample.join('\n'));
  pixelReadout->setFixedHeight(pixelReadout->sizeHint().height());
  comparisonLayout->addWidget(pixelReadout);
  const auto updatePixelReadout = [pixelReadout, hoverPrompt](const QStringList &lines) {
    pixelReadout->setText(lines.isEmpty() ? hoverPrompt : lines.join('\n'));
  };
  updatePixelReadout({});
  connect(grid_, &ViewerGridWidget::pixelInfoChanged, pixelReadout, updatePixelReadout);
  connect(grid_, &ViewerGridWidget::sessionAboutToChange, pixelReadout,
          [updatePixelReadout]() { updatePixelReadout({}); });
  sidebarScroll->setMinimumWidth(sidebar->minimumSizeHint().width() +
                                 sidebarScroll->verticalScrollBar()->sizeHint().width());
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({330, 830});

  connect(close, &QPushButton::clicked, this, &ImageComparisonWidget::endRequested);
  connect(fit_, &QPushButton::clicked, this, [this]() {
    grid_->resetView();
    grid_->requestFitToCells();
  });
  connect(reload, &QPushButton::clicked, this, &ImageComparisonWidget::reloadFolders);
  connect(&folderTask_, &QFutureWatcherBase::finished, this,
          &ImageComparisonWidget::populateEntries);
  connect(filter_, &QComboBox::currentIndexChanged, this, &ImageComparisonWidget::updateFilter);
  connect(entries_, &QTreeWidget::currentItemChanged, this,
          &ImageComparisonWidget::showCurrentEntry);
  connect(previousFile_, &QPushButton::clicked, this, [this]() { stepFiles(-1); });
  connect(nextFile_, &QPushButton::clicked, this, [this]() { stepFiles(1); });
  connect(externalViewer_, &QPushButton::clicked, this, &ImageComparisonWidget::openExternalViewer);
  connect(chooseViewer, &QToolButton::clicked, this, [this]() { chooseExternalViewer(); });
  connect(display_, &QButtonGroup::idToggled, this, [this, displayHint](int id, bool checked) {
    if (checked) {
      displayHint->setText(display_->button(id)->toolTip());
      applyDisplayMode();
    }
  });
  connect(alternationMode_, &QButtonGroup::idToggled, this, [this](int mode, bool checked) {
    if (!checked) {
      return;
    }
    const int frame = grid_->diffBlinkFrame(kDiffCell);
    if (mode == Manual && frame >= 0) {
      alternateFrame_->button(frame)->setChecked(true);
    }
    applyAlternationPlayback();
  });
  connect(alternateFrame_, &QButtonGroup::idClicked, this,
          &ImageComparisonWidget::applyAlternationPlayback);
  connect(alternationInterval_, &QSpinBox::valueChanged, this,
          &ImageComparisonWidget::applyAlternationPlayback);
  connect(highlightDifferences_, &QCheckBox::toggled, this,
          &ImageComparisonWidget::updateHighlights);
  connect(highlightMethod_, &QButtonGroup::idToggled, this, [this](int, bool checked) {
    if (checked) {
      updateHighlights();
    }
  });
  connect(showDifferenceMap_, &QCheckBox::toggled, this, &ImageComparisonWidget::applyDisplayMode);
  connect(mapGain_, &QComboBox::currentIndexChanged, this, [this]() {
    grid_->setComparisonMapGain(mapGain_->currentData().toInt());
    updateMapLegend();
  });
  highlightTimer_.setObjectName("comparisonHighlightTimer");
  highlightTimer_.setInterval(500);
  connect(&highlightTimer_, &QTimer::timeout, this, [this]() {
    highlightPhaseVisible_ = !highlightPhaseVisible_;
    grid_->setComparisonHighlightsVisible(highlightPhaseVisible_);
  });
  const auto updateThreshold = [this, thresholdSlider, thresholdHint]() {
    const auto mode = static_cast<DiffMode>(calculation_->currentData().toInt());
    const QSignalBlocker blocker(threshold_);
    threshold_->setRange(0.0, mode == DiffMode::Ssim ? 1.0 : 255.0);
    threshold_->setSingleStep(mode == DiffMode::Ssim ? 0.01 : 1.0);
    threshold_->setValue(mode == DiffMode::Ssim ? 0.9 : mode == DiffMode::DeltaE ? 2.3 : 8.0);
    syncSlider(thresholdSlider, threshold_, kThresholdPrecision);
    QString explanation;
    switch (mode) {
      case DiffMode::Ssim:
        explanation = tr("Detects SSIM below this value (0–1).");
        break;
      case DiffMode::DeltaE:
        explanation = tr("Detects ΔE00 above this value.");
        break;
      case DiffMode::RgbMax:
        explanation = tr("Detects RGB differences above this value (0–255).");
        break;
      default:
        explanation = tr("Detects brightness differences above this value (0–255).");
        break;
    }
    thresholdHint->setText(explanation);
    threshold_->setAccessibleDescription(explanation);
    thresholdSlider->setAccessibleDescription(explanation);
    updateMapLegend();
    applyDiffSettings();
  };
  connect(calculation_, &QComboBox::currentIndexChanged, this, updateThreshold);
  updateThreshold();
  connect(sizeMatch_, &QComboBox::currentIndexChanged, this, [this]() {
    applyDiffSettings();
    if (display_->checkedId() == Blink) {
      applyDisplayMode();
    }
  });
  connect(threshold_, &QDoubleSpinBox::valueChanged, this,
          &ImageComparisonWidget::applyDiffSettings);
  connect(blockSize_, &QSpinBox::valueChanged, this, &ImageComparisonWidget::applyDiffSettings);
  connect(highlightOpacity_, &QSpinBox::valueChanged, this,
          &ImageComparisonWidget::applyDiffSettings);
  connect(zoom_, &QDoubleSpinBox::valueChanged, this,
          [this](double percent) { grid_->setGlobalScale(percent / 100.0); });
  const auto refreshZoom = [this]() {
    if (zoomEditing_) {
      return;
    }
    const QSignalBlocker blocker(zoom_);
    zoom_->setValue(grid_->globalViewState().baseScale * 100.0);
  };
  connect(zoom_->findChild<QLineEdit *>(), &QLineEdit::textEdited, this,
          [this]() { zoomEditing_ = true; });
  connect(zoom_, &QDoubleSpinBox::editingFinished, this, [this, refreshZoom]() {
    zoomEditing_ = false;
    refreshZoom();
  });
  connect(grid_, &ViewerGridWidget::viewChanged, this, refreshZoom);
  connect(grid_, &ViewerGridWidget::statusChanged, this, refreshZoom);
  connect(comparisonBasis_, &QButtonGroup::idToggled, this, [this](int, bool checked) {
    if (!checked) {
      return;
    }
    TransparencyBackground background;
    const auto basis = comparisonBasis();
    if (basis != DiffComparisonBasis::PixelValues) {
      background.solidColor = basis == DiffComparisonBasis::WhiteBackground ? Qt::white : Qt::black;
    }
    grid_->setTransparencyBackground(background);
    applyDiffSettings();
    applyDisplayMode();
    updatePixelStatus();
  });
  connect(ignoreTransparentRgb_, &QCheckBox::toggled, this, [this]() {
    pixelTask_.cancel();
    pixelsStarted_ = false;
    pixelStatus_->clear();
    applyDiffSettings();
    checkPixels();
  });
  const auto jump = [this](int direction) {
    grid_->moveToDiffRegion(kDiffCell, direction);
    updateComparisonControls();
  };
  connect(previousDifference_, &QPushButton::clicked, this, [jump]() { jump(-1); });
  connect(nextDifference_, &QPushButton::clicked, this, [jump]() { jump(1); });
  connect(grid_, &ViewerGridWidget::cellDiffChanged, this,
          &ImageComparisonWidget::updateComparisonControls);
  connect(grid_, &ViewerGridWidget::imageContentChanged, this, &ImageComparisonWidget::checkPixels);
  connect(&pixelTask_, &QFutureWatcherBase::finished, this,
          &ImageComparisonWidget::updatePixelStatus);
  updateComparisonControls();
}

ImageComparisonWidget::~ImageComparisonWidget() {
  folderTask_.cancel();
  pixelTask_.cancel();
}

void ImageComparisonWidget::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
    event->accept();
    Q_EMIT endRequested();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ImageComparisonWidget::openImages(const QStringList &paths) {
  openPreparedImages(StartupPreparationController::prepareDiffImages(paths, QDir::currentPath()));
}

void ImageComparisonWidget::setPixelValueFormat(PixelValueFormat format) {
  grid_->setPixelValueFormat(format);
}

void ImageComparisonWidget::openPreparedImages(PreparedImageSession prepared,
                                               std::shared_ptr<DecodeScheduler> scheduler) {
  pixelTask_.cancel();
  pixelsStarted_ = false;
  blinkPending_ = false;
  pixelStatus_->clear();
  comparingImages_ = prepared.sourcePaths.size() == 2;
  if (leftRoot_.isEmpty()) {
    paths_->setText(prepared.sourcePaths.join('\n'));
  }
  preview_->setCurrentWidget(grid_);
  if (!comparingImages_) {
    grid_->setComparisonMapVisible(false);
  }
  grid_->setSession(prepared.session, 0, std::move(scheduler));
  if (!comparingImages_) {
    grid_->setSoloCell(prepared.session.cells[0].imageIndex >= 0 ? 0 : 1);
  } else {
    applyDiffSettings();
    applyDisplayMode();
  }
  grid_->requestFitToCells();
  updateComparisonControls();
  checkPixels();
}

void ImageComparisonWidget::openFolders(const QString &left, const QString &right) {
  leftRoot_ = left;
  rightRoot_ = right;
  setWindowTitle(tr("Folder Comparison - Heiran"));
  paths_->setText(tr("Left: %1\nRight: %2")
                      .arg(QDir::toNativeSeparators(left), QDir::toNativeSeparators(right)));
  folderControls_->show();
  reloadFolders();
}

void ImageComparisonWidget::reloadFolders() {
  folderTask_.cancel();
  {
    const QSignalBlocker blocker(entries_);
    entries_->clear();
    folderResult_ = {};
  }
  folderStatus_->setText(tr("Comparing files…"));
  filter_->setEnabled(false);
  previousFile_->setEnabled(false);
  nextFile_->setEnabled(false);
  externalViewer_->setEnabled(false);
  showMessage(tr("Comparing folders…"));
  folderTask_.setFuture(
      QtConcurrent::run([left = leftRoot_, right = rightRoot_](
                            QPromise<heiran::app::FolderComparisonResult> &promise) {
        auto result =
            heiran::app::compareFolders(left, right, [&]() { return promise.isCanceled(); });
        if (!promise.isCanceled()) {
          promise.addResult(std::move(result));
        }
      }));
}

QString ImageComparisonWidget::fileStatus(FileComparisonStatus status) const {
  switch (status) {
    case FileComparisonStatus::Identical:
      return tr("Identical");
    case FileComparisonStatus::Different:
      return tr("Modified");
    case FileComparisonStatus::LeftOnly:
      return tr("Left only");
    case FileComparisonStatus::RightOnly:
      return tr("Right only");
    case FileComparisonStatus::Unreadable:
      return tr("Unreadable");
  }
  return {};
}

void ImageComparisonWidget::populateEntries() {
  if (folderTask_.isCanceled() || folderTask_.future().resultCount() == 0) {
    return;
  }
  folderResult_ = folderTask_.result();
  filter_->setEnabled(true);
  for (qsizetype index = 0; index < folderResult_.entries.size(); ++index) {
    const auto &entry = folderResult_.entries.at(index);
    auto *item = new QTreeWidgetItem(entries_, {entry.relativePath, fileStatus(entry.status)});
    item->setData(0, Qt::UserRole, index);
    item->setToolTip(0, entry.relativePath);
  }
  updateFilter();
}

void ImageComparisonWidget::updateFilter() {
  const QSignalBlocker blocker(entries_);
  int visible = 0;
  int changed = 0;
  QTreeWidgetItem *first = nullptr;
  for (int i = 0; i < entries_->topLevelItemCount(); ++i) {
    auto *item = entries_->topLevelItem(i);
    const auto status = folderResult_.entries.at(i).status;
    const int filter = filter_->currentIndex();
    const bool show = filter == AllFiles ||
                      (filter == Changes && status != FileComparisonStatus::Identical) ||
                      (filter == Modified && status == FileComparisonStatus::Different) ||
                      (filter == LeftOnly && status == FileComparisonStatus::LeftOnly) ||
                      (filter == RightOnly && status == FileComparisonStatus::RightOnly);
    item->setHidden(!show);
    if (show) {
      first = first ? first : item;
      ++visible;
      changed += status != FileComparisonStatus::Identical;
    }
  }
  folderStatus_->setText(tr("%1 of %2 files").arg(visible).arg(folderResult_.entries.size()));
  if (!folderResult_.unreadableDirectories.isEmpty()) {
    folderStatus_->setText(folderStatus_->text() + '\n' + tr("Some folders could not be read."));
    folderStatus_->setToolTip(folderResult_.unreadableDirectories.join('\n'));
  } else {
    folderStatus_->setToolTip({});
  }
  previousFile_->setEnabled(changed > 0);
  nextFile_->setEnabled(changed > 0);
  if (!entries_->currentItem() || entries_->currentItem()->isHidden()) {
    entries_->setCurrentItem(first);
    showCurrentEntry();
  }
}

void ImageComparisonWidget::stepFiles(int direction) {
  const int count = entries_->topLevelItemCount();
  int index = entries_->indexOfTopLevelItem(entries_->currentItem());
  if (index < 0) {
    index = direction > 0 ? count - 1 : 0;
  }
  for (int i = 0; i < count; ++i) {
    index = (index + count + direction) % count;
    auto *item = entries_->topLevelItem(index);
    if (!item->isHidden() &&
        folderResult_.entries.at(index).status != FileComparisonStatus::Identical) {
      entries_->setCurrentItem(item);
      entries_->scrollToItem(item);
      return;
    }
  }
}

const heiran::app::FolderComparisonEntry *ImageComparisonWidget::currentEntry() const {
  const auto *item = entries_->currentItem();
  if (!item || item->isHidden()) {
    return nullptr;
  }
  return &folderResult_.entries.at(item->data(0, Qt::UserRole).toLongLong());
}

void ImageComparisonWidget::showCurrentEntry() {
  const auto *entry = currentEntry();
  externalViewer_->setEnabled(entry && !entry->image && !entry->leftPath.isEmpty() &&
                              !entry->rightPath.isEmpty());
  if (!entry) {
    showMessage(tr("No files match the filter."));
  } else if (!entry->image) {
    showMessage(tr("%1\n%2").arg(entry->relativePath, fileStatus(entry->status)));
  } else {
    openPreparedImages(StartupPreparationController::prepareDiffImages(
        {entry->leftPath, entry->rightPath}, QDir::currentPath(),
        {.left = tr("Left: %1").arg(entry->relativePath),
         .right = tr("Right: %1").arg(entry->relativePath)}));
  }
}

void ImageComparisonWidget::showMessage(const QString &message) {
  comparingImages_ = false;
  blinkPending_ = false;
  pixelTask_.cancel();
  pixelsStarted_ = false;
  pixelStatus_->clear();
  grid_->setDiffBlinking(kDiffCell, false);
  grid_->setComparisonMapVisible(false);
  grid_->setSession({});
  previewMessage_->setText(message);
  preview_->setCurrentWidget(previewMessage_);
  updateComparisonControls();
}

void ImageComparisonWidget::openExternalViewer() {
  const auto *entry = currentEntry();
  if (!entry || !externalViewer_->isEnabled()) {
    return;
  }
  const QStringList arguments{QDir::toNativeSeparators(entry->leftPath),
                              QDir::toNativeSeparators(entry->rightPath)};
  QSettings settings;
  QString program = settings.value("comparison/externalViewer").toString();
  if (!QFileInfo(program).isFile()) {
    program = chooseExternalViewer();
    if (program.isEmpty()) {
      return;
    }
  }
  if (!QProcess::startDetached(program, arguments)) {
    QMessageBox::warning(this, windowTitle(), tr("Could not start the external diff viewer."));
  }
}

QString ImageComparisonWidget::chooseExternalViewer() {
  QSettings settings;
  const QString key = "comparison/externalViewer";
  const QString program = QFileDialog::getOpenFileName(this, tr("Choose an External Diff Viewer"),
                                                       settings.value(key).toString(),
                                                       tr("Programs (*.exe);;All Files (*)"));
  if (!program.isEmpty()) {
    settings.setValue(key, program);
  }
  return program;
}

void ImageComparisonWidget::applyDiffSettings() {
  auto diff = grid_->cellDiff(kDiffCell);
  if (!diff) {
    return;
  }
  diff->settings.mode = static_cast<DiffMode>(calculation_->currentData().toInt());
  diff->settings.threshold = threshold_->value();
  diff->settings.comparisonBasis = comparisonBasis();
  diff->settings.ignoreFullyTransparentRgb = ignoreTransparentRgb_->isChecked();
  diff->settings.sizeMatch = static_cast<DiffSizeMatch>(sizeMatch_->currentIndex());
  diff->settings.highlightBlockSize = blockSize_->value();
  diff->settings.highlightOpacity = highlightOpacity_->value();
  grid_->setCellDiff(kDiffCell, diff);
}

void ImageComparisonWidget::applyDisplayMode() {
  if (!comparingImages_) {
    return;
  }
  const bool sideBySide = display_->checkedId() == SideBySide;
  blinkPending_ = false;
  grid_->setDiffBlinking(kDiffCell, false);
  grid_->setComparisonMapVisible(sideBySide && showDifferenceMap_->isChecked());
  grid_->setSoloCell(sideBySide ? -1 : kDiffCell);
  blinkPending_ = !sideBySide;
  updateComparisonControls();
}

void ImageComparisonWidget::applyAlternationPlayback() {
  const bool automatic = alternationMode_->checkedId() == Automatic;
  alternationIntervalControls_->setVisible(automatic);
  alternateFrameControls_->setVisible(!automatic);
  grid_->configureDiffBlinkPlayback(kDiffCell, automatic ? alternationInterval_->value() : 0,
                                    automatic ? std::nullopt
                                              : std::optional(alternateFrame_->checkedId()));
}

void ImageComparisonWidget::updateMapLegend() {
  const auto kind = diffFieldKindOf(static_cast<DiffMode>(calculation_->currentData().toInt()));
  const QString white =
      locale().toString(differenceMapWhiteLevel(kind) / mapGain_->currentData().toInt(), 'g', 4);
  switch (kind) {
    case DiffFieldKind::Ssim:
      mapLegend_->setText(tr("Black: 0 · White: %1 (1 − SSIM)").arg(white));
      break;
    case DiffFieldKind::DeltaE:
      mapLegend_->setText(tr("Black: 0 · White: %1 ΔE00").arg(white));
      break;
    default:
      mapLegend_->setText(tr("Black: 0 · White: %1 (0–255 scale)").arg(white));
      break;
  }
}

void ImageComparisonWidget::updateHighlights() {
  const bool sideBySide = comparingImages_ && display_->checkedId() == SideBySide;
  const bool highlighting = sideBySide && highlightDifferences_->isChecked();
  const bool visible = highlighting && highlightOpacity_->value() > 0;
  showDifferenceMap_->setEnabled(sideBySide);
  mapAppearance_->setVisible(sideBySide && showDifferenceMap_->isChecked());
  highlightDifferences_->setEnabled(sideBySide);
  setChoicesEnabled(highlightMethod_, highlighting);
  highlightOpacity_->parentWidget()->setEnabled(highlighting);

  const auto metrics = grid_->cellDiffMetrics(kDiffCell);
  if (visible && highlightMethod_->checkedId() == Blinking && metrics &&
      metrics->exceededPixelCount > 0) {
    if (!highlightTimer_.isActive()) {
      highlightPhaseVisible_ = true;
      highlightTimer_.start();
    }
  } else {
    highlightTimer_.stop();
    highlightPhaseVisible_ = true;
  }
  grid_->setComparisonHighlightsVisible(visible && highlightPhaseVisible_);
}

void ImageComparisonWidget::updateComparisonControls() {
  const auto metrics = grid_->cellDiffMetrics(kDiffCell);
  const bool ready = comparingImages_ && metrics.has_value();
  const bool changes = ready && metrics->exceededPixelCount > 0;
  setChoicesEnabled(display_, ready);
  alternationControls_->setVisible(comparingImages_ && display_->checkedId() == Blink);
  alternationControls_->setEnabled(ready);
  calculation_->setEnabled(comparingImages_);
  sizeMatch_->setEnabled(comparingImages_);
  blockSize_->parentWidget()->setEnabled(comparingImages_);
  threshold_->parentWidget()->setEnabled(comparingImages_);
  setChoicesEnabled(comparisonBasis_, comparingImages_);
  ignoreTransparentRgb_->setEnabled(comparingImages_ &&
                                    comparisonBasis() == DiffComparisonBasis::PixelValues);
  fit_->setEnabled(preview_->currentWidget() == grid_);
  zoom_->setEnabled(fit_->isEnabled());
  previousDifference_->setEnabled(changes);
  nextDifference_->setEnabled(changes);
  const auto [position, count] = grid_->diffRegionPosition(kDiffCell);
  if (!comparingImages_) {
    differences_->clear();
  } else if (!ready) {
    differences_->setText(grid_->cellDiffMessage(kDiffCell));
  } else if (position > 0) {
    differences_->setText(tr("Difference %1 of %2").arg(position).arg(count));
  } else if (changes) {
    differences_->setText(
        tr("Current settings: %1 pixels detected").arg(metrics->exceededPixelCount));
  } else {
    differences_->setText(tr("Current settings: no differences detected."));
  }
  if (ready && comparisonBasis() == DiffComparisonBasis::PixelValues &&
      grid_->diffHasAlphaChanges(kDiffCell)) {
    differences_->setText(differences_->text() + '\n' + tr("Opacity differs."));
  }
  if (ready && std::exchange(blinkPending_, false)) {
    grid_->setDiffBlinking(kDiffCell, true);
    applyAlternationPlayback();
  }
  updateHighlights();
}

DiffComparisonBasis ImageComparisonWidget::comparisonBasis() const {
  return static_cast<DiffComparisonBasis>(comparisonBasis_->checkedId());
}

void ImageComparisonWidget::updatePixelStatus() {
  if (!pixelsStarted_ || pixelTask_.isCanceled() || pixelTask_.future().resultCount() == 0) {
    return;
  }
  const QString result = pixelTask_.result();
  pixelStatus_->setText(tr("Source images:\n%1").arg(result));
}

void ImageComparisonWidget::checkPixels() {
  if (pixelsStarted_ || !comparingImages_) {
    return;
  }
  const auto *left = grid_->comparisonSource(0);
  const auto *right = grid_->comparisonSource(1);
  if (!left || !right) {
    return;
  }
  pixelsStarted_ = true;
  const bool multiple = left->subImageCount > 1 || right->subImageCount > 1;
  const QString equal = multiple ? tr("First images: pixels match.") : tr("Pixels match.");
  const QString different = multiple ? tr("First images: pixels differ.") : tr("Pixels differ.");
  const QString profile = tr("Color profiles differ.");
  const QString dimensions = left->size() != right->size()
                                 ? tr("Image sizes differ: %1 × %2 / %3 × %4")
                                       .arg(left->size().width())
                                       .arg(left->size().height())
                                       .arg(right->size().width())
                                       .arg(right->size().height())
                                 : QString();
  const bool ignoreTransparentRgb = ignoreTransparentRgb_->isChecked();
  pixelTask_.setFuture(
      QtConcurrent::run([left = *left, right = *right, equal, different, profile, dimensions,
                         ignoreTransparentRgb](QPromise<QString> &promise) {
        const bool same = heiran::app::equalDecodedPixels(left, right, ignoreTransparentRgb,
                                                          [&]() { return promise.isCanceled(); });
        if (!promise.isCanceled()) {
          QString result = same ? equal : different;
          if (left.colorSpace != right.colorSpace) {
            result += '\n' + profile;
          }
          if (!dimensions.isEmpty()) {
            result += '\n' + dimensions;
          }
          promise.addResult(result);
        }
      }));
}

}  // namespace heiran::ui
