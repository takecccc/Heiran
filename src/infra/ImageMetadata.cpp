#include "heiran/infra/ImageMetadata.hpp"

#include "heiran/infra/ExifReader.hpp"
#if defined(HEIRAN_HAVE_OPENEXR)
#include "heiran/infra/ExrDecoder.hpp"
#endif

#include <QFile>

namespace heiran::infra {

QVector<MetadataEntry> readFileMetadata(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }

  const QByteArray magic = file.peek(4);
#if defined(HEIRAN_HAVE_OPENEXR)
  // EXR は、OpenEXR が読む属性 (ヘッダー) を使う。
  if (looksLikeExr(magic)) {
    file.close();
    return readExrMetadata(filePath);
  }
#endif

  // TIFF は、ファイルそのものが EXIF と同じ IFD の並び。それ以外 (JPEG、PNG、WebP)
  // は、入れ物の中のブロック。
  const bool isTiff = (magic.startsWith("II") && magic.at(2) == '*' && magic.at(3) == 0) ||
                      (magic.startsWith("MM") && magic.at(2) == 0 && magic.at(3) == '*');
  if (isTiff) {
    return readExifMetadata(file);
  }
  const QByteArray block = extractExifBlock(file);
  return block.isEmpty() ? QVector<MetadataEntry>() : readExifMetadata(block);
}

}  // namespace heiran::infra
