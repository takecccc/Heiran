# Heiran

複数の画像を同期表示するイメージビューワー `Heiran` のプロジェクトです。

関連文書:

使い方は、ユーザーマニュアル ([manual/](./manual/index.md)) にある。アプリの Help > Manual (F1) からも開ける。
設計文書は [docs/](./docs/README.md) にある。

- [要件定義](./docs/要件定義.md): 何を実現するか
- [アーキテクチャ設計](./docs/アーキテクチャ設計.md): 構成と責務
- [設計判断](./docs/設計判断.md): なぜそうしたか

## 実装状況

普段使いのイメージビューワーとして使える状態にある。

表示:

- `Qt Widgets + CMake` のアプリ骨格
- 任意の行列でセルを並べ、パン / ズーム / 回転 / 反転 / ページ送りを同期する。
  セルごとに同期を切り、倍率、ガンマ、回転、反転、チャネルを変えられる
- `GlobalViewState` (同期共有) と `CellViewState` (セル固有差分) を
  `computeFinalViewState()` で合成する表示状態モデル
- 描画は `QOpenGLWidget` 系の API を使い、`RGBA8` / `RGBA16F` のテクスチャで持つ
- 露出とチャネル表示 (`color` / `gray` / `R` / `G` / `B` / `A`)
- 拡大時 (`Nearest` / `Smooth`) と縮小時 (`Nearest` / `Area`) の補間を選べる
- 色管理。埋め込みの ICC プロファイルから、画面のプロファイルへ変換して表示する (CMYK も)
- 画素値情報パネル、ステータスバーの値、ルーペ。値は表示用画像ではなく元の精度の解析用バッファから読む
- 単独表示 (Tab / Alt+1-9): 1 つのセルを画面いっぱいに出し、同じ場所を同じ倍率で切り替えて見比べる (A/B の比較)
- 操作の対象のセルを枠で示す。全画面では、画面の上端へ寄せるとメニューとツールバーが出る
- 画像を直接開いた場合は、そのフォルダを画像セットとして扱う。起動直後は、始め方の案内と最近開いた画像を出す
- 画像セットとセルの設定を、表で編集する (Images > Manage Image Sets...)。画像の割り当て、格子、並べ替えなどは、Ctrl+Z で戻せる

解析:

- ヒストグラム (2 次元と 3 次元)。画素数で正規化し、縦軸の倍率と対数軸、目盛り、
  チャネルごとの平均 / 標準偏差 / 最小 / 最大 / ピークを出す
- 波形モニタ (輝度 / RGB / Parade) とベクトルスコープ。位置ごとの値の分布と、色の分布を、セルごとに出す
- 変化グラフ (列ごと、行ごとの平均の変化) と、画像情報 (メタデータをセルの間で比べる)
- 差分。**ほかのセルと並べて、同じズームとパンで見られるセルとして出せる** (Analysis > Diff Cell)。
  計算は、絶対値 / 符号付き / 二値 / **色差 (CIEDE2000)** / **SSIM**、表示は、グレースケール / 符号色 / 重ね合わせ。
  大きさの違う画像は、重なる部分だけを比べる (拡大縮小して比べることもできる)。PSNR、RMSE などの指標つき
- ワイプ (Analysis > Wipe Cell)。2 枚を、ドラッグで動かせる境目で、つなぎ合わせて見る
- 選択範囲を出すと、その範囲だけを集計する

入出力:

- `.hrn` (JSON5 相当サブセット) の読み書き。コメント / 非引用キー / 単一引用符 /
  末尾カンマに対応する。比較の状態 (格子、画像セット、セルごとの設定、差分とワイプ) を残せる
- `png` は `libspng`、`webp` は `libwebp`、`avif` は `libavif` (AV1 の復号は dav1d)、
  `jpeg` / `bmp` / `gif` などは `QImageReader`、
  `jxl` (JPEG XL) は `libjxl`、`hdr` / `dpx` / `psd` / `tga` は自前の復号器、
  `exr` は `OpenEXR`、`tiff` は `libtiff` を使う。
  HEIC は対応しない (HEVC の特許の扱いが重いため。経緯は docs/設計判断.md)
- View > Image Info (I キー) で、ファイル、画素の形式、色、EXIF などのメタデータを、
  セルを列にして並べる。セルの間で値が違う行は強調し、違う行だけに絞れる
- アニメーション (GIF / WebP / APNG) は再生とコマ送り、マルチページ TIFF はページ送りができる。
  複数のセルで動かすときの進め方 (同期 / 独立 / 自動) は View > Animation Sync で選べる。
  ファイルの連番とは別の軸で、複数のセルを同じコマで並べて比べられる
- 16bit と 32bit float をネイティブ精度のまま解析へ渡す
- `EXIF` の orientation を復号時に適用し、画素値の位置は元ファイルの座標で出す
- 非同期デコードと LRU のデコードキャッシュ、前後ページの先読み
- アプリ全体設定 (画素値の表示形式、ページ切り替え時に Fit するか、配色、
  デコードキャッシュの上限、補間、色管理) は `QSettings` に持つ

