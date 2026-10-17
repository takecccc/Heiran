#include "heiran/infra/ApngDecoder.hpp"

#include <QFile>
#include <array>
#include <cstring>

namespace heiran::infra {

namespace {

constexpr int kSignatureBytes = 8;
constexpr char kSignature[kSignatureBytes] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1A', '\n'};

quint32 readBigEndian32(const uchar *bytes) {
  return (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16) |
         (static_cast<quint32>(bytes[2]) << 8) | static_cast<quint32>(bytes[3]);
}

quint16 readBigEndian16(const uchar *bytes) {
  return static_cast<quint16>((bytes[0] << 8) | bytes[1]);
}

void appendBigEndian32(QByteArray *bytes, quint32 value) {
  bytes->append(static_cast<char>((value >> 24) & 0xFF));
  bytes->append(static_cast<char>((value >> 16) & 0xFF));
  bytes->append(static_cast<char>((value >> 8) & 0xFF));
  bytes->append(static_cast<char>(value & 0xFF));
}

// PNG の CRC-32。
quint32 crc32(quint32 crc, const uchar *data, qsizetype length) {
  static const std::array<quint32, 256> table = []() {
    std::array<quint32, 256> entries{};
    for (quint32 index = 0; index < 256; ++index) {
      quint32 value = index;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
      }
      entries[index] = value;
    }
    return entries;
  }();

