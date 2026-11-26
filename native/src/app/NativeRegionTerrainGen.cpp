#include "app/NativeRegionTerrainGen.h"

#include <png.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace native {
namespace {

std::uint64_t SplitMix64(std::uint64_t& x) {
    x += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

class Perlin2D {
public:
    explicit Perlin2D(std::uint64_t seed) {
        std::array<std::uint8_t, 256> base{};
        for (int i = 0; i < 256; ++i) base[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        std::uint64_t s = seed ? seed : 0x5c47e57ULL;
        for (int i = 255; i > 0; --i) {
            const std::size_t j = static_cast<std::size_t>(SplitMix64(s) % static_cast<std::uint64_t>(i + 1));
            std::swap(base[static_cast<std::size_t>(i)], base[j]);
        }
        for (int i = 0; i < 512; ++i) perm_[static_cast<std::size_t>(i)] = base[static_cast<std::size_t>(i & 255)];
    }

    float Noise(float x, float y) const {
        const int xi = static_cast<int>(std::floor(x)) & 255;
        const int yi = static_cast<int>(std::floor(y)) & 255;
        const float xf = x - std::floor(x);
        const float yf = y - std::floor(y);
        const float u = Fade(xf);
        const float v = Fade(yf);

        const int aa = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(xi)] + yi)];
        const int ab = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(xi)] + yi + 1)];
        const int ba = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(xi + 1)] + yi)];
        const int bb = perm_[static_cast<std::size_t>(perm_[static_cast<std::size_t>(xi + 1)] + yi + 1)];

        const float x1 = Lerp(Grad(aa, xf, yf), Grad(ba, xf - 1.0f, yf), u);
        const float x2 = Lerp(Grad(ab, xf, yf - 1.0f), Grad(bb, xf - 1.0f, yf - 1.0f), u);
        return Lerp(x1, x2, v);
    }

    float Fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const {
        float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = 1.0f;
        for (int i = 0; i < octaves; ++i) {
            sum += amp * Noise(x * freq, y * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.0f ? sum / norm : 0.0f;
    }

    float Ridged(float x, float y, int octaves, float lacunarity = 2.05f, float gain = 0.52f) const {
        float sum = 0.0f, amp = 0.5f, norm = 0.0f, freq = 1.0f, weight = 1.0f;
        for (int i = 0; i < octaves; ++i) {
            float signal = 1.0f - std::fabs(Noise(x * freq, y * freq));
            signal = signal * signal * weight;
            weight = std::clamp(signal * 1.8f, 0.0f, 1.0f);
            sum += signal * amp;
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.0f ? sum / norm : 0.0f;
    }

private:
    static float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    static float Grad(int hash, float x, float y) {
        switch (hash & 7) {
            case 0: return x + y;
            case 1: return x - y;
            case 2: return -x + y;
            case 3: return -x - y;
            case 4: return x;
            case 5: return -x;
            case 6: return y;
            default: return -y;
        }
    }

    std::array<std::uint8_t, 512> perm_{};
};

float SmoothStep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float RandUnit(std::uint64_t& state) {
    return static_cast<float>((SplitMix64(state) >> 40) & 0xffffffu) / 16777216.0f;
}

float RandRange(std::uint64_t& state, float lo, float hi) {
    return lo + (hi - lo) * RandUnit(state);
}

float SoftTerrace(float v, float step, float sharpness) {
    if (step <= 0.0f) return v;
    const float q = v / step;
    const float f = std::floor(q);
    const float frac = q - f;
    const float halfSpan = std::clamp(0.5f * (1.0f - sharpness), 0.02f, 0.5f);
    const float s = SmoothStep(0.5f - halfSpan, 0.5f + halfSpan, frac);
    return (f + s) * step;
}

void Put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xffu));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xffu));
}

void Put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xffu));
}

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb LerpRgb(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<std::uint8_t>(std::lround(a.r + (b.r - a.r) * t)),
        static_cast<std::uint8_t>(std::lround(a.g + (b.g - a.g) * t)),
        static_cast<std::uint8_t>(std::lround(a.b + (b.b - a.b) * t)),
    };
}

Rgb ShadeRgb(Rgb c, float shade) {
    return {
        static_cast<std::uint8_t>(std::clamp(std::lround(c.r * shade), 0l, 255l)),
        static_cast<std::uint8_t>(std::clamp(std::lround(c.g * shade), 0l, 255l)),
        static_cast<std::uint8_t>(std::clamp(std::lround(c.b * shade), 0l, 255l)),
    };
}

