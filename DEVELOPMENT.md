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

- Windows (64bit)、Visual Studio (MSVC)、Ninja
- CMake 3.24 以上
- C++20 対応コンパイラ
- Qt 6.5 以上 (`Core`, `Gui`, `Widgets`, `Test`)
- 画像の復号ライブラリ (libavif、libjxl、OpenEXR、libtiff、libwebp、libspng) は vcpkg で入る。
  無いものは、その形式が開けないだけでビルドはできる
- 補助スクリプトとマニュアルの作成には、Python と `uv` を使う

Windows では `PowerShell` スクリプトをビルド入口として使います。
ビルド構成は `Ninja + msvc` 固定です。
ローカル依存の値は `scripts/local.settings.ps1` に閉じ込めます (コミットしません)。

## 初回設定

```powershell
Copy-Item scripts/local.settings.sample.ps1 scripts/local.settings.ps1
```

`QtPrefixPath` などを自分の環境に合わせて編集します。
vcpkg は `./scripts/setup-vcpkg.ps1` で用意し、`VcpkgRoot` を設定します。

## ビルドと実行

設定:

```powershell
./scripts/configure.ps1
```

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
cmake -S . -B build
cmake --build build
./build/heiran.exe examples/sample.hrn
```

## テスト

```powershell
ctest --test-dir build --output-on-failure
```

テストは、窓を出さない `offscreen` プラットフォームで動きます (実行中も、マウスやキーボードの操作を奪いません)。
実際の窓で動かしたいときは、`cmake -S . -B build -DHEIRAN_TEST_QPA_PLATFORM=` (空) で構成し直します。

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
./scripts/configure.ps1   # BuildConfig = Release にしておく
./scripts/package.ps1
```

`build/Heiran-<version>-win64.exe` (NSIS インストーラ) と `build/Heiran-<version>-win64.zip` ができます。
詳細は [AGENTS.md](./AGENTS.md) の「配布物の作成」にあります。

## リリース

配布物は、リポジトリへ入れず、GitHub の Releases に添付する。手順は `scripts/release.ps1` が二段に分けて行う。
前提は、`main` にいること、作業ツリーがきれいなこと、`uv` と `makensis` があること、公開には `gh` (GitHub CLI) へログイン済みなこと。

1. **版を上げる。** `./scripts/release.ps1 -Version 1.0.0` を実行する。`CMakeLists.txt` と `vcpkg.json` の版が違えば、
   書き換えて止まる。差分を確かめてコミットし、もう一度実行する。
2. **準備 (同じコマンド)。** Release 構成で `build-release/` へ作り直し、テストを通し、`-Fast` なしでパッケージを作る。
   続けて、ZIP を一時フォルダへ展開し (PATH から Qt を外して)、`heiran.exe` が起動し続けるかを確かめる。
   成果物は `build-release/release/` に出る (インストーラ、ZIP、`SHA256SUMS.txt`、`release-notes.md`)。
   - 依存を更新した後など、確実に作り直したいときは `-CleanBuildDir` (vcpkg の依存も作り直すため遅い)。
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
