#include "heiran/infra/HrnConfigLoader.hpp"

#include "heiran/domain/ChannelMode.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <cctype>

namespace heiran::infra {

namespace {

// 設定項目単位の問題を上限まで集める。上限を超えた分は数えるだけにする。
class IssueCollector {
public:
  void add(HrnIssueSeverity severity, const QString &location, const QString &detail) {
    if (severity == HrnIssueSeverity::Error) {
      hasError_ = true;
    }
    if (issues_.size() >= kMaxHrnIssues) {
      truncated_ = true;
      return;
    }
    issues_.push_back({.severity = severity, .location = location, .detail = detail});
  }

  void addError(const QString &location, const QString &detail) {
    add(HrnIssueSeverity::Error, location, detail);
  }

  void addWarning(const QString &location, const QString &detail) {
    add(HrnIssueSeverity::Warning, location, detail);
  }

  const QVector<HrnIssue> &issues() const { return issues_; }
  bool truncated() const { return truncated_; }
  bool hasError() const { return hasError_; }

private:
  QVector<HrnIssue> issues_;
  bool truncated_ = false;
  bool hasError_ = false;
};

QString summarize(const IssueCollector &collector) {
  int errorCount = 0;
  int warningCount = 0;
  for (const auto &issue : collector.issues()) {
    if (issue.severity == HrnIssueSeverity::Error) {
      ++errorCount;
    } else {
      ++warningCount;
    }
  }

  QStringList parts;
  if (errorCount > 0) {
    parts << QString("%1 error(s)").arg(errorCount);
  }
  if (warningCount > 0) {
    parts << QString("%1 warning(s)").arg(warningCount);
  }
  if (parts.isEmpty()) {
    return {};
  }

  QString summary = QString("Found %1 in the .hrn settings.").arg(parts.join(" and "));
  if (collector.truncated()) {
    summary += QString(" Only the first %1 are listed.").arg(kMaxHrnIssues);
  }
  return summary;
}

// サポートする .hrn の形式バージョン。
constexpr int kSupportedFormatVersion = 1;

// 1 セッションで扱えるセル数の上限。
constexpr int kMaxCells = 16;

int readLayoutExtent(const QJsonObject &object, const char *key, const QString &location,
                     IssueCollector *collector) {
  const auto value = object.value(QString::fromUtf8(key));
  if (value.isUndefined()) {
    return 1;
  }
  if (!value.isDouble()) {
    collector->addError(location, "A number is required.");
    return 1;
  }

  const int extent = value.toInt(1);
  if (extent < 1 || extent > kMaxCells) {
    collector->addError(
        location, QString("Must be between 1 and %1, but was %2.").arg(kMaxCells).arg(extent));
    return 1;
  }
  return extent;
}

double readPositiveDouble(const QJsonObject &object, const char *key, double fallback,
                          const QString &location, IssueCollector *collector) {
  const auto value = object.value(QString::fromUtf8(key));
  if (value.isUndefined()) {
    return fallback;
  }
  if (!value.isDouble()) {
    collector->addWarning(location, QString("A number is required. Using %1.").arg(fallback));
    return fallback;
  }

  const double result = value.toDouble(fallback);
  if (!(result > 0.0)) {
    collector->addWarning(
        location,
        QString("Must be greater than 0, but was %1. Using %2.").arg(result).arg(fallback));
    return fallback;
  }
  return result;
}

QString readEnumeratedString(const QJsonObject &object, const char *key,
                             const QStringList &allowedValues, const QString &fallback,
                             const QString &location, IssueCollector *collector) {
  const auto value = object.value(QString::fromUtf8(key));
  if (value.isUndefined()) {
    return fallback;
  }
  if (!value.isString()) {
    collector->addWarning(location, QString("A string is required. Using '%1'.").arg(fallback));
    return fallback;
  }

  const QString text = value.toString();
  if (!allowedValues.contains(text)) {
    collector->addWarning(location, QString("'%1' is not one of %2. Using '%3'.")
                                        .arg(text, allowedValues.join(", "), fallback));
    return fallback;
  }
  return text;
}

bool readRequiredString(const QJsonObject &object, const char *key, QString *out,
                        const QString &location, IssueCollector *collector) {
  const auto value = object.value(QString::fromUtf8(key));
  if (!value.isString() || value.toString().isEmpty()) {
    collector->addError(QString("%1.%2").arg(location, QString::fromUtf8(key)),
                        "A non-empty string is required.");
    return false;
  }
  *out = value.toString();
  return true;
}

QByteArray stripJson5Comments(const QByteArray &source) {
  QByteArray output;
  output.reserve(source.size());

  bool inDoubleQuote = false;
  bool inSingleQuote = false;
  bool inLineComment = false;
  bool inBlockComment = false;
  bool escaped = false;

  for (int index = 0; index < source.size(); ++index) {
    const char current = source.at(index);
    const char next = index + 1 < source.size() ? source.at(index + 1) : '\0';

    if (inLineComment) {
      if (current == '\n') {
        inLineComment = false;
        output.append(current);
      }
      continue;
    }

    if (inBlockComment) {
      if (current == '*' && next == '/') {
        inBlockComment = false;
        ++index;
      }
      continue;
    }

    if (!inDoubleQuote && !inSingleQuote && current == '/' && next == '/') {
      inLineComment = true;
      ++index;
      continue;
    }

    if (!inDoubleQuote && !inSingleQuote && current == '/' && next == '*') {
      inBlockComment = true;
      ++index;
      continue;
    }

    output.append(current);

    if (escaped) {
      escaped = false;
      continue;
    }

    if ((inDoubleQuote || inSingleQuote) && current == '\\') {
      escaped = true;
      continue;
    }

    if (!inSingleQuote && current == '"') {
      inDoubleQuote = !inDoubleQuote;
      continue;
    }

    if (!inDoubleQuote && current == '\'') {
      inSingleQuote = !inSingleQuote;
    }
  }

  return output;
}

QByteArray normalizeJson5SingleQuotedStrings(const QByteArray &source) {
  QByteArray output;
  output.reserve(source.size());

  bool inDoubleQuote = false;
  bool inSingleQuote = false;
  bool escaped = false;

  for (int index = 0; index < source.size(); ++index) {
    const char current = source.at(index);

    if (inSingleQuote) {
      if (escaped) {
        if (current == '\'') {
          output.append('\'');
        } else if (current == '"') {
          output.append("\\\"");
        } else {
          output.append('\\');
          output.append(current);
        }
        escaped = false;
        continue;
      }

      if (current == '\\') {
        escaped = true;
        continue;
      }

      if (current == '\'') {
        output.append('"');
        inSingleQuote = false;
        continue;
      }

      if (current == '"') {
        output.append("\\\"");
      } else {
        output.append(current);
      }
      continue;
    }

    output.append(current == '\'' && !inDoubleQuote ? '"' : current);

    if (escaped) {
      escaped = false;
      continue;
    }

    if (current == '\\' && inDoubleQuote) {
      escaped = true;
      continue;
    }

    if (current == '"' && !inDoubleQuote) {
      inDoubleQuote = true;
    } else if (current == '"' && inDoubleQuote) {
      inDoubleQuote = false;
    } else if (current == '\'' && !inDoubleQuote) {
      inSingleQuote = true;
    }
  }

  return output;
}

QByteArray quoteJson5UnquotedKeys(const QByteArray &source) {
  QByteArray output;
  output.reserve(source.size() + source.size() / 8);

  bool inDoubleQuote = false;
  bool escaped = false;
  bool expectKey = false;

  auto isKeyStart = [](char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || ch == '$';
  };
  auto isKeyChar = [&](char ch) { return isKeyStart(ch) || (ch >= '0' && ch <= '9') || ch == '-'; };

  for (int index = 0; index < source.size(); ++index) {
    const char current = source.at(index);

    if (inDoubleQuote) {
      output.append(current);
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        inDoubleQuote = false;
      }
      continue;
    }

    if (current == '"') {
      inDoubleQuote = true;
      output.append(current);
      continue;
    }

    if (current == '{' || current == ',') {
      expectKey = true;
      output.append(current);
      continue;
    }

    if (!expectKey) {
      output.append(current);
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(current))) {
      output.append(current);
      continue;
    }