struct IsoVertex {
    float px = 0.0f;
    float py = 0.0f;
    float depth = 0.0f;
};

void RasterizeTriangle(const IsoVertex& v0, const IsoVertex& v1, const IsoVertex& v2,
                       Rgb color, std::uint8_t maskR, std::uint8_t maskG,
                       std::uint32_t w, std::uint32_t h, std::vector<float>& zBuf,
                       std::vector<std::uint8_t>& outColor, std::vector<std::uint8_t>& outMask) {
    const float minXf = std::min({v0.px, v1.px, v2.px});
    const float maxXf = std::max({v0.px, v1.px, v2.px});
    const float minYf = std::min({v0.py, v1.py, v2.py});
    const float maxYf = std::max({v0.py, v1.py, v2.py});
    const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
    const int maxX = std::min(static_cast<int>(w) - 1, static_cast<int>(std::ceil(maxXf)));
    const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
    const int maxY = std::min(static_cast<int>(h) - 1, static_cast<int>(std::ceil(maxYf)));
    if (minX > maxX || minY > maxY) return;

    auto edgeFn = [](const IsoVertex& a, const IsoVertex& b, float cx, float cy) {
        return (cx - a.px) * (b.py - a.py) - (cy - a.py) * (b.px - a.px);
    };
    const float area = edgeFn(v0, v1, v2.px, v2.py);
    if (std::fabs(area) < 1e-4f) return;
    const float invArea = 1.0f / area;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float cx = static_cast<float>(x) + 0.5f;
            const float cy = static_cast<float>(y) + 0.5f;
            float w0 = edgeFn(v1, v2, cx, cy) * invArea;
            float w1 = edgeFn(v2, v0, cx, cy) * invArea;
            float w2 = edgeFn(v0, v1, cx, cy) * invArea;
            if (w0 < -0.01f || w1 < -0.01f || w2 < -0.01f) continue;

            const float z = w0 * v0.depth + w1 * v1.depth + w2 * v2.depth;
            const std::size_t idx = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
            if (z < zBuf[idx]) continue;
            zBuf[idx] = z;

            const std::size_t p = idx * 4;
            outColor[p + 0] = color.b;
            outColor[p + 1] = color.g;
            outColor[p + 2] = color.r;
            outColor[p + 3] = 0xff;

            outMask[p + 0] = 0xff;   // blue = opaque city footprint alpha in WinRegionView::Prepare
            outMask[p + 1] = maskG;
            outMask[p + 2] = maskR;
            outMask[p + 3] = 0xff;
        }
    }
}

void RasterizeQuad(const IsoVertex& a, const IsoVertex& b, const IsoVertex& c, const IsoVertex& d,
                   Rgb color, std::uint8_t maskR, std::uint8_t maskG,
                   std::uint32_t w, std::uint32_t h, std::vector<float>& zBuf,
                   std::vector<std::uint8_t>& outColor, std::vector<std::uint8_t>& outMask) {
    RasterizeTriangle(a, b, c, color, maskR, maskG, w, h, zBuf, outColor, outMask);
    RasterizeTriangle(a, c, d, color, maskR, maskG, w, h, zBuf, outColor, outMask);
}

}  // namespace

