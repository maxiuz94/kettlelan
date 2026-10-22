#include "random.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace kettle::random {

void fill(std::span<uint8_t> out) {
#ifdef _WIN32
    if (!out.empty() &&
        !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data(), ULONG(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        std::fputs("KettleLAN: the system random generator failed\n", stderr);
        std::abort();
    }
#else
    size_t done = 0;
    while (done < out.size()) {
        const ssize_t n = ::getrandom(out.data() + done, out.size() - done, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fputs("KettleLAN: the system random generator failed\n", stderr);
            std::abort();
        }
        done += size_t(n);
    }
#endif
}

} // namespace kettle::random
