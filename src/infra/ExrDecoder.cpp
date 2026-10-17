#include "heiran/infra/ExrDecoder.hpp"

#include <ImfBoxAttribute.h>
#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfChromaticitiesAttribute.h>
#include <ImfCompressionAttribute.h>
#include <ImfDoubleAttribute.h>
#include <ImfFloatAttribute.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfIntAttribute.h>
#include <ImfMultiPartInputFile.h>
#include <ImfPartType.h>
#include <ImfStandardAttributes.h>
#include <ImfStringAttribute.h>
#include <ImfStringVectorAttribute.h>
#include <ImfThreading.h>
#include <ImfTiledInputPart.h>
#include <ImfVecAttribute.h>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <algorithm>
#include <cstring>
#include <vector>

namespace heiran::infra {

namespace {

// Qt のファイルを OpenEXR の入力ストリームにする。ファイル名を狭い文字で渡す口
// (Windows で日本語のパスが開けない) を使わずに済む。
// OpenEXR は、複数のスレッドから読むことがある。位置と読みを、1 つの鍵の下で行う。
class QtFileStream final : public Imf::IStream {
public:
  explicit QtFileStream(const QString &filePath) : Imf::IStream(filePath.toStdString().c_str()) {
    file_.setFileName(filePath);
    opened_ = file_.open(QIODevice::ReadOnly);
  }

  bool opened() const { return opened_; }

  bool isMemoryMapped() const override { return false; }

  bool read(char c[], int n) override {
    const QMutexLocker locker(&mutex_);
    const qint64 count = file_.read(c, n);
    if (count != n) {
      throw Iex::InputExc("Unexpected end of file");
    }
    return file_.pos() < file_.size();
  }

  uint64_t tellg() override {
    const QMutexLocker locker(&mutex_);
    return static_cast<uint64_t>(file_.pos());
  }

  void seekg(uint64_t pos) override {
    const QMutexLocker locker(&mutex_);
    file_.seek(static_cast<qint64>(pos));
  }

  void clear() override {}

private:
  QFile file_;
  bool opened_ = false;
  QMutex mutex_;
};

ExrResult failure(const QString &message) {
  ExrResult result;
  result.raw = rawImageFailure(message);
  return result;
}

// 選んだチャンネルの組。
struct ChannelSelection {
  std::vector<std::string> names;  // 出力の順 (R, G, B, A / Y, A / …)
  bool ok = false;
};

bool hasChannel(const Imf::ChannelList &channels, const std::string &name) {
  return channels.findChannel(name.c_str()) != nullptr;
}

// 出力するチャンネルを決める。
ChannelSelection selectChannels(const Imf::ChannelList &channels) {
  ChannelSelection selection;
  const auto make = [&](const std::string &prefix, const std::vector<std::string> &base) {
    std::vector<std::string> names;
    for (const auto &name : base) {
      names.push_back(prefix + name);
    }
    return names;
  };

  // 層 (名前の前半) ごとに、RGB を探す。層が無ければ、接頭辞は空。
  std::vector<std::string> prefixes = {""};
  for (auto it = channels.begin(); it != channels.end(); ++it) {
    const std::string name = it.name();
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
      const std::string prefix = name.substr(0, dot + 1);
      if (std::find(prefixes.begin(), prefixes.end(), prefix) == prefixes.end()) {
        prefixes.push_back(prefix);
      }
    }
  }

  for (const auto &prefix : prefixes) {
    if (hasChannel(channels, prefix + "R") && hasChannel(channels, prefix + "G") &&
        hasChannel(channels, prefix + "B")) {
      selection.names = make(prefix, {"R", "G", "B"});
      if (hasChannel(channels, prefix + "A")) {
        selection.names.push_back(prefix + "A");
      }
      selection.ok = true;
      return selection;
    }
  }
  for (const auto &prefix : prefixes) {
    if (hasChannel(channels, prefix + "Y") && !hasChannel(channels, prefix + "RY") &&
        !hasChannel(channels, prefix + "BY")) {
      selection.names = make(prefix, {"Y"});
      if (hasChannel(channels, prefix + "A")) {
        selection.names.push_back(prefix + "A");
      }
      selection.ok = true;
      return selection;
    }
  }