bool GenerateProceduralRegionHeightmap(const RegionTerrainParams& params, std::vector<std::uint8_t>& outGray) {
    if (params.width < 2 || params.height < 2) return false;
    outGray.resize(static_cast<std::size_t>(params.width) * params.height);

    const float sea = params.seaLevel > 0.0f ? params.seaLevel : 250.0f;
    const float scale = params.scaleFactor > 0.0f ? params.scaleFactor : 3.0f;

    std::uint64_t rngState = params.seed ? params.seed : 0x5c47e57ULL;
    SplitMix64(rngState);
    SplitMix64(rngState);

    Perlin2D noiseElev(SplitMix64(rngState) ^ 0x13572468ULL);
    Perlin2D noiseWarp(SplitMix64(rngState) ^ 0x9e3779b9ULL);
    Perlin2D noiseRidge(SplitMix64(rngState) ^ 0x4b1d5a2cULL);
    Perlin2D noiseRiver(SplitMix64(rngState) ^ 0x71c3a90fULL);
    Perlin2D noiseDetail(SplitMix64(rngState) ^ 0x2a8e4c17ULL);

    const float theta = RandRange(rngState, 0.0f, 6.2831853f);
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    const float scaleMul = RandRange(rngState, 0.75f, 1.35f);
    const float invScale = 1.0f / scaleMul;
    const float warpAmp = RandRange(rngState, 14.0f, 36.0f);
    const float hillMul = RandRange(rngState, 0.60f, 1.25f);
    const float offX = RandRange(rngState, -0.35f, 0.35f);
    const float offZ = RandRange(rngState, -0.35f, 0.35f);

    const std::uint32_t archetype = static_cast<std::uint32_t>(SplitMix64(rngState) % 5ULL);

    // Pre-draw archetype-specific shape parameters so every vertex sees consistent map geometry.
    const bool hasRiver = RandUnit(rngState) < (archetype == 0 ? 0.75f : (archetype == 1 ? 0.70f : 0.80f));
    const float shiftPrimary = RandRange(rngState, -0.25f, 0.25f);
    const float shiftSecondary = RandRange(rngState, -0.18f, 0.18f);
    const float riverHalfW = RandRange(rngState, 0.045f, 0.085f);
    const float flankStart = RandRange(rngState, 0.05f, 0.35f);
    const float valleyHalfW = RandRange(rngState, 0.24f, 0.42f);
    const float lakeCutoff = RandRange(rngState, -0.32f, -0.19f);
    const float terraceStep = RandRange(rngState, 45.0f, 75.0f);
    const float terraceSharp = RandRange(rngState, 0.48f, 0.75f);

    const float bayPos = RandRange(rngState, -0.10f, 0.18f);
    const float bayPhase = RandRange(rngState, 0.0f, 6.2831853f);
    const float coastCutoff = archetype == 1 ? RandRange(rngState, -0.10f, 0.02f) : RandRange(rngState, -0.12f, 0.01f);
    const float straitHalfW = RandRange(rngState, 0.18f, 0.30f);
    const bool isAtoll = (SplitMix64(rngState) & 1ULL) != 0ULL;
    const float islandRadius = RandRange(rngState, 0.68f, 0.84f);
    const float atollRingR = RandRange(rngState, 0.56f, 0.70f);
    const float atollHalfW = RandRange(rngState, 0.24f, 0.34f);

    float mtnMul = 0.5f;
    if (params.baseTerrainType == 0) {
        if (archetype == 0) mtnMul = RandRange(rngState, 0.0f, 0.22f);
        else if (archetype == 1) mtnMul = RandRange(rngState, 0.75f, 1.15f);
        else if (archetype == 2) mtnMul = RandRange(rngState, 0.25f, 0.55f);
        else if (archetype == 3) mtnMul = RandRange(rngState, 0.35f, 0.68f);
        else mtnMul = RandRange(rngState, 0.75f, 1.15f);
    } else {
        if (archetype == 0) mtnMul = RandRange(rngState, 0.25f, 0.72f);
        else if (archetype == 1) mtnMul = RandRange(rngState, 0.15f, 0.45f);
        else if (archetype == 2) mtnMul = RandRange(rngState, 0.22f, 0.65f);
        else if (archetype == 3) mtnMul = RandRange(rngState, 0.75f, 1.15f);
        else mtnMul = RandRange(rngState, 0.28f, 0.92f);
    }

    const float invW = 1.0f / static_cast<float>(params.width - 1u);
    const float invH = 1.0f / static_cast<float>(params.height - 1u);

    for (std::uint32_t z = 0; z < params.height; ++z) {
        for (std::uint32_t x = 0; x < params.width; ++x) {
            const float fx = static_cast<float>(x);
            const float fz = static_cast<float>(z);
            const float nx = (fx * invW - 0.5f) * 2.0f;
            const float nz = (fz * invH - 0.5f) * 2.0f;
            const float rx = nx * cosT - nz * sinT;
            const float rz = nx * sinT + nz * cosT;

            const float wx = noiseWarp.Fbm(fx * (1.0f / 110.0f) * invScale + 3.1f,
                                           fz * (1.0f / 110.0f) * invScale + 7.4f, 3) * warpAmp;
            const float wz = noiseWarp.Fbm(fx * (1.0f / 110.0f) * invScale + 19.7f,
                                           fz * (1.0f / 110.0f) * invScale + 11.3f, 3) * warpAmp;
            const float px = fx + wx;
            const float pz = fz + wz;

            const float macro = noiseElev.Fbm(px * (1.0f / 145.0f) * invScale,
                                              pz * (1.0f / 145.0f) * invScale, 4);
            const float hills = noiseElev.Fbm(px * (1.0f / 58.0f) * invScale + 12.0f,
                                              pz * (1.0f / 58.0f) * invScale + 5.0f, 4);
            const float detail = noiseDetail.Fbm(fx * (1.0f / 26.0f) + 3.0f,
                                                 fz * (1.0f / 26.0f) + 9.0f, 3);
            const float ridge = noiseRidge.Ridged(px * (1.0f / 76.0f) * invScale + 4.3f,
                                                  pz * (1.0f / 76.0f) * invScale + 8.9f, 5);
            const float riverNoise = noiseRiver.Fbm((fx + wx * 1.15f) * (1.0f / 165.0f) + 25.0f,
                                                    (fz + wz * 1.15f) * (1.0f / 165.0f) + 33.0f, 3);

            float meters = sea + 40.0f;

            if (params.baseTerrainType == 0) {
                switch (archetype) {
                    case 0: {
                        // Rolling Prairie & River Valley: open buildable plains with gentle hills and optional river.
                        float elev = (sea + 42.0f) + macro * 46.0f + hills * (38.0f * hillMul) + detail * 11.0f +
                                     SmoothStep(0.06f, 0.42f, macro) * (35.0f + mtnMul * 95.0f);
                        elev = std::min(elev, sea + 185.0f);
                        if (hasRiver) {
                            const float rPos = shiftPrimary + macro * 0.28f + riverNoise * 0.34f;
                            const float rDist = std::fabs(rx - rPos);
                            const float riverT = 1.0f - SmoothStep(riverHalfW * 0.45f, riverHalfW * 1.35f, rDist);
                            const float bankT = 1.0f - SmoothStep(riverHalfW, riverHalfW * 3.0f, rDist);
                            const float bankElev = sea + 18.0f + hills * 14.0f;
                            elev = elev * (1.0f - bankT * 0.65f) + bankElev * (bankT * 0.65f);
                            elev = elev * (1.0f - riverT) + (sea - 26.0f + hills * 6.0f) * riverT;
                        }
                        meters = elev;
                        break;
                    }
                    case 1: {
                        // Flank Mountain Range & Wide Basin: broad lowlands on ~65-75% of the region, massif on one side.
                        const float flank = SmoothStep(flankStart, 0.95f, rx + 0.38f * macro + 0.22f * hills);
                        float elev = (sea + 38.0f) + macro * 38.0f + hills * (36.0f * hillMul) + detail * 11.0f;
                        elev += flank * (72.0f + mtnMul * (95.0f + ridge * 300.0f));
                        if (hasRiver) {
                            const float basinMask = 1.0f - SmoothStep(0.08f, 0.32f, flank);
                            const float rPos = -0.35f + shiftSecondary + macro * 0.22f + riverNoise * 0.26f;
                            const float rDist = std::fabs(rx - rPos);
                            const float riverT = (1.0f - SmoothStep(riverHalfW * 0.45f, riverHalfW * 1.25f, rDist)) * basinMask;
                            elev = elev * (1.0f - riverT) + (sea - 24.0f) * riverT;
                        }
                        meters = elev;
                        break;
                    }
                    case 2: {
                        // Rolling Highlands & Inland Lakes: moderate rounded hills + shallow lake basins.
                        const float hillMask = SmoothStep(-0.08f, 0.38f, macro + 0.30f * hills);
                        float elev = (sea + 40.0f) + macro * 54.0f + hills * (48.0f * hillMul) + detail * 12.0f +
                                     hillMask * mtnMul * (42.0f + ridge * 138.0f);
                        const float lakeSignal = macro + 0.30f * hills;
                        if (lakeSignal < lakeCutoff) {
                            const float depth = SmoothStep(lakeCutoff, lakeCutoff - 0.26f, lakeSignal);
                            elev = (sea - 1.5f) - depth * 42.0f + detail * 4.0f;
                        }
                        meters = elev;
                        break;
                    }
                    case 3: {
                        // Terraced Plateaus & River Gorge: stepped flat-topped mesas cut by a river gorge.
                        const float rawPlateau = 55.0f + (macro + 0.42f) * 140.0f + hills * (42.0f * hillMul) +
                                                 SmoothStep(0.05f, 0.40f, ridge) * (35.0f + mtnMul * 105.0f);
                        float elev = (sea + 26.0f) + SoftTerrace(std::max(0.0f, rawPlateau), terraceStep, terraceSharp) +
                                     detail * 6.5f;
                        const float rPos = shiftPrimary + macro * 0.24f + riverNoise * 0.28f;
                        const float rDist = std::fabs(rx - rPos);
                        const float gorgeT = 1.0f - SmoothStep(riverHalfW * 0.8f, riverHalfW * 2.8f, rDist);
                        const float channelT = 1.0f - SmoothStep(riverHalfW * 0.45f, riverHalfW * 1.25f, rDist);
                        const float bench = sea + 16.0f + hills * 12.0f;
                        elev = elev * (1.0f - gorgeT * 0.85f) + bench * (gorgeT * 0.85f);
                        elev = elev * (1.0f - channelT) + (sea - 24.0f) * channelT;
                        meters = elev;
                        break;
                    }
                    default: {
                        // Alpine Valley & Framing Ridges: wide central valley flanked by mountain ridges.
                        const float vDist = std::fabs(rx - shiftSecondary + macro * 0.30f + riverNoise * 0.22f);
                        const float wallMask = SmoothStep(valleyHalfW, valleyHalfW + 0.50f, vDist);
                        float elev = (sea + 36.0f) + macro * 32.0f + hills * (34.0f * hillMul) + detail * 11.0f +
                                     wallMask * (75.0f + mtnMul * (85.0f + ridge * 300.0f));
                        if (hasRiver) {
                            const float riverT = 1.0f - SmoothStep(riverHalfW * 0.45f, riverHalfW * 1.25f, vDist);
                            elev = elev * (1.0f - riverT) + (sea - 26.0f) * riverT;
                        }
                        meters = elev;
                        break;
                    }
                }
            } else {
                // Water region archetypes
                float landSignal = 0.0f;
                float inlandBoost = 0.0f;
                switch (archetype) {
                    case 0: {
                        // Sweeping Coastal Bay / Harbor: curved mainland coast + sheltered harbor + small outer islets.
                        const float bayCurve = bayPos + 0.28f * std::cos(rz * 2.1f + bayPhase) + macro * 0.44f + hills * 0.16f;
                        const float islandPing = SmoothStep(0.32f, 0.52f, macro + 0.35f * hills) * 0.35f;
                        landSignal = (bayCurve - rx) + islandPing;
                        inlandBoost = SmoothStep(0.0f, 0.95f, std::max(0.0f, landSignal));
                        break;
                    }
                    case 1: {
                        // Gentle Archipelago: varied green islands & sandy cays without towering mountains.
                        landSignal = (macro + 0.34f * hills) - coastCutoff;
                        inlandBoost = SmoothStep(0.0f, 0.42f, landSignal);
                        break;
                    }
                    case 2: {
                        // River Estuary / Twin Peninsulas: broad winding strait splitting two gentle peninsulas.
                        const float channelCenter = shiftSecondary + 0.30f * macro + 0.28f * riverNoise;
                        const float coveDip = hills * 0.16f;
                        landSignal = (std::fabs(rx - channelCenter) - straitHalfW) + coveDip;
                        inlandBoost = SmoothStep(0.0f, 0.62f, landSignal);
                        break;
                    }
                    case 3: {
                        // Fjords & Coastal Peaks: carved inlets flanked by dramatic ridged coastal mountains.
                        landSignal = (macro * 0.78f + hills * 0.48f - 0.18f * ridge) - coastCutoff;
                        inlandBoost = SmoothStep(0.0f, 0.40f, landSignal);
                        break;
                    }
                    default: {
                        // Central Island or Atoll Ring enclosing a lagoon.
                        const float rad = std::hypot(rx - offX, rz - offZ) + 0.24f * macro + 0.12f * hills;
                        if (!isAtoll) {
                            landSignal = islandRadius - rad;
                            inlandBoost = SmoothStep(0.0f, 0.55f, landSignal);
                        } else {
                            const float ringDist = std::fabs(rad - atollRingR);
                            const float breakGap = SmoothStep(-0.18f, 0.08f, macro + 0.22f);
                            landSignal = (atollHalfW - ringDist) * (0.45f + 0.55f * breakGap) - 0.02f;
                            inlandBoost = SmoothStep(0.0f, 0.28f, landSignal);
                        }
                        break;
                    }
                }

                if (landSignal < 0.0f) {
                    const float depth = SmoothStep(0.0f, -0.58f, landSignal);
                    meters = (sea - 1.5f) - depth * 112.0f + hills * 7.0f;
                } else {
                    const float hillFade = SmoothStep(0.0f, 0.12f, landSignal);
                    meters = (sea + 4.0f) + inlandBoost * (78.0f + mtnMul * 70.0f) +
                             std::pow(inlandBoost, 1.35f) * ridge * (270.0f * mtnMul) +
                             hills * (30.0f * hillMul) * hillFade + detail * 8.0f * hillFade;
                }
            }

            // Convert meters -> SC4 8-bit heightmap grayscale (imageImportScaleFactor ~= 3.0).
            const long byteVal = std::clamp(std::lround(meters / scale), 10l, 248l);
            outGray[static_cast<std::size_t>(z) * params.width + x] = static_cast<std::uint8_t>(byteVal);
        }
    }
    return true;
}

