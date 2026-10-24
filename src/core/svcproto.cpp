#include "svcproto.h"

#include <algorithm>

namespace kettle::svc {

namespace {
constexpr char kMagic[4] = {'K', 'T', 'L', 'S'};

bool knownType(uint8_t t) { return t >= uint8_t(Type::Hello) && t <= uint8_t(Type::Packet); }
} // namespace

std::vector<uint8_t> encode(Type type, std::span<const uint8_t> payload) {
    const size_t n = std::min(payload.size(), kMaxPayload);
    std::vector<uint8_t> out(3 + n);
    out[0] = uint8_t(type);
    out[1] = uint8_t(n >> 8);
    out[2] = uint8_t(n);
    std::copy_n(payload.begin(), n, out.begin() + 3);
    return out;
}

std::vector<uint8_t> encodeHello(ip::Address4 address) {
    std::vector<uint8_t> p(kMagic, kMagic + 4);
    p.push_back(kVersion);
    const auto b = address.bytes();
    p.insert(p.end(), b.begin(), b.end());
    return encode(Type::Hello, p);
}

std::vector<uint8_t> encodeError(ErrorCode code, const std::string& text) {
    std::vector<uint8_t> p{uint8_t(code)};
    p.insert(p.end(), text.begin(), text.begin() + long(std::min<size_t>(text.size(), 500)));
    return encode(Type::Error, p);
}

std::optional<Hello> decodeHello(std::span<const uint8_t> p) {
    if (p.size() != 9 || !std::equal(p.begin(), p.begin() + 4, kMagic)) return std::nullopt;
    Hello h;
    h.version = p[4];
    h.address = ip::Address4(p[5], p[6], p[7], p[8]);
    return h;
}

std::optional<ErrorInfo> decodeError(std::span<const uint8_t> p) {
    if (p.empty()) return std::nullopt;
    return ErrorInfo{ErrorCode(p[0]), std::string(p.begin() + 1, p.end())};
}

bool FrameReader::feed(std::span<const uint8_t> data) {
    if (broken_) return false;
    if (pos_ > 0 && pos_ == buf_.size()) {
        buf_.clear();
        pos_ = 0;
    }
    buf_.insert(buf_.end(), data.begin(), data.end());
    if (buf_.size() - pos_ >= 1 && !knownType(buf_[pos_])) broken_ = true;
    return !broken_;
}

std::optional<Frame> FrameReader::next() {
    if (broken_ || buf_.size() - pos_ < 3) return std::nullopt;
    if (!knownType(buf_[pos_])) {
        broken_ = true;
        return std::nullopt;
    }
    const size_t len = size_t(buf_[pos_ + 1]) << 8 | buf_[pos_ + 2];
    if (buf_.size() - pos_ < 3 + len) return std::nullopt;
    Frame f{Type(buf_[pos_]), std::vector<uint8_t>(buf_.begin() + long(pos_ + 3), buf_.begin() + long(pos_ + 3 + len))};
    pos_ += 3 + len;
    if (pos_ > 65536) { // keep the buffer small
        buf_.erase(buf_.begin(), buf_.begin() + long(pos_));
        pos_ = 0;
    }
    if (pos_ < buf_.size() && !knownType(buf_[pos_])) broken_ = true;
    return f;
}

bool acceptFromProgram(std::span<const uint8_t> packet, ip::Address4 self) {
    if (packet.size() > kMaxPayload) return false;
    const auto hdr = ip::parseIPv4(packet);
    if (!hdr) return false;
    if (!ip::kVirtualNet.contains(hdr->src) || hdr->src == self) return false;
    return hdr->dst == self || ip::isGroupDestination(hdr->dst);
}

} // namespace kettle::svc