  crc = ~crc;
  for (qsizetype index = 0; index < length; ++index) {
    crc = table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

struct Chunk {
  QByteArray type;
  qsizetype start = 0;                                       // 長さの欄の位置
  qsizetype dataOffset = 0;                                  // 中身の位置
  quint32 length = 0;                                        // 中身の長さ
  qsizetype end() const { return dataOffset + length + 4; }  // CRC の後ろ
};

// 先頭のシグネチャの後ろから、チャンクを順に並べる。途中で切れていたら、そこまで。
QVector<Chunk> parseChunks(const QByteArray &bytes) {
  QVector<Chunk> chunks;
  if (bytes.size() < kSignatureBytes || std::memcmp(bytes.constData(), kSignature, 8) != 0) {
    return chunks;
  }

  const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
  qsizetype position = kSignatureBytes;
  while (position + 12 <= bytes.size()) {
    const quint32 length = readBigEndian32(data + position);
    if (position + 12 + static_cast<qsizetype>(length) > bytes.size()) {
      break;
    }
    Chunk chunk;
    chunk.type = bytes.mid(position + 4, 4);
    chunk.start = position;
    chunk.dataOffset = position + 8;
    chunk.length = length;
    chunks.push_back(chunk);
    position = chunk.end();
  }
  return chunks;
}

// fcTL の中身。disposal と blend の値は、共通の合成の値と同じ (APNG の仕様に合わせてある)。
AnimationFrameControl readFrameControl(const uchar *body) {
  AnimationFrameControl control;
  control.width = readBigEndian32(body + 4);
  control.height = readBigEndian32(body + 8);
  control.xOffset = readBigEndian32(body + 12);
  control.yOffset = readBigEndian32(body + 16);
  const quint16 delayNumerator = readBigEndian16(body + 20);
  quint16 delayDenominator = readBigEndian16(body + 22);
  // 分母が 0 なら 100 として扱う (仕様)。
  if (delayDenominator == 0) {
    delayDenominator = 100;
  }
  control.delayMs = static_cast<int>(static_cast<qint64>(delayNumerator) * 1000 / delayDenominator);
  control.disposeOp = body[24];
  control.blendOp = body[25];
  return control;
}

// 1 フレームが持つ画素のデータ。
struct FrameData {
  AnimationFrameControl control;
  // 単独の PNG の IDAT として並べるチャンク (長さ、種類、中身、CRC の全体)。
  QVector<QByteArray> idatChunks;
};

// チャンク 1 つ分のバイト列を組む。
QByteArray makeChunk(const char *type, const uchar *data, qsizetype length) {
  QByteArray chunk;
  appendBigEndian32(&chunk, static_cast<quint32>(length));
  chunk.append(type, 4);
  chunk.append(reinterpret_cast<const char *>(data), length);
  // CRC は種類と中身にかかる。
  quint32 crc = crc32(0, reinterpret_cast<const uchar *>(chunk.constData()) + 4, 4 + length);
  appendBigEndian32(&chunk, crc);
  return chunk;
}

struct ParsedApng {
  bool ok = false;
  QString errorMessage;
  quint32 width = 0;
  quint32 height = 0;
  quint8 bitDepth = 8;
  quint8 colorType = 6;
  quint8 interlace = 0;
  // 単独の PNG に写す、フレームに共通の補助チャンク (PLTE と tRNS)。
  QByteArray sharedChunks;
  QVector<FrameData> frames;
};

ParsedApng parseApng(const QByteArray &bytes) {
  ParsedApng parsed;
  const QVector<Chunk> chunks = parseChunks(bytes);
  if (chunks.isEmpty() || chunks.first().type != "IHDR" || chunks.first().length != 13) {
    parsed.errorMessage = QStringLiteral("Not a valid PNG file");
    return parsed;
  }

  const auto *data = reinterpret_cast<const uchar *>(bytes.constData());
  const uchar *ihdr = data + chunks.first().dataOffset;
  parsed.width = readBigEndian32(ihdr);
  parsed.height = readBigEndian32(ihdr + 4);
  parsed.bitDepth = ihdr[8];
  parsed.colorType = ihdr[9];
  parsed.interlace = ihdr[12];
  if (parsed.width == 0 || parsed.height == 0) {
    parsed.errorMessage = QStringLiteral("Invalid PNG header");
    return parsed;
  }

  bool sawIdat = false;
  // 最初の IDAT 群が、フレームの画素かどうか。IDAT は連続して並ぶため、最初の 1 つで決める。
  bool idatBelongsToFrame = false;
  for (const Chunk &chunk : chunks) {
    const uchar *body = data + chunk.dataOffset;
    if (chunk.type == "PLTE" || chunk.type == "tRNS") {
      // 画素を復号するのに要る。最初の画像データより前にしか置けない。
      if (!sawIdat) {
        parsed.sharedChunks.append(bytes.mid(chunk.start, chunk.end() - chunk.start));
      }
    } else if (chunk.type == "fcTL" && chunk.length >= 26) {
      FrameData frame;
      frame.control = readFrameControl(body);
      parsed.frames.push_back(frame);
    } else if (chunk.type == "IDAT") {
      // 最初の画像データより前に fcTL が無ければ、IDAT は対応していない環境向けの
      // 代わりの画像で、アニメーションのフレームではない。
      if (!sawIdat) {
        idatBelongsToFrame = !parsed.frames.isEmpty();
        sawIdat = true;
      }
      if (idatBelongsToFrame) {
        // 画像データの後ろに fcTL は来ないため、対応するフレームは先頭。
        parsed.frames.first().idatChunks.push_back(
            bytes.mid(chunk.start, chunk.end() - chunk.start));
      }
    } else if (chunk.type == "fdAT" && chunk.length >= 4 && !parsed.frames.isEmpty()) {
      // 先頭の 4 バイトは連番。画素のデータはその後ろ。IDAT として組み直す。
      parsed.frames.last().idatChunks.push_back(makeChunk("IDAT", body + 4, chunk.length - 4));
    }
  }

  if (parsed.frames.isEmpty()) {
    parsed.errorMessage = QStringLiteral("The PNG has no animation frames");
    return parsed;
  }
  parsed.ok = true;
  return parsed;
}

// フレームだけで完結した PNG を組み、RGBA の画像へ復号する。
QImage decodeApngFrame(const ParsedApng &parsed, const FrameData &frame) {
  QByteArray png(kSignature, kSignatureBytes);

  uchar header[13];
  std::memset(header, 0, sizeof(header));
  const auto putBigEndian32 = [&header](int offset, quint32 value) {
    header[offset] = static_cast<uchar>((value >> 24) & 0xFF);
    header[offset + 1] = static_cast<uchar>((value >> 16) & 0xFF);
    header[offset + 2] = static_cast<uchar>((value >> 8) & 0xFF);
    header[offset + 3] = static_cast<uchar>(value & 0xFF);
  };
  putBigEndian32(0, frame.control.width);
  putBigEndian32(4, frame.control.height);
  header[8] = parsed.bitDepth;
  header[9] = parsed.colorType;
  header[12] = parsed.interlace;
  png.append(makeChunk("IHDR", header, sizeof(header)));

  png.append(parsed.sharedChunks);
  for (const QByteArray &idat : frame.idatChunks) {
    png.append(idat);
  }
  png.append(makeChunk("IEND", nullptr, 0));

  QImage image;
  if (!image.loadFromData(png, "PNG") || image.isNull()) {
    return {};
  }
  if (image.size() !=
      QSize(static_cast<int>(frame.control.width), static_cast<int>(frame.control.height))) {
    return {};
  }
  // アルファを掛けない RGBA へ揃える。合成の計算がこの並びを前提にする。
  return image.convertToFormat(QImage::Format_RGBA8888);
}

// APNG のフレームの取り出し方。フレームごとに、それだけで完結した PNG を組み直して復号する。
class ApngSource final : public AnimationSource {
public:
  explicit ApngSource(ParsedApng parsed) : parsed_(std::move(parsed)) {
    for (const auto &frame : parsed_.frames) {
      for (const QByteArray &idat : frame.idatChunks) {
        compressedBytes_ += idat.size();
      }
    }
  }

  int canvasWidth() const override { return static_cast<int>(parsed_.width); }
  int canvasHeight() const override { return static_cast<int>(parsed_.height); }
  int frameCount() const override { return static_cast<int>(parsed_.frames.size()); }
  const AnimationFrameControl &control(int index) const override {
    return parsed_.frames.at(index).control;
  }
  QImage decodeFrame(int index) const override {
    return decodeApngFrame(parsed_, parsed_.frames.at(index));
  }
  qint64 compressedBytes() const override { return compressedBytes_; }

private:
  const ParsedApng parsed_;
  qint64 compressedBytes_ = 0;
};

// ファイルを読んで、フレームの取り出し方を作る。失敗したら null。
std::shared_ptr<const AnimationSource> makeApngSource(const QString &filePath,
                                                      QString *errorMessage) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    if (errorMessage != nullptr) {
      *errorMessage = file.errorString();
    }
    return nullptr;
  }
  const QByteArray bytes = file.readAll();
  file.close();