  // どれも無ければ、先頭の 4 つまで。
  for (auto it = channels.begin(); it != channels.end() && selection.names.size() < 4; ++it) {
    selection.names.push_back(it.name());
  }
  selection.ok = !selection.names.empty();
  return selection;
}

// OpenEXR の例外 (Iex::BaseExc は std::exception から派生している) を失敗へ変える。
template <typename Function> ExrResult guarded(Function function) {
  try {
    return function();
  } catch (const std::exception &error) {
    return failure(QString::fromUtf8(error.what()));
  } catch (...) {
    return failure(QStringLiteral("Unknown OpenEXR error"));
  }
}

// OpenEXR の内部の並列度。既定は 0 (並列にしない)。画素を読む速さに効くため、1 度だけ決める。
void configureThreads() {
  static const bool configured = []() {
    Imf::setGlobalThreadCount(std::clamp(QThread::idealThreadCount(), 1, 16));
    return true;
  }();
  Q_UNUSED(configured);
}

}  // namespace

ExrResult decodeExr(const QString &filePath, int partIndex) {
  configureThreads();

  return guarded([&]() -> ExrResult {
    QtFileStream stream(filePath);
    if (!stream.opened()) {
      return failure(QStringLiteral("Failed to open the file"));
    }

    Imf::MultiPartInputFile file(stream);
    const int partCount = file.parts();
    if (partIndex < 0 || partIndex >= partCount) {
      return failure(QStringLiteral("The file has no such part"));
    }

    const Imf::Header &header = file.header(partIndex);
    const std::string type = header.hasType() ? header.type() : std::string("scanlineimage");
    const bool tiled = type == Imf::TILEDIMAGE;
    if (type != Imf::SCANLINEIMAGE && !tiled) {
      return failure(QStringLiteral("Deep or unsupported EXR part type"));
    }

    const Imath::Box2i dataWindow = header.dataWindow();
    const int width = dataWindow.max.x - dataWindow.min.x + 1;
    const int height = dataWindow.max.y - dataWindow.min.y + 1;
    if (width <= 0 || height <= 0 || static_cast<qint64>(width) * height > kMaxRawImagePixels) {
      return failure(QStringLiteral("The image size is invalid"));
    }

    const Imf::ChannelList &channels = header.channels();
    const ChannelSelection selection = selectChannels(channels);
    if (!selection.ok) {
      return failure(QStringLiteral("The part has no channels"));
    }

    // 精度の決定。全部が half なら half、それ以外は float。標本化 (間引き) は対応しない。
    bool allHalf = true;
    for (const auto &name : selection.names) {
      const Imf::Channel *channel = channels.findChannel(name.c_str());
      if (channel->xSampling != 1 || channel->ySampling != 1) {
        return failure(QStringLiteral("Subsampled EXR channels are not supported"));
      }
      allHalf = allHalf && channel->type == Imf::HALF;
    }
    const int channelCount = static_cast<int>(selection.names.size());
    const PixelSampleType sampleType =
        allHalf ? PixelSampleType::Float16 : PixelSampleType::Float32;
    const size_t sampleBytes = allHalf ? sizeof(quint16) : sizeof(float);

    ExrResult result;
    result.partCount = partCount;
    RawImage &raw = result.raw;
    raw.width = width;
    raw.height = height;
    raw.channels = channelCount;
    raw.sampleType = sampleType;
    raw.pixels.resize(static_cast<qsizetype>(width) * height * channelCount *
                      static_cast<qsizetype>(sampleBytes));
    if (raw.pixels.size() != static_cast<qsizetype>(width) * height * channelCount *
                                 static_cast<qsizetype>(sampleBytes)) {
      return failure(QStringLiteral("Failed to allocate the image"));
    }

    // 絶対座標で引けるよう、起点を (データウィンドウの左上) 分だけ戻した基準点から並べる。
    const size_t pixelStride = static_cast<size_t>(channelCount) * sampleBytes;
    const size_t rowStride = pixelStride * static_cast<size_t>(width);
    char *origin = raw.pixels.data() - static_cast<ptrdiff_t>(dataWindow.min.x) * pixelStride -
                   static_cast<ptrdiff_t>(dataWindow.min.y) * rowStride;

    Imf::FrameBuffer frameBuffer;
    for (int index = 0; index < channelCount; ++index) {
      frameBuffer.insert(selection.names[index].c_str(),
                         Imf::Slice(allHalf ? Imf::HALF : Imf::FLOAT,
                                    origin + static_cast<size_t>(index) * sampleBytes, pixelStride,
                                    rowStride, 1, 1));
    }

    if (tiled) {
      Imf::TiledInputPart part(file, partIndex);
      part.setFrameBuffer(frameBuffer);
      // ミップマップやリップマップは、最も大きい段 (0, 0) だけ。
      part.readTiles(0, part.numXTiles(0) - 1, 0, part.numYTiles(0) - 1, 0);
    } else {
      Imf::InputPart part(file, partIndex);
      part.setFrameBuffer(frameBuffer);
      part.readPixels(dataWindow.min.y, dataWindow.max.y);
    }

    // 原色の申告。EXR は線形光を入れる約束なので、伝達関数は線形。
    if (Imf::hasChromaticities(header)) {
      const Imf::Chromaticities &chromaticities = Imf::chromaticities(header);
      QColorSpace::PrimaryPoints points;
      points.redPoint = QPointF(chromaticities.red.x, chromaticities.red.y);
      points.greenPoint = QPointF(chromaticities.green.x, chromaticities.green.y);
      points.bluePoint = QPointF(chromaticities.blue.x, chromaticities.blue.y);
      points.whitePoint = QPointF(chromaticities.white.x, chromaticities.white.y);
      if (points.isValid()) {
        const QColorSpace space(points, QColorSpace::TransferFunction::Linear);
        if (space.isValid()) {
          result.declaredColorSpace = space;
        }
      }
    }

    raw.ok = true;
    return result;
  });
}

