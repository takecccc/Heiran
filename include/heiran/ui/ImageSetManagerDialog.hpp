#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QDialog>
#include <memory>

namespace heiran::ui {

class ImageSetManagerDialog final : public QDialog {
public:
  struct Result {
    QVector<heiran::domain::ImageSet> images;
    QVector<heiran::domain::CellConfig> cells;
  };

  ImageSetManagerDialog(const heiran::domain::SessionModel &session,
                        const QVector<heiran::domain::CellConfig> &cells,
                        QWidget *parent = nullptr);
  ~ImageSetManagerDialog() override;

  Result result() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace heiran::ui