    if (!isKeyStart(current)) {
      expectKey = false;
      output.append(current);
      continue;
    }

    const int keyStart = index;
    int keyEnd = index + 1;
    while (keyEnd < source.size() && isKeyChar(source.at(keyEnd))) {
      ++keyEnd;
    }

    int scan = keyEnd;
    while (scan < source.size() && std::isspace(static_cast<unsigned char>(source.at(scan)))) {
      ++scan;
    }

    if (scan < source.size() && source.at(scan) == ':') {
      output.append('"');
      output.append(source.mid(keyStart, keyEnd - keyStart));
      output.append('"');
      index = keyEnd - 1;
    } else {
      output.append(source.mid(keyStart, keyEnd - keyStart));
      index = keyEnd - 1;
    }
    expectKey = false;
  }

  return output;
}

QByteArray removeJson5TrailingCommas(const QByteArray &source) {
  static const QRegularExpression trailingCommaPattern(R"(,(\s*[}\]]))");
  QString normalized = QString::fromUtf8(source);
  normalized.replace(trailingCommaPattern, "\\1");
  return normalized.toUtf8();
}

QByteArray normalizeJson5Subset(const QByteArray &source) {
  QByteArray normalized = stripJson5Comments(source);
  normalized = normalizeJson5SingleQuotedStrings(normalized);
  normalized = quoteJson5UnquotedKeys(normalized);
  normalized = removeJson5TrailingCommas(normalized);
  return normalized;
}

}  // namespace

