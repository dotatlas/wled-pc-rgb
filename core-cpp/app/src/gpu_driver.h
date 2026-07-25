// gpu_driver — direct per-LED control of an MSI Blackwell (RTX 50-series) GPU's RGB.
//
// Transport: NVAPI (nvapi64.dll, loaded dynamically) over the GPU's OWN I2C bus — port 1,
// device address 0x68. This is NOT the motherboard SMBus: no host-SMBus code exists here, so
// the project's DDR5/RAM rule is respected by construction, and no administrator rights are
// needed (NVAPI is user-mode, reaching the display driver through D3DKMT escapes).
//
// Protocol taken from SignalRGB's own plugin for this exact card (MSI_GPU_Blackwell.js), which
// drives it smoothly: enable per-LED mode with 0x46=0x01 / 0x06=0x00 / 0x20=0x00, then ONE
// 31-byte block write to register 0x04 per frame (header 0x01 + six 5-byte index,R,G,B,0x01
// records). OpenRGB cannot do this: it has no per-LED MSI GPU protocol, never writes 0x46, and
// pins the card to a single 1-LED zone with an empty update stub.
//
// SAFETY (see docs/DESIGN.md). Every write funnels through one choke point that enforces:
//   * address whitelist — 7-bit 0x68 only (the << 1 to 8-bit happens in exactly one place)
//   * register whitelist — 0x46, 0x06, 0x20, 0x04 only. 0x3F (SAVE/APPLY, a probable NVM
//     commit) and the single-zone/mode registers are deliberately unreachable.
//   * value whitelist on the byte registers
//   * never scan, probe or enumerate the bus; never read-modify-write; never touch DDC
//     (bIsDDCPort/displayMask pinned to 0 — DDC reaches monitor EEPROM at 0x50)
//   * exact PCI-ID gate; fail closed and latch the session off after repeated errors
// The GPU's internal bus also carries the VRM and thermal sensors; these rules make those
// addresses unreachable rather than merely unlikely.
#pragma once
#include <QColor>
#include <QList>
#include <QString>

class GpuDriver {
public:
    static constexpr int kLeds = 6;   // 3 logo + 3 fan-strip (hardware mirrors the strips)

    // Read-only: is this machine's GPU one we can drive? (Resolves NVAPI + checks the PCI gate;
    // makes no writes.) Cached after the first call, so the UI can label the device row.
    static bool supportedHere();

    ~GpuDriver();
    bool open(QString* err = nullptr);          // resolve NVAPI, match the card, enable per-LED mode
    bool isOpen() const { return open_; }
    void setLeds(const QList<QColor>& leds);    // per-LED (resampled to 6, segment-average)
    void setColor(const QColor& c);             // solid
    void blackout();                            // all-LED black NOW — bypasses the rate cap and
                                                // skip-unchanged, so releasing the card really darkens it
    void close();
    // True once the path has failed too many times in a row and is off for the session. The caller
    // must then hand the device back (otherwise it would be driven by nothing).
    bool isLatched() const { return latched_; }

    // --- verifiers only (CLI); never used by the mirror path ---------------------
    bool probeInfo(QString* out);                // READ-ONLY: resolve + enumerate + PCI match + one read of 0x20
    bool noopWrite(QString* err);                // the single benign write 0x20 = 0x00
    void setNoRegisterFraming(bool on) { noReg_ = on; }   // fallback framing if 31 B + regAddr is rejected

private:
    bool ensureNvapi(QString* err);              // load the DLL + resolve entry points (idempotent)
    bool selectGpu(QString* err);                // enumerate + exact PCI-ID match
    bool gpuI2cWrite(quint8 addr7, quint8 reg, const quint8* data, int len, QString* err);  // THE choke point
    bool gpuI2cReadByte(quint8 addr7, quint8 reg, quint8* out, QString* err);               // verifiers only

    void*         gpu_    = nullptr;   // NvPhysicalGpuHandle
    bool          open_   = false;
    bool          latched_ = false;    // a hard failure disabled the path for this session
    bool          noReg_  = false;
    int           fails_  = 0;
    qint64        lastAt_ = 0;         // ms — driver-side ~30 FPS cap
    QList<QColor> last_;               // skip-unchanged
};
