## Heiran {{VERSION}}

複数の画像を並べて、同じ位置を同じ倍率で見比べるための Windows 用イメージビューアです。

## 変更点

{{CHANGES}}

## ダウンロード

| ファイル | 内容 |
| --- | --- |
| `{{INSTALLER_NAME}}` | インストーラ。`.hrn` と画像形式の関連付けを選べます |
| `{{ZIP_NAME}}` | 展開して使う形式。`bin/heiran.exe` を起動します |

SHA-256:

```
{{SHA256_INSTALLER}}  {{INSTALLER_NAME}}
{{SHA256_ZIP}}  {{ZIP_NAME}}
```

## 動作環境

- Windows (64bit)
- OpenGL 3.0 以上に対応した GPU とドライバ
- Microsoft Visual C++ ランタイム (x64)。入っていない環境では、インストーラが案内します。
  ZIP から使う場合は、先に [最新の vc_redist.x64.exe](https://aka.ms/vs/17/release/vc_redist.x64.exe) を入れてください

## 注意

- 実行ファイルには、コード署名をしていません。起動のときに Windows の SmartScreen が警告を出すことがあります。
  配布元とハッシュを確かめたうえで、「詳細情報」から実行してください。
- 使い方は、同梱のマニュアル (アプリの **ヘルプ > マニュアル**、<kbd>F1</kbd>) にあります。
