#include "gpu_driver.h"
#include <QDateTime>
#include <windows.h>
#include <cstring>

namespace {

// --- NVAPI, resolved dynamically -------------------------------------------------
// We never include nvapi.h nor link nvapi64.lib (it is MSVC COFF; MinGW cannot consume it).
// Entry points come from nvapi_QueryInterface by their well-known interface IDs.
enum : unsigned int {
    ID_Initialize            = 0x0150E828,
    ID_Unload                = 0xD22BDD7E,
    ID_EnumPhysicalGPUs      = 0xE5AC921F,
    ID_GPU_GetPCIIdentifiers = 0x2DDFB66E,
    ID_I2CWriteEx            = 0x283AC65A,
    ID_I2CReadEx             = 0x4D7B0709,
};
constexpr int NVAPI_OK = 0;

// NV_I2C_INFO_V3 — exactly 64 bytes on x64. Explicit padding pins every offset, because a
// wrong layout is silently rejected (or worse, misread as a different field).
struct NvI2cInfoV3 {
    quint32 version;            // 0   MAKE_NVAPI_VERSION(NV_I2C_INFO_V3, 3) = 0x00030040
    quint32 displayMask;        // 4   0 — never DDC
    quint8  bIsDDCPort;         // 8   0 — never DDC
    quint8  i2cDevAddress;      // 9   8-bit form (7-bit << 1)
    quint8  _pad0[6];           // 10
    quint8* pbI2cRegAddress;    // 16
    quint32 regAddrSize;        // 24
    quint32 _pad1;              // 28
    quint8* pbData;             // 32
    quint32 cbSize;             // 40
    quint32 i2cSpeed;           // 44  deprecated — 0xFFFF
    quint32 i2cSpeedKhz;        // 48  4 == 100 kHz
    quint8  portId;             // 52
    quint8  _pad2[3];           // 53
    quint32 bIsPortIdSet;       // 56
    quint32 _pad3;              // 60
};
static_assert(sizeof(NvI2cInfoV3) == 64, "NV_I2C_INFO_V3 must be exactly 64 bytes");

using QueryInterface_t   = void* (*)(unsigned int);
using Initialize_t       = int   (*)();
using EnumPhysicalGPUs_t = int   (*)(void**, unsigned int*);
using GetPCIIds_t        = int   (*)(void*, unsigned int*, unsigned int*, unsigned int*, unsigned int*);
using I2CXferEx_t        = int   (*)(void*, NvI2cInfoV3*, unsigned int*);

HMODULE            gDll   = nullptr;
Initialize_t       pInit   = nullptr;
EnumPhysicalGPUs_t pEnum   = nullptr;
GetPCIIds_t        pPciIds = nullptr;
I2CXferEx_t        pWrite  = nullptr;
I2CXferEx_t        pRead   = nullptr;
bool               gReady  = false;

// --- this card, and only this card ----------------------------------------------
constexpr unsigned int kDevId = 0x2C0510DE;   // RTX 5070 Ti (0x2C05) + NVIDIA (0x10DE)
constexpr unsigned int kSubId = 0x53151462;   // MSI Gaming Trio (0x5315) + MSI (0x1462)

// Consecutive failures (of open OR of a frame write) before the path gives up for the session.
// The counter resets only on a SUCCESSFUL FRAME WRITE — resetting it on a successful open would
// make it unreachable whenever open keeps succeeding and only the frame write fails.
constexpr int kMaxFails = 8;

constexpr quint8 kAddr7   = 0x68;   // the RGB controller — the ONLY address we may touch
constexpr quint8 kPortId  = 1;      // confirmed; never iterate ports
constexpr int    kPayload = 31;     // GAMING TRIO per-LED frame

// Registers we are allowed to write, and nothing else.
constexpr quint8 kRegEnable50 = 0x46;   // "50 Series flags" — the write OpenRGB never makes
constexpr quint8 kRegMode     = 0x06;   // 0x00 = per-LED streaming
constexpr quint8 kRegAux      = 0x20;
constexpr quint8 kRegFrame    = 0x04;   // per-LED block write

bool regAllowed(quint8 r) {
    return r == kRegEnable50 || r == kRegMode || r == kRegAux || r == kRegFrame;
}
// Value whitelist for the single-byte registers. A bad colour is only a wrong colour, so the
// frame register's payload is unconstrained.
bool valueAllowed(quint8 r, quint8 v) {
    switch (r) {
        case kRegEnable50: return v == 0x01;
        case kRegMode:     return v == 0x00;
        case kRegAux:      return v == 0x00;
        default:           return true;
    }
}
} // namespace

