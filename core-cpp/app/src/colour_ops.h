// colour_ops — the mirror's two colour transforms, shared by the GUI and the headless CLI so both
// apply exactly the same maths.
//
// ORDER MATTERS: stripBg() first, then scale(). The background estimate lives in wire units, so
// scale(x, pct) - bg != (x - bg) * pct/100 — subtracting after scaling over-subtracts by 1/pct.
#pragma once
#include <QColor>
#include <array>
#include <cmath>

namespace colour {

// WLED's own gamma table: gammaT[i] = (i/255)^2.2 * 255, rounded — exactly what colors.cpp builds at
// the default gammaCorrectVal 2.2. WLED paints the strip through this, so applying it as our FINAL
// output stage is what makes the PC match the strip instead of running ~5x brighter at the bottom end.
// It also crushes the bottom of the range the way the strip does: gammaT[i] == 0 for every i <= 14
// (gammaT[14] = 0, gammaT[15] = 1), so a few counts of leftover background render as true black here
// just as they do on the strip.
inline int gammaByte(int v) {
    static const std::array<unsigned char, 256> table = []{
        std::array<unsigned char, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[size_t(i)] = static_cast<unsigned char>(std::pow(i / 255.0, 2.2) * 255.0 + 0.5);
        return t;
    }();
    return table[size_t(v < 0 ? 0 : (v > 255 ? 255 : v))];
}

// Apply the gamma table to a colour. LAST stage only: strip -> scale -> gamma, which is WLED's own
// order. Applying it earlier would break the additive arithmetic that stripBg() depends on.
inline QColor gammaOut(const QColor& c) {
    return QColor(gammaByte(c.red()), gammaByte(c.green()), gammaByte(c.blue()));
}

// Scale each channel by the single Brightness slider (0-100%). Black stays black (0 * n = 0).
inline QColor scale(const QColor& c, int pct) {
    return QColor(c.red() * pct / 100, c.green() * pct / 100, c.blue() * pct / 100);
}

// Remove the source's always-on background colour, per channel, clamped at 0.
//
// This is the exact inverse of LedFx's additive background blend (`pixels += bg_color`, computed in
// float with no gamma stage on either side, and WLED stores realtime data byte-exact), so it is
// arithmetic in wire units rather than an approximation. What it cannot undo is clipping: LedFx
// clamps to 255 AFTER folding the background in, so where bg + reactive exceeded 255 the layers
// merged before the clamp and the peak reads slightly dim. That is accepted deliberately — the
// obvious "re-gain" correction fabricates values on unsaturated pixels.
//
// The +1 absorbs LedFx's uint8 truncation, which floors and can leave the wire value one LSB below
// the true pedestal. Nothing more is added on purpose: a bigger margin would swallow the faint
// leading edge of the reactive layer, which is the cue worth keeping.
inline QColor stripBg(const QColor& in, const QColor& bg) {
    auto ch = [](int v, int b) { return v - (b + 1) > 0 ? v - (b + 1) : 0; };
    return QColor(ch(in.red(), bg.red()), ch(in.green(), bg.green()), ch(in.blue(), bg.blue()));
}

// Apply stripBg() across a bucket list and return the mean of the STRIPPED buckets. The mean must be
// taken after stripping, never as avg-minus-bg: the clamp at zero is non-linear, so it has to be
// applied at exactly one stage.
inline QColor stripBgAll(const QList<QColor>& in, const QColor& bg, QList<QColor>* out) {
    if (in.isEmpty()) { if (out) out->clear(); return QColor(0, 0, 0); }
    QList<QColor> tmp;
    tmp.reserve(in.size());
    long r = 0, g = 0, b = 0;
    for (const QColor& c : in) {
        const QColor s = stripBg(c, bg);
        tmp.push_back(s); r += s.red(); g += s.green(); b += s.blue();
    }
    const QColor avg(int(r / in.size()), int(g / in.size()), int(b / in.size()));
    if (out) *out = tmp;
    return avg;
}

} // namespace colour
