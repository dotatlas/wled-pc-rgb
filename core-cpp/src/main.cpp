// orgb_probe — Phase-0 spike for wled-pc-rgb
// -----------------------------------------------------------------------------
// Proves the C++ -> OpenRGB integration seam: connect to a running OpenRGB SDK
// server, negotiate the protocol version, and list the detected controllers.
//
// This is a *dependency-free* client that speaks the OpenRGB network protocol
// directly over TCP (magic "ORGB", 16-byte header, little-endian). It is
// intentionally small and best-effort; the real client (Phase 1) will use a
// proper SDK wrapper. Verify the output on your first build and report it back.
//
// Protocol reference:
//   https://github.com/CalcProgrammer1/OpenRGB/blob/master/Documentation/OpenRGBSDK.md
//
// Usage:  orgb_probe [host] [port]        (defaults: 127.0.0.1 6742)
// Prereq: OpenRGB running as:  OpenRGB.exe --server --noautoconnect
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static const socket_t INVALID_SOCK = INVALID_SOCKET;
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t INVALID_SOCK = -1;
  #define CLOSESOCK ::close
#endif

// OpenRGB network command IDs (subset).
enum : uint32_t {
    CMD_REQUEST_CONTROLLER_COUNT = 0,
    CMD_REQUEST_CONTROLLER_DATA  = 1,
    CMD_REQUEST_PROTOCOL_VERSION = 40,
    CMD_SET_CLIENT_NAME          = 50,
};

static constexpr uint32_t CLIENT_PROTOCOL_VERSION = 3; // we support up to v3

// ---- little-endian helpers --------------------------------------------------
static void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));       b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
static uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint16_t getU16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

// ---- socket I/O -------------------------------------------------------------
static bool sendAll(socket_t s, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(s, reinterpret_cast<const char*>(data + sent), int(len - sent), 0);
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}
static bool recvExact(socket_t s, uint8_t* data, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = ::recv(s, reinterpret_cast<char*>(data + got), int(len - got), 0);
        if (n <= 0) return false;
        got += size_t(n);
    }
    return true;
}

// Send one OpenRGB packet: 16-byte header ("ORGB", devIdx, cmd, size) + payload.
static bool sendPacket(socket_t s, uint32_t devIdx, uint32_t cmd,
                       const uint8_t* payload = nullptr, uint32_t size = 0) {
    std::vector<uint8_t> hdr;
    hdr.push_back('O'); hdr.push_back('R'); hdr.push_back('G'); hdr.push_back('B');
    putU32(hdr, devIdx);
    putU32(hdr, cmd);
    putU32(hdr, size);
    if (!sendAll(s, hdr.data(), hdr.size())) return false;
    if (size && payload)  return sendAll(s, payload, size);
    return true;
}

// Receive one packet. Fills cmd + body. Returns false on error / bad magic.
static bool recvPacket(socket_t s, uint32_t& outCmd, std::vector<uint8_t>& body) {
    uint8_t hdr[16];
    if (!recvExact(s, hdr, 16)) return false;
    if (std::memcmp(hdr, "ORGB", 4) != 0) {
        std::cerr << "! bad magic in response header\n";
        return false;
    }
    outCmd        = getU32(hdr + 8);
    uint32_t size = getU32(hdr + 12);
    body.resize(size);
    if (size && !recvExact(s, body.data(), size)) return false;
    return true;
}

// Best-effort extraction of the device name from a controller-data blob.
// Blob layout (v0+): [u32 data_size][u32 type][u16 name_len][name...] ...
// Some versions omit `type`, so we try with-type first, then without.
static std::string parseName(const std::vector<uint8_t>& b) {
    auto tryAt = [&](size_t off) -> std::string {
        if (off + 2 > b.size()) return {};
        uint16_t len = getU16(b.data() + off);
        if (len == 0 || len > 512 || off + 2 + len > b.size()) return {};
        std::string s(reinterpret_cast<const char*>(b.data() + off + 2), len);
        if (!s.empty() && s.back() == '\0') s.pop_back(); // strip null terminator
        for (char c : s) if (c != '\t' && (c < 0x20 || c > 0x7e)) return {}; // printable?
        return s;
    };
    std::string n = tryAt(8);          // [data_size][type][name]
    if (n.empty()) n = tryAt(4);       // [data_size][name]
    return n.empty() ? "(unparsed)" : n;
}

int main(int argc, char** argv) {
    const std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
    const std::string port = (argc > 2) ? argv[2] : "6742";

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::cerr << "WSAStartup failed\n"; return 1; }
#endif

    // Resolve + connect.
    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        std::cerr << "cannot resolve " << host << ":" << port << "\n";
        return 1;
    }
    socket_t s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCK || ::connect(s, res->ai_addr, int(res->ai_addrlen)) != 0) {
        std::cerr << "connect failed to " << host << ":" << port
                  << " — is OpenRGB running with --server?\n";
        freeaddrinfo(res);
        return 1;
    }
    freeaddrinfo(res);
    std::cout << "connected to OpenRGB SDK at " << host << ":" << port << "\n";

    // 1) Announce our client name (no response expected).
    const char* name = "wled-pc-rgb probe";
    sendPacket(s, 0, CMD_SET_CLIENT_NAME,
               reinterpret_cast<const uint8_t*>(name), uint32_t(std::strlen(name) + 1));

    // 2) Negotiate protocol version.
    uint32_t negotiated = 0;
    {
        std::vector<uint8_t> ver;
        putU32(ver, CLIENT_PROTOCOL_VERSION);
        sendPacket(s, 0, CMD_REQUEST_PROTOCOL_VERSION, ver.data(), uint32_t(ver.size()));
        uint32_t cmd; std::vector<uint8_t> body;
        if (recvPacket(s, cmd, body) && body.size() >= 4) {
            uint32_t server = getU32(body.data());
            negotiated = server < CLIENT_PROTOCOL_VERSION ? server : CLIENT_PROTOCOL_VERSION;
        }
        std::cout << "negotiated protocol version: " << negotiated << "\n";
    }

    // 3) How many controllers?
    sendPacket(s, 0, CMD_REQUEST_CONTROLLER_COUNT);
    uint32_t cmd; std::vector<uint8_t> body;
    if (!recvPacket(s, cmd, body) || body.size() < 4) {
        std::cerr << "no controller count returned\n";
        CLOSESOCK(s);
        return 1;
    }
    uint32_t count = getU32(body.data());
    std::cout << "detected " << count << " controller(s):\n";

    // 4) Fetch + name each controller.
    for (uint32_t i = 0; i < count; ++i) {
        std::vector<uint8_t> req;
        if (negotiated >= 1) putU32(req, negotiated); // v1+ sends the protocol version
        sendPacket(s, i, CMD_REQUEST_CONTROLLER_DATA,
                   req.empty() ? nullptr : req.data(), uint32_t(req.size()));
        uint32_t c2; std::vector<uint8_t> blob;
        if (recvPacket(s, c2, blob)) {
            std::cout << "  [" << i << "] " << parseName(blob)
                      << "  (" << blob.size() << " bytes)\n";
        } else {
            std::cout << "  [" << i << "] <no data>\n";
        }
    }

    CLOSESOCK(s);
#if defined(_WIN32)
    WSACleanup();
#endif
    std::cout << "done.\n";
    return 0;
}
