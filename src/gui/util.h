// KettleLAN – drobne funkcje pomocnicze GUI.
#pragma once

#include <QIcon>
#include <QString>

namespace kettle::gui {

// Ikona czajnika rysowana w kodzie (bez plików graficznych). Zielona kropka = połączono z siecią.
QIcon makeAppIcon(bool connected);

// Plik dziennika programu (do wysyłania przy zgłaszaniu błędów).
QString logFilePath();

} // namespace kettle::gui
