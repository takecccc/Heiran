# プロジェクト概要

複数の画像を同期表示するイメージビューワー `Heiran` のプロジェクト。

C++20 / Qt6 Widgets / CMake、Windows + Clang (clang-cl) + Ninja 前提。Qt は MSVC 版を使う。

要件定義は [docs/要件定義.md](./docs/要件定義.md) に記載してある。

## ドキュメント

仕様と設計の正は `docs/` 配下。実装前に該当文書を確認する。

文書の一覧と、読む順、役割は [docs/README.md](./docs/README.md) にある。

- 要件定義: [docs/要件定義.md](./docs/要件定義.md)
- アーキテクチャ設計: [docs/アーキテクチャ設計.md](./docs/アーキテクチャ設計.md)
- 仕様: [表示状態](./docs/表示状態仕様.md) / [座標と画素値](./docs/座標と画素値の仕様.md) / [設定ファイル (`.hrn`)](./docs/設定ファイル仕様.md) / [エラー処理](./docs/エラー処理仕様.md)
- 設計: [画像表示](./docs/画像表示設計.md) / [解析機能](./docs/解析機能設計.md)
- テスト観点: [docs/テスト観点.md](./docs/テスト観点.md)
- 設計判断 (なぜそうしたか): [docs/設計判断.md](./docs/設計判断.md)

設計文書には、未実装のもの (プラグイン関連の `PluginRegistry` など) も含まれる。
実装状況は `CMakeLists.txt` の `heiran_core` に登録されたファイルを正とする。

# タスク管理

- 未着手・未完了の作業は [open_tasks](./open_tasks.md) で管理する。
- タスクに着手したとき、新たに残作業が判明したときは `open_tasks.md` を更新する。
- タスクが完了したら `open_tasks.md` から該当項目を削除する。完了の履歴は git のコミットに残す
  (コミットメッセージに、何をしたかと理由を書く)。

# 判断の記録

判断に悩んだ内容と、その決定は、**該当する設計文書に、決定と理由を書く**。複数の文書にまたがるもの、
採らなかった案は [docs/設計判断.md](./docs/設計判断.md) に書く。

- 日記のように追記せず、**今の決定を書く**。方針を変えたら、その項目を書き換える (古い決定は消す)。
  経緯は git の履歴にある。
- 項目は、**決定 / 理由 / 見直す条件** の順に書く。採らなかった案は、理由と一緒に末尾の表へ 1 行で足す。
- 性能を根拠にした判断は、Release 構成で測った数字を添える。

# ビルド & テスト

`README.md` は利用者向け (機能、動作環境、入手方法)、[DEVELOPMENT.md](./DEVELOPMENT.md) は開発者向け (ビルドと配布物の作り方) に分けている。
手順を変えたら、DEVELOPMENT.md も合わせて直す。

Qt の場所は環境変数 `Qt6_ROOT` で設定することを推奨する。通常、`scripts/local.settings.ps1` の作成は不要。

```powershell
$env:Qt6_ROOT = "C:/Qt/6.11.1/msvc2022_64"
[Environment]::SetEnvironmentVariable("Qt6_ROOT", $env:Qt6_ROOT, "User")
```

パスは環境に合わせる。既存の IDE や別のターミナルは再起動する。
必要な場合だけ雛形から `local.settings.ps1` を作り、`QtPrefixPath` などを上書きする。既存の `QtPrefixPath` は `Qt6_ROOT` より優先する。
画像の復号ライブラリ (libavif、libjxl、OpenEXR、libtiff、libwebp など) は vcpkg のマニフェストモードで入る。`./scripts/setup-vcpkg.ps1` を実行する。
配置先はローカル設定の `VcpkgRoot`、環境変数 `VCPKG_ROOT`、`.tools/vcpkg` の順で決まり、標準の場所なら記入は不要。

```powershell
./scripts/configure.ps1      # -Clean で CMake キャッシュを初期化
./scripts/build.ps1
./scripts/test.ps1
./scripts/run.ps1 -SessionPath examples/sample.hrn
```

- スクリプトは内部で VsDevShell に入るため、通常の PowerShell から実行してよい。
- ジェネレータは `Ninja` 固定。共通の既定値は `scripts/common.ps1` にあり、ビルド構成は Release。
  コンパイラは Visual Studio 同梱の x64 `clang-cl` を優先し、なければ PATH から探す。別のコンパイラからの移行には `configure.ps1 -Clean` を使う。
  `local.settings.ps1` には環境差分だけを書く。構成は `BuildConfig` または `configure.ps1 -BuildConfig Debug` などで上書きできる。
  `Ninja` は単一構成ジェネレータのため、構成は `configure.ps1` が `CMAKE_BUILD_TYPE` として決める。
  `BuildConfig` を変更した場合は `configure.ps1` からやり直す必要がある。
