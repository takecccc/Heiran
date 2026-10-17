#include "heiran/infra/HdrDecoder.hpp"

#include <cmath>
#include <vector>

namespace heiran::infra {

namespace {

struct Cursor {
  const uchar *data = nullptr;
  qsizetype size = 0;
  qsizetype position = 0;

  bool has(qsizetype count) const { return position + count <= size; }
  int next() { return data[position++]; }
};

// 1 行 (改行まで) を読む。改行は含めない。末尾まで来たら false。
bool readLine(Cursor *cursor, QByteArray *line) {
  line->clear();
  while (cursor->position < cursor->size) {
    const char character = static_cast<char>(cursor->next());
    if (character == '\n') {
      return true;
    }
    line->append(character);
  }
  return false;
}

// 新しい RLE の 1 行 (先頭が 2 2 幅の上位 幅の下位)。4 つの平面を順に読む。
bool readNewRleScanline(Cursor *cursor, int width, std::vector<uchar> *rgbe) {
  for (int plane = 0; plane < 4; ++plane) {
    int column = 0;
    while (column < width) {
      if (!cursor->has(1)) {
        return false;
      }
      int count = cursor->next();
      if (count > 128) {
        // 同じ値の繰り返し。
        count -= 128;
        if (!cursor->has(1) || column + count > width) {
          return false;
        }
        const uchar value = static_cast<uchar>(cursor->next());
        for (int index = 0; index < count; ++index) {
          (*rgbe)[static_cast<size_t>(column++) * 4 + plane] = value;
        }
      } else {
        // そのままの並び。
        if (count == 0 || !cursor->has(count) || column + count > width) {
          return false;
        }
        for (int index = 0; index < count; ++index) {
          (*rgbe)[static_cast<size_t>(column++) * 4 + plane] = static_cast<uchar>(cursor->next());
        }
      }
    }
  }
  return true;
}

// 平らな行、または古い RLE の行。古い RLE は `1 1 1 回数` が、直前の画素の繰り返しを表す
// (回数は上の桁へ積み上がる)。
bool readFlatOrOldRleScanline(Cursor *cursor, int width, std::vector<uchar> *rgbe) {
  int column = 0;
  int shift = 0;
  while (column < width) {
    if (!cursor->has(4)) {
      return false;
    }
    const int red = cursor->next();
    const int green = cursor->next();
    const int blue = cursor->next();
    const int exponent = cursor->next();
    if (red == 1 && green == 1 && blue == 1) {
      if (column == 0) {
        return false;  // 繰り返す直前の画素が無い。
      }
      const int repeat = exponent << shift;
      if (column + repeat > width) {
        return false;
      }
      const size_t previous = static_cast<size_t>(column - 1) * 4;
      for (int index = 0; index < repeat; ++index) {
        for (int component = 0; component < 4; ++component) {
          (*rgbe)[static_cast<size_t>(column) * 4 + component] = (*rgbe)[previous + component];
        }
        ++column;
      }
      shift += 8;
    } else {
      (*rgbe)[static_cast<size_t>(column) * 4 + 0] = static_cast<uchar>(red);
      (*rgbe)[static_cast<size_t>(column) * 4 + 1] = static_cast<uchar>(green);
      (*rgbe)[static_cast<size_t>(column) * 4 + 2] = static_cast<uchar>(blue);
      (*rgbe)[static_cast<size_t>(column) * 4 + 3] = static_cast<uchar>(exponent);
      ++column;
      shift = 0;
    }
  }
  return true;
}

}  // namespace

RawImage decodeRadianceHdr(const QByteArray &fileContents) {
  Cursor cursor{.data = reinterpret_cast<const uchar *>(fileContents.constData()),
                .size = fileContents.size()};

  QByteArray line;
  if (!readLine(&cursor, &line) || !(line.startsWith("#?RADIANCE") || line.startsWith("#?RGBE"))) {
    return rawImageFailure(QStringLiteral("Not a Radiance HDR file"));
  }

  // 空行までがヘッダー。形式の指定を探す。
  bool formatIsRgbe = true;
  bool sawBlankLine = false;
  while (readLine(&cursor, &line)) {
    if (line.trimmed().isEmpty()) {
      sawBlankLine = true;
      break;
    }
    if (line.startsWith("FORMAT=")) {
      formatIsRgbe = line.mid(7).trimmed() == "32-bit_rle_rgbe";
    }
  }
  if (!sawBlankLine) {
    return rawImageFailure(QStringLiteral("The header is incomplete"));
  }
  if (!formatIsRgbe) {
    return rawImageFailure(QStringLiteral("Only 32-bit_rle_rgbe is supported"));
  }

  // 解像度の行。`-Y 高さ +X 幅` が標準。
  if (!readLine(&cursor, &line)) {
    return rawImageFailure(QStringLiteral("The resolution line is missing"));
  }
  const QList<QByteArray> fields = line.trimmed().split(' ');
  if (fields.size() != 4) {
    return rawImageFailure(QStringLiteral("The resolution line is malformed"));
  }
  const QByteArray yAxis = fields.at(0);
  const QByteArray xAxis = fields.at(2);
  if (!((yAxis == "-Y" || yAxis == "+Y") && (xAxis == "+X" || xAxis == "-X"))) {
    return rawImageFailure(QStringLiteral("This orientation is not supported"));
  }
  bool heightOk = false;
  bool widthOk = false;
  const int height = fields.at(1).toInt(&heightOk);
  const int width = fields.at(3).toInt(&widthOk);
  if (!heightOk || !widthOk || width <= 0 || height <= 0 ||
      static_cast<qint64>(width) * height > kMaxRawImagePixels) {
    return rawImageFailure(QStringLiteral("The image size is invalid"));
  }
  const bool flipVertically = yAxis == "+Y";
  const bool flipHorizontally = xAxis == "-X";

  RawImage image;
  image.width = width;
  image.height = height;
  image.channels = 3;
  image.sampleType = PixelSampleType::Float32;
  image.pixels.resize(static_cast<qsizetype>(width) * height * 3 *
                      static_cast<qsizetype>(sizeof(float)));
  auto *output = reinterpret_cast<float *>(image.pixels.data());

  std::vector<uchar> rgbe(static_cast<size_t>(width) * 4);
  for (int row = 0; row < height; ++row) {
    bool rowOk = false;
    // 新しい RLE の印 (2 2 幅の上位 幅の下位)。幅が 8 以上 32767 以下のときだけ使われる。
    if (width >= 8 && width <= 32767 && cursor.has(4) && cursor.data[cursor.position] == 2 &&
        cursor.data[cursor.position + 1] == 2 && (cursor.data[cursor.position + 2] & 0x80) == 0) {
      const int declaredWidth =
          (cursor.data[cursor.position + 2] << 8) | cursor.data[cursor.position + 3];
      if (declaredWidth != width) {
        return rawImageFailure(QStringLiteral("A scanline has the wrong width"));
      }
      cursor.position += 4;
      rowOk = readNewRleScanline(&cursor, width, &rgbe);
    } else {
      rowOk = readFlatOrOldRleScanline(&cursor, width, &rgbe);
    }
    if (!rowOk) {
      return rawImageFailure(QStringLiteral("The pixel data is truncated or corrupt"));
    }

    const int destinationRow = flipVertically ? height - 1 - row : row;
    float *destination = output + static_cast<size_t>(destinationRow) * width * 3;
    for (int column = 0; column < width; ++column) {
      const int sourceColumn = flipHorizontally ? width - 1 - column : column;
      const uchar *pixel = rgbe.data() + static_cast<size_t>(sourceColumn) * 4;
      float *target = destination + static_cast<size_t>(column) * 3;
      if (pixel[3] == 0) {
        target[0] = target[1] = target[2] = 0.0F;
      } else {
        // 指数が共通の浮動小数。仮数は 8bit の整数で、指数は 128 の偏りを持ち、さらに 8bit
        // 分を引く。
        const float scale = std::ldexp(1.0F, static_cast<int>(pixel[3]) - 136);
        target[0] = static_cast<float>(pixel[0]) * scale;
        target[1] = static_cast<float>(pixel[1]) * scale;
        target[2] = static_cast<float>(pixel[2]) * scale;
      }
    }
  }

  image.ok = true;
  return image;
}

}  // namespace heiran::infra
