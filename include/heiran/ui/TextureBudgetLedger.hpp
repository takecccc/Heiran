#pragma once

#include <QHash>
#include <QString>
#include <QtTypes>
#include <functional>

namespace heiran::ui {

// GPU テクスチャの使用量を、全セルの合計で管理する台帳。
//
// テクスチャはセルごとの `GpuImageRenderer` が持つ。上限をセルごとに掛けると、
// 合計はセル数に比例して増える (4K で 1 セル 506 MiB、8 セル 4 GiB だった)。
// そのため合計に対して 1 つの上限を掛け、溢れたら全セルを通して最も使われて
// いないものから捨てる。
//
// **この台帳は捨てるものを決めるだけで、GL のオブジェクトには触れない。**
// テクスチャは作ったコンテキストが現在の状態でないと安全に破棄できず、
// 別のセルの描き込み中に他のセルのものを消せない。捨てると決めた鍵は持ち主の
// 退避関数へ渡し、持ち主が次に描くとき (コンテキストが現在の状態) に消す。
//
// GUI スレッドからだけ使う。
class TextureBudgetLedger {
public:
  using OwnerId = const void *;
  // 捨てると決めた鍵が渡される。台帳の側ではもう数えていない。
  using EvictFunction = std::function<void(const QString &key)>;

  TextureBudgetLedger() = default;
  TextureBudgetLedger(const TextureBudgetLedger &) = delete;
  TextureBudgetLedger &operator=(const TextureBudgetLedger &) = delete;

  // アプリで 1 つを共有する。初期の上限は既定値か、環境変数
  // `HEIRAN_GPU_TEXTURE_BUDGET_MB` の指定。
  static TextureBudgetLedger &shared();

  static constexpr qint64 kDefaultBudgetBytes = 2048LL * 1024LL * 1024LL;

  // 上限。下げた場合はその場で溢れた分を捨てる。
  void setBudgetBytes(qint64 budgetBytes);
  qint64 budgetBytes() const { return budgetBytes_; }

  // 台帳が数えている合計。捨てると決めた分は含まない。
  qint64 usedBytes() const { return usedBytes_; }

  void registerOwner(OwnerId owner, EvictFunction evict);
  // 持ち主の分をすべて外す。退避関数は呼ばない。
  void unregisterOwner(OwnerId owner);

  // 持ち主がいま描いているテクスチャとして記録する。載せたとき、載せ済みを
  // 使い直したとき、捨てると決められたものを使い直したときのすべてがこれを使う。
  // 未登録の鍵なら数え直す。描いているものは捨てる対象にならない。
  // 呼んだ時点で、溢れていれば他を捨てる。
  void markUsed(OwnerId owner, const QString &key, qint64 bytes);

  // 持ち主の分をすべて外す。所有権は残し、以後も `markUsed` で登録できる。
  void clearOwner(OwnerId owner);

  int entryCount() const { return static_cast<int>(entries_.size()); }

private:
  struct Key {
    OwnerId owner = nullptr;
    QString key;
    bool operator==(const Key &other) const { return owner == other.owner && key == other.key; }
  };
  friend size_t qHash(const Key &key, size_t seed) noexcept {
    return qHashMulti(seed, reinterpret_cast<quintptr>(key.owner), key.key);
  }

  struct Entry {
    qint64 bytes = 0;
    quint64 lastUsed = 0;
  };

  void enforceBudget();

  QHash<Key, Entry> entries_;
  QHash<OwnerId, EvictFunction> owners_;
  // 描いているもの。持ち主ごとに 1 つ。
  QHash<OwnerId, QString> currentKeys_;
  qint64 budgetBytes_ = kDefaultBudgetBytes;
  qint64 usedBytes_ = 0;
  quint64 clock_ = 0;
};

}  // namespace heiran::ui