- `scripts/local.settings.ps1` は環境依存のためコミットしない。
- **テストは、既定で、画面を持たない `offscreen` プラットフォームで動く** (CMake が、全テストへ `QT_QPA_PLATFORM=offscreen` を渡す)。
  実際の窓を出すと、テストの間、マウスとキーボードの操作を奪い、前面の窓のフォーカスを移してしまう。`offscreen` はフォントを持たないため、
  Windows のフォントの場所も、`QT_QPA_FONTDIR` で渡す。
  - `offscreen` で動かないテスト (画面の更新に合わせた描画、配色の切り替え) は、`QGuiApplication::platformName() == "offscreen"` で `QSKIP` する。
  - 実際の窓で確かめたいときは、`cmake -DHEIRAN_TEST_QPA_PLATFORM= ...` (空) で構成し直す (`configure.ps1` の後に、`cmake -S . -B build -DHEIRAN_TEST_QPA_PLATFORM=`)。
  - 1 つのテストを手で動かすときも、`$env:QT_QPA_PLATFORM = "offscreen"` を付けると、窓が出ない。

デバッグ用環境変数:

- `HEIRAN_IMAGE_DECODER` = `auto` | `qt` | `spng`。`spng` は PNG の比較計測用。
- `HEIRAN_PROFILE_PAN` / `HEIRAN_PROFILE_LOAD` / `HEIRAN_TRACE_DECODER` = `0` | `1`
- `HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB` = アニメーション (APNG / WebP) を逐次展開にする境目
  (展開後の MiB)。指定するとアプリ全体設定より優先される。
- `HEIRAN_GPU_TEXTURE_BUDGET_MB` = GPU テクスチャの上限 (MiB)。全セルの合計に掛かる。
  指定するとアプリ全体設定より優先される。`heiran_interaction_benchmark` でも効く。
- `HEIRAN_DECODE_CACHE_BUDGET_MB` = decode キャッシュのソフト上限 (MiB)。
  指定するとアプリ全体設定より優先される。設定を変えずに検証したい場合に使う。
- `HEIRAN_TRACE_MEMORY` = `0` | `1`。ステータス欄へ decode キャッシュと
  GPU テクスチャの使用量を出す。実セッションでの使用量を確かめたいときに使う。
- `QT_ASSUME_STDERR_HAS_CONSOLE` = `1`。`qDebug()` の出力をファイルへ取る場合に要る。
  詳細は「GUI の挙動を確認する場合」を参照。

## 高精度画像の確認

`examples/images` は 8bit PNG のみで、ネイティブ精度の経路を通らない。
16bit PNG と 32bit float の OpenEXR は次で生成する。生成物はコミットしない。

```powershell
py tools/generate_precision_fixtures.py
./build/heiran_decode_benchmark.exe examples/precision/hdr.exr
```

`heiran_decode_benchmark` は中心画素の値と全画素のチェックサムを表示する。
backend を切り替えて比較する場合は、速度だけでなくチェックサムの一致も必ず確認する。

デコーダを変更する場合は、形の異なる PNG で全画素が一致することを確認する。

```powershell
py tools/generate_png_variants.py
```

## デコード性能の計測

`heiran_decode_benchmark` でデコード時間を計測する。`ctest` には登録していない。

```powershell
$env:HEIRAN_IMAGE_DECODER = "qt"   # auto | qt | spng
./build/heiran_decode_benchmark.exe <画像ファイル> [反復回数]
```

初回はファイルキャッシュの影響が大きいため計測から除外し、min / median / mean / max を出す。
性能を根拠に判断する場合は、`Release` 構成で計測すること。
`Debug` 構成の数値を根拠にすると結論を誤る。

## 操作性能の計測

`heiran_interaction_benchmark` でパン、ズーム、hover を計測する。
マウスとホイールのイベントを合成するため、手作業の操作は要らない。
`ctest` には登録していない。

```powershell
./build/heiran_interaction_benchmark.exe <画像ファイル> [反復回数] [セル数]
```

画像ファイルと同じフォルダを画像セットとして扱う。ドロップしたときと同じ扱いになる。

- 測るのは UI スレッドが使う時間。GL の描画命令は非同期に積まれるため、
  GPU 側の完了時間は含まない。
