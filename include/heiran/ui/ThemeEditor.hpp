#pragma once

#include "heiran/ui/ColorTheme.hpp"

#include <QWidget>

#include <array>

class QComboBox;
class QPushButton;

namespace heiran::ui {

class ThemeEditor final : public QWidget {
  Q_OBJECT

public:
  explicit ThemeEditor(QWidget *parent = nullptr);
  void setTheme(const CustomColorTheme &theme, bool isPreset);
  const CustomColorTheme &theme() const { return theme_; }

Q_SIGNALS:
  void themeChanged();

private:
  void updateSwatches();

  CustomColorTheme theme_;
  bool isPreset_ = false;
  QComboBox *scheme_;
  QPushButton *reset_;
  std::array<QPushButton *, 5> colors_;
};

}  // namespace heiran::ui
