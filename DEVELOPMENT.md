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
- CMake 3.27 以上
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

`heiran_screenshot` の `--favorites-file <JSON>` で、確認用のお気に入りファイルを指定できます。
`--action "Favorites Only"` や `--action "Cell Headers"` と組み合わせ、絞り込みとマークの表示を確認します。
確認用ツールの設定とキャッシュは、通常のアプリとは別の `HeiranScreenshot` フォルダーへ保存します。
画像一覧のサイドパネルは `--action "Image List"` で開き、メイン画面と一緒に撮影できます。
`--list-mode details` で詳細表示を確認できます。既定は `thumbnails` です。
`--list-select 0,2` で複数選択、`--compare-selection` で選択画像の比較結果を撮影できます。
`--thumbnail-width 320` でサムネイルの最大幅、`--zoom-percent 150.25` で全体の拡大率を指定できます。

## 配布物の作成

インストーラーの生成には [Inno Setup](https://jrsoftware.org/inno-setup/downloads) 6.3 以降が必要です。
パッケージ作成時に、リポジトリ内の `.tools/inno-setup/ISCC.exe`、Windows の標準インストール先、`PATH` からコンパイラを探します。
ポータブル版を使う場合は、`ISCC.exe` と付属のファイルをまとめて `.tools/inno-setup/` に置きます。
この場所や `PATH` に追加した後は、パッケージ作成を再実行すれば見つかります。
別の場所を固定で使う場合は、CMake の `HEIRAN_ISCC_EXECUTABLE` に `ISCC.exe` のパスを指定します。

```powershell
./scripts/configure.ps1 -BuildConfig Release
./scripts/package.ps1
```

`build/Heiran-<version>-win64.exe` (Inno Setup) と `build/Heiran-<version>-win64.zip` ができます。
インストーラーだけを手早く作るには `./scripts/package.ps1 -Fast -Generator INNOSETUP` を使います。
ZIP のみなら `./scripts/package.ps1 -Generator ZIP` で、Inno Setup は不要です。

### インストーラーの設定と確認

CPack がアプリと DLL を集め、`resources/installer/` のスクリプトからインストーラーを生成します。
生成されたスクリプトは `build/_CPack_Packages/win64/INNOSETUP/ISScript.iss` で確認できます。
独自の文言は `messages.iss`、関連付けは `associations.iss.in` と `cmake/WindowsInstaller.cmake` で管理します。
外観を変える場合は標準ウィザードと `sidebar.bmp`、`header.bmp` を調整します。
アプリアイコンを変更した場合は `./tools/generate_installer_artwork.ps1` で画像を再生成します。

セットアップの言語は `/LANG=japanese` と `/LANG=english`、範囲は `/CURRENTUSER` と `/ALLUSERS` でも指定できます。
配布前に、標準ユーザーのアカウントでユーザー単位のインストール、管理者への昇格を伴う全ユーザー向けインストール、
両方のアンインストールを確認します。旧 NSIS 版のある環境では全ユーザー向けで上書きし、旧アンインストール登録が
更新完了後に消えることを確認します。UAC とウィザードをキャンセルして旧版が起動することも確かめます。
上書きで旧版だけにあるファイルは残るため、配布構成を変えたときは不要ファイルの影響を確認します。
関連付けは言語ごとの表示、カテゴリの一括操作、拡張子ごとの登録・解除とアンインストール時の削除を確認します。
`heiran_installer_configuration_tests` は範囲と各言語の設定、関連付けのタスクと登録・削除の対応を検査します。

## リリース

配布物は、リポジトリへ入れず、GitHub の Releases に添付する。手順は `scripts/release.ps1` が二段に分けて行う。
前提は、`main` にいること、作業ツリーがきれいなこと、`uv` と `ISCC.exe` があること、公開には `gh` (GitHub CLI) へログイン済みなこと。

1. **リリースノートを用意する。** `manual/release-notes/v1.0.0.md` に、版名をレベル 1 の見出し (`# Heiran 1.0.0`) として置き、変更点と配布案内を書く。
   [リリースノートの目次](./manual/release-notes/index.md) の先頭にその版を加え、`./scripts/build-manual.ps1` で表示を確認し、配布準備の前にコミットする。
2. **版を上げる。** `./scripts/release.ps1 -Version 1.0.0` を実行する。`CMakeLists.txt` と `vcpkg.json` の版が違えば、
   書き換えて止まる。差分を確かめてコミットし、もう一度実行する。
3. **準備 (同じコマンド)。** Release 構成で `build-release/` へ作り直し、テストを通し、`-Fast` なしでパッケージを作る。
   続けて、ZIP を一時フォルダへ展開し (PATH から Qt を外して)、`heiran.exe` が起動し続けるかを確かめる。
   成果物は `build-release/release/` に出る (インストーラ、ZIP、`SHA256SUMS.txt`、`release-notes.md`)。
   `release-notes.md` はマニュアルの各版の本文に、確定した配布ファイルの SHA-256 を加えて生成する。
   - アプリを確実に作り直したいときは `-CleanBuildDir` (共有の `vcpkg_installed/` は残す)。
   - `-SkipTests`、`-SkipSmokeTest` もある (どちらも、公開前には外さない)。
4. **手で確かめる。** 生成した `release-notes.md` の内容を確認する。本文に修正があればマニュアル側を更新してコミットし、準備をやり直す。インストーラは、きれいな環境 (VM など) で実行し、
   関連付け、アンインストール、VC++ ランタイムの案内を確かめる (レジストリへの書き込みは、スクリプトでは確かめない)。
5. **公開する。** `./scripts/release.ps1 -Version 1.0.0 -Publish`。準備のときの commit と HEAD が同じかを確かめ、
   `main` と、タグ `v1.0.0` を push し、**下書き (draft)** のリリースを作る。GitHub の画面で内容を確かめて、公開する。

版別の本文がまだない場合は、[雛形](./resources/release/release-notes.template.md) とコミット一覧から下書きを生成する。
コード署名はしていないため、リリースノートには SmartScreen の警告が出る場合があることを記載する。

## ライセンスの表記

依存ライブラリのライセンスは `THIRD_PARTY_NOTICES.txt` にまとめます。依存を足す、外す、版を上げたときは、
次で作り直して一緒にコミットします。

```powershell
py tools/generate_third_party_notices.py
```
