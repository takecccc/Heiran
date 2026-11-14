#include "heiran/ui/WelcomeWidget.hpp"
#include "heiran/ui/AppStyle.hpp"

#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QVBoxLayout>

namespace heiran::ui {

namespace {

QStringList localPathsOf(const QMimeData *mimeData) {
  QStringList paths;
  if (mimeData == nullptr || !mimeData->hasUrls()) {
    return paths;
  }
  for (const auto &url : mimeData->urls()) {
    if (url.isLocalFile()) {
      paths.push_back(url.toLocalFile());
    }
  }
  return paths;
}

QLabel *makeLabel(const QString &text, int pointSize, bool bold, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  QFont font = label->font();
  font.setPointSize(pointSize);
  font.setBold(bold);
  label->setFont(font);
  label->setAlignment(Qt::AlignHCenter);
  label->setWordWrap(true);
  label->setAttribute(Qt::WA_TransparentForMouseEvents);
  return label;
}

}  // namespace

WelcomeWidget::WelcomeWidget(QWidget *parent) : QWidget(parent) {
  setObjectName("welcome");
  setAcceptDrops(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setStyleSheet(
      "#welcome QLabel { color: #b4b9c2; }"
      "#welcome QLabel#welcomeTitle { color: #e4e7ec; }"
      "#welcome QLabel#welcomeHint { color: #8d929b; }"
      "#welcome QPushButton#welcomeOpen { padding: 8px 22px; }"
      "#welcome QPushButton#welcomeRecent { border: none; color: #8fb8d8; padding: 2px 8px;"
      " background: transparent; }"
      "#welcome QPushButton#welcomeRecent:hover { color: #c4defa; text-decoration: underline; }");

  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setStyleSheet(
      "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
  scroll->viewport()->setAutoFillBackground(false);
  auto *content = new QWidget;
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSizeConstraint(QLayout::SetMinimumSize);
  scroll->setWidget(content);
  outerLayout->addWidget(scroll);
  layout->setAlignment(Qt::AlignVCenter);
  layout->setSpacing(10);

  auto *title = makeLabel(tr("Heiran"), 22, true, this);
  title->setObjectName("welcomeTitle");
  layout->addWidget(title);
  layout->addWidget(
      makeLabel(tr("An image viewer that shows several images in sync"), 10, false, this));
  layout->addSpacing(14);
  layout->addWidget(makeLabel(tr("Drop images or a folder here"), 13, false, this));

  auto *openButton = new QPushButton(tr("Open Image..."), this);
  openButton->setObjectName("welcomeOpen");
  openButton->setCursor(Qt::PointingHandCursor);
  connect(openButton, &QPushButton::clicked, this, &WelcomeWidget::openImageRequested);
  layout->addWidget(openButton, 0, Qt::AlignHCenter);

  auto *hint =
      makeLabel(tr("Ctrl+O  open     F1  manual     Tab  compare one by one"), 9, false, this);
  hint->setObjectName("welcomeHint");
  layout->addWidget(hint);

  recentContainer_ = new QWidget(this);
  recentContainer_->setObjectName("welcomeRecentList");
  recentLayout_ = new QVBoxLayout(recentContainer_);
  recentLayout_->setContentsMargins(0, 12, 0, 0);
  recentLayout_->setSpacing(0);
  layout->addWidget(recentContainer_);
  rebuildRecentList();

  // 親 (画像の面) の大きさに、いつも合わせる。
  if (parent != nullptr) {
    parent->installEventFilter(this);
    setGeometry(parent->rect());
  }
}

void WelcomeWidget::setRecentImages(const QStringList &paths) {
  if (paths == recentImages_) {
    return;
  }
  recentImages_ = paths;
  rebuildRecentList();
}

void WelcomeWidget::rebuildRecentList() {
  while (QLayoutItem *item = recentLayout_->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  recentContainer_->setVisible(!recentImages_.isEmpty());
  if (recentImages_.isEmpty()) {
    return;
  }
  auto *heading = makeLabel(tr("Recent images"), 9, false, recentContainer_);
  heading->setObjectName("welcomeHint");
  recentLayout_->addWidget(heading);
  const int count = std::min<int>(recentImages_.size(), kMaxRecentImages);
  for (int index = 0; index < count; ++index) {
    const QString path = recentImages_.at(index);
    // 長いパスは、フォルダ名とファイル名だけを見せる
    // (同じ名前の連番を見分けるため)。全体は説明に出す。
    const QFileInfo info(path);
    const QString folder = info.absoluteDir().dirName();
    auto *button = new QPushButton(folder.isEmpty() ? info.fileName()
                                                    : QString("%1/%2").arg(folder, info.fileName()),
                                   recentContainer_);
    button->setObjectName("welcomeRecent");
    button->setProperty("displayPath", button->text());
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(QDir::toNativeSeparators(path));
    connect(button, &QPushButton::clicked, this,
            [this, path]() { Q_EMIT recentImageRequested(path); });
    recentLayout_->addWidget(button);
  }
  updateRecentButtonTexts();
}

void WelcomeWidget::updateRecentButtonTexts() {
  for (auto *button : findChildren<QPushButton *>("welcomeRecent")) {
    button->setText(button->fontMetrics().elidedText(button->property("displayPath").toString(),
                                                     Qt::ElideMiddle, std::max(0, width() - 80)));
  }
}

void WelcomeWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), AppStyleTokens::viewerBackground());
}

void WelcomeWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateRecentButtonTexts();
}

bool WelcomeWidget::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parentWidget() && event->type() == QEvent::Resize) {
    setGeometry(parentWidget()->rect());
    raise();
  }
  return QWidget::eventFilter(watched, event);
}

void WelcomeWidget::dragEnterEvent(QDragEnterEvent *event) {
  if (localPathsOf(event->mimeData()).isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
}

void WelcomeWidget::dragMoveEvent(QDragMoveEvent *event) {
  if (localPathsOf(event->mimeData()).isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
}

void WelcomeWidget::dropEvent(QDropEvent *event) {
  const QStringList paths = localPathsOf(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }
  Q_EMIT filesDropped(paths);
  event->acceptProposedAction();
}

}  // namespace heiran::ui
