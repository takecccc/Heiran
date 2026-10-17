#pragma once

#include "heiran/domain/ChannelMode.hpp"

namespace heiran::ui {

using ChannelMode = heiran::domain::ChannelMode;

inline QString toChannelModeString(ChannelMode channelMode) {
  return heiran::domain::toChannelModeString(channelMode);
}

inline ChannelMode channelModeFromString(const QString &value) {
  return heiran::domain::channelModeFromString(value);
}

}  // namespace heiran::ui
