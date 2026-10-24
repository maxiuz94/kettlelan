// KettleLAN – messages between the KettleLAN program and the KettleLAN service (Windows).
//
// The service owns the Wintun adapter and runs as SYSTEM; the program runs as the user. They talk
// over a local named pipe. Everything that comes from the program is checked strictly, because the
// service hands it to Windows with system rights.
//
// Frame: type (1 byte) | length (2 bytes, big endian) | payload (length bytes)
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ip.h"

namespace kettle::svc {

inline constexpr char kPipeName[] = R"(\\.\pipe\KettleLAN)";
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kMaxPayload = 65535;

enum class Type : uint8_t {
    Hello = 1,  // program → service: version + the program's KettleLAN address
    Ready = 2,  // service → program: the adapter has the address, packets may flow
    Error = 3,  // service → program: code + English text, then the service closes the pipe
    Packet = 4, // both ways: one IPv4 packet
};

enum class ErrorCode : uint8_t {
    BadHello = 1,
    WrongVersion = 2,
    NotAllowed = 3, // the connecting program is not KettleLAN from the service's folder
    Busy = 4,       // another KettleLAN window or terminal is already connected
    AdapterFailed = 5,
};

struct Frame {
    Type type;
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> encode(Type type, std::span<const uint8_t> payload);
std::vector<uint8_t> encodeHello(ip::Address4 address);
std::vector<uint8_t> encodeError(ErrorCode code, const std::string& text);

struct Hello {
    uint8_t version = 0;
    ip::Address4 address;
};
std::optional<Hello> decodeHello(std::span<const uint8_t> payload);

struct ErrorInfo {
    ErrorCode code;
    std::string text;
};
std::optional<ErrorInfo> decodeError(std::span<const uint8_t> payload);

// Splits a byte stream into frames; feed it whatever a read returned.
class FrameReader {
public:
    // Returns false when the stream is broken (unknown type) – the connection must be closed.
    bool feed(std::span<const uint8_t> data);
    std::optional<Frame> next();

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool broken_ = false;
};

// Whether the service may hand a packet from the program to Windows: a valid IPv4 packet from
// another KettleLAN address to our address (or a broadcast/multicast), like the router accepts.
bool acceptFromProgram(std::span<const uint8_t> packet, ip::Address4 self);

} // namespace kettle::svc
