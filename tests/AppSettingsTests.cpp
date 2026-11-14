#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/SettingsDialog.hpp"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QtTest>

using heiran::ui::AppSettings;
using heiran::ui::PixelValueFormat;

class AppSettingsTests : public QObject {
  Q_OBJECT

private Q_SLOTS:
  void initTestCase() {
    // 利用者の設定を書き換えないよう、テスト専用の保存先を使う。
    QCoreApplication::setOrganizationName("HeiranTests");
    QCoreApplication::setApplicationName("AppSettingsTests");
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
      QVERIFY(categories != nullptr);
      QVERIFY(pages != nullptr);
      QCOMPARE(categories->count(), 4);
      QCOMPARE(categories->item(0)->text(), QString("General"));
      QCOMPARE(categories->item(1)->text(), QString("Display and playback"));
      QCOMPARE(categories->item(2)->text(), QString("Color management"));
      QCOMPARE(categories->item(3)->text(), QString("Cache"));
      QCOMPARE(categories->currentRow(), 0);
      auto *theme = dialog->findChild<QComboBox *>("colorTheme");
      auto *language = dialog->findChild<QComboBox *>("language");
      theme->setCurrentIndex(theme->findData(static_cast<int>(heiran::ui::ColorTheme::Dark)));
      language->setCurrentIndex(language->findData("ja"));
      categories->setCurrentRow(1);
      QCOMPARE(pages->currentIndex(), 1);
      auto *background = dialog->findChild<QComboBox *>("transparencyPattern");
      background->setCurrentIndex(1);
      categories->setCurrentRow(2);
      auto *profile = dialog->findChild<QComboBox *>("displayProfileMode");
      profile->setCurrentIndex(
          profile->findData(static_cast<int>(heiran::ui::DisplayProfileMode::SRgb)));
      categories->setCurrentRow(3);
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
        dialog->accept();
      } else {
        dialog->reject();
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
    settings.colorTheme = heiran::ui::ColorTheme::Dark;
    settings.save();

    QCOMPARE(AppSettings::load().colorTheme, heiran::ui::ColorTheme::Dark);
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
