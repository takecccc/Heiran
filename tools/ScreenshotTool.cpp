// 開発用。主ウィンドウを開いて、画面を PNG へ書き出す。見た目の調整を目で確かめるためのもの。
//
//   heiran_screenshot <out.png> [--theme light|dark] [--lang en|ja] [--size 1280x800] [--menu View]
//                     [--analysis Histogram|"Diff Window"|"Change Graph"] [--diff-cell]
//                     [--hover-cell N] [--hrn file.hrn] [--dialog "Settings..."]
//                     <画像ファイル...>
//
// --hrn は (QApplication が -session を取ってしまうため、この名前にする)、.hrn を読み込む
// (画像ファイルの代わり。差分の設定なども、.hrn で指定できる)。
// --dialog 名前 は、その名前のメニュー項目 (モーダルなダイアログを開くもの)
// を実行して、開いたダイアログを書き出して閉じる (例: --dialog "Settings..."、--dialog "About
// Heiran...")。ファイル選択のようなネイティブのダイアログは写せない。
// --action 名前 は、その名前のメニュー項目を実行する (例: --action "Pixel
// Info")。何度でも指定できる。
// --cursor セル,x,y は、そのセルの画像の面の上へ、マウスを動かす
// (画素値の表示と、共有のカーソルが出る)。
// --diff-cell は、画像を開いたあと、差分のセルを足してから書き出す。
// --dialog-tab 名前 は、--dialog
// で開いたダイアログのタブのうち、その名前のものを開いてから書き出す。
// --dialog-wait ms は、ダイアログを開いてから書き出すまでの待ち (既定
// 1500。サムネイルのように、読み込みを待つものは長くする)。
// --solo N は、N 番目 (0 から) のセルだけを、格子の全体へ広げて見せる (単独表示)。
// --diff-popup は、--hover-cell
// のセルの差分の帯で、歯車のボタンを押して開いた、設定のポップアップを書き出す。
// --hover-cell N は、N 番目 (0 から) のセルへマウスが乗った状態にする (差分の帯が出る)。
//
// セルの画像 (GPU 描画) も、ウィンドウの grab に写る
// (手元の環境で確認済み)。写らない環境では、枠まわり
// (メニュー、ツールバー、ステータス、ドック、セルの枠) の確認に使う。

#include "heiran/app/ApplicationController.hpp"
#include "heiran/app/Localization.hpp"
#include "heiran/ui/AppSettings.hpp"
#include "heiran/ui/ColorTheme.hpp"
#include "heiran/ui/ImageViewWidget.hpp"
#include "heiran/ui/MainWindow.hpp"
#include "heiran/ui/ViewerGridWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFrame>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSize>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>

namespace {

// 画面の項目の名前が、指定の (英語の) 名前と同じか。--lang ja のときは、翻訳された名前でも合う。
// アクセラレーターの印 (&F、(&F)) は、無視する。
bool sameName(const QString &shown, const QString &english) {
  static const QRegularExpression accelerator("\(&.\)");
  const auto normalize = [](QString text) { return text.remove(accelerator).remove('&'); };
  const QString target = normalize(shown);
  if (target == normalize(english)) {
    return true;
  }
  // メニューの題は、アクセラレーターの印つきで訳してある
  // (&File)。印のない名前でも、引けるようにする。
  for (const QString &candidate : {english, "&" + english}) {
    const QByteArray source = candidate.toUtf8();
    for (const char *context : {"MainWindow", "ImageSetManagerDialog", "SettingsDialog",
                                "HistogramWindow", "heiran::ui::DiffBarWidget"}) {
      if (target == normalize(QCoreApplication::translate(context, source.constData()))) {
        return true;
      }
    }
  }
  return false;
}

// イベントを回しながら、指定した時間だけ待つ。
void pump(int milliseconds) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("HeiranScreenshot");
  QCoreApplication::setApplicationName("HeiranScreenshot");

