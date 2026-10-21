// KettleLAN – small GUI helpers.
#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace kettle::gui {

// Kettle icon drawn in code (no image files). Green dot = connected to a network.
QIcon makeAppIcon(bool connected);
QPixmap renderAppIcon(int size, bool connected);

// The program's log file (to attach when reporting bugs).
QString logFilePath();

} // namespace kettle::gui
