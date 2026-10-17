#include "heiran/ui/TextureBudgetLedger.hpp"

#include <QtGlobal>
#include <algorithm>

namespace heiran::ui {

TextureBudgetLedger &TextureBudgetLedger::shared() {
  static TextureBudgetLedger ledger;
  static const bool budgetInitialized = [] {
    const int overrideMib = qEnvironmentVariableIntValue("HEIRAN_GPU_TEXTURE_BUDGET_MB");
    if (overrideMib > 0) {
      ledger.budgetBytes_ = static_cast<qint64>(overrideMib) * 1024LL * 1024LL;
    }
    return true;
  }();
  Q_UNUSED(budgetInitialized);
  return ledger;
}

void TextureBudgetLedger::setBudgetBytes(qint64 budgetBytes) {
  budgetBytes_ = std::max(qint64{0}, budgetBytes);
  enforceBudget();
}

void TextureBudgetLedger::registerOwner(OwnerId owner, EvictFunction evict) {
  owners_.insert(owner, std::move(evict));
}

void TextureBudgetLedger::unregisterOwner(OwnerId owner) {
  clearOwner(owner);
  owners_.remove(owner);
}

void TextureBudgetLedger::clearOwner(OwnerId owner) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it.key().owner == owner) {
      usedBytes_ -= it->bytes;
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
  currentKeys_.remove(owner);
}

void TextureBudgetLedger::markUsed(OwnerId owner, const QString &key, qint64 bytes) {
  const Key entryKey{owner, key};
  const auto existing = entries_.find(entryKey);
  if (existing != entries_.end()) {
    existing->lastUsed = ++clock_;
  } else {
    entries_.insert(entryKey, {.bytes = bytes, .lastUsed = ++clock_});
    usedBytes_ += bytes;
  }
  currentKeys_.insert(owner, key);
  enforceBudget();
}

void TextureBudgetLedger::enforceBudget() {
  while (usedBytes_ > budgetBytes_) {
    const Key *victim = nullptr;
    quint64 oldest = 0;
    for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
      const auto current = currentKeys_.constFind(it.key().owner);
      if (current != currentKeys_.constEnd() && current.value() == it.key().key) {
        continue;
      }
      if (victim == nullptr || it->lastUsed < oldest) {
        victim = &it.key();
        oldest = it->lastUsed;
      }
    }
    // 残っているのが描いているものだけなら、それ以上は捨てられない。
    if (victim == nullptr) {
      return;
    }

    const Key evicted = *victim;
    usedBytes_ -= entries_.value(evicted).bytes;
    entries_.remove(evicted);
    if (const auto owner = owners_.constFind(evicted.owner); owner != owners_.constEnd()) {
      owner.value()(evicted.key);
    }
  }
}

}  // namespace heiran::ui
