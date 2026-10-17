# 一覧

## キー操作

```{list-table}
:header-rows: 1
:widths: 40 60

* - キー
  - 内容
* - <kbd>Ctrl</kbd> + <kbd>O</kbd> / <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>O</kbd>
  - 画像を開く / `.hrn` を開く
* - <kbd>→</kbd> <kbd>PageDown</kbd> <kbd>Space</kbd> / <kbd>←</kbd> <kbd>PageUp</kbd> <kbd>Backspace</kbd>
  - 次のページ / 前のページ
* - <kbd>Home</kbd> / <kbd>End</kbd>
  - 最初のページ / 最後のページ
* - <kbd>F</kbd> / <kbd>1</kbd> / <kbd>0</kbd>
  - フィット / 等倍 / 表示のリセット
* - <kbd>+</kbd> / <kbd>-</kbd>
  - 拡大 / 縮小
* - <kbd>Shift</kbd> + <kbd>F</kbd>
  - ページを替えたときのフィットの入り切り
* - <kbd>R</kbd> / <kbd>Shift</kbd> + <kbd>R</kbd>
  - 右へ回転 / 左へ回転
* - <kbd>[</kbd> / <kbd>]</kbd> / <kbd>\</kbd>
  - 露出を下げる / 上げる / 0 に戻す
* - <kbd>Tab</kbd> / <kbd>Shift</kbd> + <kbd>Tab</kbd>
  - 単独表示で、次のセル / 前のセル
* - <kbd>Alt</kbd> + <kbd>1</kbd> - <kbd>9</kbd> / <kbd>Alt</kbd> + <kbd>0</kbd>
  - そのセルだけを見る / 全セルの表示へ戻る
* - <kbd>P</kbd>
  - アニメーションの再生と一時停止
* - <kbd>Ctrl</kbd> + <kbd>←</kbd> / <kbd>→</kbd>
  - 前のコマ / 次のコマ
* - <kbd>T</kbd> / <kbd>I</kbd>
  - ページの一覧 / 画像情報
* - <kbd>Shift</kbd> + ドラッグ / <kbd>Esc</kbd>
  - 範囲を選ぶ / 範囲を解除する (範囲が無いときは、単独表示から戻る)
* - <kbd>F11</kbd> / <kbd>Alt</kbd> + <kbd>Enter</kbd>
  - 全画面
* - <kbd>Ctrl</kbd> + <kbd>Z</kbd> / <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>Z</kbd>
  - 割り当てや格子などの変更を、戻す / やり直す
* - <kbd>F2</kbd>
  - ファイルの名前を変える
* - <kbd>F1</kbd>
  - このマニュアルを開く
```

## 対応する画像形式

```{list-table}
:header-rows: 1
:widths: 25 75

* - 区分
  - 形式
* - 一般
  - PNG (16bit、APNG を含む)、JPEG、BMP、GIF、WebP (アニメーションも可)
* - 次世代
  - AVIF、JPEG XL
* - 高精度・制作
  - OpenEXR、TIFF (8bit、16bit、half、float、CMYK、複数ページ)、HDR、TGA、DPX、PSD
```

16bit の整数や、浮動小数の画像は、元の精度のまま扱います。HEIC は開けません。
