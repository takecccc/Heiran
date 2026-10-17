#pragma once

#include "heiran/domain/ChannelMode.hpp"
#include "heiran/domain/DiffSettings.hpp"

#include <QString>
#include <QVector>
#include <optional>

namespace heiran::domain {

struct ViewerLayout {
  int rows = 1;
  int cols = 1;
  QString order = "row-major";
};

struct ViewerSettings {
  ViewerLayout layout;
  QString pageSort = "natural";
};

struct ImageSetDocument {
  QString name;
  QString label;
  QString path;
  double scale = 1.0;
  int pageOffset = 0;
  // 既定はラベル無し。1 枚を見るだけの使い方では、名前を重ねない方が見やすい。
  bool labelVisible = false;
  bool honorExifOrientation = true;
  // 色空間の上書き。空なら埋め込みプロファイルと形式の既定に従う。
  // 名前は `srgb`, `srgb-linear`, `display-p3`, `adobe-rgb`, `rec2020`,
  // `prophoto-rgb`, `bt2100-pq`, `bt2100-hlg` を受ける。
  QString colorSpaceName;
  // ICC プロファイルの経路。指定があれば `colorSpaceName` より優先する。
  // `.hrn` からの相対経路で書ける。
  QString iccProfilePath;
};

struct ImageSet {
  QString name;
  QString label;
  QString path;
  double scale = 1.0;
  int pageOffset = 0;
  // 既定はラベル無し。1 枚を見るだけの使い方では、名前を重ねない方が見やすい。
  bool labelVisible = false;
  bool honorExifOrientation = true;
  // 色空間の上書き。空なら埋め込みプロファイルと形式の既定に従う。
  // 名前は `srgb`, `srgb-linear`, `display-p3`, `adobe-rgb`, `rec2020`,
  // `prophoto-rgb`, `bt2100-pq`, `bt2100-hlg` を受ける。
  QString colorSpaceName;
  // ICC プロファイルの経路。指定があれば `colorSpaceName` より優先する。
  // `.hrn` からの相対経路で書ける。
  QString iccProfilePath;
  QVector<QString> resolvedPaths;
};

// セルに、画像そのものではなく、2 つのセルの差分を見せる指定。
// セルの番号は 0 から。差分は、他のセルが見せている画像から作る (差分のセルを元にはしない)。
struct CellDiff {
  // 基準になるセル。
  int baseCell = -1;
  // 比べる側のセル。-1 なら、このセル自身に割り当てた画像 (表示は差分に置き換わる)。
  int targetCell = -1;
  DiffSettings settings;

  bool operator==(const CellDiff &) const = default;
};

struct CellConfig {
  int imageIndex = -1;
  bool sync = true;
  double scale = 1.0;
  double gamma = 1.0;
  int rotation = 0;
  bool flipX = false;
  bool flipY = false;
  bool invert = false;
  ChannelMode channel = ChannelMode::Color;
  // あれば、このセルは差分を見せる。
  std::optional<CellDiff> diff;
};

struct SessionDocument {
  int formatVersion = 1;
  ViewerSettings viewer;
  QVector<ImageSetDocument> images;
  QVector<CellConfig> cells;
};

struct SessionModel {
  int formatVersion = 1;
  QString sessionFilePath;
  QString sessionDirectory;
  ViewerSettings viewer;
  QVector<ImageSet> images;
  QVector<CellConfig> cells;
};

}  // namespace heiran::domain
