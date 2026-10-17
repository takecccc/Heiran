#pragma once

#include "heiran/domain/ColorSpace.hpp"
#include "heiran/infra/ImageDecoder.hpp"
#include "heiran/ui/CellStatus.hpp"
#include "heiran/ui/ChannelMode.hpp"
#include "heiran/ui/PixelValueFormat.hpp"

#include <QImage>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace heiran::ui {

// 画素 1 点について出す 1 チャネル分の値。
struct PixelChannelValue {
  // "R" や "Gray" のような表示名。
  QString label;
  // 正規化値。整数型は 0.0-1.0、浮動小数型は 1.0 を超えうる。
  float value = 0.0F;
};

// 色空間から導いた重みで輝度を求める。
float luminanceOf(float red, float green, float blue, const heiran::domain::LumaWeights &weights);

// 色モデルが不明な場合は表示用画像の性質で補う。
bool sourceIsGrayscale(const heiran::infra::DecodedImage &decodedImage, const QImage &fallback);
bool sourceHasAlpha(const heiran::infra::DecodedImage &decodedImage, const QImage &fallback);

// 表示チャネルと色モデルから、出すべきチャネルと値を決める。
// `sample` は RGBA の正規化値を返す。`luma` は輝度表示のときの正規化値を返す。
//
// 画素値表示とルーペで同じ選び方をする必要がある。並べ方 (区切りとラベルの体裁)
// は場所によって違うため、ここでは選ぶところまでを担い、文字列化は呼び出し側に任せる。
//
// 輝度の求め方は呼び出し側から渡す。画素値表示は `luminanceOf()` の重み付け、
// ルーペは `AnalysisSampler::lumaAtDisplayPixel()` の重み付けを使っており、
// 2 つは一致していない。どちらへ寄せるかは未判断のため、ここでは選ばない。
QVector<PixelChannelValue> selectPixelChannelValues(ChannelMode channelMode, bool grayscale,
                                                    bool hasAlpha,
                                                    const std::function<float(int)> &sample,
                                                    const std::function<float()> &luma);

QString formatPixelPosition(int x, int y);

// セルに割り当てた画像セットの番号。負なら未割り当てとして扱う。
QString formatImageIndexLabel(int imageSetIndex);

// 画素値表示 1 行分のチャネル部分を組み立てる。1 チャネルだけの場合は
// `Gray=0.5`、複数ある場合は `R= 0.5 G= 0.5 B= 0.5` の形にする。
QString formatPixelChannelValues(const QVector<PixelChannelValue> &values,
                                 PixelValueFormat valueFormat);

// 画像を持たないセルの行。状態を出す。
QString buildPixelInfoStatusLine(int imageSetIndex, const CellStatusInfo &statusInfo);

// 表示範囲の外を指している行。
QString buildPixelInfoOutOfBoundsLine(int imageSetIndex, const QPoint &position);

// 値のある行。位置は元ファイルの座標で渡す。
// ステータスバー用の短い 1 行。位置と、各セルの値だけを並べる (Pixel Info を閉じていても、値を読める)。
// 例: `x=87 y=33  [1] 242 244 247  [2] 52 130 72`。`infoLines` は、セルごとの値の行 (`[ 1] (...): R= 242.000 ...`)。
// 値の行でないもの (読込中など) は、飛ばす。値の無いセル (範囲外など) は `-`。セルが多いときは、先頭の `maxCells` 個と、残りの数 (`+3`)。
QString buildPixelStatusSummary(int x, int y, const QStringList &infoLines, int maxCells = 4);

QString buildPixelInfoValueLine(int imageSetIndex, const QPoint &reportedPixel,
                                const QVector<PixelChannelValue> &values,
                                PixelValueFormat valueFormat);

}  // namespace heiran::ui
