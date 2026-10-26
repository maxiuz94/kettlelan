#include "text.h"

#include <cstdint>

namespace kettle::text {

std::string sanitize(const std::string& in, size_t maxChars) {
    std::string out;
    size_t chars = 0;
    size_t i = 0;
    while (i < in.size() && chars < maxChars) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        size_t len = 1;
        if (c >= 0xF8) { ++i; continue; }       // invalid UTF-8 byte
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        else if (c >= 0x80) { ++i; continue; } // stray continuation byte – skip
        if (i + len > in.size()) { ++i; continue; }
        bool valid = true;
        for (size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(in[i + k]) & 0xC0) != 0x80) valid = false;
        if (!valid) { ++i; continue; }

        uint32_t cp = 0;
        if (len == 1) cp = c;
        else if (len == 2) cp = ((c & 0x1F) << 6) | (in[i + 1] & 0x3F);
        else if (len == 3) cp = ((c & 0x0F) << 12) | ((in[i + 1] & 0x3F) << 6) | (in[i + 2] & 0x3F);
        else cp = ((c & 0x07) << 18) | ((in[i + 1] & 0x3F) << 12) | ((in[i + 2] & 0x3F) << 6) | (in[i + 3] & 0x3F);

        // overlong encodings, surrogates and code points beyond Unicode are not valid UTF-8
        const bool malformed = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
                               (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF);
        if (malformed) { i += len; continue; }

        const bool control = cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
        // invisible characters that would make a name look exactly like someone else's
        const bool invisible = cp == 0x00AD || cp == 0x034F || cp == 0x115F || cp == 0x1160 || cp == 0x17B4 ||
                               cp == 0x17B5 || cp == 0x180E || (cp >= 0x200B && cp <= 0x200D) ||
                               (cp >= 0x2060 && cp <= 0x2064) || cp == 0x2800 || cp == 0x3164 || cp == 0xFEFF ||
                               cp == 0xFFA0 || (cp >= 0xE0000 && cp <= 0xE007F);
        const bool bidi = (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) ||
                          cp == 0x200E || cp == 0x200F || cp == 0x061C;
        if (!control && !bidi && !invisible) {
            out.append(in, i, len);
            ++chars;
        } else if (cp == '\t' || cp == '\n' || cp == '\r') {
            out += ' ';
            ++chars;
        }
        i += len;
    }
    // trim spaces
    const auto b = out.find_first_not_of(' ');
    if (b == std::string::npos) return {};
    const auto e = out.find_last_not_of(' ');
    return out.substr(b, e - b + 1);
}

} // namespace kettle::text
