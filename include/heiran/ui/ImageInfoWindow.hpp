#pragma once

#include "heiran/infra/ImageMetadata.hpp"
#include "heiran/ui/AnalysisWindowBase.hpp"
#include "heiran/ui/ImageInfo.hpp"

#include <QHash>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QTableWidget;
QT_END_NAMESPACE

namespace heiran::ui {

// 画像の情報 (ファイル、画素の形式、色、EXIF などのメタデータ) を、セルを列にして並べる。
// セルの間で値が違う行には印を付ける。比べているもの同士の、撮影条件や形式の違いを探すため。
class ImageInfoWindow : public AnalysisWindowBase {
public:
  explicit ImageInfoWindow(QWidget *parent = nullptr);

  // 表の内容をタブ区切りの文字にする。分類の行は `[分類]` と書く。コピーと試験に使う。
  QString tableAsText() const;

protected:
  void analysisImagesChanged() override;

private:
  void rebuild();
  void copyToClipboard() const;
  // ファイルのメタデータは、読むたびにファイルを開くため、更新日時までを鍵に取っておく。
  QVector<heiran::infra::MetadataEntry> metadataFor(const QString &filePath);

  QTableWidget *table_ = nullptr;
  QCheckBox *differencesOnlyCheckBox_ = nullptr;
  QVector<ImageInfoCell> cells_;
  QHash<QString, QVector<heiran::infra::MetadataEntry>> metadataCache_;
};

}  // namespace heiran::ui
