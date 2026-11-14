#pragma once

#include "heiran/ui/ColorManagement.hpp"
#include "heiran/ui/ColorTheme.hpp"
#include "heiran/ui/CursorStyle.hpp"
#include "heiran/ui/DisplayProfile.hpp"
#include "heiran/ui/PixelValueFormat.hpp"
#include "heiran/ui/TransparencyBackground.hpp"

#include <QString>
#include <QtTypes>

namespace heiran::ui {

// 拡大時の補間。等倍を超えて表示するときに使う。
enum class MagnifyFilter {
  // 画素の境界をそのまま見せる。画素単位の確認に向く。
  Nearest,
  // 隣接画素で補間する。写真の閲覧に向く。
  Smooth,
};

// 縮小時の補間。等倍を下回って表示するときに使う。
enum class MinifyFilter {
  // 最も近い画素を拾う。パン時にちらつくが、元の画素値をそのまま見られる。
  Nearest,
  // 2x2 平均の段階縮小を経てから補間する。ちらつかない。
  Area,
};

// 複数のセルでアニメーションを再生するときの進め方。
enum class AnimationSyncMode {
  // 全アニメーションのフレーム数が同じなら同期し (同じ内容の別バージョンとみなす)、違えば
  // セルごとに独立して再生する。
  Auto,
  // 常に全セルを同じフレームに保つ。先頭のアニメーションの表示時間で進む。
  Locked,
  // 常にセルごとに、自分の表示時間で再生する。
  Independent,
};

QString animationSyncModeToString(AnimationSyncMode mode);
AnimationSyncMode animationSyncModeFromString(const QString &text);

QString magnifyFilterToString(MagnifyFilter filter);
MagnifyFilter magnifyFilterFromString(const QString &text);
QString minifyFilterToString(MinifyFilter filter);
MinifyFilter minifyFilterFromString(const QString &text);

// アプリ全体設定。セッションではなく利用者に属する設定を持つ。
// セッションごとに変わる設定は .hrn 側に持たせる。
struct AppSettings {
  TransparencyBackground transparencyBackground;
  PixelValueFormat pixelValueFormat = PixelValueFormat::Normalized255;
  bool fitOnPageChange = false;
  // セルの境目を強調するか。隣のセルとの間に暗い溝を挟み、外周と見出しの下に線を引く。
  bool showCellBorders = true;
  // decode キャッシュのソフト上限 (MiB)。
  int decodeCacheBudgetMib = 1024;
  // GPU テクスチャの上限 (MiB)。全セルの合計に対して掛かり、溢れた分は全セルを通して
  // 最も使われていないものから捨てる。
  int gpuTextureBudgetMib = 2048;
  // アニメーション (APNG / WebP) を全部展開せず、圧縮したまま持って要るフレームだけを作る境目
  // (MiB)。展開後の大きさ (幅 x 高さ x 4 x フレーム数) がこれを超えるものが対象。
  // 小さいほどメモリを節約できるが、再生中に復号の CPU がかかる。GIF
  // は、超えたら再生しながら圧縮して貯める。
  int animationStreamingThresholdMib = 128;
  // 複数のセルでアニメーションを再生するときの進め方。コマ送りは、どの方式でも全セルを
  // 同じ番号へそろえる。
  AnimationSyncMode animationSyncMode = AnimationSyncMode::Auto;
  MagnifyFilter magnifyFilter = MagnifyFilter::Nearest;
  MinifyFilter minifyFilter = MinifyFilter::Area;
  // 表示先の色空間の決め方。既定は画面に割り当てられているプロファイルを使う。
  DisplayProfileMode displayProfileMode = DisplayProfileMode::System;
  // `displayProfileMode` が File のときに使う ICC ファイルの経路。
  QString displayProfilePath;
  // 解析の値をどの空間で出すか。既定はファイルに入っているまま。
  AnalysisValueSpace analysisValueSpace = AnalysisValueSpace::Source;
  // 色域の外へ出る色の扱い。表を持つプロファイルでのみ効く。
  RenderingIntent renderingIntent = RenderingIntent::RelativeColorimetric;
  bool blackPointCompensation = false;
  // 画面の配色。既定は OS の設定に従う。
  ColorTheme colorTheme = ColorTheme::System;
  // 共有カーソルの描き方。縁付きの固定色か、画素の色から離れた色か。
  CursorStyle cursorStyle = CursorStyle::Outline;

  // 表示言語。auto (OS に従う) / en / ja。変えると、次の起動から効く。
  QString language = QStringLiteral("auto");

  static AppSettings load();
  void save() const;
};

// 上限として扱える範囲。極端な値で操作不能になるのを避ける。
inline constexpr int kMinDecodeCacheBudgetMib = 64;
inline constexpr int kMaxDecodeCacheBudgetMib = 65536;

int clampDecodeCacheBudgetMib(int budgetMib);

// GPU テクスチャの上限として扱える範囲。4K のテクスチャ 1 枚が mipmap 込みで
// 約 42 MiB のため、それより小さいと描いている分しか残らない。
inline constexpr int kMinGpuTextureBudgetMib = 64;
inline constexpr int kMaxGpuTextureBudgetMib = 65536;

int clampGpuTextureBudgetMib(int budgetMib);

// 逐次展開の境目として扱える範囲。0 に近いと小さなアニメーションまで逐次にして、コマ戻しが
// 遅くなる。大きすぎると、全部を展開してメモリを使い切る。
inline constexpr int kMinAnimationStreamingThresholdMib = 16;
inline constexpr int kMaxAnimationStreamingThresholdMib = 16384;

int clampAnimationStreamingThresholdMib(int thresholdMib);

// 環境変数 HEIRAN_ANIMATION_STREAMING_THRESHOLD_MB が指定されていればそれを優先する。
qint64 resolveAnimationStreamingThresholdBytes(int thresholdMib, int environmentOverrideMib);

// 環境変数 HEIRAN_GPU_TEXTURE_BUDGET_MB が指定されていればそれを優先する。
qint64 resolveGpuTextureBudgetBytes(int budgetMib, int environmentOverrideMib);

// 環境変数 HEIRAN_DECODE_CACHE_BUDGET_MB が指定されていればそれを優先する。
// 設定を変えずに一時的な検証を行えるようにするため。
qint64 resolveDecodeCacheBudgetBytes(int budgetMib, int environmentOverrideMib);

}  // namespace heiran::ui
