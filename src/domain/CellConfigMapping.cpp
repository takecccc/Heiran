#include "heiran/domain/CellConfigMapping.hpp"

#include "heiran/domain/SessionPages.hpp"

namespace heiran::domain {

CellConfig cellConfigAt(const SessionModel &session, int cellIndex) {
  return cellIndex >= 0 && cellIndex < session.cells.size() ? session.cells.at(cellIndex)
                                                            : CellConfig{};
}

void applyCellConfigDisplayFields(const CellConfig &config, CellViewState *state) {
  state->scale = config.scale;
  state->gamma = config.gamma;
  state->rotationQuarterTurns = config.rotation;
  state->flipX = config.flipX;
  state->flipY = config.flipY;
  state->invert = config.invert;
}

void applyCellConfigToViewState(const SessionModel &session, int cellIndex, CellViewState *state) {
  const CellConfig config = cellConfigAt(session, cellIndex);
  state->syncEnabled = config.sync;
  applyCellConfigDisplayFields(config, state);
  const ImageSet *imageSet = imageSetForCell(session, cellIndex);
  state->honorExifOrientation = imageSet != nullptr ? imageSet->honorExifOrientation : true;
}

void writeCellViewStateToConfig(const CellViewState &state, CellConfig *config) {
  config->sync = state.syncEnabled;
  config->scale = state.scale;
  config->gamma = state.gamma;
  config->rotation = state.rotationQuarterTurns;
  config->flipX = state.flipX;
  config->flipY = state.flipY;
  config->invert = state.invert;
}

}  // namespace heiran::domain