namespace {

QString compressionName(Imf::Compression compression) {
  switch (compression) {
    case Imf::NO_COMPRESSION:
      return "none";
    case Imf::RLE_COMPRESSION:
      return "rle";
    case Imf::ZIPS_COMPRESSION:
      return "zips";
    case Imf::ZIP_COMPRESSION:
      return "zip";
    case Imf::PIZ_COMPRESSION:
      return "piz";
    case Imf::PXR24_COMPRESSION:
      return "pxr24";
    case Imf::B44_COMPRESSION:
      return "b44";
    case Imf::B44A_COMPRESSION:
      return "b44a";
    case Imf::DWAA_COMPRESSION:
      return "dwaa";
    case Imf::DWAB_COMPRESSION:
      return "dwab";
    default:
      return QString("compression %1").arg(static_cast<int>(compression));
  }
}

QString pixelTypeName(Imf::PixelType type) {
  switch (type) {
    case Imf::UINT:
      return "uint";
    case Imf::HALF:
      return "half";
    case Imf::FLOAT:
      return "float";
    default:
      return "?";
  }
}

// 属性の値を、読める文字にする。扱わない型は空 (呼び側が捨てる)。
QString attributeText(const Imf::Attribute &attribute) {
  if (const auto *value = dynamic_cast<const Imf::IntAttribute *>(&attribute)) {
    return QString::number(value->value());
  }
  if (const auto *value = dynamic_cast<const Imf::FloatAttribute *>(&attribute)) {
    return QString::number(value->value(), 'g', 8);
  }
  if (const auto *value = dynamic_cast<const Imf::DoubleAttribute *>(&attribute)) {
    return QString::number(value->value(), 'g', 12);
  }
  if (const auto *value = dynamic_cast<const Imf::StringAttribute *>(&attribute)) {
    return QString::fromStdString(value->value());
  }
  if (const auto *value = dynamic_cast<const Imf::StringVectorAttribute *>(&attribute)) {
    QStringList parts;
    for (const auto &item : value->value()) {
      parts << QString::fromStdString(item);
    }
    return parts.join(", ");
  }
  if (const auto *value = dynamic_cast<const Imf::V2iAttribute *>(&attribute)) {
    return QString("%1, %2").arg(value->value().x).arg(value->value().y);
  }
  if (const auto *value = dynamic_cast<const Imf::V2fAttribute *>(&attribute)) {
    return QString("%1, %2").arg(value->value().x, 0, 'g', 6).arg(value->value().y, 0, 'g', 6);
  }
  if (const auto *value = dynamic_cast<const Imf::V3fAttribute *>(&attribute)) {
    return QString("%1, %2, %3")
        .arg(value->value().x, 0, 'g', 6)
        .arg(value->value().y, 0, 'g', 6)
        .arg(value->value().z, 0, 'g', 6);
  }
  if (const auto *value = dynamic_cast<const Imf::Box2iAttribute *>(&attribute)) {
    const auto &box = value->value();
    return QString("(%1, %2) - (%3, %4)")
        .arg(box.min.x)
        .arg(box.min.y)
        .arg(box.max.x)
        .arg(box.max.y);
  }
  if (const auto *value = dynamic_cast<const Imf::Box2fAttribute *>(&attribute)) {
    const auto &box = value->value();
    return QString("(%1, %2) - (%3, %4)")
        .arg(box.min.x, 0, 'g', 6)
        .arg(box.min.y, 0, 'g', 6)
        .arg(box.max.x, 0, 'g', 6)
        .arg(box.max.y, 0, 'g', 6);
  }
  if (const auto *value = dynamic_cast<const Imf::ChromaticitiesAttribute *>(&attribute)) {
    const auto &c = value->value();
    return QString("R(%1, %2) G(%3, %4) B(%5, %6) W(%7, %8)")
        .arg(c.red.x, 0, 'g', 5)
        .arg(c.red.y, 0, 'g', 5)
        .arg(c.green.x, 0, 'g', 5)
        .arg(c.green.y, 0, 'g', 5)
        .arg(c.blue.x, 0, 'g', 5)
        .arg(c.blue.y, 0, 'g', 5)
        .arg(c.white.x, 0, 'g', 5)
        .arg(c.white.y, 0, 'g', 5);
  }
  if (const auto *value = dynamic_cast<const Imf::CompressionAttribute *>(&attribute)) {
    return compressionName(value->value());
  }
  return {};
}

}  // namespace