bool SaveGrayscaleBmp(const std::string& path, std::uint32_t width, std::uint32_t height,
                      const std::vector<std::uint8_t>& gray) {
    if (path.empty() || width == 0 || height == 0 || gray.size() < static_cast<std::size_t>(width) * height)
        return false;

    const std::uint32_t rowStride = (width + 3u) & ~3u;
    const std::uint32_t paletteBytes = 256u * 4u;
    const std::uint32_t pixelOffset = 14u + 40u + paletteBytes;
    const std::uint32_t imageBytes = rowStride * height;

    std::vector<std::uint8_t> bmp;
    bmp.reserve(pixelOffset + imageBytes);

    // BITMAPFILEHEADER (14 bytes)
    bmp.push_back('B');
    bmp.push_back('M');
    Put32(bmp, pixelOffset + imageBytes);
    Put32(bmp, 0);
    Put32(bmp, pixelOffset);

    // BITMAPINFOHEADER (40 bytes)
    Put32(bmp, 40);
    Put32(bmp, width);
    Put32(bmp, height);
    Put16(bmp, 1);
    Put16(bmp, 8);
    Put32(bmp, 0);
    Put32(bmp, imageBytes);
    Put32(bmp, 2835);
    Put32(bmp, 2835);
    Put32(bmp, 256);
    Put32(bmp, 0);

    // Grayscale BGRA palette
    for (int i = 0; i < 256; ++i) {
        const auto v = static_cast<std::uint8_t>(i);
        bmp.push_back(v);
        bmp.push_back(v);
        bmp.push_back(v);
        bmp.push_back(0);
    }

    // Bottom-up pixel rows
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t srcRow = height - 1u - y;
        const std::uint8_t* row = &gray[static_cast<std::size_t>(srcRow) * width];
        bmp.insert(bmp.end(), row, row + width);
        for (std::uint32_t pad = width; pad < rowStride; ++pad) bmp.push_back(0);
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t wrote = std::fwrite(bmp.data(), 1, bmp.size(), f);
    std::fclose(f);
    return wrote == bmp.size();
}

