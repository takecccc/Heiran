#include "heiran/ui/AppSettings.hpp"

#include <QSettings>
#include <algorithm>

namespace heiran::ui {

namespace {

constexpr auto kPixelValueFormatKey = "pixel_value_format";
constexpr auto kFitOnPageChangeKey = "fit_on_page_change";
constexpr auto kWrapNavigationKey = "display/wrapNavigation";
constexpr auto kDecodeCacheBudgetKey = "decode_cache_budget_mib";
constexpr auto kGpuTextureBudgetKey = "gpu_texture_budget_mib";
constexpr auto kAnimationStreamingThresholdKey = "animation_streaming_threshold_mib";
constexpr auto kShowCellBordersKey = "show_cell_borders";
constexpr auto kAnimationSyncModeKey = "animation/sync_mode";
constexpr auto kMagnifyFilterKey = "magnify_filter";
constexpr auto kMinifyFilterKey = "minify_filter";
constexpr auto kDisplayProfileModeKey = "display/profileMode";
constexpr auto kDisplayProfilePathKey = "display/profilePath";
constexpr auto kAnalysisValueSpaceKey = "analysis/valueSpace";
constexpr auto kRenderingIntentKey = "display/renderingIntent";
constexpr auto kBlackPointCompensationKey = "display/blackPointCompensation";
constexpr auto kColorThemeKey = "display/colorTheme";
constexpr auto kLanguageKey = "general/language";
constexpr auto kCursorStyleKey = "display/cursorStyle";
constexpr auto kTransparencyPatternKey = "display/transparencyPattern";
constexpr auto kTransparencySolidKey = "display/transparencySolidColor";
constexpr auto kTransparencyChecker1Key = "display/transparencyCheckerColor1";
constexpr auto kTransparencyChecker2Key = "display/transparencyCheckerColor2";

QColor loadBackgroundColor(const QSettings &settings, const char *key, const QColor &fallback) {
  QColor color(settings.value(key, fallback.name()).toString());
  if (!color.isValid()) {
    return fallback;
  }
  color.setAlpha(255);
  return color;
}

}  // namespace

QString animationSyncModeToString(AnimationSyncMode mode) {
  switch (mode) {
    case AnimationSyncMode::Locked:
      return QStringLiteral("locked");
    case AnimationSyncMode::Independent:
      return QStringLiteral("independent");
    case AnimationSyncMode::Auto:
    default:
      return QStringLiteral("auto");
  }
}

AnimationSyncMode animationSyncModeFromString(const QString &text) {
  const QString normalized = text.trimmed().toLower();
  if (normalized == QStringLiteral("locked")) {
    return AnimationSyncMode::Locked;
  }
  if (normalized == QStringLiteral("independent")) {
    return AnimationSyncMode::Independent;
  }
  return AnimationSyncMode::Auto;
}

QString magnifyFilterToString(MagnifyFilter filter) {
  return filter == MagnifyFilter::Smooth ? QStringLiteral("smooth") : QStringLiteral("nearest");
}

MagnifyFilter magnifyFilterFromString(const QString &text) {
  return text.trimmed().toLower() == QStringLiteral("smooth") ? MagnifyFilter::Smooth
                                                              : MagnifyFilter::Nearest;
}

QString minifyFilterToString(MinifyFilter filter) {
  return filter == MinifyFilter::Nearest ? QStringLiteral("nearest") : QStringLiteral("area");
}

MinifyFilter minifyFilterFromString(const QString &text) {
  return text.trimmed().toLower() == QStringLiteral("nearest") ? MinifyFilter::Nearest
                                                               : MinifyFilter::Area;
}

AppSettings AppSettings::load() {
  QSettings settings;
  AppSettings appSettings;
  auto &background = appSettings.transparencyBackground;
  background.pattern = settings.value(kTransparencyPatternKey).toString() == "checkerboard"
                           ? TransparencyPattern::Checkerboard
                           : TransparencyPattern::Solid;
  background.solidColor =
      loadBackgroundColor(settings, kTransparencySolidKey, background.solidColor);
  background.checkerColor1 =
      loadBackgroundColor(settings, kTransparencyChecker1Key, background.checkerColor1);
  background.checkerColor2 =
      loadBackgroundColor(settings, kTransparencyChecker2Key, background.checkerColor2);
  appSettings.pixelValueFormat =
      pixelValueFormatFromString(settings.value(kPixelValueFormatKey).toString());
  appSettings.fitOnPageChange = settings.value(kFitOnPageChangeKey, false).toBool();
  appSettings.wrapNavigation = settings.value(kWrapNavigationKey, false).toBool();
  appSettings.showCellBorders = settings.value(kShowCellBordersKey, true).toBool();
  appSettings.decodeCacheBudgetMib =
      clampDecodeCacheBudgetMib(settings.value(kDecodeCacheBudgetKey, 1024).toInt());
  appSettings.gpuTextureBudgetMib =
      clampGpuTextureBudgetMib(settings.value(kGpuTextureBudgetKey, 2048).toInt());
  appSettings.animationStreamingThresholdMib = clampAnimationStreamingThresholdMib(
      settings.value(kAnimationStreamingThresholdKey, 128).toInt());
  appSettings.animationSyncMode =
      animationSyncModeFromString(settings.value(kAnimationSyncModeKey).toString());
  appSettings.magnifyFilter = magnifyFilterFromString(settings.value(kMagnifyFilterKey).toString());
  appSettings.minifyFilter = minifyFilterFromString(settings.value(kMinifyFilterKey).toString());
  appSettings.displayProfileMode =
      displayProfileModeFromString(settings.value(kDisplayProfileModeKey).toString());
  appSettings.displayProfilePath = settings.value(kDisplayProfilePathKey).toString();
  appSettings.analysisValueSpace =
      analysisValueSpaceFromString(settings.value(kAnalysisValueSpaceKey).toString());
  appSettings.renderingIntent =
      renderingIntentFromString(settings.value(kRenderingIntentKey).toString());
  appSettings.blackPointCompensation = settings.value(kBlackPointCompensationKey, false).toBool();
  appSettings.colorTheme = colorThemeFromString(settings.value(kColorThemeKey).toString());
  appSettings.cursorStyle = cursorStyleFromString(settings.value(kCursorStyleKey).toString());
  appSettings.language = settings.value(kLanguageKey, QStringLiteral("auto")).toString();
  return appSettings;
}

void AppSettings::save() const {
  QSettings settings;
  settings.setValue(kTransparencyPatternKey,
                    transparencyBackground.pattern == TransparencyPattern::Checkerboard
                        ? "checkerboard"
                        : "solid");
  settings.setValue(kTransparencySolidKey, transparencyBackground.solidColor.name());
  settings.setValue(kTransparencyChecker1Key, transparencyBackground.checkerColor1.name());
  settings.setValue(kTransparencyChecker2Key, transparencyBackground.checkerColor2.name());
  settings.setValue(kPixelValueFormatKey, pixelValueFormatToString(pixelValueFormat));
  settings.setValue(kFitOnPageChangeKey, fitOnPageChange);
  settings.setValue(kWrapNavigationKey, wrapNavigation);
  settings.setValue(kShowCellBordersKey, showCellBorders);
  settings.setValue(kDecodeCacheBudgetKey, clampDecodeCacheBudgetMib(decodeCacheBudgetMib));
  settings.setValue(kGpuTextureBudgetKey, clampGpuTextureBudgetMib(gpuTextureBudgetMib));
  settings.setValue(kAnimationStreamingThresholdKey,
                    clampAnimationStreamingThresholdMib(animationStreamingThresholdMib));
  settings.setValue(kAnimationSyncModeKey, animationSyncModeToString(animationSyncMode));
  settings.setValue(kMagnifyFilterKey, magnifyFilterToString(magnifyFilter));
  settings.setValue(kMinifyFilterKey, minifyFilterToString(minifyFilter));
  settings.setValue(kDisplayProfileModeKey, displayProfileModeToString(displayProfileMode));
  settings.setValue(kDisplayProfilePathKey, displayProfilePath);
  settings.setValue(kAnalysisValueSpaceKey, analysisValueSpaceToString(analysisValueSpace));
  settings.setValue(kRenderingIntentKey, renderingIntentToString(renderingIntent));
  settings.setValue(kBlackPointCompensationKey, blackPointCompensation);
  settings.setValue(kColorThemeKey, colorThemeToString(colorTheme));
  settings.setValue(kCursorStyleKey, cursorStyleToString(cursorStyle));
  settings.setValue(kLanguageKey, language);
}

int clampDecodeCacheBudgetMib(int budgetMib) {
  return std::clamp(budgetMib, kMinDecodeCacheBudgetMib, kMaxDecodeCacheBudgetMib);
}

int clampAnimationStreamingThresholdMib(int thresholdMib) {
  return std::clamp(thresholdMib, kMinAnimationStreamingThresholdMib,
                    kMaxAnimationStreamingThresholdMib);
}

qint64 resolveAnimationStreamingThresholdBytes(int thresholdMib, int environmentOverrideMib) {
  const int resolvedMib = environmentOverrideMib > 0
                              ? environmentOverrideMib
                              : clampAnimationStreamingThresholdMib(thresholdMib);
  return static_cast<qint64>(resolvedMib) * 1024LL * 1024LL;
}

int clampGpuTextureBudgetMib(int budgetMib) {
  return std::clamp(budgetMib, kMinGpuTextureBudgetMib, kMaxGpuTextureBudgetMib);
}

qint64 resolveGpuTextureBudgetBytes(int budgetMib, int environmentOverrideMib) {
  const int resolvedMib =
      environmentOverrideMib > 0 ? environmentOverrideMib : clampGpuTextureBudgetMib(budgetMib);
  return static_cast<qint64>(resolvedMib) * 1024LL * 1024LL;
}

qint64 resolveDecodeCacheBudgetBytes(int budgetMib, int environmentOverrideMib) {
  const int resolvedMib =
      environmentOverrideMib > 0 ? environmentOverrideMib : clampDecodeCacheBudgetMib(budgetMib);
  return static_cast<qint64>(resolvedMib) * 1024LL * 1024LL;
}

}  // namespace heiran::ui