QString HrnIssue::toDisplayText() const {
  const QString prefix =
      severity == HrnIssueSeverity::Error ? QStringLiteral("Error") : QStringLiteral("Warning");
  return QString("%1  %2: %3").arg(prefix, location, detail);
}

bool HrnLoadResult::hasErrors() const {
  for (const auto &issue : issues) {
    if (issue.severity == HrnIssueSeverity::Error) {
      return true;
    }
  }
  return false;
}

HrnLoadResult HrnConfigLoader::loadFromFile(const QString &filePath) const {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {.ok = false,
            .errorMessage = "Could not open the file.",
            .issues = {{HrnIssueSeverity::Error, filePath, "The file could not be opened."}}};
  }

  const QByteArray source = file.readAll();
  const QByteArray normalizedSource = normalizeJson5Subset(source);
  QJsonParseError mutableParseError;
  const QJsonDocument document = QJsonDocument::fromJson(normalizedSource, &mutableParseError);
  if (document.isNull()) {
    return {.ok = false,
            .errorMessage = "The .hrn file could not be parsed.",
            .issues = {{HrnIssueSeverity::Error, filePath,
                        QString("Parse error at offset %1.").arg(mutableParseError.offset)}}};
  }

  if (!document.isObject()) {
    return {.ok = false,
            .errorMessage = "The .hrn file could not be parsed.",
            .issues = {{HrnIssueSeverity::Error, filePath, "The root must be an object."}}};
  }

  const QJsonObject root = document.object();
  IssueCollector collector;
  heiran::domain::SessionDocument documentModel;

  // format_version は解釈の前提が変わるため、他の検証より先に確かめる。
  const auto formatVersionValue = root.value("format_version");
  if (formatVersionValue.isUndefined()) {
    documentModel.formatVersion = 1;
  } else if (!formatVersionValue.isDouble()) {
    collector.addError("format_version", "A number is required.");
  } else {
    documentModel.formatVersion = formatVersionValue.toInt(1);
    if (documentModel.formatVersion < 1) {
      collector.addError(
          "format_version",
          QString("Must be 1 or greater, but was %1.").arg(documentModel.formatVersion));
    } else if (documentModel.formatVersion > kSupportedFormatVersion) {
      collector.addError("format_version",
                         QString("Version %1 is newer than the supported version %2.")
                             .arg(documentModel.formatVersion)
                             .arg(kSupportedFormatVersion));
    }
  }

  const QJsonObject viewerObject = root.value("viewer").toObject();
  const QJsonObject layoutObject = viewerObject.value("layout").toObject();
  documentModel.viewer.layout.rows =
      readLayoutExtent(layoutObject, "rows", "viewer.layout.rows", &collector);
  documentModel.viewer.layout.cols =
      readLayoutExtent(layoutObject, "cols", "viewer.layout.cols", &collector);
  const int cellCapacity = documentModel.viewer.layout.rows * documentModel.viewer.layout.cols;
  if (cellCapacity > kMaxCells) {
    collector.addError(
        "viewer.layout",
        QString("rows x cols must be %1 or fewer, but was %2.").arg(kMaxCells).arg(cellCapacity));
  }

  documentModel.viewer.layout.order =
      readEnumeratedString(layoutObject, "order", {"row-major", "column-major"}, "row-major",
                           "viewer.layout.order", &collector);
  documentModel.viewer.pageSort = readEnumeratedString(
      viewerObject, "page_sort", {"natural", "string"}, "natural", "viewer.page_sort", &collector);

  const QJsonObject defaultsObject = root.value("defaults").toObject();
  const double defaultScale =
      readPositiveDouble(defaultsObject, "scale", 1.0, "defaults.scale", &collector);
  const bool defaultHonorExif = defaultsObject.value("honor_exif_orientation").toBool(true);
  const QString defaultColorSpaceName = defaultsObject.value("color_space").toString();
  const QString defaultIccProfilePath = defaultsObject.value("icc_profile").toString();
  const bool defaultLabelVisible = defaultsObject.value("label_visible").toBool(false);
  const bool defaultSync = defaultsObject.value("sync").toBool(true);

  const QJsonArray imagesArray = root.value("images").toArray();
  for (int index = 0; index < imagesArray.size(); ++index) {
    const QString location = QString("images[%1]").arg(index);
    if (!imagesArray.at(index).isObject()) {
      collector.addError(location, "An object is required.");
      continue;
    }

    const QJsonObject imageObject = imagesArray.at(index).toObject();
    heiran::domain::ImageSetDocument imageSet;
    const bool nameOk =
        readRequiredString(imageObject, "name", &imageSet.name, location, &collector);
    const bool pathOk =
        readRequiredString(imageObject, "path", &imageSet.path, location, &collector);
    if (!nameOk || !pathOk) {
      continue;
    }

    imageSet.label =
        imageObject.contains("label") ? imageObject.value("label").toString() : imageSet.name;
    imageSet.scale =
        readPositiveDouble(imageObject, "scale", defaultScale, location + ".scale", &collector);
    imageSet.pageOffset = imageObject.value("page_offset").toInt(0);
    if (imageSet.pageOffset < 0) {
      collector.addWarning(
          location + ".page_offset",
          QString("Must be 0 or greater, but was %1. Using 0.").arg(imageSet.pageOffset));
      imageSet.pageOffset = 0;
    }
    imageSet.labelVisible = imageObject.value("label_visible").toBool(defaultLabelVisible);
    imageSet.honorExifOrientation =
        imageObject.value("honor_exif_orientation").toBool(defaultHonorExif);
    imageSet.colorSpaceName =
        imageObject.value("color_space").toString(defaultColorSpaceName).trimmed();
    imageSet.iccProfilePath =
        imageObject.value("icc_profile").toString(defaultIccProfilePath).trimmed();
    documentModel.images.push_back(imageSet);
  }

  const QJsonArray cellsArray = root.value("cells").toArray();
  for (int index = 0; index < cellsArray.size(); ++index) {
    const QString location = QString("cells[%1]").arg(index);
    if (!cellsArray.at(index).isObject()) {
      collector.addError(location, "An object is required.");
      continue;
    }

    const QJsonObject cellObject = cellsArray.at(index).toObject();
    heiran::domain::CellConfig cellConfig;
    cellConfig.imageIndex = cellObject.value("image").toInt(-1);
    if (cellConfig.imageIndex >= documentModel.images.size()) {
      collector.addWarning(
          location + ".image",
          QString("No image set at index %1. Leaving the cell empty.").arg(cellConfig.imageIndex));
      cellConfig.imageIndex = -1;
    }

    cellConfig.sync = cellObject.value("sync").toBool(defaultSync);
    double cellScale = defaultScale;
    if (cellConfig.imageIndex >= 0) {
      cellScale = documentModel.images.at(cellConfig.imageIndex).scale;
    }
    cellConfig.scale =
        readPositiveDouble(cellObject, "scale", cellScale, location + ".scale", &collector);
    cellConfig.gamma =
        readPositiveDouble(cellObject, "gamma", 1.0, location + ".gamma", &collector);

    cellConfig.rotation = cellObject.value("rotation").toInt(0);
    if (cellConfig.rotation < 0 || cellConfig.rotation > 3) {
      collector.addWarning(
          location + ".rotation",
          QString("Must be 0, 1, 2 or 3, but was %1. Using 0.").arg(cellConfig.rotation));
      cellConfig.rotation = 0;
    }

    cellConfig.flipX = cellObject.value("flip_x").toBool(false);
    cellConfig.flipY = cellObject.value("flip_y").toBool(false);
    cellConfig.invert = cellObject.value("invert").toBool(false);

    const QString channelText =
        readEnumeratedString(cellObject, "ch", {"color", "gray", "red", "green", "blue", "alpha"},
                             "color", location + ".ch", &collector);
    cellConfig.channel = heiran::domain::channelModeFromString(channelText);

    if (cellObject.contains("diff")) {
      const QString diffLocation = location + ".diff";
      if (!cellObject.value("diff").isObject()) {
        collector.addWarning(diffLocation, "An object is required. Ignoring the diff.");
      } else {
        const QJsonObject diffObject = cellObject.value("diff").toObject();
        heiran::domain::CellDiff diff;
        diff.baseCell = diffObject.value("base").toInt(-1);
        diff.targetCell = diffObject.value("target").toInt(-1);
        const auto validCell = [&](int value) { return value >= 0 && value < cellCapacity; };
        if (!validCell(diff.baseCell) || diff.baseCell == index) {
          collector.addWarning(
              diffLocation + ".base",
              QString("Must be the index of another cell (0 to %1). Ignoring the diff.")
                  .arg(cellCapacity - 1));
        } else if (diff.targetCell != -1 &&
                   (!validCell(diff.targetCell) || diff.targetCell == index ||
                    diff.targetCell == diff.baseCell)) {
          collector.addWarning(
              diffLocation + ".target",
              QString(
                  "Must be another cell's index (0 to %1) different from base. Ignoring the diff.")
                  .arg(cellCapacity - 1));
        } else if (diff.targetCell == -1 && cellConfig.imageIndex < 0) {
          collector.addWarning(diffLocation + ".target",
                               "A cell without an image needs a target. Ignoring the diff.");
        } else {
          diff.settings.mode = heiran::domain::diffModeFromString(readEnumeratedString(
              diffObject, "mode",
              {"absolute", "signed", "binary", "delta-e", "ssim", "wipe", "rgb-max"}, "absolute",
              diffLocation + ".mode", &collector));
          diff.settings.view = heiran::domain::diffViewModeFromString(
              readEnumeratedString(diffObject, "view", {"grayscale", "signed-color", "overlay"},
                                   "grayscale", diffLocation + ".view", &collector));
          diff.settings.gain =
              readPositiveDouble(diffObject, "gain", 1.0, diffLocation + ".gain", &collector);
          diff.settings.threshold =
              std::max(0.0, diffObject.value("threshold").toDouble(diff.settings.threshold));
          diff.settings.autoRange = diffObject.value("auto_range").toBool(true);
          diff.settings.sizeMatch = heiran::domain::diffSizeMatchFromString(readEnumeratedString(
              diffObject, "size_match", {"overlap", "off", "base", "target", "larger"}, "overlap",
              diffLocation + ".size_match", &collector));
          diff.settings.wipeOrientation = heiran::domain::wipeOrientationFromString(
              readEnumeratedString(diffObject, "wipe_direction", {"left-right", "top-bottom"},
                                   "left-right", diffLocation + ".wipe_direction", &collector));
          diff.settings.wipePosition =
              std::clamp(diffObject.value("wipe_position").toDouble(diff.settings.wipePosition), 0.0, 1.0);
          cellConfig.diff = diff;
        }
      }
    }
    documentModel.cells.push_back(cellConfig);
  }

  if (documentModel.cells.isEmpty()) {
    for (int cellIndex = 0; cellIndex < cellCapacity; ++cellIndex) {
      documentModel.cells.push_back(
          {.imageIndex = cellIndex < documentModel.images.size() ? cellIndex : -1,
           .sync = defaultSync,
           .scale = cellIndex < documentModel.images.size()
                        ? documentModel.images.at(cellIndex).scale
                        : defaultScale,
           .gamma = 1.0,
           .rotation = 0,
           .flipX = false,
           .flipY = false,
           .channel = heiran::domain::ChannelMode::Color});
    }
  }

  return {.ok = !collector.hasError(),
          .document = documentModel,
          .errorMessage = summarize(collector),
          .issues = collector.issues(),
          .issuesTruncated = collector.truncated()};
}

}  // namespace heiran::infra
