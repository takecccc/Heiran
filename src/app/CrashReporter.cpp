#include "heiran/app/CrashReporter.hpp"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h は windows.h の後に置く必要がある。
#include <dbghelp.h>
#endif

namespace heiran::app {

namespace {

QString defaultDumpDirectory() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  return base.isEmpty() ? QDir::tempPath() + "/Heiran/crashes" : base + "/crashes";
}

// 落ちた後に文字列を組み立てるのは避けたい。仕掛けるときに決めて持っておく。
QString &dumpDirectoryStorage() {
  static QString directory;
  return directory;
}

QString &lastDumpPathStorage() {
  static QString path;
  return path;
}

#if defined(Q_OS_WIN)

// 落ちたときに使う経路の元。ワイド文字で持っておき、後ろに時刻を足すだけにする。
std::wstring &dumpPathPrefix() {
  static std::wstring prefix;
  return prefix;
}

// 例外フィルタの中では、確保や locale に触る処理を避ける。時刻の整形は
// Win32 の API だけで済ませる。
void buildDumpPath(wchar_t *buffer, size_t capacity) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const std::wstring &prefix = dumpPathPrefix();
  swprintf(buffer, capacity, L"%ls/heiran-%04u%02u%02u-%02u%02u%02u-%lu.dmp", prefix.c_str(),
           now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
           GetCurrentProcessId());
}

LONG WINAPI writeMiniDump(EXCEPTION_POINTERS *exceptionPointers) {
  wchar_t dumpPath[MAX_PATH * 2] = {};
  buildDumpPath(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]));

  HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{};
  exceptionInformation.ThreadId = GetCurrentThreadId();
  exceptionInformation.ExceptionPointers = exceptionPointers;
  exceptionInformation.ClientPointers = FALSE;

  // 呼び出し履歴と、そこから辿れる範囲の値を残す。null 関数ポインタ経由の
  // 呼び出しを疑っているため、履歴が要る。全メモリを落とすと数 GiB になるので
  // 取らない。
  const auto dumpType = static_cast<MINIDUMP_TYPE>(
      MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithHandleData);

  const BOOL written = MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), file, dumpType,
      exceptionPointers != nullptr ? &exceptionInformation : nullptr, nullptr, nullptr);
  CloseHandle(file);

  if (written == FALSE) {
    DeleteFileW(dumpPath);
    return EXCEPTION_CONTINUE_SEARCH;
  }

  lastDumpPathStorage() = QString::fromWCharArray(dumpPath);
  // 既定の処理へ渡さず、ここで終わらせる。渡すと OS の報告ダイアログが出て、
  // 書き出したダンプに気づきにくくなる。
  return EXCEPTION_EXECUTE_HANDLER;
}

#endif

}  // namespace

QString crashDumpDirectory() {
  const QString &directory = dumpDirectoryStorage();
  return directory.isEmpty() ? defaultDumpDirectory() : directory;
}

QString lastCrashDumpPath() {
  return lastDumpPathStorage();
}

bool installCrashReporter(const QString &dumpDirectory) {
  const QString directory = dumpDirectory.isEmpty() ? defaultDumpDirectory() : dumpDirectory;
  if (!QDir().mkpath(directory)) {
    return false;
  }
  dumpDirectoryStorage() = directory;

#if defined(Q_OS_WIN)
  const QString nativeDirectory = QDir(directory).absolutePath();
  dumpPathPrefix() = nativeDirectory.toStdWString();
  SetUnhandledExceptionFilter(writeMiniDump);
  return true;
#else
  return false;
#endif
}

}  // namespace heiran::app