bool RenderRegionCityThumbnail(const float* altitudes, std::uint32_t vertsX, std::uint32_t vertsZ,
                               float seaLevel, std::uint32_t& outWidth, std::uint32_t& outHeight,
                               std::vector<std::uint8_t>& outColorBgra,
                               std::vector<std::uint8_t>& outMaskBgra) {
    if (!altitudes || vertsX < 2 || vertsZ < 2) return false;
    const std::uint32_t cellsX = vertsX - 1u;
    const std::uint32_t cellsZ = vertsZ - 1u;
    const std::uint32_t edge = std::max(1u, cellsX / 64u);
    const float S = static_cast<float>(cellsX) * 16.0f;

    // SC4's region projection constants (WinRegionView.cpp).
    constexpr float kA = -0.03661165f;
    constexpr float kB = 0.088388346f;
    constexpr float kC = 0.044194173f;
    constexpr float kD = 0.018305825f;
    constexpr float kE = -0.08285339f;

    const std::uint32_t W = 128u * edge;
    auto vertAlt = [&](std::uint32_t vx, std::uint32_t vz) {
        return altitudes[static_cast<std::size_t>(vz) * vertsX + vx];
    };
    auto projectUp = [&](float lx, float ly, float lz) {
        return (S - lx) * kD + (S - lz) * kC + ly * (-kE);
    };

    float maxUp = 64.0f * static_cast<float>(edge) + 20.0f;
    for (std::uint32_t z = 0; z < vertsZ; ++z) {
        for (std::uint32_t x = 0; x < vertsX; ++x) {
            const float lx = static_cast<float>(x) * 16.0f;
            const float lz = static_cast<float>(z) * 16.0f;
            const float effY = std::max(vertAlt(x, z), seaLevel);
            maxUp = std::max(maxUp, projectUp(lx, effY, lz));
        }
    }

    const std::uint32_t minH = 64u * edge + 21u;
    const std::uint32_t H = std::min<std::uint32_t>(640u, std::max(minH, static_cast<std::uint32_t>(std::ceil(maxUp)) + 2u));

    outWidth = W;
    outHeight = H;
    outColorBgra.assign(static_cast<std::size_t>(W) * H * 4u, 0);
    outMaskBgra.assign(static_cast<std::size_t>(W) * H * 4u, 0);
    std::vector<float> zBuf(static_cast<std::size_t>(W) * H, -1e9f);

    auto makeVertex = [&](float lx, float ly, float lz) -> IsoVertex {
        const float px = lx * kB + (S - lz) * (-kA);
        const float py = static_cast<float>(H - 1u) - projectUp(lx, ly, lz);
        const float depth = lx + lz + ly * 0.05f;
        return {px, py, depth};
    };

    // Normalized sunlight direction for regional hillshading
    constexpr float kSunX = -0.62f, kSunY = 0.62f, kSunZ = -0.48f;

    for (std::uint32_t cz = 0; cz < cellsZ; ++cz) {
        for (std::uint32_t cx = 0; cx < cellsX; ++cx) {
            const float y00 = vertAlt(cx, cz);
            const float y10 = vertAlt(cx + 1u, cz);
            const float y01 = vertAlt(cx, cz + 1u);
            const float y11 = vertAlt(cx + 1u, cz + 1u);

            const float s00 = std::max(y00, seaLevel);
            const float s10 = std::max(y10, seaLevel);
            const float s01 = std::max(y01, seaLevel);
            const float s11 = std::max(y11, seaLevel);

            const float x0 = static_cast<float>(cx) * 16.0f;
            const float z0 = static_cast<float>(cz) * 16.0f;
            const IsoVertex v00 = makeVertex(x0, s00, z0);
            const IsoVertex v10 = makeVertex(x0 + 16.0f, s10, z0);
            const IsoVertex v01 = makeVertex(x0, s01, z0 + 16.0f);
            const IsoVertex v11 = makeVertex(x0 + 16.0f, s11, z0 + 16.0f);

            const float avgRaw = 0.25f * (y00 + y10 + y01 + y11);
            Rgb base{102, 138, 72};
            float shade = 1.0f;

            if (avgRaw < seaLevel) {
                const float depth = seaLevel - avgRaw;
                const Rgb shallow{54, 134, 150};
                const Rgb deep{24, 66, 108};
                base = LerpRgb(shallow, deep, SmoothStep(0.0f, 80.0f, depth));
                shade = 0.96f;
            } else {
                const float hx = 0.5f * ((y10 - y00) + (y11 - y01));
                const float hz = 0.5f * ((y01 - y00) + (y11 - y10));
                float nx = -hx * (1.0f / 16.0f);
                float ny = 1.0f;
                float nz = -hz * (1.0f / 16.0f);
                const float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLen;
                ny *= invLen;
                nz *= invLen;

                const Rgb sand{196, 184, 138};
                const Rgb lowGrass{104, 140, 72};
                const Rgb midGrass{86, 120, 62};
                const Rgb highRock{134, 128, 118};
                const Rgb cliffRock{122, 114, 102};
                const Rgb snow{224, 230, 236};

                if (avgRaw < seaLevel + 8.0f) {
                    base = LerpRgb(sand, lowGrass, SmoothStep(seaLevel, seaLevel + 8.0f, avgRaw));
                } else if (avgRaw < seaLevel + 125.0f) {
                    base = LerpRgb(lowGrass, midGrass, SmoothStep(seaLevel + 8.0f, seaLevel + 125.0f, avgRaw));
                } else if (avgRaw < seaLevel + 275.0f) {
                    base = LerpRgb(midGrass, highRock, SmoothStep(seaLevel + 125.0f, seaLevel + 275.0f, avgRaw));
                } else {
                    base = LerpRgb(highRock, snow, SmoothStep(seaLevel + 275.0f, seaLevel + 365.0f, avgRaw));
                }

                // Steep slopes expose cliff rock
                const float cliffT = SmoothStep(0.92f, 0.68f, ny);
                base = LerpRgb(base, cliffRock, cliffT * 0.85f);

                const float sunDot = std::max(0.0f, nx * kSunX + ny * kSunY + nz * kSunZ);
                shade = 0.44f + 0.84f * sunDot;
            }

            const bool border = (cx == 0 || cz == 0 || cx == cellsX - 1u || cz == cellsZ - 1u);
            const std::uint8_t maskR = border ? 0xff : 0x00;
            RasterizeQuad(v00, v10, v11, v01, ShadeRgb(base, shade), maskR, 0xff, W, H, zBuf, outColorBgra, outMaskBgra);
        }
    }

    // Front-left vertical cutaway skirt along z = S (cz == cellsZ)
    for (std::uint32_t cx = 0; cx < cellsX; ++cx) {
        const float x0 = static_cast<float>(cx) * 16.0f;
        const float x1 = x0 + 16.0f;
        const float y0 = vertAlt(cx, cellsZ);
        const float y1 = vertAlt(cx + 1u, cellsZ);
        RasterizeQuad(makeVertex(x0, y0, S), makeVertex(x1, y1, S), makeVertex(x1, 0.0f, S), makeVertex(x0, 0.0f, S),
                      Rgb{106, 98, 86}, 0, 0, W, H, zBuf, outColorBgra, outMaskBgra);
        if (y0 < seaLevel || y1 < seaLevel) {
            const float b0 = std::min(y0, seaLevel);
            const float b1 = std::min(y1, seaLevel);
            const float t0 = std::max(y0, seaLevel);
            const float t1 = std::max(y1, seaLevel);
            RasterizeQuad(makeVertex(x0, t0, S), makeVertex(x1, t1, S), makeVertex(x1, b1, S), makeVertex(x0, b0, S),
                          Rgb{34, 88, 120}, 0, 0, W, H, zBuf, outColorBgra, outMaskBgra);
        }
    }

    // Front-right vertical cutaway skirt along x = S (cx == cellsX)
    for (std::uint32_t cz = 0; cz < cellsZ; ++cz) {
        const float z0 = static_cast<float>(cz) * 16.0f;
        const float z1 = z0 + 16.0f;
        const float y0 = vertAlt(cellsX, cz);
        const float y1 = vertAlt(cellsX, cz + 1u);
        RasterizeQuad(makeVertex(S, y0, z0), makeVertex(S, y1, z1), makeVertex(S, 0.0f, z1), makeVertex(S, 0.0f, z0),
                      Rgb{82, 76, 66}, 0, 0, W, H, zBuf, outColorBgra, outMaskBgra);
        if (y0 < seaLevel || y1 < seaLevel) {
            const float b0 = std::min(y0, seaLevel);
            const float b1 = std::min(y1, seaLevel);
            const float t0 = std::max(y0, seaLevel);
            const float t1 = std::max(y1, seaLevel);
            RasterizeQuad(makeVertex(S, t0, z0), makeVertex(S, t1, z1), makeVertex(S, b1, z1), makeVertex(S, b0, z0),
                          Rgb{26, 68, 96}, 0, 0, W, H, zBuf, outColorBgra, outMaskBgra);
        }
    }

    return true;
}

bool EncodeBgraToPng(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& bgra,
                     std::vector<std::uint8_t>& outPng) {
    outPng.clear();
    if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u) return false;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = PNG_FORMAT_BGRA;
    png_alloc_size_t size = 0;
    const png_int_32 stride = static_cast<png_int_32>(width * 4u);
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, bgra.data(), stride, nullptr)) return false;
    outPng.resize(size);
    if (!png_image_write_to_memory(&image, outPng.data(), &size, 0, bgra.data(), stride, nullptr)) {
        outPng.clear();
        return false;
    }
    outPng.resize(size);
    return true;
}

}  // namespace native
