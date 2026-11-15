#include "heiran/infra/ImageFileDetails.hpp"

#include "heiran/infra/ImageDecoder.hpp"

#include <QFileInfo>
#include <QImageReader>

namespace heiran::infra {

ImageFileDetails readImageFileDetails(const QString &path) {
  ImageFileDetails details;
  if (path.isEmpty()) {
    return details;
  }
  const QFileInfo file(path);
  if (file.exists() && file.isFile()) {
    details.fileSize = file.size();
  }
  QImageReader reader(path);
  details.dimensions = reader.size();
  if (!details.dimensions.isEmpty()) {
    return details;
  }
  const auto decoded = ImageDecoder().decode(path, false, 0, true);
  if (decoded.ok) {
    details.dimensions = decoded.decodedImage.size();
  } else {
    details.error = decoded.errorMessage;
  }
  return details;
}

}  // namespace heiran::infra
