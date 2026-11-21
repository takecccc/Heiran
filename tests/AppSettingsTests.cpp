#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/AppStyle.hpp"
#include "heiran/ui/SettingsDialog.hpp"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEnterEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using heiran::ui::AppSettings;
using heiran::ui::PixelValueFormat;

class AppSettingsTests : public QObject {
  Q_OBJECT

  QTemporaryDir settingsDirectory_;

private Q_SLOTS:
  void initTestCase() {
    // 利用者の設定を書き換えないよう、テスト専用の保存先を使う。
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("AppSettingsTests");
    QVERIFY(settingsDirectory_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
    QSettings().clear();
  }

  void cleanupTestCase() { QSettings().clear(); }

  void usesDefaultsWhenNothingIsStored() {
    QSettings().clear();

    const auto settings = AppSettings::load();
    QCOMPARE(settings.pixelValueFormat, PixelValueFormat::Normalized255);
    QCOMPARE(settings.fitOnPageChange, false);
    QCOMPARE(settings.decodeCacheBudgetMib, 1024);
    QCOMPARE(settings.transparencyBackground, heiran::ui::TransparencyBackground{});
  }

  void storesBothTransparencyBackgrounds() {
    QSettings().clear();
    AppSettings settings;
    settings.transparencyBackground = {.pattern = heiran::ui::TransparencyPattern::Checkerboard,
                                       .solidColor = QColor(12, 34, 56),
                                       .checkerColor1 = QColor(70, 80, 90),
                                       .checkerColor2 = QColor(170, 180, 190)};
    settings.save();
    QCOMPARE(AppSettings::load().transparencyBackground, settings.transparencyBackground);
    settings.transparencyBackground.pattern = heiran::ui::TransparencyPattern::Solid;
    settings.save();
    QCOMPARE(AppSettings::load().transparencyBackground, settings.transparencyBackground);
  }

  void validatesStoredTransparencyBackground() {
    QSettings().clear();
    QSettings().setValue("display/transparencyPattern", "unknown");
    QSettings().setValue("display/transparencySolidColor", "not-a-color");
    QSettings().setValue("display/transparencyCheckerColor1", "#80123456");
    const auto background = AppSettings::load().transparencyBackground;
    QCOMPARE(background.pattern, heiran::ui::TransparencyPattern::Solid);
    QCOMPARE(background.solidColor, QColor(245, 245, 245));
    QCOMPARE(background.checkerColor1, QColor(0x12, 0x34, 0x56));
    QCOMPARE(background.checkerColor2, QColor(245, 245, 245));
  }

  void editsAllBackgroundColorsAndPreservesThemAcrossPatterns() {
    const AppSettings current;
    QTimer::singleShot(0, []() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog != nullptr);
      dialog->findChild<QListWidget *>("settingsCategories")->setCurrentRow(1);
      auto *combo = dialog->findChild<QComboBox *>("transparencyPattern");
      QVERIFY(combo != nullptr);
      const auto chooseColor = [dialog](const char *name, QColor color) {
        auto *button = dialog->findChild<QPushButton *>(name);
        QVERIFY(button != nullptr);
        QTimer::singleShot(0, [color]() {
          auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
          QVERIFY(picker != nullptr);
          picker->setCurrentColor(color);
          picker->accept();
        });
        button->click();
      };
      chooseColor("transparencySolidColor", QColor(11, 22, 33));
      combo->setCurrentIndex(1);
      chooseColor("transparencyCheckerColor1", QColor(44, 55, 66));
      chooseColor("transparencyCheckerColor2", QColor(77, 88, 99));
      combo->setCurrentIndex(0);
      combo->setCurrentIndex(1);
      dialog->accept();
    });
    const auto updated = heiran::ui::showSettingsDialog(nullptr, current);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->transparencyBackground.pattern,
             heiran::ui::TransparencyPattern::Checkerboard);
    QCOMPARE(updated->transparencyBackground.solidColor, QColor(11, 22, 33));
    QCOMPARE(updated->transparencyBackground.checkerColor1, QColor(44, 55, 66));
    QCOMPARE(updated->transparencyBackground.checkerColor2, QColor(77, 88, 99));
    QCOMPARE(current.transparencyBackground, heiran::ui::TransparencyBackground{});
  }

  void cancelsTransparencyBackgroundChanges() {
    const AppSettings current;
    QTimer::singleShot(0, []() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog != nullptr);
      dialog->findChild<QListWidget *>("settingsCategories")->setCurrentRow(1);
      dialog->findChild<QComboBox *>("transparencyPattern")->setCurrentIndex(1);
      dialog->reject();
    });
    QVERIFY(!heiran::ui::showSettingsDialog(nullptr, current).has_value());
  }

  void keepsEditsAcrossSettingsPages_data() {
    QTest::addColumn<bool>("accept");
    QTest::newRow("accept") << true;
    QTest::newRow("cancel") << false;
  }

  void explainsHoveredLabelsFocusedEditorsAndDisabledSettings() {
    QTimer::singleShot(0, []() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog != nullptr);
      const auto closeDialog = qScopeGuard([dialog]() { dialog->reject(); });
      auto *categories = dialog->findChild<QListWidget *>("settingsCategories");
      auto *title = dialog->findChild<QLabel *>("settingsHelpTitle");
      auto *description = dialog->findChild<QPlainTextEdit *>("settingsHelpDescription");
      auto *information = dialog->findChild<QWidget *>("settingsInformation");
      QVERIFY(categories && title && description && information);
      const int height = information->height();
      const auto hover = [dialog](QWidget *widget) {
        const QPoint position = widget->rect().center();
        QEnterEvent enter(position, widget->mapTo(dialog, position), widget->mapToGlobal(position));
        QApplication::sendEvent(widget, &enter);
      };
      auto *language = dialog->findChild<QComboBox *>("language");
      hover(language);
      QCOMPARE(title->text(), QString("Language"));
      QCOMPARE(description->toPlainText(), language->toolTip());
      QVERIFY(dialog->findChild<QLabel *>("languageRestartNote")->isVisible());
      const QString languageDescription = description->toPlainText();
      QEvent leave(QEvent::Leave);
      QApplication::sendEvent(language, &leave);
      QCOMPARE(description->toPlainText(), languageDescription);
      categories->setCurrentRow(1);
      auto *magnify = dialog->findChild<QComboBox *>("magnifyFilter");
      magnify->setFocus(Qt::TabFocusReason);
      QCoreApplication::processEvents();
      QCOMPARE(title->text(), QString("Magnify filter"));
      QCOMPARE(description->toPlainText(), magnify->toolTip());
      auto *form = qobject_cast<QFormLayout *>(magnify->parentWidget()->layout());
      QVERIFY(form != nullptr);
      auto *label = qobject_cast<QWidget *>(form->labelForField(magnify));
      QVERIFY(label != nullptr);
      hover(dialog->findChild<QComboBox *>("minifyFilter"));
      QCOMPARE(title->text(), QString("Minify filter"));
      hover(label);
      QCOMPARE(title->text(), QString("Magnify filter"));
      QCOMPARE(description->toPlainText(), magnify->toolTip());
      dialog->resize(620, 400);
      auto *animation = dialog->findChild<QComboBox *>("animationSync");
      animation->setFocus(Qt::TabFocusReason);
      QFont largerFont = description->font();
      largerFont.setPointSize(largerFont.pointSize() + 8);
      description->setFont(largerFont);
      QCoreApplication::processEvents();
      QCOMPARE(title->text(), QString("Animation sync"));
      QVERIFY(description->verticalScrollBar()->maximum() > 0);
      QCOMPARE(information->height(), height);
      categories->setCurrentRow(2);
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      hover(theme);
      QCOMPARE(title->text(), QString("Color theme"));
      QCOMPARE(description->toPlainText(), theme->toolTip());
      categories->setCurrentRow(4);
      auto *cache = dialog->findChild<QSpinBox *>("decodeCacheBudget");
      auto *editor = cache->findChild<QLineEdit *>();
      QVERIFY(editor != nullptr);
      editor->setFocus(Qt::TabFocusReason);
      QCoreApplication::processEvents();
      QCOMPARE(title->text(), QString("Decode cache budget"));
      QVERIFY(description->toPlainText().contains("HEIRAN_DECODE_CACHE_BUDGET_MB"));
      QVERIFY(dialog->findChild<QLabel *>("animationStreamingNote")->isVisible());
      categories->setCurrentRow(3);
      QVERIFY(dialog->findChild<QLabel *>("displayProfileFileNote")->isVisible());
      auto *intent = dialog->findChild<QComboBox *>("renderingIntent");
      hover(intent);
      QCOMPARE(title->text(), QString("Rendering intent"));
      QCOMPARE(description->toPlainText(), intent->toolTip());
      if (!intent->isEnabled()) {
        QVERIFY(dialog->findChild<QLabel *>("renderingIntentUnavailableNote")->isVisible());
      }
      QCOMPARE(information->height(), height);
      dialog->reject();
    });
    QVERIFY(!heiran::ui::showSettingsDialog(nullptr, AppSettings{}).has_value());
  }

  void keepsEditsAcrossSettingsPages() {
    QFETCH(bool, accept);
    QSettings().clear();
    const AppSettings current;
    current.save();
    QTimer::singleShot(0, [accept]() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog != nullptr);
      auto *categories = dialog->findChild<QListWidget *>("settingsCategories");
      auto *pages = dialog->findChild<QStackedWidget *>("settingsPages");
      auto closeDialog = qScopeGuard([dialog]() { dialog->reject(); });
      QVERIFY(categories != nullptr);
      QVERIFY(pages != nullptr);
      QCOMPARE(categories->count(), 5);
      QCOMPARE(categories->item(0)->text(), QString("General"));
      QCOMPARE(categories->item(1)->text(), QString("Display and playback"));
      QCOMPARE(categories->item(2)->text(), QString("Theme"));
      QCOMPARE(categories->item(3)->text(), QString("Color management"));
      QCOMPARE(categories->item(4)->text(), QString("Cache"));
      QCOMPARE(categories->currentRow(), 0);
      categories->setFocus(Qt::TabFocusReason);
      QTest::keyClick(categories, Qt::Key_Down);
      QCOMPARE(categories->currentRow(), 1);
      QCOMPARE(pages->currentIndex(), 1);
      QTest::keyClick(categories, Qt::Key_Up);
      QCOMPARE(categories->currentRow(), 0);
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      auto *language = dialog->findChild<QComboBox *>("language");
      language->setCurrentIndex(language->findData("ja"));
      QVERIFY(!theme->isVisible());
      categories->setCurrentRow(1);
      QCOMPARE(pages->currentIndex(), 1);
      auto *background = dialog->findChild<QComboBox *>("transparencyPattern");
      background->setCurrentIndex(1);
      categories->setCurrentRow(2);
      QVERIFY(theme->isVisible());
      QVERIFY(dialog->findChild<QWidget *>("customThemeEditor")->isVisible());
      QVERIFY(!language->isVisible());
      theme->setCurrentIndex(theme->findData(static_cast<int>(heiran::ui::ColorTheme::Dark)));
      categories->setCurrentRow(3);
      auto *profile = dialog->findChild<QComboBox *>("displayProfileMode");
      profile->setCurrentIndex(
          profile->findData(static_cast<int>(heiran::ui::DisplayProfileMode::SRgb)));
      categories->setCurrentRow(4);
      auto *cache = dialog->findChild<QSpinBox *>("decodeCacheBudget");
      cache->setValue(2048);
      categories->setCurrentRow(0);
      QCOMPARE(theme->currentData().toInt(), static_cast<int>(heiran::ui::ColorTheme::Dark));
      QCOMPARE(language->currentData().toString(), QString("ja"));
      categories->setCurrentRow(1);
      QCOMPARE(background->currentIndex(), 1);
      dialog->resize(620, 400);
      for (int row = 0; row < categories->count(); ++row) {
        categories->setCurrentRow(row);
        QCoreApplication::processEvents();
        QCOMPARE(pages->currentIndex(), row);
        auto *scroll = qobject_cast<QScrollArea *>(pages->currentWidget());
        QVERIFY(scroll != nullptr);
        QVERIFY(scroll->widgetResizable());
        QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttons != nullptr && buttons->isVisible());
        QVERIFY(dialog->rect().contains(QRect(buttons->mapTo(dialog, QPoint()), buttons->size())));
        QVERIFY(dialog->height() <= 420);
      }
      if (accept) {
        closeDialog.dismiss();
        dialog->accept();
      }
    });
    const auto updated = heiran::ui::showSettingsDialog(nullptr, current);
    QCOMPARE(updated.has_value(), accept);
    if (accept) {
      QCOMPARE(updated->colorTheme, heiran::ui::ColorTheme::Dark);
      QCOMPARE(updated->language, QString("ja"));
      QCOMPARE(updated->transparencyBackground.pattern,
               heiran::ui::TransparencyPattern::Checkerboard);
      QCOMPARE(updated->displayProfileMode, heiran::ui::DisplayProfileMode::SRgb);
      QCOMPARE(updated->decodeCacheBudgetMib, 2048);
    }
    const auto stored = AppSettings::load();
    QCOMPARE(stored.colorTheme, current.colorTheme);
    QCOMPARE(stored.language, current.language);
    QCOMPARE(stored.transparencyBackground, current.transparencyBackground);
    QCOMPARE(stored.displayProfileMode, current.displayProfileMode);
    QCOMPARE(stored.decodeCacheBudgetMib, current.decodeCacheBudgetMib);
  }

  void roundTripsThroughStorage() {
    QSettings().clear();

    AppSettings settings;
    settings.pixelValueFormat = PixelValueFormat::Normalized01;
    settings.fitOnPageChange = true;
    settings.decodeCacheBudgetMib = 512;
    settings.save();

    const auto loaded = AppSettings::load();
    QCOMPARE(loaded.pixelValueFormat, PixelValueFormat::Normalized01);
    QCOMPARE(loaded.fitOnPageChange, true);
    QCOMPARE(loaded.decodeCacheBudgetMib, 512);
  }

  void clampsStoredBudgetIntoRange() {
    QCOMPARE(heiran::ui::clampDecodeCacheBudgetMib(0), heiran::ui::kMinDecodeCacheBudgetMib);
    QCOMPARE(heiran::ui::clampDecodeCacheBudgetMib(-100), heiran::ui::kMinDecodeCacheBudgetMib);
    QCOMPARE(heiran::ui::clampDecodeCacheBudgetMib(1'000'000),
             heiran::ui::kMaxDecodeCacheBudgetMib);
    QCOMPARE(heiran::ui::clampDecodeCacheBudgetMib(256), 256);
  }

  // 保存した配色を読み戻せること。読み込みが抜けていると、起動のたびに OS の設定へ戻る。
  void roundTripsTheColorTheme() {
    QSettings().clear();

    AppSettings settings;
    using heiran::ui::ColorTheme;
    for (const auto theme :
         {ColorTheme::Dark, ColorTheme::Slate, ColorTheme::Sand, ColorTheme::Sakura}) {
      settings.colorTheme = theme;
      settings.save();
      QCOMPARE(AppSettings::load().colorTheme, theme);
    }
  }

  void previewsColorThemesBeforeApplyingAndRestoresOnCancel() {
    using heiran::ui::ColorTheme;
    const auto restore = qScopeGuard([]() { heiran::ui::applyColorTheme(ColorTheme::System); });
    AppSettings current;
    current.colorTheme = ColorTheme::Dark;
    current.save();
    heiran::ui::applyColorTheme(current.colorTheme);
    const auto initialStyle = qApp->styleSheet();
    QString slateStyle, sandStyle, sakuraStyle;
    ColorTheme storedDuringPreview = ColorTheme::System;
    QTimer::singleShot(0, [&]() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      const auto cancel = qScopeGuard([dialog]() { dialog->reject(); });
      dialog->findChild<QListWidget *>("settingsCategories")->setCurrentRow(2);
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      QVERIFY(theme && theme->isVisible());
      theme->setCurrentIndex(theme->findData(static_cast<int>(ColorTheme::Slate)));
      slateStyle = qApp->styleSheet();
      theme->setCurrentIndex(theme->findData(static_cast<int>(ColorTheme::Sand)));
      sandStyle = qApp->styleSheet();
      theme->setCurrentIndex(theme->findData(static_cast<int>(ColorTheme::Sakura)));
      sakuraStyle = qApp->styleSheet();
      storedDuringPreview = AppSettings::load().colorTheme;
    });
    QVERIFY(!heiran::ui::showSettingsDialog(nullptr, current));
    QVERIFY(!slateStyle.isEmpty() && !sandStyle.isEmpty() && !sakuraStyle.isEmpty());
    QVERIFY(slateStyle != initialStyle && sandStyle != slateStyle);
    QVERIFY(sakuraStyle != sandStyle);
    QCOMPARE(storedDuringPreview, ColorTheme::Dark);
    QCOMPARE(qApp->styleSheet(), initialStyle);
  }

  void keepsCustomColorsAcrossThemeChanges() {
    QSettings().clear();
    AppSettings settings;
    using heiran::ui::ColorTheme;
    settings.customColorTheme = {.dark = true,
                                 .window = QColor("#19222b"),
                                 .input = QColor("#30404a"),
                                 .accent = QColor("#79aed8")};
    settings.colorTheme = ColorTheme::Custom;
    settings.save();
    QCOMPARE(AppSettings::load().colorTheme, ColorTheme::Custom);
    QCOMPARE(AppSettings::load().customColorTheme, settings.customColorTheme);
    const auto tokens = heiran::ui::AppStyleTokens::forCustomTheme(settings.customColorTheme);
    QCOMPARE(tokens.input, settings.customColorTheme.input);
    QCOMPARE(tokens.panel, heiran::ui::AppStyleTokens::forDarkScheme(true).panel);
    settings.colorTheme = ColorTheme::Light;
    settings.save();
    QCOMPARE(AppSettings::load().customColorTheme, settings.customColorTheme);
  }

  void cancelsPreviewedCustomThemeEditsWithTheSettingsDialog() {
    using heiran::ui::ColorTheme;
    const auto restore = qScopeGuard([]() { heiran::ui::applyColorTheme(ColorTheme::System); });
    AppSettings current;
    current.colorTheme = ColorTheme::Custom;
    current.customColorTheme.window = QColor("#ede7df");
    current.save();
    heiran::ui::applyColorTheme(current.colorTheme, current.customColorTheme);
    const auto initialStyle = qApp->styleSheet();
    bool previewChanged = false;
    QTimer::singleShot(0, [&]() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      const auto cancel = qScopeGuard([dialog]() { dialog->reject(); });
      auto *categories = dialog->findChild<QListWidget *>("settingsCategories");
      categories->setCurrentRow(2);
      auto *editor = dialog->findChild<QWidget *>("customThemeEditor");
      QVERIFY(editor && editor->isVisible());
      auto *scheme = editor->findChild<QComboBox *>("customThemeBaseScheme");
      auto *reset = editor->findChild<QPushButton *>("resetCustomThemeColors");
      QVERIFY(scheme && scheme->isVisible() && reset && reset->isVisible());
      scheme->setCurrentIndex(1);
      reset->click();
      previewChanged = qApp->styleSheet() != initialStyle;
      QCOMPARE(heiran::ui::AppStyleTokens::current().window,
               heiran::ui::AppStyleTokens::forDarkScheme(true).window);
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      theme->setCurrentIndex(theme->findData(static_cast<int>(ColorTheme::Light)));
      QVERIFY(editor->isVisible());
      QCOMPARE(scheme->currentIndex(), 0);
      QVERIFY(!reset->isEnabled());
      theme->setCurrentIndex(theme->findData(static_cast<int>(ColorTheme::Custom)));
      QVERIFY(editor->isVisible());
      QVERIFY(reset->isEnabled());
      QCOMPARE(scheme->currentIndex(), 1);
      QCOMPARE(heiran::ui::AppStyleTokens::current().window,
               heiran::ui::AppStyleTokens::forDarkScheme(true).window);
      QCOMPARE(AppSettings::load().customColorTheme, current.customColorTheme);
    });
    QVERIFY(!heiran::ui::showSettingsDialog(nullptr, current));
    QVERIFY(previewChanged);
    QCOMPARE(qApp->styleSheet(), initialStyle);
  }

  void editsTheSelectedPresetAsCustom() {
    using heiran::ui::AppStyleTokens;
    using heiran::ui::ColorTheme;
    const auto restore = qScopeGuard([]() { heiran::ui::applyColorTheme(ColorTheme::System); });
    AppSettings current;
    current.colorTheme = ColorTheme::Sakura;
    const QColor newAccent("#7651a1");
    AppStyleTokens preset;
    QTimer::singleShot(0, [&]() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      auto cancel = qScopeGuard([dialog]() { dialog->reject(); });
      dialog->findChild<QListWidget *>("settingsCategories")->setCurrentRow(2);
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      auto *accent = dialog->findChild<QPushButton *>("customThemeAccent");
      QVERIFY(theme && accent && accent->isVisible());
      preset = AppStyleTokens::current();
      QCOMPARE(accent->text(), preset.accent.name());
      QTimer::singleShot(0, []() {
        auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
        QVERIFY(picker);
        picker->accept();
      });
      accent->click();
      QCOMPARE(theme->currentData().toInt(), static_cast<int>(ColorTheme::Sakura));
      QTimer::singleShot(0, [&]() {
        auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
        QVERIFY(picker);
        picker->setCurrentColor(newAccent);
        picker->accept();
      });
      accent->click();
      QCOMPARE(theme->currentData().toInt(), static_cast<int>(ColorTheme::Custom));
      QCOMPARE(AppStyleTokens::current().accent, newAccent);
      cancel.dismiss();
      dialog->accept();
    });
    const auto updated = heiran::ui::showSettingsDialog(nullptr, current);
    QVERIFY(updated);
    QCOMPARE(updated->colorTheme, ColorTheme::Custom);
    QVERIFY(!updated->customColorTheme.dark);
    QCOMPARE(updated->customColorTheme.accent, newAccent);
    QCOMPARE(updated->customColorTheme.window, preset.window);
    QCOMPARE(updated->customColorTheme.panel, preset.panel);
    QCOMPARE(updated->customColorTheme.input, preset.input);
    QCOMPARE(updated->customColorTheme.text, preset.text);
  }

  void restoresAllDefaultsOnlyWhenAccepted_data() {
    QTest::addColumn<bool>("accept");
    QTest::newRow("OK") << true;
    QTest::newRow("Cancel") << false;
  }

  void restoresAllDefaultsOnlyWhenAccepted() {
    QFETCH(bool, accept);
    using namespace heiran::ui;
    QSettings().clear();
    const auto cleanup = qScopeGuard([]() {
      QSettings().clear();
      applyColorTheme(ColorTheme::System);
    });
    const AppSettings defaults;
    AppSettings current;
    current.language = "ja";
    current.transparencyBackground = {TransparencyPattern::Checkerboard, Qt::red, Qt::green,
                                      Qt::blue};
    current.pixelValueFormat = PixelValueFormat::Normalized01;
    current.fitOnPageChange = true;
    current.loopPages = true;
    current.wrapNavigation = true;
    current.showCellBorders = false;
    current.magnifyFilter = MagnifyFilter::Smooth;
    current.minifyFilter = MinifyFilter::Nearest;
    current.animationSyncMode = AnimationSyncMode::Independent;
    current.colorTheme = ColorTheme::Custom;
    current.customColorTheme = {.dark = true, .accent = QColor("#e68feb")};
    current.cursorStyle = CursorStyle::Contrast;
    current.displayProfileMode = DisplayProfileMode::File;
    current.displayProfilePath = "sample.icc";
    current.renderingIntent = RenderingIntent::Perceptual;
    current.blackPointCompensation = true;
    current.analysisValueSpace = AnalysisValueSpace::Linear;
    current.decodeCacheBudgetMib = 512;
    current.gpuTextureBudgetMib = 512;
    current.animationStreamingThresholdMib = 64;
    current.save();
    applyColorTheme(current.colorTheme, current.customColorTheme);
    const auto initialStyle = qApp->styleSheet();

    QTimer::singleShot(0, [&]() {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      auto close = qScopeGuard([dialog]() { dialog->reject(); });
      auto *reset = dialog->findChild<QPushButton *>("restoreAllSettingsDefaults");
      QVERIFY(reset && reset->isVisible());
      const auto confirmReset = [reset](QMessageBox::StandardButton response) {
        QTimer::singleShot(0, [response]() {
          auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
          QVERIFY(confirmation);
          const bool defaultsToCancel =
              confirmation->defaultButton() == confirmation->button(QMessageBox::Cancel);
          confirmation->button(response)->click();
          QVERIFY(defaultsToCancel);
        });
        reset->click();
      };
      auto *language = dialog->findChild<QComboBox *>("language");
      auto *background = dialog->findChild<QComboBox *>("transparencyPattern");
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      auto *profilePath = dialog->findChild<QLineEdit *>("displayProfilePath");
      auto *cache = dialog->findChild<QSpinBox *>("decodeCacheBudget");
      QVERIFY(language && background && theme && profilePath && cache);
      language->setCurrentIndex(language->findData("en"));
      confirmReset(QMessageBox::Cancel);
      QCOMPARE(language->currentData().toString(), QString("en"));
      QCOMPARE(qApp->styleSheet(), initialStyle);
      confirmReset(QMessageBox::Ok);
      QCOMPARE(language->currentData().toString(), defaults.language);
      QCOMPARE(background->currentData().toInt(),
               static_cast<int>(defaults.transparencyBackground.pattern));
      QCOMPARE(dialog->findChild<QPushButton *>("transparencySolidColor")->text(),
               defaults.transparencyBackground.solidColor.name());
      QCOMPARE(theme->currentData().toInt(), static_cast<int>(defaults.colorTheme));
      QVERIFY(qApp->styleSheet() != initialStyle);
      QVERIFY(profilePath->text().isEmpty() && !profilePath->isEnabled());
      QCOMPARE(cache->value(), defaults.decodeCacheBudgetMib);
      QCOMPARE(AppSettings::load().language, current.language);
      QCOMPARE(AppSettings::load().customColorTheme, current.customColorTheme);
      if (accept) {
        close.dismiss();
        dialog->accept();
      }
    });
    const auto updated = showSettingsDialog(nullptr, current);
    QCOMPARE(updated.has_value(), accept);
    if (!accept) {
      QCOMPARE(qApp->styleSheet(), initialStyle);
      QCOMPARE(AppSettings::load().transparencyBackground, current.transparencyBackground);
      QCOMPARE(AppSettings::load().decodeCacheBudgetMib, current.decodeCacheBudgetMib);
      return;
    }
    updated->save();
    const auto saved = AppSettings::load();
    QCOMPARE(saved.language, defaults.language);
    QCOMPARE(saved.transparencyBackground, defaults.transparencyBackground);
    QCOMPARE(saved.pixelValueFormat, defaults.pixelValueFormat);
    QCOMPARE(saved.fitOnPageChange, defaults.fitOnPageChange);
    QCOMPARE(saved.loopPages, defaults.loopPages);
    QCOMPARE(saved.wrapNavigation, defaults.wrapNavigation);
    QCOMPARE(saved.showCellBorders, defaults.showCellBorders);
    QCOMPARE(saved.magnifyFilter, defaults.magnifyFilter);
    QCOMPARE(saved.minifyFilter, defaults.minifyFilter);
    QCOMPARE(saved.animationSyncMode, defaults.animationSyncMode);
    QCOMPARE(saved.colorTheme, defaults.colorTheme);
    QCOMPARE(saved.customColorTheme, defaults.customColorTheme);
    QCOMPARE(saved.cursorStyle, defaults.cursorStyle);
    QCOMPARE(saved.displayProfileMode, defaults.displayProfileMode);
    QCOMPARE(saved.displayProfilePath, defaults.displayProfilePath);
    QCOMPARE(saved.renderingIntent, defaults.renderingIntent);
    QCOMPARE(saved.blackPointCompensation, defaults.blackPointCompensation);
    QCOMPARE(saved.analysisValueSpace, defaults.analysisValueSpace);
    QCOMPARE(saved.decodeCacheBudgetMib, defaults.decodeCacheBudgetMib);
    QCOMPARE(saved.gpuTextureBudgetMib, defaults.gpuTextureBudgetMib);
    QCOMPARE(saved.animationStreamingThresholdMib, defaults.animationStreamingThresholdMib);
  }

  void storesTheAnimationStreamingThreshold() {
    QSettings().clear();
    QCOMPARE(AppSettings::load().animationStreamingThresholdMib, 128);

    AppSettings settings;
    settings.animationStreamingThresholdMib = 512;
    settings.save();

    QCOMPARE(AppSettings::load().animationStreamingThresholdMib, 512);
  }

  void clampsTheAnimationStreamingThresholdIntoRange() {
    QCOMPARE(heiran::ui::clampAnimationStreamingThresholdMib(0),
             heiran::ui::kMinAnimationStreamingThresholdMib);
    QCOMPARE(heiran::ui::clampAnimationStreamingThresholdMib(1'000'000),
             heiran::ui::kMaxAnimationStreamingThresholdMib);
    QCOMPARE(heiran::ui::clampAnimationStreamingThresholdMib(256), 256);
  }

  // 環境変数の指定は設定より優先すること。設定を変えずに検証するための口。
  void environmentOverridesTheAnimationStreamingThreshold() {
    QCOMPARE(heiran::ui::resolveAnimationStreamingThresholdBytes(128, 0),
             qint64{128} * 1024 * 1024);
    QCOMPARE(heiran::ui::resolveAnimationStreamingThresholdBytes(128, 64),
             qint64{64} * 1024 * 1024);
  }

  void storesWhetherToShowCellBorders() {
    QSettings().clear();
    QVERIFY(AppSettings::load().showCellBorders);  // 既定は出す。

    AppSettings settings;
    settings.showCellBorders = false;
    settings.save();
    QVERIFY(!AppSettings::load().showCellBorders);

    settings.showCellBorders = true;
    settings.save();
    QVERIFY(AppSettings::load().showCellBorders);
  }

  void storesTheCursorStyle() {
    QSettings().clear();
    QCOMPARE(AppSettings::load().cursorStyle, heiran::ui::CursorStyle::Outline);  // 既定
    AppSettings settings;
    settings.cursorStyle = heiran::ui::CursorStyle::Contrast;
    settings.save();
    QCOMPARE(AppSettings::load().cursorStyle, heiran::ui::CursorStyle::Contrast);
  }

  void storesTheAnimationSyncMode() {
    QSettings().clear();
    QCOMPARE(AppSettings::load().animationSyncMode, heiran::ui::AnimationSyncMode::Auto);

    AppSettings settings;
    settings.animationSyncMode = heiran::ui::AnimationSyncMode::Independent;
    settings.save();
    QCOMPARE(AppSettings::load().animationSyncMode, heiran::ui::AnimationSyncMode::Independent);

    settings.animationSyncMode = heiran::ui::AnimationSyncMode::Locked;
    settings.save();
    QCOMPARE(AppSettings::load().animationSyncMode, heiran::ui::AnimationSyncMode::Locked);
  }

  void fallsBackToAutoForAnUnknownAnimationSyncMode() {
    QCOMPARE(heiran::ui::animationSyncModeFromString("nonsense"),
             heiran::ui::AnimationSyncMode::Auto);
  }

  void storesTheGpuTextureBudget() {
    QSettings().clear();
    QCOMPARE(AppSettings::load().gpuTextureBudgetMib, 2048);

    AppSettings settings;
    settings.gpuTextureBudgetMib = 3072;
    settings.save();

    QCOMPARE(AppSettings::load().gpuTextureBudgetMib, 3072);
  }

  void clampsTheGpuTextureBudgetIntoRange() {
    QCOMPARE(heiran::ui::clampGpuTextureBudgetMib(0), heiran::ui::kMinGpuTextureBudgetMib);
    QCOMPARE(heiran::ui::clampGpuTextureBudgetMib(1'000'000), heiran::ui::kMaxGpuTextureBudgetMib);
    QCOMPARE(heiran::ui::clampGpuTextureBudgetMib(512), 512);
  }

  // 環境変数の指定は設定より優先すること。設定を変えずに検証するための口。
  void environmentOverridesTheGpuTextureBudget() {
    QCOMPARE(heiran::ui::resolveGpuTextureBudgetBytes(1024, 0), qint64{1024} * 1024 * 1024);
    QCOMPARE(heiran::ui::resolveGpuTextureBudgetBytes(1024, 128), qint64{128} * 1024 * 1024);
  }

  void clampsBudgetWhenLoading() {
    QSettings().clear();

    AppSettings settings;
    settings.decodeCacheBudgetMib = 1;
    settings.save();

    QCOMPARE(AppSettings::load().decodeCacheBudgetMib, heiran::ui::kMinDecodeCacheBudgetMib);
  }

  void defaultsToNearestMagnifyAndAreaMinify() {
    QSettings().clear();

    const auto settings = AppSettings::load();
    // 既定は画素の確認に向く Nearest と、ちらつかない Area。
    QCOMPARE(settings.magnifyFilter, heiran::ui::MagnifyFilter::Nearest);
    QCOMPARE(settings.minifyFilter, heiran::ui::MinifyFilter::Area);
  }

  void roundTripsFilters() {
    QSettings().clear();

    AppSettings settings;
    settings.magnifyFilter = heiran::ui::MagnifyFilter::Smooth;
    settings.minifyFilter = heiran::ui::MinifyFilter::Nearest;
    settings.save();

    const auto loaded = AppSettings::load();
    QCOMPARE(loaded.magnifyFilter, heiran::ui::MagnifyFilter::Smooth);
    QCOMPARE(loaded.minifyFilter, heiran::ui::MinifyFilter::Nearest);
  }

  void fallsBackToDefaultsForUnknownFilterNames() {
    QCOMPARE(heiran::ui::magnifyFilterFromString("bicubic"), heiran::ui::MagnifyFilter::Nearest);
    QCOMPARE(heiran::ui::minifyFilterFromString(QString()), heiran::ui::MinifyFilter::Area);
  }

  void convertsBudgetToBytes() {
    QCOMPARE(heiran::ui::resolveDecodeCacheBudgetBytes(1024, 0), 1024LL * 1024LL * 1024LL);
    QCOMPARE(heiran::ui::resolveDecodeCacheBudgetBytes(256, 0), 256LL * 1024LL * 1024LL);
  }

  void prefersEnvironmentOverride() {
    // 設定を変えずに一時的な検証を行えるようにする。
    QCOMPARE(heiran::ui::resolveDecodeCacheBudgetBytes(1024, 8), 8LL * 1024LL * 1024LL);
    // 上書きが無効な値なら設定を使う。
    QCOMPARE(heiran::ui::resolveDecodeCacheBudgetBytes(1024, 0), 1024LL * 1024LL * 1024LL);
    QCOMPARE(heiran::ui::resolveDecodeCacheBudgetBytes(1024, -1), 1024LL * 1024LL * 1024LL);
  }
};

QTEST_MAIN(AppSettingsTests)

#include "AppSettingsTests.moc"
