#include "heiran/ui/AppSettings.hpp"

#include <QCoreApplication>
#include <QSettings>
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
