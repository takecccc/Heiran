#pragma once

#include <QString>

namespace heiran::app {

// 落ちたときにミニダンプを残す。
//
// まれに `0xC0000005` で落ちる事象を追っているが、再現手順が無く、落ちた場所も
// 特定できていない。次に起きたときに調べられるよう、そのときの状態を書き出す。
//
// `dumpDirectory` は書き出し先。空なら `%LOCALAPPDATA%/Heiran/crashes` を使う。
// 落ちた後に領域を確保したり経路を組み立てたりはできないため、ここで先に作って
// 保持しておく。
//
// 戻り値は仕掛けられたかどうか。Windows 以外では常に false を返す。
bool installCrashReporter(const QString &dumpDirectory = QString());

// 書き出し先。`installCrashReporter()` の前に呼んだ場合は既定の場所を返す。
QString crashDumpDirectory();

// 直近に書き出したダンプの経路。まだ無ければ空。テストと調査で使う。
QString lastCrashDumpPath();

}  // namespace heiran::app
