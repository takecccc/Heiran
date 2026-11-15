# 開発者向けの手引き

`Heiran` をビルドして、テストして、配布物を作るための手順です。使い方は [README](./README.md) と
[ユーザーマニュアル](./manual/index.md) にあります。

## 文書

- 設計文書は [docs/](./docs/README.md) にあります。
  - [要件定義](./docs/要件定義.md): 何を実現するか
  - [アーキテクチャ設計](./docs/アーキテクチャ設計.md): 構成と責務
  - [設計判断](./docs/設計判断.md): なぜそうしたか
- 作業のルール (責務境界、コーディング方針、コミットメッセージなど) は [AGENTS.md](./AGENTS.md) にあります。
- 未着手の項目は [open_tasks.md](./open_tasks.md) にあります。

## 前提

- Windows (64bit)、Visual Studio (C++ ツール、Windows SDK、C++ Clang tools for Windows)、Ninja
- CMake 3.24 以上
- C++20 対応コンパイラ
- Qt 6.5 以上 (`Core`, `Gui`, `Widgets`, `Test`)
- 画像の復号ライブラリ (libavif、libjxl、OpenEXR、libtiff、libwebp、libspng) は vcpkg で入る。
  無いものは、その形式が開けないだけでビルドはできる
- 補助スクリプトとマニュアルの作成には、Python と `uv` を使う

Windows では `PowerShell` スクリプトをビルド入口として使います。
ビルド構成は `Ninja + Clang (clang-cl)` 固定です。
`clang-cl` は Visual Studio 同梱の x64 版を優先し、なければ PATH から探します。
MSVC 互換の ABI とランタイムを使うため、Qt は引き続き MSVC 版を使います。
Visual Studio の開発シェルで、Windows SDK、標準ライブラリ、リンカを利用します。
vcpkg の依存は従来の `x64-windows` (MSVC) を利用します。
既定値は `scripts/common.ps1` で共有し、ビルド構成は Release です。
Qt の場所は環境変数 `Qt6_ROOT` で設定することを推奨します。
環境ごとの上書きが必要な場合だけ `scripts/local.settings.ps1` を使います (コミットしません)。

## 初回設定

```powershell
$env:Qt6_ROOT = "C:/Qt/6.11.1/msvc2022_64"
[Environment]::SetEnvironmentVariable("Qt6_ROOT", $env:Qt6_ROOT, "User")
```

パスは自分の環境の MSVC 版 Qt のインストール先に合わせます (`bin/` や `lib/cmake/Qt6/` を含むルートディレクトリ)。
上の手順は現在の PowerShell と、今後起動するプロセスの両方に設定します。
すでに起動している IDE や別のターミナルで使う場合は、それらを再起動します。
ビルド、テスト、実行スクリプトは、この Qt の `bin/` を実行中だけ PATH に追加します。

通常、`local.settings.ps1` の作成は不要です。Qt をプロジェクト単位で上書きする場合などは、次で作成します。

```powershell
Copy-Item scripts/local.settings.sample.ps1 scripts/local.settings.ps1
```

Qt の場所の優先順位は `configure.ps1 -QtPrefixPath`、ローカル設定の `QtPrefixPath`、環境変数 `Qt6_ROOT` の順です。
コマンド引数は configure の実行だけに効くため、後続のビルド・テスト・実行で使う Qt も環境変数かローカル設定で揃えます。
既存のローカル設定に `QtPrefixPath` がある場合、環境変数を使うにはその行を削除します。
Qt を切り替える場合は、`configure.ps1 -Clean` で古い CMake の探索結果を初期化します。

vcpkg は `./scripts/setup-vcpkg.ps1` で用意します。
配置先はローカル設定の `VcpkgRoot`、環境変数 `VCPKG_ROOT`、`.tools/vcpkg` の順で決まります。
標準の場所を使う場合、`VcpkgRoot` の記入は不要です。

依存はリポジトリ直下の `vcpkg.json` によるマニフェストモードで導入します。
`configure.ps1` も不足する依存を自動で導入します。
セットアップと CMake は、リポジトリ直下の `vcpkg_installed/` を共有します。
`CMakeOptions` から toolchain、マニフェストモード、依存のインストール先を変更することはできません。

