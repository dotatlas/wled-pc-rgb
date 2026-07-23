#include "sysinfo.h"

#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <QByteArray>

// Parse SMBIOS structure type 2 (Baseboard Information) for manufacturer +
// product. The raw table comes from GetSystemFirmwareTable('RSMB'): an 8-byte
// RawSMBIOSData header, then packed structures. Each structure = a formatted
// area (its `length` byte) followed by a double-null-terminated string set;
// fields that are strings hold a 1-based index into that set.
QString sysinfo::motherboard()
{
    const DWORD sig = 0x52534D42;   // 'RSMB' — the raw-SMBIOS firmware table provider
    UINT sz = GetSystemFirmwareTable(sig, 0, nullptr, 0);
    if (sz == 0) return {};

    QByteArray buf(int(sz), Qt::Uninitialized);
    if (GetSystemFirmwareTable(sig, 0, buf.data(), sz) == 0) return {};

    const quint8* base = reinterpret_cast<const quint8*>(buf.constData());
    if (sz < 8) return {};
    quint32 tlen = 0; std::memcpy(&tlen, base + 4, 4);
    const quint8* p   = base + 8;
    const quint8* end = p + (tlen < sz - 8 ? tlen : sz - 8);

    QString manuf, product;
    while (p + 4 <= end) {
        const quint8 type = p[0];
        const quint8 hlen = p[1];
        if (hlen < 4) break;
        const quint8* strset = p + hlen;

        auto getStr = [&](quint8 idx) -> QString {
            if (idx == 0) return {};
            const quint8* s = strset;
            for (quint8 i = 1; i < idx && s < end; ++i) { while (s < end && *s) ++s; if (s < end) ++s; }
            if (s >= end) return {};
            const char* c = reinterpret_cast<const char*>(s);
            size_t maxLen = size_t(end - s);
            return QString::fromLatin1(c, int(strnlen(c, maxLen)));
        };

        if (type == 2 && hlen >= 8) { manuf = getStr(p[4]); product = getStr(p[5]); }

        // Skip to the next structure: past the string set (double null).
        const quint8* s = p + hlen;
        while (s + 1 < end && !(s[0] == 0 && s[1] == 0)) ++s;
        p = s + 2;
        if (type == 127) break;   // end-of-table
    }

    return QString(manuf + " " + product).simplified();
}

#else
QString sysinfo::motherboard() { return {}; }
#endif