  const QStringList arguments = app.arguments().mid(1);
  QString outPath;
  QString theme = "dark";
  QSize size(1280, 800);
  QString menuTitle;
  QString analysisTitle;
  bool addDiffCell = false;
  int hoverCell = -1;
  bool diffPopup = false;
  int soloCell = -1;
  QStringList actionTitles;
  QString sessionPath;
  QString dialogTitle;
  int dialogWaitMs = 1500;
  QString dialogTab;
  int cursorCell = -1;
  QPoint cursorPosition;
  QPoint hoverPosition(-1, -1);
  int wheelSteps = 0;
  QString checkText;
  QStringList images;
  for (int index = 0; index < arguments.size(); ++index) {
    const QString &argument = arguments.at(index);
    if (argument == "--theme" && index + 1 < arguments.size()) {
      theme = arguments.at(++index);
    } else if (argument == "--lang" && index + 1 < arguments.size()) {
      heiran::app::installTranslators(app, arguments.at(++index));
    } else if (argument == "--menu" && index + 1 < arguments.size()) {
      menuTitle = arguments.at(++index);
    } else if (argument == "--analysis" && index + 1 < arguments.size()) {
      analysisTitle = arguments.at(++index);
    } else if (argument == "--hover-cell" && index + 1 < arguments.size()) {
      hoverCell = arguments.at(++index).toInt();
    } else if (argument == "--solo" && index + 1 < arguments.size()) {
      soloCell = arguments.at(++index).toInt();
    } else if (argument == "--diff-popup") {
      diffPopup = true;
    } else if (argument == "--hrn" && index + 1 < arguments.size()) {
      sessionPath = arguments.at(++index);
    } else if (argument == "--dialog-tab" && index + 1 < arguments.size()) {
      dialogTab = arguments.at(++index);
    } else if (argument == "--dialog-wait" && index + 1 < arguments.size()) {
      dialogWaitMs = arguments.at(++index).toInt();
    } else if (argument == "--dialog" && index + 1 < arguments.size()) {
      dialogTitle = arguments.at(++index);
    } else if (argument == "--action" && index + 1 < arguments.size()) {
      actionTitles.push_back(arguments.at(++index));
    } else if (argument == "--cursor" && index + 1 < arguments.size()) {
      const QStringList parts = arguments.at(++index).split(',');
      if (parts.size() == 3) {
        cursorCell = parts.at(0).toInt();
        cursorPosition = QPoint(parts.at(1).toInt(), parts.at(2).toInt());
      }
    } else if (argument == "--diff-cell") {
      addDiffCell = true;
    } else if (argument == "--hover" && index + 1 < arguments.size()) {
      const QStringList parts = arguments.at(++index).split(',');
      if (parts.size() == 2) {
        hoverPosition = QPoint(parts.at(0).toInt(), parts.at(1).toInt());
      }
    } else if (argument == "--check" && index + 1 < arguments.size()) {
      checkText = arguments.at(++index);
    } else if (argument == "--wheel" && index + 1 < arguments.size()) {
      wheelSteps = arguments.at(++index).toInt();
    } else if (argument == "--size" && index + 1 < arguments.size()) {
      const QStringList parts = arguments.at(++index).split('x');
      if (parts.size() == 2) {
        size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
      }
    } else if (outPath.isEmpty()) {
      outPath = argument;
    } else {
      images.push_back(argument);
    }
  }
  if (outPath.isEmpty()) {
    return 2;
  }

