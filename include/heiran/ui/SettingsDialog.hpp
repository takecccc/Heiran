#pragma once

#include "heiran/ui/AppSettings.hpp"

#include <optional>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace heiran::ui {

// アプリ全体設定を編集する。値を確定した場合だけ設定を返す。
std::optional<AppSettings> showSettingsDialog(QWidget *parent, const AppSettings &current);

}  // namespace heiran::ui
