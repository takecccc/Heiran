#include "heiran/ui/CellStatus.hpp"
#include <QCoreApplication>

namespace heiran::ui {

QString cellStatusHeadline(const CellStatusInfo &statusInfo) {
  switch (statusInfo.status) {
    case CellStatus::Failed:
      return QCoreApplication::translate("CellStatus", "Load failed");
    case CellStatus::Loading:
      return QCoreApplication::translate("CellStatus", "Loading...");
    case CellStatus::Empty:
      return QCoreApplication::translate("CellStatus", "No image");
    case CellStatus::Ready:
    default:
      return {};
  }
}

}  // namespace heiran::ui