配布:

- `NSIS` インストーラと ZIP。`./scripts/package.ps1` で作る (ユーザーマニュアルも同梱する)
- インストール時に `.hrn` / 一般的な画像形式 / 高精度形式の関連付けを個別に選べる

未着手の項目は [open_tasks.md](./open_tasks.md) にある。

## 動作環境

- Windows (64bit)。
- 画面の言語は、英語と日本語。OS の表示言語に従います (設定の Language で選べます。次の起動から効きます)。
- **OpenGL 3.0 以上に対応した GPU とドライバ**。描画に使います。ソフトウェアの OpenGL は、配布物に入れていません。
  使えない環境では、起動のときに、理由を知らせます。グラフィックスドライバを更新してください。
- Microsoft Visual C++ ランタイム (x64)。配布物には入れていません。入っていない、または古い環境では、
  インストーラが確かめて、[Microsoft の配布ページ](https://aka.ms/vs/17/release/vc_redist.x64.exe) を開くかを尋ねます。
  ZIP から使う場合は、先に、最新の `vc_redist.x64.exe` を入れてください。

## ビルド

前提:

- CMake 3.24 以上
- C++20 対応コンパイラ
- Qt 6.5 以上 (`Core`, `Gui`, `Widgets`, `Test`)
- 画像の復号ライブラリ (libavif、libjxl、OpenEXR、libtiff、libwebp、libspng) は vcpkg で入る。無いものは、その形式が開けないだけでビルドはできる

Windows では `PowerShell` スクリプトをビルド入口として使います。
ビルド構成は `Ninja + msvc` 固定です。
ローカル依存の値は `scripts/local.settings.ps1` に閉じ込め、共有スクリプト本体はそのまま push できます。

### 初回設定

```powershell
Copy-Item scripts/local.settings.sample.ps1 scripts/local.settings.ps1
```

`QtPrefixPath` などを自分の環境に合わせて編集します。

### サンプル画像

`examples/sample.hrn` 用の最小サンプル画像は `examples/images/` に含めています。
再生成する場合は `uv` を使って次を実行します。

```powershell
uv run python tools/generate_example_images.py
```

差分とワイプの見本は `examples/diff-delta-e.hrn`、`examples/diff-ssim.hrn`、`examples/wipe.hrn` です。
アニメーションの見本は `examples/animation/` にあります (`tools/generate_animation_samples.py` が作ります。
絵はスクリプトが描くため、権利の問題はありません)。

### ユーザーマニュアル

`Sphinx` で HTML を作ります (`uv` が要ります)。

```powershell
./scripts/build-manual.ps1 -Open
```

出力は `build/manual` です。配布物には `doc/manual/` として入り、アプリの Help > Manual (F1) から開けます。
マニュアルの画面の画像は、`./scripts/capture-manual-images.ps1` で撮り直せます。

### PowerShell スクリプト

設定:

```powershell
./scripts/configure.ps1
```

以前の `clang` キャッシュが残っている場合は、次でビルドディレクトリのメタデータを初期化できます。

```powershell
./scripts/configure.ps1 -Clean
```

ビルド:

```powershell
./scripts/build.ps1
```

テスト:

```powershell
./scripts/test.ps1
```

起動:

```powershell
./scripts/run.ps1
```

サンプル `.hrn` 付き起動:

```powershell
./scripts/run.ps1 -SessionPath examples/sample.hrn
```

### 直接 CMake を使う場合

設定:

```powershell
cmake -S . -B build
```

ビルド:

```powershell
cmake --build build
```

起動:

```powershell
./build/heiran.exe
```

`.hrn` を指定して起動する場合:

```powershell
./build/heiran.exe sample.hrn
```

サンプル設定:

```powershell
./build/heiran.exe examples/sample.hrn
```

## テスト

```powershell
ctest --test-dir build --output-on-failure
```

テストは、窓を出さない `offscreen` プラットフォームで動きます (実行中も、マウスやキーボードの操作を奪いません)。
実際の窓で動かしたいときは、`cmake -S . -B build -DHEIRAN_TEST_QPA_PLATFORM=` (空) で構成し直します。

## ライセンス

`Heiran` は [Apache License 2.0](./LICENSE) で公開します。
Copyright 2026 Atelier Takec。著作権の表示は [NOTICE](./NOTICE) にもあります。

依存ライブラリ (Qt、vcpkg のライブラリ、アイコンなど) のライセンスと著作権表示は
[THIRD_PARTY_NOTICES.txt](./THIRD_PARTY_NOTICES.txt) にまとめてあります。
配布物 (インストーラ、ZIP) にも同梱し、アプリの Help > About Heiran でも読めます。
Qt は LGPL-3.0 で、DLL として動的に使っています。