  ParsedApng parsed = parseApng(bytes);
  if (!parsed.ok) {
    if (errorMessage != nullptr) {
      *errorMessage = parsed.errorMessage;
    }
    return nullptr;
  }
  return std::make_shared<ApngSource>(std::move(parsed));
}

}  // namespace

bool isAnimatedPng(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  char signature[kSignatureBytes];
  if (file.read(signature, kSignatureBytes) != kSignatureBytes ||
      std::memcmp(signature, kSignature, kSignatureBytes) != 0) {
    return false;
  }

  // 画素のデータ (IDAT) より前に acTL があるかを見る。中身は読まず、飛ばす。
  while (true) {
    uchar header[8];
    if (file.read(reinterpret_cast<char *>(header), 8) != 8) {
      return false;
    }
    const quint32 length = readBigEndian32(header);
    if (std::memcmp(header + 4, "acTL", 4) == 0) {
      return true;
    }
    if (std::memcmp(header + 4, "IDAT", 4) == 0 || std::memcmp(header + 4, "IEND", 4) == 0) {
      return false;
    }
    if (!file.seek(file.pos() + length + 4)) {
      return false;
    }
  }
}

ApngInfo probeApng(const QString &filePath) {
  ApngInfo info;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return info;
  }

  char signature[kSignatureBytes];
  if (file.read(signature, kSignatureBytes) != kSignatureBytes ||
      std::memcmp(signature, kSignature, kSignatureBytes) != 0) {
    return info;
  }

  // IHDR は先頭、acTL は最初の IDAT より前。中身の要る 2 つだけを読み、他は飛ばす。
  while (true) {
    uchar header[8];
    if (file.read(reinterpret_cast<char *>(header), 8) != 8) {
      return {};
    }
    const quint32 length = readBigEndian32(header);
    if (std::memcmp(header + 4, "IHDR", 4) == 0 && length >= 8) {
      uchar body[8];
      if (file.read(reinterpret_cast<char *>(body), 8) != 8) {
        return {};
      }
      info.width = static_cast<int>(readBigEndian32(body));
      info.height = static_cast<int>(readBigEndian32(body + 4));
      if (!file.seek(file.pos() + (length - 8) + 4)) {
        return {};
      }
      continue;
    }
    if (std::memcmp(header + 4, "acTL", 4) == 0 && length >= 8) {
      uchar body[8];
      if (file.read(reinterpret_cast<char *>(body), 8) != 8) {
        return {};
      }
      info.frameCount = static_cast<int>(readBigEndian32(body));
      info.animated = info.frameCount > 0 && info.width > 0 && info.height > 0;
      return info;
    }
    if (std::memcmp(header + 4, "IDAT", 4) == 0 || std::memcmp(header + 4, "IEND", 4) == 0) {
      return {};
    }
    if (!file.seek(file.pos() + length + 4)) {
      return {};
    }
  }
}

ApngResult decodeApng(const QString &filePath, qint64 maxBytes, bool firstFrameOnly) {
  QString error;
  const auto source = makeApngSource(filePath, &error);
  if (source == nullptr) {
    return {.errorMessage = error};
  }
  return composeAllFrames(*source, maxBytes, firstFrameOnly);
}

std::shared_ptr<FrameStream> openApngStream(const QString &filePath, QString *errorMessage,
                                            int snapshotInterval) {
  const auto source = makeApngSource(filePath, errorMessage);
  if (source == nullptr) {
    return nullptr;
  }
  return openAnimationStream(source, errorMessage, snapshotInterval);
}

}  // namespace heiran::infra