GpuDriver::~GpuDriver() { close(); }

bool GpuDriver::ensureNvapi(QString* err) {
    if (gReady) return true;
    if (!gDll) gDll = LoadLibraryW(L"nvapi64.dll");
    if (!gDll) { if (err) *err = "nvapi64.dll not found (no NVIDIA driver?)"; return false; }
    auto qi = reinterpret_cast<QueryInterface_t>(
        reinterpret_cast<void*>(GetProcAddress(gDll, "nvapi_QueryInterface")));
    if (!qi) { if (err) *err = "nvapi_QueryInterface missing"; return false; }

    pInit   = reinterpret_cast<Initialize_t>(qi(ID_Initialize));
    pEnum   = reinterpret_cast<EnumPhysicalGPUs_t>(qi(ID_EnumPhysicalGPUs));
    pPciIds = reinterpret_cast<GetPCIIds_t>(qi(ID_GPU_GetPCIIdentifiers));
    pWrite  = reinterpret_cast<I2CXferEx_t>(qi(ID_I2CWriteEx));
    pRead   = reinterpret_cast<I2CXferEx_t>(qi(ID_I2CReadEx));
    if (!pInit || !pEnum || !pPciIds || !pWrite) {
        if (err) *err = "NVAPI entry points could not be resolved";
        return false;
    }
    if (pInit() != NVAPI_OK) { if (err) *err = "NvAPI_Initialize failed"; return false; }
    gReady = true;
    return true;
}

bool GpuDriver::selectGpu(QString* err) {
    gpu_ = nullptr;
    void* handles[64] = {nullptr};
    unsigned int n = 0;
    if (pEnum(handles, &n) != NVAPI_OK || n == 0) {
        if (err) *err = "NvAPI_EnumPhysicalGPUs found no GPU";
        return false;
    }
    for (unsigned int i = 0; i < n && i < 64; ++i) {
        unsigned int devId = 0, subId = 0, revId = 0, extId = 0;
        if (pPciIds(handles[i], &devId, &subId, &revId, &extId) != NVAPI_OK) continue;
        if (devId == kDevId && subId == kSubId) { gpu_ = handles[i]; return true; }   // exact gate
    }
    if (err) *err = "no supported GPU (need devId 0x2C0510DE / subId 0x53151462)";
    return false;
}

// THE choke point. Every GPU I2C write in this program goes through here.
bool GpuDriver::gpuI2cWrite(quint8 addr7, quint8 reg, const quint8* data, int len, QString* err) {
    if (!gReady || !gpu_ || !pWrite) { if (err) *err = "NVAPI not ready"; return false; }
    if (addr7 != kAddr7)  { if (err) *err = "blocked: address not whitelisted"; return false; }
    if (!regAllowed(reg)) { if (err) *err = "blocked: register not whitelisted"; return false; }
    if (len < 1 || len > kPayload) { if (err) *err = "blocked: bad length"; return false; }
    if (len == 1 && !valueAllowed(reg, data[0])) { if (err) *err = "blocked: value not whitelisted"; return false; }
    if (reg == kRegFrame && len != kPayload)     { if (err) *err = "blocked: frame must be 31 bytes"; return false; }

    quint8 buf[kPayload + 1];
    quint8 regByte = reg;
    NvI2cInfoV3 info;
    std::memset(&info, 0, sizeof(info));
    info.version      = 0x00030040;
    info.displayMask  = 0;                      // R7: never DDC
    info.bIsDDCPort   = 0;                      // R7: never DDC
    info.i2cDevAddress = static_cast<quint8>(addr7 << 1);   // R2: the ONLY 7->8 bit shift
    info.i2cSpeed     = 0xFFFF;                 // deprecated field
    info.i2cSpeedKhz  = 4;                      // 100 kHz
    info.portId       = kPortId;                // R8: fixed port
    info.bIsPortIdSet = 1;

    if (noReg_) {                               // V4 fallback: register byte inside the payload
        buf[0] = regByte;
        std::memcpy(buf + 1, data, size_t(len));
        info.pbI2cRegAddress = nullptr;
        info.regAddrSize     = 0;
        info.pbData          = buf;
        info.cbSize          = quint32(len + 1);
    } else {
        std::memcpy(buf, data, size_t(len));
        info.pbI2cRegAddress = &regByte;
        info.regAddrSize     = 1;
        info.pbData          = buf;
        info.cbSize          = quint32(len);
    }

    unsigned int outStatus = 0;
    const int rc = pWrite(gpu_, &info, &outStatus);   // three arguments — the real ABI
    if (rc != NVAPI_OK) {
        if (err) *err = QString("NvAPI_I2CWriteEx failed (rc=%1, status=%2)").arg(rc).arg(outStatus);
        return false;
    }
    return true;
}

