#include "heiran/ui/ThemeEditor.hpp"

#include "heiran/ui/AppStyle.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>

namespace heiran::ui {
namespace {

struct ColorField {
  const char *label;
  const char *name;
  QColor CustomColorTheme::*setting;
  QColor AppStyleTokens::*effective;
};

constexpr ColorField colorFields[] = {
    {QT_TRANSLATE_NOOP("heiran::ui::ThemeEditor", "Window background"), "customThemeWindow",
     &CustomColorTheme::window, &AppStyleTokens::window},
    {QT_TRANSLATE_NOOP("heiran::ui::ThemeEditor", "Control surface"), "customThemePanel",
     &CustomColorTheme::panel, &AppStyleTokens::panel},
    {QT_TRANSLATE_NOOP("heiran::ui::ThemeEditor", "Input background"), "customThemeInput",
     &CustomColorTheme::input, &AppStyleTokens::input},
    {QT_TRANSLATE_NOOP("heiran::ui::ThemeEditor", "Text color"), "customThemeText",
     &CustomColorTheme::text, &AppStyleTokens::text},
    {QT_TRANSLATE_NOOP("heiran::ui::ThemeEditor", "Accent color"), "customThemeAccent",
     &CustomColorTheme::accent, &AppStyleTokens::accent},
};

}  // namespace

ThemeEditor::ThemeEditor(QWidget *parent)
    : QWidget(parent), scheme_(new QComboBox(this)),
      reset_(new QPushButton(tr("Reset Colors"), this)) {
  static_assert(std::size(colorFields) == std::tuple_size_v<decltype(colors_)>);
  setObjectName("customThemeEditor");
  setToolTip(tr("Changes are previewed immediately. Use OK to save or Cancel to restore the "
                "previous theme."));
  auto *form = new QFormLayout(this);
  form->setContentsMargins(0, 0, 0, 0);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  scheme_->setObjectName("customThemeBaseScheme");
  scheme_->addItem(tr("Light"));
  scheme_->addItem(tr("Dark"));
  form->addRow(tr("Base scheme"), scheme_);

  for (std::size_t index = 0; index < colors_.size(); ++index) {
    const auto &field = colorFields[index];
    const auto label = tr(field.label);
    auto *button = colors_[index] = new QPushButton(this);
    button->setObjectName(field.name);
    button->setAccessibleName(label);
    button->setAutoDefault(false);
    form->addRow(label, button);
    connect(button, &QPushButton::clicked, this, [this, field, label]() {
      const auto tokens = AppStyleTokens::forCustomTheme(theme_);
      const QColor original = tokens.*field.effective;
      const auto chosen =
          QColorDialog::getColor(original, this, label, QColorDialog::DontUseNativeDialog);
      if (chosen.isValid() && chosen.rgb() != original.rgb()) {
        theme_.*field.setting = chosen;
        updateSwatches();
        Q_EMIT themeChanged();
      }
    });
  }
  connect(scheme_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (isPreset_) {
      theme_ = {};
    }
    theme_.dark = index == 1;
    updateSwatches();
    Q_EMIT themeChanged();
  });

  reset_->setObjectName("resetCustomThemeColors");
  reset_->setAutoDefault(false);
  form->addRow(QString(), reset_);
  connect(reset_, &QPushButton::clicked, this, [this]() {
    theme_ = {.dark = theme_.dark};
    updateSwatches();
    Q_EMIT themeChanged();
  });
}

void ThemeEditor::setTheme(const CustomColorTheme &theme, bool isPreset) {
  theme_ = theme;
  isPreset_ = isPreset;
  const QSignalBlocker blocker(scheme_);
  scheme_->setCurrentIndex(theme_.dark ? 1 : 0);
  reset_->setEnabled(!isPreset_);
  updateSwatches();
}

void ThemeEditor::updateSwatches() {
  const auto tokens = AppStyleTokens::forCustomTheme(theme_);
  for (std::size_t index = 0; index < colors_.size(); ++index) {
    const auto color = tokens.*colorFields[index].effective;
    QPixmap swatch(24, 16);
    swatch.fill(color);
    colors_[index]->setIcon(QIcon(swatch));
    colors_[index]->setText(color.name());
  }
}

}  // namespace heiran::ui