- 描画を含めるには `processEvents()` で paint を捌く必要がある。
  `QOpenGLWidget` の `repaint()` では `paintGL` が走らず、イベント処理だけを測ることになる。
  ツール側で、パンの前後でフレームバッファが変化することを確認してから計測に入る。
- 内訳が要る場合は `./scripts/run.ps1 -ProfileLog pan.log` で起動する。必要な環境変数を
  立てて、計測結果を指定の経路へ書き出す。
  **`heiran.exe` は GUI サブシステムのためコンソールが繋がっておらず、
  `heiran.exe 2> pan.log` では何も取れない。** 明示的に渡す必要がある。
- **`pan` や `paint` は UI スレッドが使う時間しか見ていない。** なめらかさを見るには
  `present` を読む。実際に画面へ出た間隔で、GPU の完了と vsync の待ちが入る。
  同じ `HEIRAN_PROFILE_PAN=1` で出る。読み方:
  - **`max_` と `min_` を見る。** たまに起きる遅れは平均に埋もれる。最小は、
    平均が本当の周期なのか、周期の倍数を拾っているだけなのかを見分けるのに要る。
    最小が周期より短ければ、その計測は周期を測れていない。
  - **引っかかりの判定には `latency` を使う。`present` では判定できない。**
    `present` はマウスが動いていない間も間隔として数えるため、手が止まった 30 ms と
    本当に詰まった 30 ms を区別できない。`latency` は描き直しの要求が出ている間
    しか測らないため、詰まりだけが出る。`max_to_screen` が画面の更新間隔の数倍
    あれば本物の詰まり。
  - `present` が画面の更新間隔 (60Hz なら 16.7 ms) 前後で `paint` が小さい:
    vsync で頭打ち。正常。
  - `present` がそれより大きく `paint` が小さい: 描画ではなく、届く入力の間隔か
    画面への出し方が理由。
  - `paint` が大きい: 描画そのものが重い。
- `frameSwapped` は画面の更新に同期しない。`heiran_swap_clock_probe` で確かめられる。
  `QOpenGLWidget` は FBO へ描いて Qt が合成するため `SwapBuffers` を呼ばず、
  `QOpenGLWindow` でも `swapInterval=1` は効かなかった。**揃えるには
  `DisplayLink` (DXGI の `WaitForVBlank`) が要る。**
- 揃えることと応答の速さは取引になる。`./scripts/run.ps1 -NoDisplaySync`
  (環境変数なら `HEIRAN_PAN_SYNC=0`) で合図を待たずに描く形へ切り替えられる。
  `latency` は 1 回分から 0.5 ms へ下がるが、位置の進み方は揃わなくなる。
- 起動時に `surface swapInterval=... refresh=... vblank=... buffers=... sync=...` も出る。
  描き直しは `DisplayLink` の合図 (`DwmFlush()` 待ち) に合わせるため、
  `present avg_interval` は `vblank` とほぼ同じになるのが正常。
  出力を見るには `QT_ASSUME_STDERR_HAS_CONSOLE=1` も要る (上記「GUI の挙動を確認する場合」)。

## スクリプトを編集する場合

`scripts/*.ps1` には非 ASCII 文字を書かない。コメントも英語で書く。

Windows PowerShell 5.1 は BOM 無しの `.ps1` を UTF-8 ではなく ANSI (CP932) として読むため、
日本語コメントを入れると解析が壊れ、変数が空になるなど分かりにくい不具合になる。

## メモリ検査 (AddressSanitizer)

解放後アクセスやバッファ超過を実行時に検出したい場合は ASan ビルドを使う。

```powershell
./scripts/configure.ps1 -BuildDir build-asan -Asan
./scripts/build.ps1 -BuildDir build-asan
./scripts/test.ps1 -BuildDir build-asan
```

注意事項:

- ASan は未初期化領域の読み出しは検出しない。MSVC 向けの `/RTC1` を clang-cl での検査手段として使わない。
- 実行には `clang_rt.asan_dynamic-x86_64.dll` が必要になる。
  各スクリプトは clang-cl のランタイムディレクトリを実行中の PATH に追加する。
- `ASAN_OPTIONS` の区切り文字は `:` のため、`log_path` に `C:/...` のような
  ドライブレター付きパスを渡すと解析に失敗する。作業ディレクトリからの相対パスを使う。
- ASan ビルドは起動が遅い。ウィンドウが出る前に閉じようとすると閉じられないので、
  自動操作する場合は `MainWindowHandle` が有効になるのを待つ。

## ビルドが途中で失敗した場合

ヘッダの構造体にメンバを追加または削除した変更でビルドが途中で失敗した場合は、
`./scripts/configure.ps1 -Clean` からやり直す。