## ビルドと実行

設定:

```powershell
./scripts/configure.ps1
```

MSVC や `clang++` で構成したビルドディレクトリを移行するときは、`./scripts/configure.ps1 -Clean` で再構成します。
既存のコンパイラと異なる場合、通常の configure はエラーにして切り替え手順を示します。

`CMakeLists.txt` などの CMake 入力を変更した後は、ビルド時に自動で再構成します。
以前の自動再構成が無効なビルドディレクトリでは、一度 `./scripts/configure.ps1` を実行してください。
追加したクラスが未解決シンボルとしてリンクに失敗する場合も、まず再構成してからビルドします。

Debug が必要な場合は、ローカル設定を書き換えず、別ディレクトリへ構成できます。

```powershell
./scripts/configure.ps1 -BuildDir build-debug -BuildConfig Debug
./scripts/build.ps1 -BuildDir build-debug
./scripts/test.ps1 -BuildDir build-debug
```

既存の `local.settings.ps1` の `BuildConfig` は引き続き優先されます。
共通の Release 既定値を使うには、その行を削除し、`configure.ps1` を再実行します。
ビルドとテストは構成済みの `CMAKE_BUILD_TYPE` を使います。

以前のキャッシュが残っている場合は、次でビルドディレクトリのメタデータを初期化できます。

```powershell
./scripts/configure.ps1 -Clean
```

ビルド、テスト、起動:

```powershell
./scripts/build.ps1
./scripts/test.ps1
./scripts/run.ps1
./scripts/run.ps1 -SessionPath examples/sample.hrn   # サンプルの .hrn を開いて起動
```

直接 CMake を使う場合:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_TOOLCHAIN_FILE=.tools/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_MANIFEST_MODE=ON -DVCPKG_MANIFEST_INSTALL=ON `
  -DVCPKG_MANIFEST_DIR="$PWD" -DVCPKG_INSTALLED_DIR="$PWD/vcpkg_installed"
cmake --build build
$env:PATH = "$env:Qt6_ROOT/bin;$env:PATH"
./build/heiran.exe examples/sample.hrn
```

直接実行する場合は Developer PowerShell を使い、`clang-cl.exe` のあるディレクトリを PATH に追加し、
`Qt6_ROOT` を設定し、vcpkg のパスを環境に合わせます。
`heiran.exe` の起動前に Qt の `bin/` を PATH に追加し、DLL を探索できるようにします。

## テスト

```powershell
ctest --test-dir build --output-on-failure
```

テストは、窓を出さない `offscreen` プラットフォームで動きます (実行中も、マウスやキーボードの操作を奪いません)。
実際の窓で動かしたいときは、`cmake -S . -B build -DHEIRAN_TEST_QPA_PLATFORM=` (空) で構成し直します。

## 起動時間の計測

Release 構成でビルドし、Qt の DLL を探索できる状態で実行します。

```powershell
$env:PATH = "$env:Qt6_ROOT/bin;$env:PATH"
py tools/benchmark_startup.py --compare --runs 6 --output build/startup-results.json
py tools/benchmark_startup.py --compare-inputs --runs 4 examples/sample.hrn
py tools/benchmark_startup.py --compare-decode --runs 6 path/to/large-image.png
py tools/benchmark_startup.py examples/images/d/0001.png
py tools/benchmark_startup.py examples/sample.hrn
```

`heiran_startup_benchmark` は通常アプリと同じ `main.cpp` を使い、初回フレームと画像反映済みフレームまでを測ります。
設定は既存値を一時 INI へコピーして使うため、計測で実環境の設定や履歴を変更しません。
窓をアクティブにせず、表示対象の読み込み状態を反映したフレームが出たら自動終了します。描画が必要なため `offscreen` では実行しません。
10 秒以内にそのフレームが出なければ失敗として扱います。失敗した画像はセルの失敗表示を完了状態として扱います。

