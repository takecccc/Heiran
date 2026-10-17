# Heiran

複数の画像を同期表示するイメージビューワー。

このリポジトリの作業ルールは [AGENTS.md](./AGENTS.md) に集約している。
作業を始める前に必ず [AGENTS.md](./AGENTS.md) を読むこと。

[AGENTS.md](./AGENTS.md) には次を記載している。

- ドキュメントの所在と、仕様の正がどこにあるか
- タスク管理の方法 ([open_tasks.md](./open_tasks.md))
- 判断記録の運用 ([docs/設計判断.md](./docs/設計判断.md) と各設計文書)
- ビルド、テスト、実行の手順とデバッグ用環境変数
- アーキテクチャと守るべき責務境界
- コーディング方針とファイル追加時の `CMakeLists.txt` 登録手順
- コミットメッセージの作成手順

ルールを追加・変更する場合は、この `CLAUDE.md` ではなく [AGENTS.md](./AGENTS.md) を更新する。
`CLAUDE.md` には Claude Code 固有の補足だけを書く。

## Claude Code 固有の補足

- シェルは PowerShell を使う。`&&` や `||` は使えないため `;` と `if ($?) { ... }` で繋ぐ。
- ビルド、テスト、実行は `scripts/*.ps1` を経由する。
  スクリプトが VsDevShell 初期化と Qt の PATH 追加を行うため、`cmake` を直接呼ぶと失敗しやすい。
- `build/`, `vcpkg_installed/`, `.tools/`, `.venv/` は生成物のため、探索対象から除外してよい。
