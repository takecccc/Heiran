#pragma once

#include "heiran/infra/AnimationComposer.hpp"

#include <QString>
#include <memory>

namespace heiran::infra {

// WebP (静止画とアニメーション) のフレームの取り出し方。
//
// ファイルを 1 度読み、demux でフレームの位置と指定 (矩形、表示時間、描いた後の扱い、重ね方) を
// 取り出す。画素はここでは復号しない。合成は APNG と同じ処理 (`AnimationComposer`) を通し、
// 全部を展開することも、要るフレームだけを作ることもできる。
//
// libwebp の `WebPAnimDecoder`
// は先頭から順にしか進められず、コマ戻しや飛び越しのために合成し直せない。 そのため使わない。
//
// 失敗したら null。静止画は 1 フレームのアニメーションとして扱う。
std::shared_ptr<const AnimationSource> openWebpSource(const QString &filePath,
                                                      QString *errorMessage = nullptr);

}  // namespace heiran::infra
