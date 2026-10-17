#include "heiran/app/OpenGlCheck.hpp"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

namespace heiran::app {

QString checkOpenGlSupport() {
  QOpenGLContext context;
  if (!context.create()) {
    return QCoreApplication::translate(
        "OpenGlCheck",
        "Heiran needs OpenGL 3.0 or later, but an OpenGL context could not be created.\n\n"
        "Update the graphics driver, or run Heiran on a computer with a GPU that supports OpenGL.");
  }
  const QSurfaceFormat format = context.format();
  if (format.majorVersion() < 3) {
    return QCoreApplication::translate(
               "OpenGlCheck",
               "Heiran needs OpenGL 3.0 or later, but this computer provides OpenGL %1.%2.\n\n"
               "Update the graphics driver, or run Heiran on a computer with a newer GPU.")
        .arg(format.majorVersion())
        .arg(format.minorVersion());
  }
  return {};
}

}  // namespace heiran::app
