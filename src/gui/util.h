// KettleLAN – drobne funkcje pomocnicze GUI.
#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace kettle::gui {

// Ikona czajnika rysowana w kodzie (bez plików graficznych). Zielona kropka = połączono z siecią.
QIcon makeAppIcon(bool connected);
QPixmap renderAppIcon(int size, bool connected);

// Plik dziennika programu (do wysyłania przy zgłaszaniu błędów).
QString logFilePath();

} // namespace kettle::gui
