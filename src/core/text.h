// KettleLAN – czyszczenie tekstu pochodzącego od innych osób (nicki Steam, nazwy sieci).
//
// Usuwa znaki sterujące (np. sekwencje ESC, którymi można „sterować” terminalem przy wypisywaniu
// dziennika) oraz znaki zmieniające kierunek tekstu (podszywanie się pod inne nazwy).
#pragma once

#include <cstddef>
#include <string>

namespace kettle::text {

std::string sanitize(const std::string& in, size_t maxChars = 64);

} // namespace kettle::text
