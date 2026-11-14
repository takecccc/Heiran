#include "heiran/ui/SettingsDialog.hpp"
#include "heiran/ui/SettingsHelpController.hpp"

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
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace heiran::ui {

namespace {

QLabel *addSettingsNote(QFormLayout *form, const QString &text, const char *name) {
  auto *note = new QLabel(text, form->parentWidget());
  note->setWordWrap(true);
  note->setObjectName(name);
  form->addRow(note);
  return note;
}

void addSettingsPage(QStackedWidget *pages, QGroupBox *group) {
  if (auto *form = qobject_cast<QFormLayout *>(group->layout())) {
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  }
  auto *page = new QWidget;
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(group);
  layout->addStretch();
  auto *scroll = new QScrollArea;
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(page);
  pages->addWidget(scroll);
}

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
  dialog.setMinimumSize(620, 400);
  QSize initialSize(820, 580);
  if (const auto *screen = dialog.screen()) {
    initialSize = initialSize.boundedTo(screen->availableGeometry().size() - QSize(40, 80));
  }
  dialog.resize(initialSize);

  auto *layout = new QVBoxLayout(&dialog);
  auto *content = new QHBoxLayout;
  auto *categories = new QListWidget(&dialog);
  categories->setObjectName("settingsCategories");
  categories->setAccessibleName(QCoreApplication::translate("SettingsDialog", "Categories"));
  categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  const QStringList categoryTitles{
      QCoreApplication::translate("SettingsDialog", "General"),
      QCoreApplication::translate("SettingsDialog", "Display and playback"),
      QCoreApplication::translate("SettingsDialog", "Color management"),
      QCoreApplication::translate("SettingsDialog", "Cache")};
  int categoryWidth = 140;
  for (const auto &title : categoryTitles) {
    auto *item = new QListWidgetItem(title, categories);
    item->setSizeHint(QSize(0, 36));
    categoryWidth =
        std::max(categoryWidth, categories->fontMetrics().horizontalAdvance(title) + 36);
  }
  categories->setFixedWidth(categoryWidth);
  auto *pages = new QStackedWidget(&dialog);
  pages->setObjectName("settingsPages");
  content->addWidget(categories);
  auto *rightSide = new QVBoxLayout;
  rightSide->addWidget(pages, 1);
  content->addLayout(rightSide, 1);
  layout->addLayout(content, 1);
  auto *generalGroup = new QGroupBox(categoryTitles.at(0), &dialog);
  auto *generalForm = new QFormLayout(generalGroup);

  auto *displayGroup = new QGroupBox(categoryTitles.at(1), &dialog);
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

  auto *themeCombo = new QComboBox(generalGroup);
  themeCombo->setObjectName("colorTheme");
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
  auto *languageCombo = new QComboBox(generalGroup);
  languageCombo->setObjectName("language");
  languageCombo->addItem(QCoreApplication::translate("SettingsDialog", "System"),
                         QStringLiteral("auto"));
  languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
  languageCombo->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
  languageCombo->setCurrentIndex(std::max(0, languageCombo->findData(current.language)));
  languageCombo->setToolTip(
      QCoreApplication::translate("SettingsDialog", "Takes effect the next time Heiran starts."));
  generalForm->addRow(QCoreApplication::translate("SettingsDialog", "Language"), languageCombo);
  auto *languageNote =
      addSettingsNote(generalForm, languageCombo->toolTip(), "languageRestartNote");
  generalForm->addRow(QCoreApplication::translate("SettingsDialog", "Color theme"), themeCombo);

  auto *magnifyCombo = new QComboBox(displayGroup);
  magnifyCombo->setObjectName("magnifyFilter");
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
  animationSyncCombo->setObjectName("animationSync");
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
  minifyCombo->setObjectName("minifyFilter");
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

  auto *colorGroup = new QGroupBox(categoryTitles.at(2), &dialog);
  auto *colorForm = new QFormLayout(colorGroup);

  auto *profileModeCombo = new QComboBox(colorGroup);
  profileModeCombo->setObjectName("displayProfileMode");
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
  profilePathEdit->setObjectName("displayProfilePath");
  profilePathEdit->setPlaceholderText(
      QCoreApplication::translate("SettingsDialog", "Path to an .icc or .icm file"));
  auto *browseButton =
      new QPushButton(QCoreApplication::translate("SettingsDialog", "Browse..."), profilePathRow);
  profilePathLayout->addWidget(profilePathEdit, 1);
  profilePathLayout->addWidget(browseButton);
  colorForm->addRow(QCoreApplication::translate("SettingsDialog", "ICC file"), profilePathRow);
  auto *profilePathNote =
      addSettingsNote(colorForm,
                      QCoreApplication::translate(
                          "SettingsDialog", "Select ICC file above to choose a profile file."),
                      "displayProfileFileNote");

  const auto updateProfilePathState = [profileModeCombo, profilePathRow, profilePathNote]() {
    const bool fileMode =
        profileModeCombo->currentData().toInt() == static_cast<int>(DisplayProfileMode::File);
    profilePathRow->setEnabled(fileMode);
    profilePathNote->setVisible(!fileMode);
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
  intentCombo->setObjectName("renderingIntent");
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
  QLabel *intentAvailabilityNote = nullptr;
  if (!supportsRenderingIntents()) {
    intentAvailabilityNote =
        addSettingsNote(colorForm, intentCombo->toolTip(), "renderingIntentUnavailableNote");
  }

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

  auto *cacheGroup = new QGroupBox(categoryTitles.at(3), &dialog);
  auto *cacheForm = new QFormLayout(cacheGroup);

  auto *cacheBudgetSpinBox = new QSpinBox(cacheGroup);
  cacheBudgetSpinBox->setObjectName("decodeCacheBudget");
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

  auto *streamingNote = addSettingsNote(
      cacheForm,
      QCoreApplication::translate("SettingsDialog", "Applies to files opened afterwards."),
      "animationStreamingNote");
  for (auto *group : {generalGroup, displayGroup, colorGroup, cacheGroup}) {
    addSettingsPage(pages, group);
  }
  QObject::connect(categories, &QListWidget::currentRowChanged, pages,
                   &QStackedWidget::setCurrentIndex);
  categories->setCurrentRow(0);

  auto *information =
      new QGroupBox(QCoreApplication::translate("SettingsDialog", "Information"), &dialog);
  information->setObjectName("settingsInformation");
  information->setFixedHeight(std::max(144, dialog.fontMetrics().lineSpacing() * 6 + 44));
  auto *informationLayout = new QVBoxLayout(information);
  auto *helpTitle =
      new QLabel(QCoreApplication::translate("SettingsDialog", "Setting description"), information);
  helpTitle->setObjectName("settingsHelpTitle");
  helpTitle->setTextFormat(Qt::PlainText);
  QFont helpFont = helpTitle->font();
  helpFont.setBold(true);
  helpTitle->setFont(helpFont);
  auto *helpDescription = new QPlainTextEdit(information);
  helpDescription->setObjectName("settingsHelpDescription");
  helpDescription->setAccessibleName(
      QCoreApplication::translate("SettingsDialog", "Setting description"));
  helpDescription->setReadOnly(true);
  helpDescription->setTabChangesFocus(true);
  helpDescription->setFont(dialog.font());
  helpDescription->setFrameShape(QFrame::NoFrame);
  helpDescription->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  helpDescription->setPlainText(QCoreApplication::translate(
      "SettingsDialog",
      "Point to a setting or focus it with the keyboard to read its description."));
  informationLayout->addWidget(helpTitle);
  informationLayout->addWidget(helpDescription, 1);
  rightSide->addWidget(information);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttonBox);

  SettingsHelpController help(&dialog, helpTitle, helpDescription);
  const auto bindHelp = [&help](QFormLayout *form, QWidget *field, const QString &description,
                                QString title = QString()) {
    auto *label = qobject_cast<QLabel *>(form->labelForField(field));
    if (title.isEmpty() && label != nullptr) {
      title = label->text();
    }
    help.setHelp(field, title, description);
    if (label != nullptr) {
      help.setHelp(label, title, description);
    }
  };
  bindHelp(generalForm, languageCombo, languageCombo->toolTip());
  help.setHelp(languageNote, QCoreApplication::translate("SettingsDialog", "Language"),
               languageCombo->toolTip());
  bindHelp(generalForm, themeCombo, themeCombo->toolTip());
  bindHelp(displayForm, pixelValueCombo,
           QCoreApplication::translate("SettingsDialog",
                                       "Changes the unit used for pixel values: 0-255 "
                                       "or 0.0-1.0. The image data is not changed."));
  bindHelp(displayForm, fitCheckBox,
           QCoreApplication::translate("SettingsDialog",
                                       "Fits images to their cells whenever the page changes. You "
                                       "can also fit them at any time with F."));
  bindHelp(displayForm, bordersCheckBox,
           QCoreApplication::translate("SettingsDialog",
                                       "Separates cells with a dark gutter and an outline to make "
                                       "their boundaries easier to see."));
  bindHelp(displayForm, backgroundCombo,
           QCoreApplication::translate(
               "SettingsDialog",
               "Chooses a solid color or checkerboard behind transparent pixels in every cell and "
               "the loupe. Image margins, pixel values and analysis results are not changed."));
  const QString backgroundColorsDescription = QCoreApplication::translate(
      "SettingsDialog", "Sets the solid background color or the two alternating checkerboard "
                        "colors. Each color is kept when you change the background pattern.");
  bindHelp(displayForm, backgroundColors, backgroundColorsDescription);
  for (auto *button : {solidButton, checker1Button, checker2Button}) {
    help.setHelp(button, button->accessibleName(), backgroundColorsDescription);
  }
  bindHelp(displayForm, magnifyCombo, magnifyCombo->toolTip());
  bindHelp(displayForm, minifyCombo, minifyCombo->toolTip());
  bindHelp(displayForm, animationSyncCombo, animationSyncCombo->toolTip());
  bindHelp(colorForm, profileModeCombo,
           QStringList{profileModeCombo->toolTip(),
                       QCoreApplication::translate(
                           "SettingsDialog",
                           "Images are converted from their embedded profile to the display "
                           "profile. Images without a profile are treated as sRGB, or as linear "
                           "when the file stores floating point values.")}
               .join("\n\n"));
  bindHelp(colorForm, profilePathRow,
           QCoreApplication::translate("SettingsDialog",
                                       "Choose ICC file as the display profile to select an .icc "
                                       "or .icm file. Browse opens the file picker."));
  bindHelp(colorForm, intentCombo, intentCombo->toolTip());
  bindHelp(colorForm, blackPointCheckBox, blackPointCheckBox->toolTip(),
           QCoreApplication::translate("SettingsDialog", "Black point compensation"));
  if (intentAvailabilityNote != nullptr) {
    help.setHelp(intentAvailabilityNote,
                 QCoreApplication::translate("SettingsDialog", "Rendering intent"),
                 intentCombo->toolTip());
  }
  bindHelp(
      colorForm, analysisSpaceCombo,
      QStringList{analysisSpaceCombo->toolTip(),
                  QCoreApplication::translate(
                      "SettingsDialog",
                      "Applies to the pixel readout, loupe, histogram, diff and change graph.")}
          .join("\n\n"));
  bindHelp(cacheForm, cacheBudgetSpinBox,
           QCoreApplication::translate(
               "SettingsDialog", "Soft limit for decoded images. The least recently used pages are "
                                 "dropped first, and the current page is kept while possible. "
                                 "HEIRAN_DECODE_CACHE_BUDGET_MB overrides this value when set."));
  bindHelp(cacheForm, textureBudgetSpinBox,
           QCoreApplication::translate(
               "SettingsDialog", "The GPU texture budget is shared by all cells. Textures over the "
                                 "limit are dropped from the least recently used across all cells. "
                                 "HEIRAN_GPU_TEXTURE_BUDGET_MB overrides this value when set."));
  const QString streamingDescription = QCoreApplication::translate(
      "SettingsDialog",
      "APNG and WebP animations larger than this threshold after expansion (width x height x 4 x "
      "frames) are kept compressed and decoded as needed. This saves memory but uses the CPU "
      "during playback. GIF over the threshold is compressed and stored while it plays. Applies to "
      "files opened "
      "afterwards. HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB overrides this value when set.");
  bindHelp(cacheForm, streamingSpinBox, streamingDescription);
  help.setHelp(streamingNote,
               QCoreApplication::translate("SettingsDialog", "Animation streaming threshold"),
               streamingDescription);

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