Python 側の `process_ms` はプロセス作成から初回フレームのログを受信するまでで、DLL 読み込みも含みます。
`main_ms` は `main()` の開始から初回フレームまでです。初回を除外し、最小・中央値・最大を表示します。
`images_ms` / `main_images_ms` は、全表示セルの読み込み中が解消した後のフレームまでの値です。
最初のフレームが読み込み表示の場合でも、画像を反映するまで待って計測します。
`--compare` ではフォントの並行初期化を無効にした比較用実行と交互に測ります。
`--compare-inputs` では起動引数の準備だけを直列にした実行と交互に測り、フォントの並行初期化は両方で有効にします。
`--compare-decode` では OpenGL 確認中の先行復号を無効にした実行と比較します。窓だけの表示時間ではなく `images_ms` を比較してください。
画像数が多いフォルダー内の画像、または複数の画像セットを持つ `.hrn` を渡すと、一覧作成の待ちを比較できます。
ベンチマークだけが `HEIRAN_BENCHMARK_SEQUENTIAL_FONTS` / `HEIRAN_BENCHMARK_SEQUENTIAL_INPUTS` / `HEIRAN_BENCHMARK_SEQUENTIAL_DECODE` を認識します。
通常アプリでは並行初期化・並行準備を使い、引数がない場合は入力準備用ワーカーを起動しません。

通常アプリの内訳を調べる場合は `HEIRAN_PROFILE_STARTUP=1` と `QT_ASSUME_STDERR_HAS_CONSOLE=1` を設定します。
`startup <区間>: stage=... ms total=... ms` を標準エラーへ出し、初回描画は `first-frame`、画像反映済みの描画は `images-frame` で示します。
`session-load` は準備結果を受け取る際の残りの待ちと履歴反映の区間で、ワーカー全体の所要時間ではありません。
表示先への出力タイミングを測るため、`frameSwapped` はディスプレイへの物理的な表示完了を保証しません。

## サンプル画像

`examples/sample.hrn` 用の最小サンプル画像は `examples/images/` に含めています。
再生成する場合は `uv` を使って次を実行します。

```powershell
uv run python tools/generate_example_images.py
```

差分とワイプの見本は `examples/diff-delta-e.hrn`、`examples/diff-ssim.hrn`、`examples/wipe.hrn` です。
アニメーションの見本は `examples/animation/` にあります (`tools/generate_animation_samples.py` が作ります。
絵はスクリプトが描くため、権利の問題はありません)。

## ユーザーマニュアル

`Sphinx` で HTML を作ります (`uv` が要ります)。

```powershell
./scripts/build-manual.ps1 -Open
```

出力は `build/manual` です。配布物には `doc/manual/` として入り、アプリの Help > Manual (F1) から開けます。
マニュアルの画面の画像は、`./scripts/capture-manual-images.ps1` で撮り直せます。

## 配布物の作成

```powershell
./scripts/configure.ps1 -BuildConfig Release
./scripts/package.ps1
```

`build/Heiran-<version>-win64.exe` (NSIS インストーラ) と `build/Heiran-<version>-win64.zip` ができます。
詳細は [AGENTS.md](./AGENTS.md) の「配布物の作成」にあります。

### インストーラーの外観

外観は `resources/nsis/appearance.nsh`、歓迎・完了画面とヘッダーの画像は
`resources/nsis/sidebar.bmp` と `header.bmp` で管理します。画像はビルド入力としてコミットし、
通常のビルドでは再生成しません。アプリアイコンを変更した場合は、次で画像も再生成します
(Windows 標準の System.Drawing と Segoe UI を使用)。

```powershell
./tools/generate_installer_artwork.ps1
```

ファイルやレジストリを書き換えずに各ページを確認できるプレビューを用意しています。
`build` を作成したうえで、次を実行します。

```powershell
$preview = Join-Path (Resolve-Path build) "installer-preview.exe"
makensis "/DHEIRAN_PREVIEW_OUTPUT=$preview" resources/nsis/preview.nsi
& $preview
```