bool looksLikeExr(const QByteArray &firstBytes) {
  // マジックナンバー 20000630 (0x01312f76) を、リトルエンディアンで。
  return firstBytes.size() >= 4 && static_cast<uchar>(firstBytes[0]) == 0x76 &&
         static_cast<uchar>(firstBytes[1]) == 0x2f && static_cast<uchar>(firstBytes[2]) == 0x31 &&
         static_cast<uchar>(firstBytes[3]) == 0x01;
}

QVector<MetadataEntry> readExrMetadata(const QString &filePath) {
  QVector<MetadataEntry> entries;
  try {
    QtFileStream stream(filePath);
    if (!stream.opened()) {
      return entries;
    }
    Imf::MultiPartInputFile file(stream);
    const Imf::Header &header = file.header(0);

    if (file.parts() > 1) {
      entries.push_back({"EXR", "Parts", QString::number(file.parts())});
    }

    // チャンネルの一覧 (名前: 型)。
    QStringList channelDescriptions;
    for (auto it = header.channels().begin(); it != header.channels().end(); ++it) {
      channelDescriptions << QString("%1: %2").arg(it.name(), pixelTypeName(it.channel().type));
    }
    entries.push_back({"EXR", "Channels", channelDescriptions.join(", ")});

    for (auto it = header.begin(); it != header.end(); ++it) {
      const QString name = QString::fromUtf8(it.name());
      // チャンネルは上の一覧、プレビューは画像なので、属性としては出さない。
      if (name == "channels" || name == "preview") {
        continue;
      }
      const QString text = attributeText(it.attribute());
      if (!text.isEmpty()) {
        entries.push_back({"EXR", name, text});
      }
    }
  } catch (...) {
    return {};
  }
  return entries;
}

}  // namespace heiran::infra
