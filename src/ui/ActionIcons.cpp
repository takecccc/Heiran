#include "heiran/ui/ActionIcons.hpp"

#include "heiran/ui/AppStyle.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <QWidget>

namespace heiran::ui {

namespace {

class SvgIconEngine final : public QIconEngine {
public:
  explicit SvgIconEngine(QString resourcePath) : resourcePath_(std::move(resourcePath)) {}

  void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
    const qreal ratio = painter->device()->devicePixelRatio();
    QImage image = render(rect.size() * ratio, colorFor(mode, state));
    image.setDevicePixelRatio(ratio);
    painter->drawImage(rect.topLeft(), image);
  }

  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
    return QPixmap::fromImage(render(size, colorFor(mode, state)));
  }

  QIconEngine *clone() const override { return new SvgIconEngine(resourcePath_); }

private:
  static QColor colorFor(QIcon::Mode mode, QIcon::State state) {
    const QPalette palette = QGuiApplication::palette();
    if (mode == QIcon::Disabled) {
      return palette.color(QPalette::Disabled, QPalette::ButtonText);
    }
    if (state == QIcon::On) {
      return AppStyleTokens::forDarkScheme(isDarkPalette()).accentText;
    }
    return palette.color(QPalette::Active, QPalette::ButtonText);
  }

  // SVG を描いて、形の部分を指定の色で塗る。SVG 側の色 (currentColor) は使わない。
  QImage render(const QSize &size, const QColor &color) const {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(resourcePath_);
    if (!renderer.isValid() || size.isEmpty()) {
      return image;
    }
    QPainter painter(&image);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    return image;
  }

  QString resourcePath_;
};

}  // namespace

QIcon themedIcon(const QString &name) {
  return QIcon(new SvgIconEngine(QString(":/icons/%1.svg").arg(name)));
}

const QHash<QString, QString> &actionIconNames() {
  static const QHash<QString, QString> names = {
      // ツールバー。
      {"Prev", "arrow-left"},
      {"Next", "arrow-right"},
      {"Fit", "scan"},
      {"Fit on Page Change", "scan-line"},
      {"Reset Exposure", "sun"},
      {"Reset View", "rotate-ccw"},
      // File。
      {"Open Image...", "folder-open"},
      {"Settings...", "settings"},
      {"Save", "save"},
      {"Save As...", "save"},
      {"Rename...", "pencil"},
      {"Move to Recycle Bin...", "trash-2"},
      {"Copy File Path", "clipboard-copy"},
      {"Show in Folder", "external-link"},
      {"Print...", "printer"},
      {"Refresh File List", "refresh-cw"},
      // Images、View。
      {"Clear All Cells", "x"},
      {"Manage Image Sets...", "images"},
      {"Rotate Right", "rotate-cw"},
      {"Rotate Left", "rotate-ccw"},
      {"Invert All Cells", "contrast"},
      {"Copy Image", "copy"},
      {"Favorite", "star"},
      {"Favorites Only", "images"},
      {"Image List", "panel-left"},
      {"Copy Favorites...", "copy"},
      {"Image Info...", "info"},
      {"Channel", "layers"},
      // セルの右クリック。
      {"Enable Sync", "link"},
      {"Disable Sync", "unlink"},
      {"Scale", "zoom-in"},
      {"Gamma", "sun-moon"},
      {"Rotate", "rotate-cw"},
      {"Flip Horizontally", "flip-horizontal"},
      {"Flip Vertically", "flip-vertical"},
      {"Invert", "contrast"},
      {"Show Label", "tag"},
      {"Edit Label...", "pencil"},
      {"Clear Cell", "x"},
      {"Assign Image", "image"},
      {"Reset Cell Display", "rotate-ccw"},
  };
  // 項目の文言は翻訳される。翻訳された文言でも引けるようにする (項目を作る側 MainWindow と
  // CellContextMenu の文脈で引く)。
  // 文言は、利用者向けに表示されるものと同じ文言で、ここで増やしているのは、引く鍵だけ。
  static const QHash<QString, QString> withTranslations = [&]() {
    QHash<QString, QString> all = names;
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
      const QByteArray source = it.key().toUtf8();
      for (const char *context :
           {"MainWindow", "CellContextMenu", "heiran::ui::FavoritesController",
            "heiran::ui::ImageListPanelController"}) {
        all.insert(QCoreApplication::translate(context, source.constData()), it.value());
      }
    }
    return all;
  }();
  return withTranslations;
}

void applyActionIcons(QWidget *root) {
  if (root == nullptr) {
    return;
  }
  const auto &names = actionIconNames();
  for (auto *action : root->findChildren<QAction *>()) {
    if (!action->icon().isNull()) {
      continue;
    }
    const auto it = names.constFind(action->text().remove('&'));
    if (it != names.constEnd()) {
      action->setIcon(themedIcon(it.value()));
    }
  }
}

}  // namespace heiran::ui
