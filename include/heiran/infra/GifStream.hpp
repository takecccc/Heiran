#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QString>
#include <QVector>
#include <memory>

namespace heiran::infra {

// GIF のヘッダーとブロックだけから読む、フレームの数と大きさと表示時間。画素は復号しない。
struct GifInfo {
  bool valid = false;
  int width = 0;
  int height = 0;
  int frameCount = 0;
  // フレームごとの表示時間 (ミリ秒)。ファイルの値そのまま (0 や極端に短い値もそのまま)。
  QVector<int> delaysMs;

  // 全部を展開したときの大きさ (幅 x 高さ x 4 x フレーム数)。
  qint64 decodedBytes() const {
    return static_cast<qint64>(width) * height * 4 * static_cast<qint64>(frameCount);
  }
};

// ファイルを走査して GifInfo を作る。GIF でなければ、壊れていれば valid が偽。
GifInfo probeGif(const QString &filePath);

// 貯めておく圧縮フレームの大きさの既定の上限。
inline constexpr qint64 kDefaultGifStoreBytes = 512LL * 1024LL * 1024LL;

// 再生しながら貯めるアニメーション GIF。
//
// GIF は先頭から順にしか復号できず、任意のフレームへ飛べない。そこで、再生が進むにつれて
// 復号したフレームを可逆に圧縮 (zstd) して貯める。1 周目は順に復号し、2 周目からは貯めた
// ものを伸ばすだけで済む。コマ戻しや飛び越しも、貯めた範囲なら復号し直さずに済む。
//
// 貯めた量が上限を超えたら、超えた分は貯めない。貯めていないフレームが要るときは、先頭から
// 復号し直す。遅くはなるが、結果は変わらない。
//
// 失敗したら null。1 コマ目が復号できなければ失敗。
std::shared_ptr<FrameStream> openGifStream(const QString &filePath, QString *errorMessage = nullptr,
                                           qint64 storeCapBytes = kDefaultGifStoreBytes);

// 試験用。貯めたフレームの数。GIF のストリームでなければ 0。
int gifStoredFrameCountForTesting(const FrameStream &stream);

}  // namespace heiran::infra
