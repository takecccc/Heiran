#pragma once

#include <QString>

namespace heiran::domain {

enum class ChannelMode {
  Color,
  Gray,
  Red,
  Green,
  Blue,
  Alpha,
};

inline QString toChannelModeString(ChannelMode channelMode) {
  switch (channelMode) {
    case ChannelMode::Color:
      return "color";
    case ChannelMode::Gray:
      return "gray";
    case ChannelMode::Red:
      return "red";
    case ChannelMode::Green:
      return "green";
    case ChannelMode::Blue:
      return "blue";
    case ChannelMode::Alpha:
      return "alpha";
  }

  return "color";
}

inline ChannelMode channelModeFromString(const QString &value) {
  if (value == "gray") {
    return ChannelMode::Gray;
  }
  if (value == "red") {
    return ChannelMode::Red;
  }
  if (value == "green") {
    return ChannelMode::Green;
  }
  if (value == "blue") {
    return ChannelMode::Blue;
  }
  if (value == "alpha") {
    return ChannelMode::Alpha;
  }
  return ChannelMode::Color;
}

}  // namespace heiran::domain
