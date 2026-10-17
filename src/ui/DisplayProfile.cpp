#include "heiran/ui/DisplayProfile.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QRect>
#include <QScreen>
#include <cmath>
#include <limits>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace heiran::ui {

namespace {

// 画面ごとに取っておく。プロファイルはファイルから読むため、描画のたびに
// 読み直すわけにはいかない。
QHash<QString, QColorSpace> &systemProfileCache() {
  static QHash<QString, QColorSpace> cache;
  return cache;
}

#if defined(Q_OS_WIN)

// GDI の装置名を持つモニタの情報。
struct MonitorEntry {
  // GDI の装置名。バックスラッシュ 2 つで始まる DISPLAY1 のような名前。
  QString deviceName;
  QRect nativeGeometry;
};

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info) == FALSE) {
    return TRUE;
  }

  auto *entries = reinterpret_cast<QList<MonitorEntry> *>(userData);
  entries->append({.deviceName = QString::fromWCharArray(info.szDevice),
                   .nativeGeometry = QRect(info.rcMonitor.left, info.rcMonitor.top,
                                           info.rcMonitor.right - info.rcMonitor.left,
                                           info.rcMonitor.bottom - info.rcMonitor.top)});
  return TRUE;
}

// 画面に対応する GDI の装置名を返す。
//
// `QScreen::name()` は Windows ではモニタの表示名 (EDID の名前) を返すため、
// GDI の装置名としては使えない。配置と大きさで突き合わせる。
QString gdiDeviceNameForScreen(const QScreen *screen) {
  if (screen == nullptr) {
    return {};
  }

  QList<MonitorEntry> entries;
  EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&entries));
  if (entries.isEmpty()) {
    return {};
  }
  if (entries.size() == 1) {
    return entries.first().deviceName;
  }

  // Qt の座標は論理値のため、画面ごとの倍率を掛けて物理値へ戻してから比べる。
  // 倍率の違う画面が混ざると原点がずれることがあるので、最も近いものを採る。
  const qreal ratio = screen->devicePixelRatio();
  const QRect logical = screen->geometry();
  const QRect native(static_cast<int>(std::lround(logical.x() * ratio)),
                     static_cast<int>(std::lround(logical.y() * ratio)),
                     static_cast<int>(std::lround(logical.width() * ratio)),
                     static_cast<int>(std::lround(logical.height() * ratio)));

  QString bestDeviceName;
  qint64 bestScore = std::numeric_limits<qint64>::max();
  for (const auto &entry : entries) {
    const qint64 score = std::abs(entry.nativeGeometry.width() - native.width()) +
                         std::abs(entry.nativeGeometry.height() - native.height()) +
                         std::abs(entry.nativeGeometry.x() - native.x()) +
                         std::abs(entry.nativeGeometry.y() - native.y());
    if (score < bestScore) {
      bestScore = score;
      bestDeviceName = entry.deviceName;
    }
  }
  return bestDeviceName;
}

// 装置に割り当てられている ICC プロファイルの経路を返す。
QString iccProfilePathForDevice(const QString &deviceName) {
  // 装置名が分からない場合は既定の画面の DC を使う。
  const HDC deviceContext =
      deviceName.isEmpty()
          ? CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr)
          : CreateDCW(nullptr, reinterpret_cast<const wchar_t *>(deviceName.utf16()), nullptr,
                      nullptr);
  if (deviceContext == nullptr) {
    return {};
  }

  DWORD pathLength = 0;
  // 1 度目は長さだけを聞く。用意が足りない場合も FALSE を返すため、
  // 戻り値ではなく長さを見る。
  GetICMProfileW(deviceContext, &pathLength, nullptr);
  if (pathLength == 0) {
    DeleteDC(deviceContext);
    return {};
  }

  std::wstring buffer(pathLength, L'\0');
  const BOOL ok = GetICMProfileW(deviceContext, &pathLength, buffer.data());
  DeleteDC(deviceContext);
  if (ok == FALSE) {
    return {};
  }

  return QString::fromWCharArray(buffer.data());
}
#endif

}  // namespace

QString displayProfileModeToString(DisplayProfileMode mode) {
  switch (mode) {
    case DisplayProfileMode::SRgb:
      return "srgb";
    case DisplayProfileMode::File:
      return "file";
    case DisplayProfileMode::System:
    default:
      return "system";
  }
}

DisplayProfileMode displayProfileModeFromString(const QString &text) {
  const QString normalized = text.trimmed().toLower();
  if (normalized == "srgb") {
    return DisplayProfileMode::SRgb;
  }
  if (normalized == "file") {
    return DisplayProfileMode::File;
  }
  return DisplayProfileMode::System;
}

QColorSpace colorSpaceFromIccFile(const QString &filePath) {
  if (filePath.isEmpty()) {
    return {};
  }

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }

  // ICC プロファイルは大きくても数 MiB。上限を置いて、誤った経路を渡された
  // ときに読み込み続けないようにする。
  constexpr qint64 kMaximumProfileBytes = 16 * 1024 * 1024;
  return QColorSpace::fromIccProfile(file.read(kMaximumProfileBytes));
}

QColorSpace systemDisplayColorSpace(const QScreen *screen) {
  const QScreen *targetScreen = screen != nullptr ? screen : QGuiApplication::primaryScreen();
  const QString deviceName = targetScreen != nullptr ? targetScreen->name() : QString();

  auto &cache = systemProfileCache();
  const auto cached = cache.constFind(deviceName);
  if (cached != cache.constEnd()) {
    return cached.value();
  }

  QColorSpace colorSpace;
#if defined(Q_OS_WIN)
  colorSpace = colorSpaceFromIccFile(iccProfilePathForDevice(gdiDeviceNameForScreen(targetScreen)));
#endif
  cache.insert(deviceName, colorSpace);
  return colorSpace;
}

QColorSpace resolveDisplayColorSpace(
    DisplayProfileMode mode, const QString &profilePath, const QScreen *screen,
    const std::function<QColorSpace(const QScreen *)> &systemProvider) {
  const QColorSpace fallback(QColorSpace::SRgb);

  switch (mode) {
    case DisplayProfileMode::SRgb:
      return fallback;
    case DisplayProfileMode::File: {
      const QColorSpace fromFile = colorSpaceFromIccFile(profilePath);
      // 読めない経路を指していても表示は続ける。sRGB として出すほうが、
      // 何も出さないより使える。
      return fromFile.isValid() ? fromFile : fallback;
    }
    case DisplayProfileMode::System:
    default: {
      const QColorSpace fromSystem =
          systemProvider ? systemProvider(screen) : systemDisplayColorSpace(screen);
      return fromSystem.isValid() ? fromSystem : fallback;
    }
  }
}

void clearDisplayProfileCache() {
  systemProfileCache().clear();
}

}  // namespace heiran::ui
