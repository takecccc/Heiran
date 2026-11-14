#include "heiran/ui/SettingsDialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace heiran::ui {

namespace {

QPushButton *backgroundColorButton(QColor *color, QWidget *parent, const QString &title,
                                   const char *objectName) {
  auto *button = new QPushButton(parent);
  button->setObjectName(objectName);
  button->setToolTip(title);
  button->setAccessibleName(title);
  const auto refresh = [button, color]() {
    QPixmap swatch(24, 16);
    swatch.fill(*color);
    button->setIcon(QIcon(swatch));
    button->setText(color->name());
  };
  refresh();
  QObject::connect(button, &QPushButton::clicked, button, [button, color, title, refresh]() {
    const QColor chosen =
        QColorDialog::getColor(*color, button, title, QColorDialog::DontUseNativeDialog);
    if (chosen.isValid()) {
      *color = chosen;
      refresh();
    }
  });
  return button;
}

}  // namespace

std::optional<AppSettings> showSettingsDialog(QWidget *parent, const AppSettings &current) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QCoreApplication::translate("SettingsDialog", "Settings"));
  dialog.setMinimumWidth(460);

  auto *layout = new QVBoxLayout(&dialog);

  auto *displayGroup =
      new QGroupBox(QCoreApplication::translate("SettingsDialog", "Display"), &dialog);
  auto *displayForm = new QFormLayout(displayGroup);

  auto *pixelValueCombo = new QComboBox(displayGroup);
  pixelValueCombo->addItem("0 - 255", static_cast<int>(PixelValueFormat::Normalized255));
  pixelValueCombo->addItem("0.0 - 1.0", static_cast<int>(PixelValueFormat::Normalized01));
  pixelValueCombo->setCurrentIndex(current.pixelValueFormat == PixelValueFormat::Normalized01 ? 1
                                                                                              : 0);
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Pixel value unit"),
                      pixelValueCombo);

  auto *fitCheckBox =
      new QCheckBox(QCoreApplication::translate("SettingsDialog",
                                                "Fit the images into their cells on page change"),
                    displayGroup);
  fitCheckBox->setChecked(current.fitOnPageChange);
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Page change"), fitCheckBox);

  auto *bordersCheckBox =
      new QCheckBox(QCoreApplication::translate(
                        "SettingsDialog", "Separate the cells with a dark gutter and an outline"),
                    displayGroup);
  bordersCheckBox->setChecked(current.showCellBorders);
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Cell borders"),
                      bordersCheckBox);

  auto *themeCombo = new QComboBox(displayGroup);
  themeCombo->addItem(QCoreApplication::translate("SettingsDialog", "System"),
                      static_cast<int>(ColorTheme::System));
  themeCombo->addItem(QCoreApplication::translate("SettingsDialog", "Light"),
                      static_cast<int>(ColorTheme::Light));
  themeCombo->addItem(QCoreApplication::translate("SettingsDialog", "Dark"),
                      static_cast<int>(ColorTheme::Dark));
  themeCombo->setCurrentIndex(themeCombo->findData(static_cast<int>(current.colorTheme)));
  themeCombo->setToolTip(QCoreApplication::translate(
      "SettingsDialog",
      "The area that shows the images keeps its neutral dark backdrop either way, so images are "
      "judged against the same background."));
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Color theme"), themeCombo);

  TransparencyBackground background = current.transparencyBackground;
  auto *backgroundCombo = new QComboBox(displayGroup);
  backgroundCombo->setObjectName("transparencyPattern");
  backgroundCombo->addItem(QCoreApplication::translate("SettingsDialog", "Solid color"),
                           static_cast<int>(TransparencyPattern::Solid));
  backgroundCombo->addItem(QCoreApplication::translate("SettingsDialog", "Checkerboard"),
                           static_cast<int>(TransparencyPattern::Checkerboard));
  backgroundCombo->setCurrentIndex(backgroundCombo->findData(static_cast<int>(background.pattern)));
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Transparency background"),
                      backgroundCombo);
  auto *backgroundColors = new QWidget(displayGroup);
  auto *backgroundColorsLayout = new QHBoxLayout(backgroundColors);
  backgroundColorsLayout->setContentsMargins(0, 0, 0, 0);
  auto *solidButton =
      backgroundColorButton(&background.solidColor, backgroundColors,
                            QCoreApplication::translate("SettingsDialog", "Solid background color"),
                            "transparencySolidColor");
  auto *checker1Button =
      backgroundColorButton(&background.checkerColor1, backgroundColors,
                            QCoreApplication::translate("SettingsDialog", "Checkerboard color 1"),
                            "transparencyCheckerColor1");
  auto *checker2Button =
      backgroundColorButton(&background.checkerColor2, backgroundColors,
                            QCoreApplication::translate("SettingsDialog", "Checkerboard color 2"),
                            "transparencyCheckerColor2");
  backgroundColorsLayout->addWidget(solidButton);
  backgroundColorsLayout->addWidget(checker1Button);
  backgroundColorsLayout->addWidget(checker2Button);
  const auto updateBackgroundButtons = [backgroundCombo, solidButton, checker1Button,
                                        checker2Button]() {
    const bool checkerboard = backgroundCombo->currentData().toInt() ==
                              static_cast<int>(TransparencyPattern::Checkerboard);
    solidButton->setVisible(!checkerboard);
    checker1Button->setVisible(checkerboard);
    checker2Button->setVisible(checkerboard);
  };
  updateBackgroundButtons();
  QObject::connect(backgroundCombo, &QComboBox::currentIndexChanged, &dialog,
                   updateBackgroundButtons);
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Background colors"),
                      backgroundColors);

  // 言語の名前は、どの言語で動いていても、その言語の文字で出す
  // (読めない言語になったときに戻れるように)。
  auto *languageCombo = new QComboBox(displayGroup);
  languageCombo->addItem(QCoreApplication::translate("SettingsDialog", "System"),
                         QStringLiteral("auto"));
  languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
  languageCombo->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
  languageCombo->setCurrentIndex(std::max(0, languageCombo->findData(current.language)));
  languageCombo->setToolTip(
      QCoreApplication::translate("SettingsDialog", "Takes effect the next time Heiran starts."));
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Language"), languageCombo);

  auto *magnifyCombo = new QComboBox(displayGroup);
  magnifyCombo->addItem(QCoreApplication::translate("SettingsDialog", "Nearest"),
                        static_cast<int>(MagnifyFilter::Nearest));
  magnifyCombo->addItem(QCoreApplication::translate("SettingsDialog", "Smooth"),
                        static_cast<int>(MagnifyFilter::Smooth));
  magnifyCombo->setCurrentIndex(current.magnifyFilter == MagnifyFilter::Smooth ? 1 : 0);
  magnifyCombo->setToolTip(QCoreApplication::translate(
      "SettingsDialog", "Nearest keeps pixel edges visible. Smooth interpolates neighbours."));
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Magnify filter"),
                      magnifyCombo);

  auto *animationSyncCombo = new QComboBox(displayGroup);
  animationSyncCombo->addItem(QCoreApplication::translate("SettingsDialog", "Auto"),
                              static_cast<int>(AnimationSyncMode::Auto));
  animationSyncCombo->addItem(QCoreApplication::translate("SettingsDialog", "Locked"),
                              static_cast<int>(AnimationSyncMode::Locked));
  animationSyncCombo->addItem(QCoreApplication::translate("SettingsDialog", "Independent"),
                              static_cast<int>(AnimationSyncMode::Independent));
  animationSyncCombo->setCurrentIndex(
      animationSyncCombo->findData(static_cast<int>(current.animationSyncMode)));
  animationSyncCombo->setToolTip(QCoreApplication::translate(
      "SettingsDialog",
      "How animations advance when several cells show one. Auto syncs the cells when the "
      "animations have the same number of frames and plays each at its own speed otherwise. "
      "Locked always keeps every cell on the same frame. Independent always plays each cell at "
      "its own speed. Stepping frame by frame always aligns every cell."));
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Animation sync"),
                      animationSyncCombo);

  auto *minifyCombo = new QComboBox(displayGroup);
  minifyCombo->addItem(QCoreApplication::translate("SettingsDialog", "Area"),
                       static_cast<int>(MinifyFilter::Area));
  minifyCombo->addItem(QCoreApplication::translate("SettingsDialog", "Nearest"),
                       static_cast<int>(MinifyFilter::Nearest));
  minifyCombo->setCurrentIndex(current.minifyFilter == MinifyFilter::Nearest ? 1 : 0);
  minifyCombo->setToolTip(QCoreApplication::translate(
      "SettingsDialog",
      "Area averages 2x2 blocks step by step, which avoids flicker while panning. "
      "Nearest picks a single pixel, which flickers but shows the original values."));
  displayForm->addRow(QCoreApplication::translate("SettingsDialog", "Minify filter"), minifyCombo);
  layout->addWidget(displayGroup);

  auto *colorGroup = new QGroupBox(QCoreApplication::translate("SettingsDialog", "Color"), &dialog);
  auto *colorForm = new QFormLayout(colorGroup);

  auto *profileModeCombo = new QComboBox(colorGroup);
  profileModeCombo->addItem(QCoreApplication::translate("SettingsDialog", "Display profile"),
                            static_cast<int>(DisplayProfileMode::System));
  profileModeCombo->addItem(QCoreApplication::translate("SettingsDialog", "sRGB"),
                            static_cast<int>(DisplayProfileMode::SRgb));
  profileModeCombo->addItem(QCoreApplication::translate("SettingsDialog", "ICC file"),
                            static_cast<int>(DisplayProfileMode::File));
  profileModeCombo->setCurrentIndex(
      profileModeCombo->findData(static_cast<int>(current.displayProfileMode)));
  profileModeCombo->setToolTip(QCoreApplication::translate(
      "SettingsDialog",
      "Display profile uses the profile assigned to the screen. sRGB ignores it, which helps "
      "when comparing against another machine."));
  colorForm->addRow(QCoreApplication::translate("SettingsDialog", "Display profile"),
                    profileModeCombo);

  auto *profilePathRow = new QWidget(colorGroup);
  auto *profilePathLayout = new QHBoxLayout(profilePathRow);
  profilePathLayout->setContentsMargins(0, 0, 0, 0);
  auto *profilePathEdit = new QLineEdit(current.displayProfilePath, profilePathRow);
  profilePathEdit->setPlaceholderText(
      QCoreApplication::translate("SettingsDialog", "Path to an .icc or .icm file"));
  auto *browseButton =
      new QPushButton(QCoreApplication::translate("SettingsDialog", "Browse..."), profilePathRow);
  profilePathLayout->addWidget(profilePathEdit, 1);
  profilePathLayout->addWidget(browseButton);
  colorForm->addRow(QCoreApplication::translate("SettingsDialog", "ICC file"), profilePathRow);

  const auto updateProfilePathState = [profileModeCombo, profilePathRow]() {
    profilePathRow->setEnabled(profileModeCombo->currentData().toInt() ==
                               static_cast<int>(DisplayProfileMode::File));
  };
  updateProfilePathState();
  QObject::connect(profileModeCombo, &QComboBox::currentIndexChanged, &dialog,
                   [updateProfilePathState](int) { updateProfilePathState(); });
  QObject::connect(browseButton, &QPushButton::clicked, &dialog, [&dialog, profilePathEdit]() {
    const QString chosen = QFileDialog::getOpenFileName(
        &dialog, QCoreApplication::translate("SettingsDialog", "Select an ICC profile"),
        profilePathEdit->text(),
        QCoreApplication::translate("SettingsDialog", "ICC profiles (*.icc *.icm)"));
    if (!chosen.isEmpty()) {
      profilePathEdit->setText(chosen);
    }
  });

  auto *intentCombo = new QComboBox(colorGroup);
  intentCombo->addItem(QCoreApplication::translate("SettingsDialog", "Relative colorimetric"),
                       static_cast<int>(RenderingIntent::RelativeColorimetric));
  intentCombo->addItem(QCoreApplication::translate("SettingsDialog", "Perceptual"),
                       static_cast<int>(RenderingIntent::Perceptual));
  intentCombo->addItem(QCoreApplication::translate("SettingsDialog", "Saturation"),
                       static_cast<int>(RenderingIntent::Saturation));
  intentCombo->addItem(QCoreApplication::translate("SettingsDialog", "Absolute colorimetric"),
                       static_cast<int>(RenderingIntent::AbsoluteColorimetric));
  intentCombo->setCurrentIndex(intentCombo->findData(static_cast<int>(current.renderingIntent)));
  intentCombo->setEnabled(supportsRenderingIntents());
  intentCombo->setToolTip(
      supportsRenderingIntents()
          ? QCoreApplication::translate("SettingsDialog",
                                        "Only profiles that carry conversion tables react to this. "
                                        "Matrix profiles give the "
                                        "same result for every intent.")
          : QCoreApplication::translate(
                "SettingsDialog", "This build has no Little-CMS, so the intent cannot be chosen."));
  colorForm->addRow(QCoreApplication::translate("SettingsDialog", "Rendering intent"), intentCombo);

  auto *blackPointCheckBox = new QCheckBox(
      QCoreApplication::translate("SettingsDialog", "Black point compensation"), colorGroup);
  blackPointCheckBox->setChecked(current.blackPointCompensation);
  blackPointCheckBox->setEnabled(supportsRenderingIntents());
  blackPointCheckBox->setToolTip(QCoreApplication::translate(
      "SettingsDialog",
      "Stretches the range so the darkest tone of the image lands on the darkest tone the display "
      "can show."));
  colorForm->addRow(QString(), blackPointCheckBox);

  auto *analysisSpaceCombo = new QComboBox(colorGroup);
  analysisSpaceCombo->addItem(QCoreApplication::translate("SettingsDialog", "Source values"),
                              static_cast<int>(AnalysisValueSpace::Source));
  analysisSpaceCombo->addItem(QCoreApplication::translate("SettingsDialog", "Linear light"),
                              static_cast<int>(AnalysisValueSpace::Linear));
  analysisSpaceCombo->setCurrentIndex(
      analysisSpaceCombo->findData(static_cast<int>(current.analysisValueSpace)));
  analysisSpaceCombo->setToolTip(QCoreApplication::translate(
      "SettingsDialog",
      "Source values reports the numbers as stored in the file. Linear light decodes them first, "
      "which makes differences comparable as light rather than as encoded values."));
  colorForm->addRow(QCoreApplication::translate("SettingsDialog", "Analysis values"),
                    analysisSpaceCombo);

  auto *analysisNote =
      new QLabel(QCoreApplication::translate(
                     "SettingsDialog",
                     "Applies to the pixel readout, loupe, histogram, diff and change graph."),
                 colorGroup);
  analysisNote->setWordWrap(true);
  colorForm->addRow(analysisNote);

  auto *colorNote = new QLabel(
      QCoreApplication::translate(
          "SettingsDialog",
          "Images are converted from their embedded profile to the display profile. Images without "
          "a "
          "profile are treated as sRGB, or as linear when the file stores floating point values."),
      colorGroup);
  colorNote->setWordWrap(true);
  colorForm->addRow(colorNote);
  layout->addWidget(colorGroup);

  auto *cacheGroup = new QGroupBox(QCoreApplication::translate("SettingsDialog", "Cache"), &dialog);
  auto *cacheForm = new QFormLayout(cacheGroup);

  auto *cacheBudgetSpinBox = new QSpinBox(cacheGroup);
  cacheBudgetSpinBox->setRange(kMinDecodeCacheBudgetMib, kMaxDecodeCacheBudgetMib);
  cacheBudgetSpinBox->setSingleStep(64);
  cacheBudgetSpinBox->setSuffix(QCoreApplication::translate("SettingsDialog", " MiB"));
  cacheBudgetSpinBox->setValue(clampDecodeCacheBudgetMib(current.decodeCacheBudgetMib));
  cacheForm->addRow(QCoreApplication::translate("SettingsDialog", "Decode cache budget"),
                    cacheBudgetSpinBox);

  auto *textureBudgetSpinBox = new QSpinBox(cacheGroup);
  textureBudgetSpinBox->setRange(kMinGpuTextureBudgetMib, kMaxGpuTextureBudgetMib);
  textureBudgetSpinBox->setSingleStep(64);
  textureBudgetSpinBox->setSuffix(QCoreApplication::translate("SettingsDialog", " MiB"));
  textureBudgetSpinBox->setValue(clampGpuTextureBudgetMib(current.gpuTextureBudgetMib));
  cacheForm->addRow(QCoreApplication::translate("SettingsDialog", "GPU texture budget"),
                    textureBudgetSpinBox);

  auto *streamingSpinBox = new QSpinBox(cacheGroup);
  streamingSpinBox->setRange(kMinAnimationStreamingThresholdMib,
                             kMaxAnimationStreamingThresholdMib);
  streamingSpinBox->setSingleStep(16);
  streamingSpinBox->setSuffix(QCoreApplication::translate("SettingsDialog", " MiB"));
  streamingSpinBox->setValue(
      clampAnimationStreamingThresholdMib(current.animationStreamingThresholdMib));
  cacheForm->addRow(QCoreApplication::translate("SettingsDialog", "Animation streaming threshold"),
                    streamingSpinBox);

  auto *cacheNote = new QLabel(
      QCoreApplication::translate(
          "SettingsDialog",
          "Soft limit. The least recently used pages are dropped first, and the current page is "
          "kept "
          "while possible.\nHEIRAN_DECODE_CACHE_BUDGET_MB overrides this value when set.\n"
          "The GPU texture budget is shared by all cells. Textures over the limit are dropped from "
          "the least recently used across all cells, so more cells share the same video memory. "
          "HEIRAN_GPU_TEXTURE_BUDGET_MB overrides it.\n"
          "APNG and WebP animations larger than the streaming threshold once expanded (width x "
          "height "
          "x 4 x frames) are kept compressed and only the frames needed are decoded, which saves "
          "memory but uses the CPU while playing. It applies to files opened afterwards. GIF over "
          "the "
          "threshold is "
          "compressed and stored while it plays. HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB overrides "
          "it."),
      cacheGroup);
  cacheNote->setWordWrap(true);
  cacheForm->addRow(cacheNote);
  layout->addWidget(cacheGroup);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttonBox);

  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }

  AppSettings updated = current;
  background.pattern = static_cast<TransparencyPattern>(backgroundCombo->currentData().toInt());
  updated.transparencyBackground = background;
  updated.pixelValueFormat = static_cast<PixelValueFormat>(pixelValueCombo->currentData().toInt());
  updated.fitOnPageChange = fitCheckBox->isChecked();
  updated.showCellBorders = bordersCheckBox->isChecked();
  updated.decodeCacheBudgetMib = cacheBudgetSpinBox->value();
  updated.gpuTextureBudgetMib = textureBudgetSpinBox->value();
  updated.animationStreamingThresholdMib = streamingSpinBox->value();
  updated.animationSyncMode =
      static_cast<AnimationSyncMode>(animationSyncCombo->currentData().toInt());
  updated.magnifyFilter = static_cast<MagnifyFilter>(magnifyCombo->currentData().toInt());
  updated.minifyFilter = static_cast<MinifyFilter>(minifyCombo->currentData().toInt());
  updated.displayProfileMode =
      static_cast<DisplayProfileMode>(profileModeCombo->currentData().toInt());
  updated.displayProfilePath = profilePathEdit->text().trimmed();
  updated.analysisValueSpace =
      static_cast<AnalysisValueSpace>(analysisSpaceCombo->currentData().toInt());
  updated.renderingIntent = static_cast<RenderingIntent>(intentCombo->currentData().toInt());
  updated.blackPointCompensation = blackPointCheckBox->isChecked();
  updated.colorTheme = static_cast<ColorTheme>(themeCombo->currentData().toInt());
  updated.language = languageCombo->currentData().toString();
  return updated;
}

}  // namespace heiran::ui