// Verifiers only — a single known register, never a sweep (R5/R6).
bool GpuDriver::gpuI2cReadByte(quint8 addr7, quint8 reg, quint8* out, QString* err) {
    if (!gReady || !gpu_ || !pRead) { if (err) *err = "NVAPI read not available"; return false; }
    if (addr7 != kAddr7 || reg != kRegAux) { if (err) *err = "blocked: read not whitelisted"; return false; }
    quint8 regByte = reg, val = 0;
    NvI2cInfoV3 info;
    std::memset(&info, 0, sizeof(info));
    info.version       = 0x00030040;
    info.i2cDevAddress = static_cast<quint8>(addr7 << 1);
    info.pbI2cRegAddress = &regByte;
    info.regAddrSize   = 1;
    info.pbData        = &val;
    info.cbSize        = 1;
    info.i2cSpeed      = 0xFFFF;
    info.i2cSpeedKhz   = 4;
    info.portId        = kPortId;
    info.bIsPortIdSet  = 1;
    unsigned int outStatus = 0;
    const int rc = pRead(gpu_, &info, &outStatus);
    if (rc != NVAPI_OK) {
        if (err) *err = QString("NvAPI_I2CReadEx failed (rc=%1, status=%2)").arg(rc).arg(outStatus);
        return false;
    }
    if (out) *out = val;
    return true;
}

bool GpuDriver::supportedHere() {
    static int cached = -1;                      // -1 unknown, 0 no, 1 yes
    if (cached >= 0) return cached == 1;
    GpuDriver probe;
    QString err;
    cached = (probe.ensureNvapi(&err) && probe.selectGpu(&err)) ? 1 : 0;   // read-only
    return cached == 1;
}

bool GpuDriver::open(QString* err) {
    if (open_) return true;
    // Bounded retries rather than a one-shot latch: a tray app runs for days, and a stale NVAPI
    // handle (a known driver bug after ~4h) or a sleep/resume must be recoverable. Re-selecting
    // the GPU here re-enumerates handles, which is exactly the fix for staleness. The caller's
    // ~2s reopen floor bounds the rate, so kMaxFails failures span >=16s before we give up.
    if (latched_) { if (err) *err = "GPU path disabled for this session after repeated failures"; return false; }
    if (!ensureNvapi(err) || !selectGpu(err)) {
        if (++fails_ >= kMaxFails) latched_ = true;
        return false;
    }
    // Enable per-LED streaming: three single-byte writes, in this exact order, once.
    struct { quint8 reg, val; } const init[] = {
        { kRegEnable50, 0x01 },   // 50-series flag — without this the card ignores per-LED data
        { kRegMode,     0x00 },   // per-LED (0x01 would be single-zone)
        { kRegAux,      0x00 },
    };
    for (const auto& w : init) {
        if (!gpuI2cWrite(kAddr7, w.reg, &w.val, 1, err)) {
            if (++fails_ >= kMaxFails) latched_ = true;
            return false;
        }
    }
    // NB: fails_ is deliberately NOT reset here — only a successful frame write clears it, so a
    // "open succeeds, frame always fails" loop still accumulates toward the latch.
    open_ = true; last_.clear(); lastAt_ = 0;
    return true;
}

void GpuDriver::close() {
    open_ = false;
    gpu_ = nullptr;
    last_.clear();
}

void GpuDriver::setLeds(const QList<QColor>& leds) {
    if (!open_ || leds.isEmpty()) return;

    // Driver-side ~30 FPS cap (R13): the mirror has no OrgbMirror gate on this path.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastAt_ && now - lastAt_ < 33) return;

    // Resample the strip onto our 6 logical LEDs (segment-average), so a moving gradient shows
    // across the logo and fan strips instead of collapsing to a single colour.
    QList<QColor> six;
    six.reserve(kLeds);
    const int n = leds.size();
    for (int i = 0; i < kLeds; ++i) {
        int lo = int(static_cast<long long>(i)     * n / kLeds);
        int hi = int(static_cast<long long>(i + 1) * n / kLeds);
        if (hi <= lo) hi = lo + 1;
        if (hi > n)   hi = n;
        long r = 0, g = 0, b = 0; int cnt = 0;
        for (int k = lo; k < hi; ++k) { r += leds[k].red(); g += leds[k].green(); b += leds[k].blue(); ++cnt; }
        if (!cnt) cnt = 1;
        six.push_back(QColor(int(r / cnt), int(g / cnt), int(b / cnt)));
    }
    if (six == last_) return;   // skip-unchanged

    // One 31-byte frame: header 0x01, then six records of (index, R, G, B, 0x01). MSI's own
    // plugin stores record i at slot vLeds[i], which swaps the last two fan-strip slots — we
    // replicate that byte-for-byte because it is what the working SignalRGB build sends.
    static const int vLeds[kLeds] = { 0, 1, 2, 3, 5, 4 };
    quint8 buf[kPayload];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    for (int i = 0; i < kLeds; ++i) {
        const int off = 1 + vLeds[i] * 5;
        const QColor& c = six[i];
        buf[off + 0] = quint8(i);                  // index byte
        buf[off + 1] = quint8(c.red());
        buf[off + 2] = quint8(c.green());
        buf[off + 3] = quint8(c.blue());
        buf[off + 4] = 0x01;
    }
    QString err;
    if (!gpuI2cWrite(kAddr7, kRegFrame, buf, kPayload, &err)) {
        if (++fails_ >= kMaxFails) latched_ = true;   // bound it here too: reopens keep succeeding
        close();      // drop the handle; the caller's reopen re-enumerates (fixes stale handles)
        return;
    }
    fails_ = 0; lastAt_ = now; last_ = six;
}