  // 主ウィンドウは設定から配色を適用する。専用の保存先へ書いて、そこから読ませる。
  auto settings = heiran::ui::AppSettings::load();
  settings.colorTheme = heiran::ui::colorThemeFromString(theme);
  settings.save();
  heiran::ui::MainWindow window;
  window.resize(size);
  if (!sessionPath.isEmpty()) {
    heiran::app::ApplicationController controller;
    const auto opened = controller.openSession(sessionPath);
    if (!opened.ok) {
      return 5;
    }
    window.setSession(opened.session);
  } else if (!images.isEmpty()) {
    window.openImageFiles(images);
  } else {
    // 何も指定しなければ、アプリを引数なしで起動したときと同じ、既定のセッションで始める。
    heiran::app::ApplicationController controller;
    const auto started = controller.startup(std::nullopt);
    window.setSession(started.session);
    window.setStartupMessage(started.startupMessage);
  }
  window.show();

  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  if (addDiffCell) {
    for (auto *action : window.findChildren<QAction *>()) {
      if (sameName(action->text(), "Diff Cell")) {
        action->trigger();
      }
    }
    QElapsedTimer diffTimer;
    diffTimer.start();
    while (diffTimer.elapsed() < 2000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
  }
  // 項目を、名前で実行する (Pixel Info の表示など)。--action は何度でも指定できる。
  for (const QString &title : actionTitles) {
    for (auto *action : window.findChildren<QAction *>()) {
      if (sameName(action->text(), title)) {
        action->trigger();
        break;
      }
    }
    QElapsedTimer actionTimer;
    actionTimer.start();
    while (actionTimer.elapsed() < 600) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
  }
  // 指定したセルの画像の上へ、マウスを動かす (画素値の表示や、共有のカーソルが出る)。
  // 位置は、そのセルの画像の面の左上からの論理座標。
  if (cursorCell >= 0) {
    for (auto *cell : window.findChildren<heiran::ui::ImageViewWidget *>()) {
      if (cell->cellIndex() != cursorCell) {
        continue;
      }
      if (QWidget *canvas = cell->findChild<QWidget *>("imageCanvas")) {
        QMouseEvent move(QEvent::MouseMove, QPointF(cursorPosition),
                         canvas->mapToGlobal(cursorPosition), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &move);
      }
    }
    QElapsedTimer cursorTimer;
    cursorTimer.start();
    while (cursorTimer.elapsed() < 600) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
  }
  if (hoverCell >= 0) {
    const auto cells = window.findChildren<heiran::ui::ImageViewWidget *>();
    for (auto *cell : cells) {
      if (cell->cellIndex() == hoverCell) {
        cell->setAttribute(Qt::WA_UnderMouse, true);
        QEnterEvent enter(QPointF(10, 10), QPointF(10, 10), QPointF(10, 10));
        QCoreApplication::sendEvent(cell, &enter);
      }
    }
    QElapsedTimer hoverTimer;
    hoverTimer.start();
    while (hoverTimer.elapsed() < 500) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
  }
  if (soloCell >= 0) {
    // 単独表示 (1 つのセルを、格子の全体へ広げる)。
    if (auto *grid = window.findChild<heiran::ui::ViewerGridWidget *>()) {
      grid->setSoloCell(soloCell);
    }
    pump(800);
  }
  if (diffPopup) {
    // 歯車のボタンを押して、開いたポップアップだけを書き出す。
    for (auto *cell : window.findChildren<heiran::ui::ImageViewWidget *>()) {
      if (cell->cellIndex() != hoverCell) {
        continue;
      }
      for (auto *button : cell->findChildren<QToolButton *>()) {
        if (sameName(button->toolTip(), "Diff settings")) {
          button->click();
        }
      }
    }
    pump(600);
    for (auto *widget : window.findChildren<QFrame *>("diffSettingsPopup")) {
      if (widget->isVisible()) {
        return widget->grab().save(outPath) ? 0 : 1;
      }
    }
    return 8;
  }
  if (!dialogTitle.isEmpty()) {
    // モーダルなダイアログは、開くと呼び出しが戻らない。開いたあとに動く時計で、書き出して閉じる。
    int result = 6;
    QTimer::singleShot(dialogWaitMs, [&]() {
      if (QWidget *modal = QApplication::activeModalWidget()) {
        if (!dialogTab.isEmpty()) {
          // タブのあるダイアログで、指定した名前のタブを開く。
          for (auto *tabs : modal->findChildren<QTabWidget *>()) {
            for (int tab = 0; tab < tabs->count(); ++tab) {
              if (sameName(tabs->tabText(tab), dialogTab)) {
                tabs->setCurrentIndex(tab);
              }
            }
          }
          QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }
        result = modal->grab().save(outPath) ? 0 : 1;
        modal->close();
      }
    });
    for (auto *action : window.findChildren<QAction *>()) {
      if (sameName(action->text(), dialogTitle)) {
        action->trigger();
        return result;
      }
    }
    return 7;
  }
  if (!analysisTitle.isEmpty()) {
    // 解析メニューの項目を実行し、開いた解析ウィンドウを書き出す。
    for (auto *action : window.findChildren<QAction *>()) {
      if (sameName(action->text(), analysisTitle)) {
        action->trigger();
      }
    }
    QElapsedTimer analysisTimer;
    analysisTimer.start();
    while (analysisTimer.elapsed() < 2500) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (widget != &window && widget->isVisible() && widget->isWindow() &&
          !widget->inherits("QMenu")) {
        widget->resize(widget->size().expandedTo(QSize(760, 520)));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        if (!checkText.isEmpty()) {
          // 指定した文字のチェックボックスを入れる。
          for (auto *box : widget->findChildren<QCheckBox *>()) {
            if (sameName(box->text(), checkText)) {
              box->setChecked(true);
            }
          }
          QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        }
        if (hoverPosition.x() >= 0) {
          // 解析ウィンドウの座標で指したところへ、ホイールとカーソルの動きを送る。
          QWidget *target = widget->childAt(hoverPosition);
          if (target != nullptr) {
            const QPoint local = target->mapFrom(widget, hoverPosition);
            for (int step = 0; step < std::abs(wheelSteps); ++step) {
              QWheelEvent wheel(local, target->mapToGlobal(local), QPoint(),
                                QPoint(0, wheelSteps > 0 ? 120 : -120), Qt::NoButton,
                                Qt::NoModifier, Qt::NoScrollPhase, false);
              QCoreApplication::sendEvent(target, &wheel);
            }
            QMouseEvent move(QEvent::MouseMove, local, target->mapToGlobal(local), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &move);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
          }
        }
        return widget->grab().save(outPath) ? 0 : 1;
      }
    }
    return 4;
  }
  if (!menuTitle.isEmpty()) {
    // 指定したメニューを開いた姿を書き出す。
    for (auto *action : window.menuBar()->actions()) {
      if (sameName(action->text(), menuTitle) && action->menu() != nullptr) {
        auto *menu = action->menu();
        menu->ensurePolished();
        menu->adjustSize();
        return menu->grab().save(outPath) ? 0 : 1;
      }
    }
    return 3;
  }
  return window.grab().save(outPath) ? 0 : 1;
}
