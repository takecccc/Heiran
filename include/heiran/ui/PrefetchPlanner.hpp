#pragma once

#include <QVector>
#include <QtGlobal>

namespace heiran::ui {

// 先読みをどこまで、どの順で行うかを決める。
//
// 「入りきらない分まで読む」と、届いたそばから捨てられて無駄になる。予算に何ページ
// 分入るかから距離を決める必要がある。読む順も近い側からでなければ、いま要る
// ページの到着が遅れる。どちらも表示とは独立した判断なので、ここに置いて直接
// テストする。

// 予算に何ページ分入るかから先読みの距離を決める。
//
// `entriesPerPage` はセルの数。セルごとに別の画像を出す場合、1 ページで複数の
// エントリを使うため、その分だけ入る枚数が減る。
//
// まだ何も抱えていない場合は 1 ページあたりの大きさを見積もれない。その場合は
// `maxRadius` を返し、実際に抱えてから絞る。
int prefetchRadiusForBudget(int entryCount, qint64 usedBytes, qint64 budgetBytes,
                            int entriesPerPage, int maxRadius);

// 先読みするページを、現在ページに近い順で並べる。
//
// 同じ距離なら前のページを先に置く。ページ送りは前後どちらへも起きるため、
// 片側へ寄せる理由が無い。範囲の外は含めない。
QVector<int> prefetchPageOrder(int currentPage, int totalPages, int radius);

}  // namespace heiran::ui