途中で失敗したビルドは、新しいヘッダで作られたオブジェクトと古いオブジェクトが混ざる。
そのまま増分ビルドを続けると構造体の配置が食い違い、実行時にアクセス違反として現れる。
`MainWindowTests` がこの状態で落ちたことがあり、クリーンビルドで解消した。

## 落ちた場合

起動時にミニダンプの書き出しを仕掛けている。落ちると次の場所へ `.dmp` が残る。

```
%LOCALAPPDATA%/Heiran/crashes/heiran-<日付>-<時刻>-<プロセスID>.dmp
```

調べ方:

- Visual Studio で `.dmp` を開き、「ネイティブのみでデバッグ」を選ぶ。落ちた時点の
  呼び出し履歴が出る。
- そのビルドの `.pdb` が要る。`build/` を消した後では履歴が読めないため、
  調査する見込みがあるなら `.pdb` を残しておく。
- 全メモリは落としていない。呼び出し履歴、スレッド情報、履歴から辿れる範囲の値、
  ハンドルまで。全部落とすと数 GiB になる。

デバッガを繋いだ状態では OS 側が先に受け取るため、ダンプは出ない。

## GUI の挙動を確認する場合

`qDebug()` の出力をファイルへリダイレクトすると、**何も書かれない**。
Qt の既定のメッセージハンドラは stderr がコンソールでない場合に
`OutputDebugString` へ切り替えるため、リダイレクト先には届かない。
ファイルへ取りたい場合は `QT_ASSUME_STDERR_HAS_CONSOLE=1` を設定する。

```powershell
$env:QT_ASSUME_STDERR_HAS_CONSOLE = "1"
```

出力が無いことを「その処理が動いていない」根拠にしないこと。
これで誤った判断をしかけたことがある。まず上記を設定して出力が出るかを確かめる。

プロセスを強制終了すると未フラッシュの出力が失われる点にも注意する。
原因調査では正常終了させるか、出力を消さない方法を使う。

## テストの追加

テストは `QtTest` ベースで、`private Q_SLOTS:` にテストケースを書く。

GUI を伴うテストは実際にウィンドウを生成する。`MainWindowTests` が該当する。
不具合を直したら、その不具合を再現するテストを先に書いて落ちることを確認する。

`moc` は raw string の中でも、**ダブルクォートで囲んだ中の `/*` をコメントの開始として
扱う**。以降をファイル末尾まで読み飛ばすため、`Q_OBJECT` を見つけられず何も生成しない。

```cpp
// これは壊れる。moc が /* から後ろを読み飛ばす。
const char *source = R"({ path: "a/*.png" })";

// これは通る。シングルクォートなら影響しない。
const char *source = R"({ path: 'a/*.png' })";
```

症状は `metaObject` / `qt_metacast` / `qt_metacall` が未解決というリンクエラーで、
テストの中身とは無関係に見える。`.hrn` のグロブをテストへ書くときはシングル
クォートを使う。既存のテストがすべてシングルクォートなのはこのため。

`CMakeLists.txt` はソースを glob していないため、テスト追加時は
`add_executable` / `target_link_libraries` (`heiran_core`, `Qt6::Test`) / `add_test` を手書きで追記する。

# アーキテクチャ

`heiran::ui` -> `heiran::app` -> `heiran::domain` / `heiran::infra` の依存方向を保つ。

各層のクラスは [docs/アーキテクチャ設計.md](./docs/アーキテクチャ設計.md) にある。

守るべき責務境界:

- UI 層は `.hrn` の生テキストを直接解釈しない。
- 永続モデル `SessionDocument` と runtime モデル `SessionModel` を混ぜない。
  `sessionFilePath` / `sessionDirectory` / `resolvedPaths` は runtime 側だけが持つ。
- glob 展開と `resolvedPaths` の再計算は `SessionPathResolver` に集約する。
- 表示状態は `GlobalViewState` (同期共有) と `CellViewState` (セル固有差分) に分け、
  最終値は `computeFinalViewState()` の合成で求める。
- 非同期 decode は `DecodeScheduler`、decode 結果の LRU 管理は `DecodeCache` が持つ。
  `ViewerGridWidget` は表示合成とイベント集約に留める。

進行中のリファクタ方針:

- `MainWindow` / `ViewerGridWidget` / `ImageViewWidget` は肥大化しているため、
  新規責務は既存クラスに足さず controller として切り出す。
- ウィジェット間の外向き通知は `std::function` setter ではなく signal/slot を用いる。

## 画像デコード

