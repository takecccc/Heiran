#include "heiran/infra/HrnConfigWriter.hpp"

#include "heiran/domain/ChannelMode.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextStream>

namespace heiran::infra {

namespace {

QString toJson5String(const QString &value) {
  QString escaped = value;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  return QString("\"%1\"").arg(escaped);
}

QString relativeOrOriginalPath(const QString &baseDirectory, const QString &path) {
  QFileInfo fileInfo(path);
  if (!fileInfo.isAbsolute() || baseDirectory.isEmpty()) {
    return path;
  }

  return QDir(baseDirectory).relativeFilePath(path);
}

void writeIndentedLine(QTextStream &stream, int indentLevel, const QString &line) {
  stream << QString(indentLevel * 2, ' ') << line << '\n';
}

}  // namespace

HrnSaveResult HrnConfigWriter::saveToFile(const heiran::domain::SessionDocument &document,
                                          const QString &filePath) const {
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    return {.ok = false, .errorMessage = "Could not open file for writing."};
  }

  QTextStream stream(&file);
  stream.setEncoding(QStringConverter::Utf8);

  const QString outputDirectory = QFileInfo(filePath).absolutePath();

  stream << "{\n";
  writeIndentedLine(stream, 1, QString("format_version: %1,").arg(document.formatVersion));
  writeIndentedLine(stream, 1, "defaults: {");
  writeIndentedLine(stream, 2, "scale: 1.0,");
  writeIndentedLine(stream, 2, "sync: true,");
  writeIndentedLine(stream, 2, "label_visible: true,");
  writeIndentedLine(stream, 2, "honor_exif_orientation: true,");
  writeIndentedLine(stream, 1, "},");
  writeIndentedLine(stream, 1, "viewer: {");
  writeIndentedLine(stream, 2, "layout: {");
  writeIndentedLine(stream, 3, QString("rows: %1,").arg(document.viewer.layout.rows));
  writeIndentedLine(stream, 3, QString("cols: %1,").arg(document.viewer.layout.cols));
  writeIndentedLine(stream, 3,
                    QString("order: %1,").arg(toJson5String(document.viewer.layout.order)));
  writeIndentedLine(stream, 2, "},");
  writeIndentedLine(stream, 2,
                    QString("page_sort: %1,").arg(toJson5String(document.viewer.pageSort)));
  writeIndentedLine(stream, 1, "},");

  writeIndentedLine(stream, 1, "images: [");
  for (int index = 0; index < document.images.size(); ++index) {
    const auto &imageSet = document.images.at(index);
    writeIndentedLine(stream, 2, "{");
    writeIndentedLine(stream, 3, QString("name: %1,").arg(toJson5String(imageSet.name)));
    writeIndentedLine(stream, 3, QString("label: %1,").arg(toJson5String(imageSet.label)));
    writeIndentedLine(
        stream, 3,
        QString("path: %1,")
            .arg(toJson5String(relativeOrOriginalPath(outputDirectory, imageSet.path))));
    writeIndentedLine(stream, 3, QString("scale: %1,").arg(imageSet.scale, 0, 'f', 6));
    writeIndentedLine(stream, 3, QString("page_offset: %1,").arg(imageSet.pageOffset));
    writeIndentedLine(stream, 3,
                      QString("label_visible: %1,").arg(imageSet.labelVisible ? "true" : "false"));
    writeIndentedLine(stream, 3,
                      QString("honor_exif_orientation: %1,")
                          .arg(imageSet.honorExifOrientation ? "true" : "false"));
    // 指定が無い場合は書かない。書くと「auto ではない」ように見えてしまう。
    if (!imageSet.colorSpaceName.isEmpty()) {
      writeIndentedLine(stream, 3,
                        QString("color_space: %1,").arg(toJson5String(imageSet.colorSpaceName)));
    }
    if (!imageSet.iccProfilePath.isEmpty()) {
      writeIndentedLine(stream, 3,
                        QString("icc_profile: %1,")
                            .arg(toJson5String(
                                relativeOrOriginalPath(outputDirectory, imageSet.iccProfilePath))));
    }
    writeIndentedLine(stream, 2, index + 1 < document.images.size() ? "}," : "}");
  }
  writeIndentedLine(stream, 1, "],");

  writeIndentedLine(stream, 1, "cells: [");
  for (int index = 0; index < document.cells.size(); ++index) {
    const auto &cell = document.cells.at(index);
    writeIndentedLine(stream, 2, "{");
    writeIndentedLine(stream, 3, QString("image: %1,").arg(cell.imageIndex));
    writeIndentedLine(stream, 3, QString("sync: %1,").arg(cell.sync ? "true" : "false"));
    writeIndentedLine(stream, 3, QString("scale: %1,").arg(cell.scale, 0, 'f', 6));
    writeIndentedLine(stream, 3, QString("gamma: %1,").arg(cell.gamma, 0, 'f', 6));
    writeIndentedLine(stream, 3, QString("rotation: %1,").arg(cell.rotation));
    writeIndentedLine(stream, 3, QString("flip_x: %1,").arg(cell.flipX ? "true" : "false"));
    writeIndentedLine(stream, 3, QString("flip_y: %1,").arg(cell.flipY ? "true" : "false"));
    writeIndentedLine(stream, 3, QString("invert: %1,").arg(cell.invert ? "true" : "false"));
    writeIndentedLine(
        stream, 3,
        QString("ch: %1,").arg(toJson5String(heiran::domain::toChannelModeString(cell.channel))));
    if (cell.diff) {
      const auto &diff = *cell.diff;
      writeIndentedLine(stream, 3, "diff: {");
      writeIndentedLine(stream, 4, QString("base: %1,").arg(diff.baseCell));
      if (diff.targetCell >= 0) {
        writeIndentedLine(stream, 4, QString("target: %1,").arg(diff.targetCell));
      }
      writeIndentedLine(
          stream, 4,
          QString("mode: %1,")
              .arg(toJson5String(heiran::domain::toDiffModeString(diff.settings.mode))));
      writeIndentedLine(
          stream, 4,
          QString("view: %1,")
              .arg(toJson5String(heiran::domain::toDiffViewModeString(diff.settings.view))));
      writeIndentedLine(stream, 4, QString("gain: %1,").arg(diff.settings.gain, 0, 'f', 6));
      writeIndentedLine(stream, 4,
                        QString("threshold: %1,").arg(diff.settings.threshold, 0, 'f', 6));
      writeIndentedLine(stream, 4,
                        QString("auto_range: %1,").arg(diff.settings.autoRange ? "true" : "false"));
      writeIndentedLine(
          stream, 4,
          QString("size_match: %1,")
              .arg(toJson5String(heiran::domain::toDiffSizeMatchString(diff.settings.sizeMatch))));
      if (diff.settings.mode == heiran::domain::DiffMode::Wipe) {
        writeIndentedLine(stream, 4,
                          QString("wipe_direction: %1,")
                              .arg(toJson5String(heiran::domain::toWipeOrientationString(
                                  diff.settings.wipeOrientation))));
        writeIndentedLine(stream, 4,
                          QString("wipe_position: %1,").arg(diff.settings.wipePosition, 0, 'f', 6));
      }
      writeIndentedLine(stream, 3, "},");
    }
    writeIndentedLine(stream, 2, index + 1 < document.cells.size() ? "}," : "}");
  }
  writeIndentedLine(stream, 1, "]");
  stream << "}\n";

  return {.ok = stream.status() == QTextStream::Ok,
          .errorMessage = stream.status() == QTextStream::Ok
                              ? QString()
                              : QString("Failed while writing .hrn file.")};
}

}  // namespace heiran::infra