void GpuDriver::setColor(const QColor& c) {
    QList<QColor> one; one.push_back(c);
    setLeds(one);
}

// Darken every LED immediately. Used when we release the card, so it does not stay lit with no
// writer. This must bypass BOTH the ~30 FPS cap and skip-unchanged: a normal setColor(black) issued
// right after a mirror frame is dropped by the cap, and close() sends nothing.
void GpuDriver::blackout() {
    // A failed frame write closes the handle, and the caller only retries every ~2s. If we are
    // releasing the card inside that window, reopen once so the card does not stay lit with no
    // writer. (open() only issues whitelisted init writes, so this is safe.)
    if (!open_ && !open(nullptr)) return;
    quint8 buf[kPayload];
    std::memset(buf, 0, sizeof(buf));
    buf[0] = 0x01;
    static const int vLeds[kLeds] = { 0, 1, 2, 3, 5, 4 };
    for (int i = 0; i < kLeds; ++i) {
        const int off = 1 + vLeds[i] * 5;
        buf[off + 0] = quint8(i);      // index; R/G/B stay 0
        buf[off + 4] = 0x01;
    }
    QString err;
    gpuI2cWrite(kAddr7, kRegFrame, buf, kPayload, &err);   // best-effort; we are releasing anyway
    last_.clear(); lastAt_ = 0;
}

// --- verifiers ------------------------------------------------------------------
bool GpuDriver::probeInfo(QString* out) {
    QString err, log;
    if (!ensureNvapi(&err)) { if (out) *out = "FAIL: " + err; return false; }
    log += "NVAPI loaded + initialised OK\n";
    void* handles[64] = {nullptr};
    unsigned int n = 0;
    if (pEnum(handles, &n) != NVAPI_OK) { if (out) *out = log + "FAIL: EnumPhysicalGPUs\n"; return false; }
    log += QString("EnumPhysicalGPUs OK — %1 GPU(s)\n").arg(n);
    for (unsigned int i = 0; i < n && i < 64; ++i) {
        unsigned int devId = 0, subId = 0, revId = 0, extId = 0;
        if (pPciIds(handles[i], &devId, &subId, &revId, &extId) != NVAPI_OK) { log += QString("  gpu%1: PCI ids unavailable\n").arg(i); continue; }
        log += QString("  gpu%1: devId=0x%2 subId=0x%3 %4\n").arg(i)
                   .arg(devId, 8, 16, QChar('0')).arg(subId, 8, 16, QChar('0'))
                   .arg((devId == kDevId && subId == kSubId) ? "<= MATCH (supported)" : "(not supported)");
    }
    if (!selectGpu(&err)) { if (out) *out = log + "FAIL: " + err + "\n"; return false; }
    log += "PCI gate passed\n";
    quint8 v = 0;
    if (gpuI2cReadByte(kAddr7, kRegAux, &v, &err))
        log += QString("I2C READ ok — addr 0x68 port 1 reg 0x20 = 0x%1\n").arg(v, 2, 16, QChar('0'));
    else
        log += "I2C read: " + err + "\n";
    log += "read-only probe complete (NO writes were made)\n";
    if (out) *out = log;
    return true;
}

bool GpuDriver::noopWrite(QString* err) {
    if (!ensureNvapi(err) || !selectGpu(err)) return false;
    const quint8 v = 0x00;
    return gpuI2cWrite(kAddr7, kRegAux, &v, 1, err);   // the single benign whitelisted write
}
