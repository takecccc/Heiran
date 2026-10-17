#pragma once

#include <QString>
#include <functional>

namespace heiran::ui {

struct FileOperationResult {
  bool ok = false;
  // 失敗の理由。利用者へそのまま見せられる文にする。成功なら空。
  QString errorMessage;
  // 名前を変えた後のファイル。それ以外では空。
  QString newPath;
};

// 表示中のファイルに対する操作。ダイアログは持たず、確認と表示は呼び側が行う。
class FileOperations {
public:
  // ごみ箱へ移す処理。テストが実際のごみ箱を汚さないよう差し替えられる。
  using TrashFunction = std::function<bool(const QString &path)>;

  // 新しいファイル名として使えるか。使えなければ理由を返し、使えるなら空を返す。
  // フォルダの区切りや、Windows で使えない文字を含む名前は受け付けない。
  static QString validateFileName(const QString &name);

  // 同じフォルダの中で名前を変える。既にある名前へは上書きしない。
  // 大文字小文字だけを変える場合は、同じファイルへの変更として通す。
  static FileOperationResult rename(const QString &path, const QString &newName);

  // ごみ箱へ移す。元が無い場合は失敗を返す。
  static FileOperationResult moveToTrash(const QString &path,
                                         const TrashFunction &trash = defaultTrash);

  // 貼り付けて使うパスの文字列。区切りは OS の表記にする。
  static QString pathForClipboard(const QString &path);

  static bool defaultTrash(const QString &path);

  // ファイルをエクスプローラで選択した状態で見せる。プラットフォームごとに違うため、
  // Windows 以外はフォルダを開くだけにする。
  static void showInFolder(const QString &path);
};

}  // namespace heiran::ui
