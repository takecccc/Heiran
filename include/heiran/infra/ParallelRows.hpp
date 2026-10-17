#pragma once

#include <QThread>
#include <QVector>
#include <QtConcurrent>
#include <algorithm>
#include <functional>
#include <numeric>

namespace heiran::infra {

// 行を帯へ分けて並列に走らせる。全画素を走査する解析で使う。
//
// 画素ごとに独立した処理を前提とする。帯の数は CPU 数に合わせる。1 行ずつ配ると
// 取り回しの費用が処理そのものを上回るため、まとめて配る。
// 高さが 1 行以下、または CPU が 1 個の場合はそのまま呼び出す。
inline void forEachRowBand(int height, const std::function<void(int, int)> &processBand) {
  if (height <= 0) {
    return;
  }

  const int threadCount = std::max(1, QThread::idealThreadCount());
  const int bandCount = std::min(threadCount, height);
  if (bandCount <= 1) {
    processBand(0, height);
    return;
  }

  QVector<int> bandIndices(bandCount);
  std::iota(bandIndices.begin(), bandIndices.end(), 0);
  const int rowsPerBand = (height + bandCount - 1) / bandCount;
  QtConcurrent::blockingMap(bandIndices, [&](int bandIndex) {
    const int firstRow = bandIndex * rowsPerBand;
    const int lastRow = std::min(height, firstRow + rowsPerBand);
    if (firstRow < lastRow) {
      processBand(firstRow, lastRow);
    }
  });
}

}  // namespace heiran::infra
