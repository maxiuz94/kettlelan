// KettleLAN – cleans text coming from other people (Steam names, network names).
//
// Removes control characters (e.g. ESC sequences that could "control" a terminal when the log is
// printed) and characters that change text direction (used to impersonate other names).
#pragma once

#include <cstddef>
#include <string>

namespace kettle::text {

std::string sanitize(const std::string& in, size_t maxChars = 64);

} // namespace kettle::text