英語で確認する場合は `/DHEIRAN_PREVIEW_ENGLISH`、完了画面だけを確認する場合は
`/DHEIRAN_PREVIEW_FINISH` を `makensis` に追加します。
起動時の旧版削除は無効にし、同じインストール先へ上書き更新します。［インストール］を押す前のキャンセルでは既存版を変更しません。
コピー開始後の失敗に対する自動ロールバックはありません。旧版だけにあるファイルは残すため、配布構成の変更時は不要ファイルの影響を確認してください。
`/DHEIRAN_PREVIEW_UPGRADE` は CPack 標準の旧版削除メッセージの翻訳確認用です。通常の配布物では表示せず、プレビューでも削除を実行しません。
関連付けのページだけを確認する場合は `/DHEIRAN_PREVIEW_ASSOCIATIONS` を追加します。
プレビューにはインストール処理がなく、歓迎、ライセンス、関連付け、保存先、完了画面の配置を確認できます。
実際の配布物は `./scripts/package.ps1 -Fast -Generator NSIS` で生成し、最終確認は上記の通常手順に従います。

インストーラー独自の文言は `resources/nsis/strings_en.nsh` と `strings_ja.nsh` で管理します。
日本語の OS では日本語、それ以外では英語になります。アプリ内の言語設定とは独立しています。
`cmake/InstallerLocalization.cmake` は、利用中の CPack テンプレートの確認文・対応言語・関連付けページの案内を調整します。
CPack の該当部分が変わった場合は configure を失敗させるため、CMake 更新時にテンプレートの差分を確認してください。
関連付けページの見出しと案内は `resources/nsis/association_page.nsh` で設定します。
カテゴリと拡張子は `association_sections.nsh`、共通の拡張子一覧は `association_formats.nsh` にあります。
`.hrn`、一般画像、高精度画像の順で表示し、すべて既定で選択します。画像はカテゴリ単位でも拡張子単位でも切り替えられます。
関連付けには CPack のコンポーネントや配布用のダミーファイルを使いません。
`heiran_installer_association_selection_tests` は実際の選択判定を使い、登録先をテスト用ファイルに置き換えて検証します (makensis がある環境だけで登録)。

## リリース

配布物は、リポジトリへ入れず、GitHub の Releases に添付する。手順は `scripts/release.ps1` が二段に分けて行う。
前提は、`main` にいること、作業ツリーがきれいなこと、`uv` と `makensis` があること、公開には `gh` (GitHub CLI) へログイン済みなこと。

1. **版を上げる。** `./scripts/release.ps1 -Version 1.0.0` を実行する。`CMakeLists.txt` と `vcpkg.json` の版が違えば、
   書き換えて止まる。差分を確かめてコミットし、もう一度実行する。
2. **準備 (同じコマンド)。** Release 構成で `build-release/` へ作り直し、テストを通し、`-Fast` なしでパッケージを作る。
   続けて、ZIP を一時フォルダへ展開し (PATH から Qt を外して)、`heiran.exe` が起動し続けるかを確かめる。
   成果物は `build-release/release/` に出る (インストーラ、ZIP、`SHA256SUMS.txt`、`release-notes.md`)。
   - アプリを確実に作り直したいときは `-CleanBuildDir` (共有の `vcpkg_installed/` は残す)。
   - `-SkipTests`、`-SkipSmokeTest` もある (どちらも、公開前には外さない)。
3. **手で確かめる。** `release-notes.md` を書き直す。インストーラは、きれいな環境 (VM など) で実行し、
   関連付け、アンインストール、VC++ ランタイムの案内を確かめる (レジストリへの書き込みは、スクリプトでは確かめない)。
4. **公開する。** `./scripts/release.ps1 -Version 1.0.0 -Publish`。準備のときの commit と HEAD が同じかを確かめ、
   `main` と、タグ `v1.0.0` を push し、**下書き (draft)** のリリースを作る。GitHub の画面で内容を確かめて、公開する。

コード署名はしていないため、SmartScreen の警告が出ることがある。リリースノートの雛形 (`resources/release/release-notes.template.md`)
に、その案内を入れてある。

## ライセンスの表記

依存ライブラリのライセンスは `THIRD_PARTY_NOTICES.txt` にまとめます。依存を足す、外す、版を上げたときは、
次で作り直して一緒にコミットします。

```powershell
py tools/generate_third_party_notices.py
```