形式ごとの読み手と、選んだ理由は [docs/設計判断.md](./docs/設計判断.md#画像の読み込み)、構成は
[docs/アーキテクチャ設計.md](./docs/アーキテクチャ設計.md#インフラ層) にある。ここには、**実装するときの注意**だけを書く。

- 入口は `ImageDecoder::decode`。形式ごとの読み手は、`RawImage` (すき間のない画素の並び) かフレームの並びを返し、`ImageDecoder` が解析用バッファと
  表示用の画像へ組み立てる。**自前の復号器は、失敗しても別の読み手へ退避しない** (読めない理由をそのまま出す)。退避があるのは、PNG (libspng
  が失敗したら Qt) と、TIFF の一部 (libtiff が扱わない形式を Qt へ) だけ。
- 読み手は、ライブラリが見つかったときだけ有効になる (`HEIRAN_HAVE_LIBSPNG` / `LIBWEBP` / `LIBAVIF` / `LIBJXL` / `OPENEXR` / `LIBTIFF` /
  `ZSTD` / `LCMS2`)。無ければ、その形式は開けない (PNG と TIFF は Qt が退避先)。依存は vcpkg の manifest (`vcpkg.json`) で入る。
- 手を入れるとき:
  - ファイルは、全部読んでからライブラリへ渡す (日本語のパスを扱うため。`QFile` で読む)。
  - **EXIF の向きは、復号器の側で適用した場合は、適用したと記録しない**。適用したページの値だけを記録する (画素値の位置を、元のファイルの座標へ戻す計算が誤る)。
    WebP、AVIF、JPEG XL は、向きを適用しない / 復号器の側で済ませている。
  - 埋め込みの色プロファイル、EXIF、メタデータは、`decode()` の 1 か所で読む (`EmbeddedColorProfile`、`ExifReader`、`ImageMetadata`)。
  - 解析用バッファは、元の精度のまま持つ。8bit の PNG / JPEG は、表示用の画像がそのまま元データの場合がある。
  - CMYK (PSD、TIFF) は、`convertCmykToSrgb` で sRGB へ変換して返す。CMYK 用のプロファイルを RGB の画素へ付けない。
- **アニメーションとページ**: ページ (TIFF、EXR のパート) は指定した 1 つだけを読み、フレーム (GIF、WebP、APNG) は `FrameSequence` か、後から作る
  `FrameStream` で持つ。サムネイルなど動きが要らない用途は、`firstFrameOnly` で先頭だけを読む。どちらも、ファイルの連番 (`currentPage`) とは別の軸
  (`subImageIndex`) で、全セルが共有する。
  - 展開後の大きさ (幅 x 高さ x 4 x フレーム数) が境目 (既定 128 MiB) を超える APNG / WebP は、圧縮したまま持ち、要るフレームだけを作る
    (`openApngStream` / `openWebpSource`。大きさは `probeApng` が画素を読まずに見積もる)。GIF は、再生しながら zstd で貯める (`GifStream`、`probeGif`)。
  - 境目は、設定 (Settings > Cache) と環境変数 `HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB` で変えられる。テストは `setAnimationStreamingThresholdBytes` で、
    小さなファイルでこの経路を通す。
  - **WebP に `WebPAnimDecoder` は使わない** (先頭から順にしか進められず、コマ戻しや飛び越しのために合成し直せない)。合成は APNG と共通 (`AnimationComposer`)。
  - テストの見本は、`tests/MultiFrameFixtures.hpp` がバイト列から組み立てる (外部のツールに頼らない)。
- 見本のファイルは、`py tools/generate_format_samples.py` が `examples/formats` へ作る。
- アニメーションの見本は、`tools/generate_animation_samples.py` が `examples/animation` へ作る (Pillow が要る。自作の絵だけで、権利の問題はない)。GIF のストリーム復号のテストが使う。外部で拾ったファイルは、リポジトリへ入れない。
- **復号の確認は、別の書き手が作ったファイルで行う** ([見た目 (デザイン)](#見た目-デザイン) の末尾)。

# コーディング方針

コードを作成する際は、長期でメンテナンスすることを考えて、良い構造かどうか判断した上で作成する。
フォーマットの崩れを防止するため、clang-formatの定義を作成して用いる。

- 変更後は `.clang-format` (LLVM ベース / indent 2 / 100 桁) に従って整形する。
  `clang-format` は PATH に無いため、Visual Studio 同梱のものを使う。
  `"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"`
  違反確認は `--dry-run -Werror`、適用は `-i` を使う。
- `QT_NO_KEYWORDS` が有効。`signals` / `slots` / `emit` は使えないため、
  `Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT` を使う。
- ヘッダは `include/heiran/<layer>/` に置き、`#pragma once` と
  `#include "heiran/..."` 形式で参照する。
- メンバ変数は末尾アンダースコア (`session_`)、集約初期化には指定初期化子を使う。
- 既存コードはコメントを持たず、命名と関数分割で説明するスタイルで統一されている。

## ファイル追加時

`CMakeLists.txt` はソースを glob していない。新規ファイルは手動登録が必要。

- 実装は `add_library(heiran_core ...)` に追加する。
- ヘッダは `target_sources(heiran_core PRIVATE ...)` に追加する。

`CMAKE_SUPPRESS_REGENERATION OFF` により、`CMakeLists.txt` や CMake の入力を編集すると、
ビルド時に CMake が自動で再実行される。新規ファイルの登録も、通常のビルドで反映される。
自動再構成が無効だった古いビルドディレクトリでは、最初に `./scripts/configure.ps1` を実行する。
コンパイラ、Qt、ビルド構成を変更するときも、引き続き明示的に configure を実行する。

## 画面の文言 (日本語対応)

画面の文言は、**コードには英語で書き、日本語は翻訳ファイルに持つ**。英語と日本語に対応する (OS の表示言語に従う。設定の Language で選べる。切り替えは、次の起動から効く)。

- 利用者へ見せる文字列は、`tr("...")` (`Q_OBJECT` のクラス) か `QCoreApplication::translate("クラス名", "...")` (`Q_OBJECT` でないクラスと、関数) で包む。
  メニュー、ボタン、ツールチップ、ダイアログ、ステータスバー、エラーの文を含む。日本語の文言をコードへ直接書かない。
  - 包まないもの: 設定のキー、`setObjectName`、スタイルシート、ショートカット、ログ (`qDebug` など)、画素値の表示の記号、デコーダー (`src/infra`) の診断メッセージ (英語のまま。ライブラリの文言とファイルの経路を含むため)。
  - 実行時に決まる文を組み立てるときは、文を分けずに、`%1` で値を差し込む (`tr("Page %1 / %2").arg(...)`)。文を継ぎ足すと、語順の違う言語で訳せない。
  - 名前で項目を探す処理 (`ActionIcons` のアイコン) は、翻訳された文言でも引けるようにする。文言の比較で処理を分けない。
- 翻訳は `resources/i18n/heiran_ja.ts`。新しい文言を足したら、`cmake --build build --target update_translations` で `.ts` を更新し、`<translation>` を埋める
  (`type="unfinished"` のまま残さない。試験 `heiran_localization_tests` が、訳し忘れと、`%1` の番号の食い違いを検出する)。`.qm` は、ビルドで作って実行ファイルへ埋め込む。
- **試験は、翻訳を読み込まない (英語)**。試験は、メニューの文言などを英語で探すため。翻訳を読み込む処理は、`src/main.cpp` だけ (`HEIRAN_LANGUAGE=ja` で、設定にかかわらず日本語にできる。`heiran_screenshot --lang ja` でも確かめられる)。
- Qt 標準の文言 (ファイルダイアログ、OK / キャンセルなど) は、配布物の `translations/qt_ja.qm` を使う (`windeployqt --translations ja`)。
- マニュアル (`manual/`) は日本語だけ。メニューやボタンの名前は、**日本語の表示の名前**で書き、各ページの初めに出るところへ、英語の名前を括弧で添える (`**表示 > 前のセル** (View > Next Cell)`)。画面の画像は、`--lang ja` で撮る (`capture-manual-images.ps1`)。文言を変えたら、マニュアルの名前と、画像を直す。

## 見た目 (デザイン)

- **方向性は「画像を邪魔しない控えめな見た目」。** 彩度を抑え、強調色は 1 色だけ。画像を見比べる
  面 (セルの背景、セルの見出し) は配色に関わらず中立な暗さに揃え、ウィンドウ側は明暗の配色に従う。
- **配色そのものは Qt に任せ、余白、角丸、ホバー、強調を `AppStyle` のスタイルシートで揃える**
  (`include/heiran/ui/AppStyle.hpp`)。パレットを自前で組まない (標準のダイアログだけ浮くため)。
  色は `AppStyleTokens` に集め、明るい配色と暗い配色で同じ形のまま色だけを変える。
  見た目を変えるときは、個々のウィジェットへ色を書かず、ここへトークンとして足す。
- **見た目を確かめるときは `heiran_screenshot`** (`tools/ScreenshotTool.cpp`) で画面を PNG に出して
  目で見る。`heiran_screenshot out.png --theme dark|light [--menu View] <画像...>`。
  - `--hrn file.hrn` で `.hrn` を読む (差分の設定など、状態を .hrn で指定できる。`examples/diff-delta-e.hrn` が例。
    `--session` は QApplication が取ってしまうため使えない)。`--dialog "Settings..."` で、モーダルなダイアログを書き出す。
    `--action 名前` (メニュー項目の実行)、`--cursor セル,x,y`、`--hover-cell N`、`--analysis 名前` もある (一覧は `tools/ScreenshotTool.cpp` の冒頭)。
  - セルの画像も grab に写る (手元の環境で確認済み)。写らない環境では、枠まわりの確認用。
  - PowerShell から呼ぶときは `Invoke-HeiranProcess` へ **`-StandardErrorPath` に空でない値**を渡す。空だと、GUI のプロセスを待たず、
    終了コードも取れない (`scripts/capture-manual-images.ps1` は、そうしている)。
- **復号が正しいかは、別の書き手が作ったファイルで確かめる**。自作の書き手のテストだけでは、読み手と
  書き手が同じ思い込みを持っていても気付けない。`py tools/generate_cross_writer_samples.py` が
  ImageMagick と ffmpeg で各形式のファイルと期待値を作り、`heiran_cross_writer_check <manifest.tsv>` が
  復号して比べる (一時フォルダへ作り、リポジトリには入れない)。形式を足す、復号器を直すときに流す。
  書き手の側の癖で合わないものは、スクリプトの先頭に理由を書いて外してある。

- **項目のアイコンは `ActionIcons` (`include/heiran/ui/ActionIcons.hpp`)。** Lucide (ISC、
  `resources/icons/`) の線画 SVG を、描くたびにパレットの文字色で塗る (無効は薄く、チェック中は
  強調色)。配色の切り替えに追従するため、配色ごとの画像は要らない。
  **項目の文字とアイコンの対応表 (`actionIconNames`) に足すだけで、ツールバーとメニューと
  セルの右クリックへ付く。** 役割が文字から明らかな項目だけに付け、ツールバーはアイコンのみ
  (文字はツールチップ)。アイコンを足すときは `resources/icons/` へ SVG を置き、`CMakeLists.txt` の
  `qt_add_resources(heiran_core heiran_icons ...)` へ登録する。

## アプリアイコン

`resources/heiran.ico` と `resources/heiran-256.png` は生成物だが、ビルド入力なのでコミットする。
作り直す場合は次を実行する。

```powershell
py tools/generate_app_icon.py --preview
```

- `--preview` を付けると `resources/icon-preview.png` に 3 案の比較シートを出す。
  このシートは生成物のためコミットしない。
- `--layout` で採用案を切り替える (`grid` / `split` / `stack`)。既定は `grid`。
- 生成後は `./scripts/build.ps1` でアイコンが差し替わったことを確認する。
  確認は `[System.Drawing.Icon]::ExtractAssociatedIcon()` で exe から取り出して目視する。

`.ico` は `resources/heiran.rc` 経由で exe へ埋め込む。エクスプローラとタスクバーが使う。
ウィンドウのアイコンは `resources/heiran.qrc` の PNG から
`QApplication::setWindowIcon()` で設定する。`.rc` は Windows 以外では読まれないため両方要る。

## 配布物の作成

```powershell
./scripts/configure.ps1 -BuildConfig Release
./scripts/package.ps1
```

`build/Heiran-<version>-win64.exe` (Inno Setup) と `build/Heiran-<version>-win64.zip` ができる。
`ISCC.exe` を利用できる状態にする。インストーラーの試作は `-Fast -Generator INNOSETUP`、ZIP だけなら `-Generator ZIP` を指定する。
詳しい手順は [DEVELOPMENT.md](./DEVELOPMENT.md#配布物の作成) を参照する。

- Direct3D のシェーダコンパイラとソフトウェア OpenGL は含めない。OpenGL 3.0 以上を起動前に確認する。
- VC++ ランタイムは同梱せず、必要な版をインストーラーで確認する。必要な版は CRT ヘッダーで決まる。
- インストール先や ZIP は `bin/heiran.exe` と DLL、`plugins/`、`translations/` を含む。
  `heiran.exe` は `qt.conf` が効く `bin/` から動かす。ZIP の確認はビルドディレクトリの外で行う。

### ユーザーマニュアル

- 一般の利用者向けのマニュアルは、`manual/` に Markdown (MyST) で書き、Sphinx (テーマは furo) で HTML にする。日本語だけ。
  設計文書 (`docs/`) とは別で、**使い方**を書く (仕様の詳細は、設計文書へ任せ、リンクしない)。
- 作り方: `./scripts/build-manual.ps1 [-Open]` (`uv` が要る。依存は `pyproject.toml` の `docs` グループ)。出力は `<BuildDir>/manual`。
  警告 (リンク切れなど) はエラーになる。
- 配布物には `doc/manual/` として入る。`package.ps1` が、先にマニュアルを作る。Help > Manual (F1) が、`ManualLocator` の探す場所
  (インストールした形、ZIP、ビルドしたフォルダ) の `index.html` を、既定のブラウザで開く。
- 書き方: メニューや項目の名前は、画面に出る英語の表記のまま書く。キーは `<kbd>` で囲む。メニューの道筋は **View > Pixel Info** のように太字で書く。
  `heiran_manual_content_tests` が、太字の道筋が実在するメニューか、太字でない道筋が残っていないかを確かめる (名前を変えると落ちる)。
- 画面の画像は `./scripts/capture-manual-images.ps1 [-Only <名前>]` で撮り直す (`heiran_screenshot` を使い、`examples/images` の見本を、`manual/_static/images/` へ出す)。画面を変えたら撮り直して、画像も一緒にコミットする。
- マニュアルの HTML に入るテーマとスクリプトのライセンスは、`THIRD_PARTY_NOTICES.txt` に入っている (furo、Sphinx、Pygments)。

### ライセンスの表記

- Heiran は Apache-2.0 (`LICENSE`、著作権者は Atelier Takec)。`NOTICE` に著作権の表示を置く。
- **依存ライブラリのライセンスは `THIRD_PARTY_NOTICES.txt` にまとめる。** 手で書き換えず、
  `py tools/generate_third_party_notices.py` で作る (vcpkg の `copyright` と、Qt、Lucide の表記)。
  **依存を足す、外す、版を上げたときは、作り直して一緒にコミットする。**
  知らないライブラリが入っていると、スクリプトが失敗する。`tools/generate_third_party_notices.py` の
  `PORTS` へ、名前、SPDX の識別子、公式の場所を足す。
- ctest の `heiran_third_party_notices_up_to_date` が、ファイルが最新かを確かめる (`--check`)。
- `LICENSE`、`NOTICE`、`THIRD_PARTY_NOTICES.txt` は、実行ファイルへ埋め込み (About 画面)、
  インストーラと ZIP にも入れる。
- Qt は LGPL-3.0。**動的リンクのまま (静的リンクにしない)**、Qt の DLL を入れ替えられる形で配布する。
  Qt を改変して配布する場合は、改変した部分のソースの提供が要る。
- 新しいアイコン、画像、フォントなど、外から持ち込むものは、ライセンスを確かめて表記を足す。

### ファイル関連付け

インストーラーは `.hrn`、一般画像、高精度画像の順に表示する。グループ単位と拡張子単位の選択を提供し、新規では既定で有効にする。
`cmake/WindowsInstaller.cmake` の拡張子一覧から Inno Setup の親子タスクと登録・解除処理を生成する。
選択画面ではインストール容量を表示しない。レジストリには選択した範囲で書き込み、他のアプリの値を残す。
自動テストは生成設定を検査し、実際のインストール範囲とアンインストールは Windows の検証環境で確かめる。

# 構成管理

コミットメッセージを作成する場合は、会話内容や作業記憶だけを根拠にせず、必ず staged 済みの差分を確認してから作成すること。

コミットメッセージ作成時は、少なくとも次の手順を実施すること。

1. `git status --short` を確認する。
2. `git diff --cached --stat` を確認する。
3. `git diff --cached` を確認する。
4. staged 差分だけを要約したコミットメッセージを作成する。

注意事項:

- unstaged の変更内容をコミットメッセージに含めないこと。
- リポジトリの安全性判定により通常の git コマンドが失敗する場合は、`git -c safe.directory=<repo_path> ...` を用いて差分確認してよい。
- コミットメッセージは日本語で作成すること。
- 概要は1行で記載し、1行空けてから詳細を記載すること。
- 概要には `feat:` / `fix:` / `refactor:` / `perf:` / `docs:` の prefix を付けること。
- `build/`, `vcpkg_installed/`, `.tools/`, `scripts/local.settings.ps1` はコミットしないこと。
- ローカル環境の絶対パスを含む `.hrn` は `*.local.hrn` という名前で保存すること。
  `.gitignore` で除外しているため、リポジトリへ登録されない。
  リポジトリに置くサンプルは、相対パスだけを使うこと。
